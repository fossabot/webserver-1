#include "CAdaptiveSource.h"
#include "../PullStylePinsBaseImpl.h"
#include "../MMTransport/MMTransport.h"
#include "../MMTransport/QualityOfService.h"
#include <CorbaHelpers/LazyObjref.h>
#include <CorbaHelpers/ResolveServant.h>
#include <CorbaHelpers/Reactor.h>

namespace
{
    const auto CHECK_PERIOD = std::chrono::seconds(35);

    struct Size
    {
        int Width{};
        int Height{};

        int Area() const
        {
            return Width * Height;
        }

        bool IsEmpty() const
        {
            return !Area();
        }

        bool operator<(const Size& s2) const
        {
            return (Area() < s2.Area()) || (Area() == s2.Area() && Width < s2.Width);
        }

        bool operator==(const Size& s2) const
        {
            return Width == s2.Width && Height == s2.Height;
        }

        bool operator!=(const Size& s2) const
        {
            return !operator==(s2);
        }
    };

    class CAdaptiveSource : public NMMSS::CPullStyleSourceBasePureRefcounted, public virtual NCorbaHelpers::CWeakReferableImpl, public virtual NMMSS::IQoSAwareSource, public NLogging::WithLogger
    {
    private:
        class CSink;
        using PSink = NCorbaHelpers::CAutoPtr<CSink>;

        class CEndpointGetter
        {
            NCorbaHelpers::PContainerNamed m_container;
            std::string m_id;

        public:
            using result_type = typename MMSS::Endpoint::_ptr_type;
            CEndpointGetter(NCorbaHelpers::PContainerNamed c, std::string id) : m_container(std::move(c)), m_id(std::move(id)) {}
            result_type operator()()
            {
                result_type endpoint = NCorbaHelpers::ResolveServant<MMSS::Endpoint>(m_container.Get(), m_id, 5000);
                if (CORBA::is_nil(endpoint))
                    throw CORBA::OBJECT_NOT_EXIST();
                return endpoint;
            }
        };

        class CSink : public NMMSS::CPullStyleSinkBase, public NLogging::WithLogger
        {
        public:
            CSink(DECLARE_LOGGER_ARG, NCorbaHelpers::PContainerNamed container, CAdaptiveSource* parent, const std::string& id) :
                NLogging::WithLogger(GET_LOGGER_PTR),
                m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
                m_parent(parent),
                m_id(id),
                m_endpoint(CEndpointGetter(container, id))
            {
            }

            void Receive(NMMSS::ISample* sample) override
            {
                NCorbaHelpers::CAutoPtr<CAdaptiveSource> parent(m_parent);
                if (parent)
                    parent->Receive(sample, this);
            }

            const Size GetSize() const
            {
                return { (int)m_statistics.width, (int)m_statistics.height };
            }

            int Pixels() const
            {
                return GetSize().Area();
            }

            int FpsFactor() const
            {
                auto fps = std::max(m_statistics.fps, 0.001f);
                return std::lround((fps < 1.f) ? -(1.f / fps) : fps);
            }

            void UpdateStats()
            {
                try
                {
                    m_statistics = m_endpoint->GetStatistics();
                }
                catch (const CORBA::Exception&)
                {
                }
            }

            void Connect(NCorbaHelpers::PContainerNamed container, const MMSS::QualityOfService& qos)
            {
                m_connection = NMMSS::PSinkEndpoint(NMMSS::CreatePullConnectionByNsref(GET_LOGGER_PTR, m_id.c_str(), container->GetRootNC(), this, MMSS::EINPROC, &qos));
            }

            void Disconnect()
            {
                if (m_connection)
                {
                    m_reactor->GetIO().post(std::bind(&NMMSS::ISinkEndpoint::Destroy, m_connection));
                    m_connection.Reset();
                }
            }

            void Request(unsigned int count)
            {
                RequestNextSamples(count);
            }

            bool BetterThan(PSink& sink2) const
            {
                auto size1 = GetSize();
                auto size2 = sink2->GetSize();
                return (size2 < size1) ||
                    (size2 == size1 && sink2->FpsFactor() < FpsFactor()) ||
                    (size2 == size1 && sink2->FpsFactor() == FpsFactor() && sink2->m_statistics.bitrate < m_statistics.bitrate);
            }

            bool LowQualityBetterThan(PSink& sink2) const
            {
                auto size1 = GetSize();
                auto size2 = sink2->GetSize();
                return (size1 < size2) ||
                    (size1 == size2 && sink2->FpsFactor() < FpsFactor()) ||
                    (size1 == size2 && sink2->FpsFactor() == FpsFactor() && m_statistics.bitrate < sink2->m_statistics.bitrate);
            }

