#include "AugmentedSourceFactory.h"
#include "TweakableFilter.h"
#include "../ConnectionResource.h"
#include "../PullStylePinsBaseImpl.h"
#include "../SampleAdvisor.h"
#include <CorbaHelpers/Reactor.h>
#include <boost/asio/io_service.hpp>
#include <boost/bind.hpp>
#include <mutex>

namespace {

    class CAugmentedSource
        : public virtual NMMSS::IAugmentedSource
        , public NMMSS::CPullStyleSourceBase
        , protected NLogging::WithLogger
    {
        using OwnerRefcountedImpl = NCorbaHelpers::TOwnerRefcountedImpl<CAugmentedSource>;
        using Lock = boost::unique_lock<boost::mutex>;

        class CSink
            : public NMMSS::CPullStyleSinkBasePureRefcounted
            , public OwnerRefcountedImpl
        {
        public:
            CSink(CAugmentedSource* owner)
                : OwnerRefcountedImpl(owner)
                , m_requested(0)
            {}
            void Receive(NMMSS::ISample* sample) override
            {
                {
                    Lock lock(mutex());
                    if (m_requested > 0)
                        m_requested -= 1;
                }
                OwnerRefcountedImpl::GetOwner()->onReceive(sample);
            }
            void Request(unsigned int count)
            {
                Lock lock(mutex());
                m_requested += count;
                requestNextSamples(lock, count, false);
            }
        private:
            void onConnected(Lock& lock) override
            {
                if (m_requested > 0)
                    requestNextSamples(lock, m_requested, false);
            }
        private:
            unsigned int m_requested;
        };
    private:
        void onReceive(NMMSS::ISample* sample)
        {
            bool needRequestSample = false;
            {
                Lock lock(mutex());
                if (!m_consumersConnected)
                    return;

                auto requestedCount = requested(lock);
                auto const send = [this, &lock](NMMSS::ISample* s) mutable
                {
                    return sendSample(lock, s, false);
                };
                bool sent = m_advisor.SendSample(sample, send);
                needRequestSample = !sent && requestedCount;
            }

            if (needRequestSample)
                m_sink.Request(1);
        }
        void onRequested(Lock& lock, unsigned int count) override
        {
            lock.unlock();
            m_sink.Request(count);
        }
        void onConnected(Lock& lock) override
        {
            connectAllLocked();
        }
        void onDisconnected(Lock& lock) override
        {
            if (!m_consumersConnected)
                return;

            m_consumersConnected = false;
            m_advisor.Restart();

            auto size = m_connections.size();

            decltype(m_connections) connections;
            std::swap(m_connections, connections);

            m_connections.reserve(size);
            for (size_t i = 0; i < size; i++)
                m_connections.push_back(NMMSS::CConnectionResource());

            lock.unlock();

            // Destruction of @connections will break all connections, 
            // it should be done at unlocked part.
        }
    public:
        CAugmentedSource(
            DECLARE_LOGGER_ARG,
            NMMSS::IPullStyleSource* source,
            NMMSS::CAugmentsRange const& augs
        )
            : NLogging::WithLogger(GET_LOGGER_PTR)
            , m_sink(this)
            , m_source(source, NCorbaHelpers::ShareOwnership())
            , m_connections(augs.size()+1)
        {
            m_filters.reserve(augs.size());
            for (auto const& aug : augs)
            {
                m_filters.emplace_back(NMMSS::CreateTweakableFilter(GET_LOGGER_PTR, aug));
            }
        }
        void Modify(NMMSS::EStartFrom startFrom, NMMSS::CAugmentsRange const& target) override
        {
            TLock guard(mutex());
            NMMSS::CAugments const& current = getAugmentsLocked();
            auto const lcs = buildLcsTable(current, target);
            auto const n = current.size();
            auto const m = target.size();
            auto i = n;
            auto j = m;
            while (i != 0 || j != 0)
            {
                if (i != 0 && j != 0 && typeid_of(current[i-1]) == typeid_of(target[j-1]))
                {
                    if (!(current[i-1] == target[j-1]))
                        modifyFilter(i-1, target[j-1]);
                    i -= 1;
                    j -= 1;
                }
                else if (j != 0 && (i == 0 || lcs[i*n+(j-1)] >= lcs[(i-1)*n+j]))
                {
                    insertFilter(i, target[j-1]);
                    j -= 1;
                }
                else
                {
                    removeFilter(i-1);
                    i -= 1;
                }
            }
            
            connectAllLocked();
        }
        NMMSS::CAugments GetAugments() const override
        {
            TLock guard(mutex());
            return getAugmentsLocked();
        }
    private:
        NMMSS::CAugments getAugmentsLocked() const 
        {
            auto augs = NMMSS::CAugments();
            augs.reserve(m_filters.size());
            for (auto const& filter : m_filters)
            {
                augs.emplace_back(filter->GetTweak());
            }
            return augs;
        }

        void modifyFilter(size_t pos, NMMSS::CAugment const& aug)
        {
            m_filters[pos]->Tweak(aug);
        }
        void insertFilter(size_t pos, NMMSS::CAugment const& aug)
        {
            m_filters.reserve(m_filters.size() + 1);
            m_connections.reserve(m_connections.size() + 1);
            m_filters.emplace(
                begin(m_filters) + pos,
                NMMSS::CreateTweakableFilter(GET_LOGGER_PTR, aug));
            m_connections[pos] = NMMSS::CConnectionResource();
            m_connections.emplace(
                begin(m_connections) + pos,
                NMMSS::CConnectionResource());
        }
        void removeFilter(size_t pos)
        {
            m_connections[pos + 1] = NMMSS::CConnectionResource();
            m_connections.erase(begin(m_connections) + pos);
            m_filters.erase(begin(m_filters) + pos);
        }
        void connectAllLocked()
        {
            for (size_t i = 0; i < m_connections.size(); ++i)
            {
                if (!m_connections[i])
                {
                    NMMSS::IPullStyleSource* source = (i == 0)
                        ? m_source.Get()
                        : m_filters[i-1]->GetSource();
                    NMMSS::IPullStyleSink* sink = (i == (m_connections.size()-1))
                        ? &m_sink
                        : m_filters[i]->GetSink();
                    m_connections[i] = NMMSS::CConnectionResource(source, sink, GET_LOGGER_PTR);
                    if (!m_connections[i])
                        throw std::runtime_error("CAugmentedSource: Can not establish connection");
                }
            }
            m_consumersConnected = true;
        }

        std::vector<std::uint8_t> buildLcsTable(NMMSS::CAugmentsRange const& current, NMMSS::CAugmentsRange const& target)
        {
            using size_type = NMMSS::CAugmentsRange::size_type;
            assert(current.size() <= std::numeric_limits<std::uint8_t>::max());
            assert(target.size() <= std::numeric_limits<std::uint8_t>::max());
            size_type const n = current.size() + 1;
            size_type const m = target.size() + 1;
            std::vector<std::uint8_t> lcs(n*m, 0);
            for (size_type i=1; i<n; ++i)
            {
                for (size_type j=1; j<m; ++j)
                {
                    if (typeid_of(current[i-1]) == typeid_of(target[j-1]))
                        lcs[i*n+j] = lcs[(i-1)*n+(j-1)] + 1;
                    else
                        lcs[i*n+j] = std::max(lcs[i*n+(j-1)], lcs[(i-1)*n+j]);
                }
            }
            return lcs;
        }
    private:
        bool m_consumersConnected = false;
        CSink m_sink;
        NMMSS::PPullStyleSource const m_source;
        std::vector<NMMSS::PTweakableFilter> m_filters;
        std::vector<NMMSS::CConnectionResource> m_connections;
        NMMSS::CSampleAdvisor m_advisor;
    };

} // anonymous namespace

namespace NMMSS {

    IAugmentedSource* CreateAugmentedSource(DECLARE_LOGGER_ARG,
        IPullStyleSource* source, CAugmentsRange const& augs)
    {
        return new CAugmentedSource(GET_LOGGER_PTR, source, augs);
    }

} // namespace NMMSS