            bool DiffersEnough(PSink& sink2) const
            {
                if (GetSize() != sink2->GetSize() || FpsFactor() != sink2->FpsFactor())
                {
                    return true;
                }
                int64_t rate1 = m_statistics.bitrate;
                int64_t rate2 = sink2->m_statistics.bitrate;
                return std::abs(rate2 - rate1) > (rate2 / 20);
            }

        protected:
            void onConnected(TLock& lock) override
            {
                lock.unlock();

                NCorbaHelpers::CAutoPtr<CAdaptiveSource> parent(m_parent);
                if (parent)
                    RequestNextSamples(parent->Requested());
            }

            void onDisconnected(TLock& lock) override
            {

            }

        private:

            NCorbaHelpers::PReactor m_reactor;
            NCorbaHelpers::CWeakPtr<CAdaptiveSource> m_parent;
            std::string m_id;
            MMSS::EndpointStatistics m_statistics{};
            NMMSS::PSinkEndpoint m_connection;
            NCorbaHelpers::CLazyObjref<MMSS::Endpoint> m_endpoint;
        };

    public:
        CAdaptiveSource(DECLARE_LOGGER_ARG, NCorbaHelpers::PContainerNamed container, const std::vector<std::string>& streamings, const MMSS::QualityOfService& qos) :
            NLogging::WithLogger(GET_LOGGER_PTR),
            m_container(container),
            m_qos(qos)
        {
            for (const auto& id : streamings)
            {
                m_sinks.push_back(PSink(new CSink(GET_LOGGER_PTR, container, this, id)));
            }
            checkStats(std::chrono::steady_clock::now());
        }

        ~CAdaptiveSource()
        {
        }

        void ModifyQoS(const MMSS::QualityOfService& qos) override
        {
            TLock lock(mutex());
            m_qos = qos;
            selectActiveSink(lock, true);
        }

        void ReprocessQoS() override
        {
        }

        unsigned int Requested() const
        {
            TLock lock(mutex());
            return requested(lock);
        }

        void Receive(NMMSS::ISample* sample, CSink* sink)
        {
            TLock lock(mutex());

            auto ts = std::chrono::steady_clock::now();
            if ((ts - m_lastCheckTime) >= CHECK_PERIOD)
            {
                checkStats(ts);
                selectActiveSink(lock, false);
            }

            if (sink == m_activeSink.Get() && m_oldActiveSink)
            {
                m_oldActiveSink->Disconnect();
                m_oldActiveSink.Reset();
            }
            if (sink == m_activeSink.Get() || sink == m_oldActiveSink.Get())
            {
                sendSample(lock, sample, false);
            }
        }

    protected:
        void onConnected(TLock& lock) override
        {
            selectActiveSink(lock, true);
        }

        void onDisconnected(TLock& lock) override
        {
            for (auto sink : m_sinks)
            {
                sink->Disconnect();
            }
            m_activeSink.Reset();
            m_oldActiveSink.Reset();
        }

        void onRequested(TLock& lock, unsigned int count) override
        {
            PSink sinks[2] = { m_activeSink, m_oldActiveSink };
            lock.unlock();
            for (auto sink : sinks)
            {
                if (sink)
                {
                    sink->Request(count);
                }
            }
        }

    private:
        void selectActiveSink(TLock& lock, bool forceSwitch)
        {
            m_switchRequested |= forceSwitch;
            if (auto geometry = NMMSS::GetRequest<MMSS::QoSRequest::FrameGeometry>(m_qos))
            {
                auto sink = getRequiredSink({ (int)geometry->width, (int)geometry->height });
                if (sink != m_activeSink && isConnected(lock))
                {
                    if (!m_activeSink || m_switchRequested || sink->DiffersEnough(m_activeSink))
                    {
                        switchToSink(sink);
                    }
                    else
                    {
                        m_switchRequested = true;
                    }
                }
            }
        }

        void switchToSink(PSink sink)
        {
            if (auto container = NCorbaHelpers::PContainerNamed(m_container))
            {
                bool oldSinkOk = m_oldActiveSink == sink;
                if (m_oldActiveSink)
                {
                    m_activeSink->Disconnect();
                }
                else
                {
                    m_oldActiveSink = m_activeSink;
                }
                m_activeSink = sink;
                if (oldSinkOk)
                {
                    m_oldActiveSink.Reset();
                }
                else
                {
                    m_activeSink->Connect(container, m_qos);
                }
                m_switchRequested = false;
            }
        }

        PSink getRequiredSink(const Size& viewportSize)
        {
            if (!viewportSize.Width && !viewportSize.Height)
            {
                return m_lowQualitySink;
            }

            auto lastIt = std::next(m_bestSinks.begin(), m_bestSinks.size() - 1);
            for (auto it = m_bestSinks.begin(); it != lastIt; ++it)
            {
                auto sinkSize = it->second->GetSize();
                if (!sinkSize.IsEmpty())
                {
                    Size requiredSinkSize = fit(sinkSize, viewportSize);
                    const double QUALITY_FACTOR = 1.1;
                    if (QUALITY_FACTOR * sinkSize.Width >= requiredSinkSize.Width && QUALITY_FACTOR * sinkSize.Height >= requiredSinkSize.Height)
                    {
                        return it->second;
                    }
                }
            }
            return lastIt->second;
        }

        Size fit(const Size& sinkSize, const Size& viewportSize)
        {
            if (viewportSize.IsEmpty())
            {
                return viewportSize;
            }
            double factor = std::min((double)viewportSize.Width / sinkSize.Width, (double)viewportSize.Height / sinkSize.Height);
            return { (int)std::lround(factor * sinkSize.Width), (int)std::lround(factor * sinkSize.Height) };
        }

        void checkStats(const std::chrono::steady_clock::time_point& ts)
        {
            m_bestSinks.clear();
            m_lowQualitySink.Reset();
            for (auto sink : m_sinks)
            {
                sink->UpdateStats();

                auto result = m_bestSinks.emplace(sink->GetSize(), sink);
                if (!result.second && sink->BetterThan(result.first->second))
                {
                    result.first->second = sink;
                }

                if (!m_lowQualitySink || sink->LowQualityBetterThan(m_lowQualitySink))
                {
                    m_lowQualitySink = sink;
                }
            }
            m_lastCheckTime = ts;
        }

    private:
        NCorbaHelpers::WPContainerNamed m_container;
        std::vector<PSink> m_sinks;
        std::map<Size, PSink> m_bestSinks;
        PSink m_activeSink, m_oldActiveSink, m_lowQualitySink;
        MMSS::QualityOfService m_qos;
        std::chrono::steady_clock::time_point m_lastCheckTime;
        bool m_switchRequested{};
    };
}

namespace IPINT30
{

CAdaptiveSourceFactory::CAdaptiveSourceFactory(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed* container, const std::string& accessPoint):
    NLogging::WithLogger(GET_LOGGER_PTR),
    m_container(container),
    m_accessPoint(accessPoint)
{
}

void CAdaptiveSourceFactory::Enable(const std::string& accessPoint, bool useForGreenStream)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_availableStreamings.insert(accessPoint);
    if (useForGreenStream)
    {
        m_greenStreamStreamings.insert(accessPoint);
    }
    else
    {
        m_greenStreamStreamings.erase(accessPoint);
    }
    updateStreamings();
}

void CAdaptiveSourceFactory::Disable(const std::string& accessPoint)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_availableStreamings.erase(accessPoint);
    m_greenStreamStreamings.erase(accessPoint);
    updateStreamings();
}

void CAdaptiveSourceFactory::updateStreamings()
{
    m_selectedStreamings.assign(m_greenStreamStreamings.begin(), m_greenStreamStreamings.end());
    if (m_selectedStreamings.empty() && !m_availableStreamings.empty())
    {
        m_selectedStreamings.push_back(*m_availableStreamings.begin());
    }

    if (m_selectedStreamings.empty() && m_servant)
    {
        m_servant.reset();
    }
    else if (!m_selectedStreamings.empty() && !m_servant)
    {
        if (NCorbaHelpers::PContainerNamed container = m_container)
        {
            PortableServer::Servant servant = NMMSS::CreatePullSourceEndpoint(GET_LOGGER_PTR, container.Get(), this);
            m_servant.reset(NCorbaHelpers::ActivateServant(container.Get(), servant, m_accessPoint.c_str()));
        }
    }
}

NMMSS::IQoSAwareSource* CAdaptiveSourceFactory::CreateSource(MMSS::QualityOfService const& qos)
{
    if (auto container = NCorbaHelpers::PContainerNamed(m_container))
    {
        std::vector<std::string> streamings;
        {
            std::lock_guard<std::mutex> lock(m_lock);
            streamings = m_selectedStreamings;
        }
        if (!streamings.empty())
        {
            return new CAdaptiveSource(GET_LOGGER_PTR, container, streamings, qos);
        }
    }
    return nullptr;
}

NMMSS::SAllocatorRequirements CAdaptiveSourceFactory::GetFactoryAllocatorRequirements()
{
    return NMMSS::SAllocatorRequirements();
}

const NMMSS::IStatisticsProvider* CAdaptiveSourceFactory::GetStatisticsProvider() const
{
    return nullptr;
}

}
