#include <map>
#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/type_erasure/any_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/lock_types.hpp>
#include <boost/thread/reverse_lock.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <CorbaHelpers/Reactor.h>
#include <Executors/Reactor.h>
#include "../MMSS.h"
#include "../MediaType.h"
#include "../PullStylePinsBaseImpl.h"
#include "../PtimeFromQword.h"
#include "../PulseTimer.h"
#include "Transforms.h"

namespace
{
    typedef boost::shared_ptr<NExecutors::IReactor> PReactor;

    const uint32_t MEGA = 1024 * 1024;
    const uint32_t MICROSECONDS_PER_SECOND = 1000000;

    class CTrafficTuner
        : public virtual NMMSS::ITrafficTuner
        , public NCorbaHelpers::CWeakReferableImpl
    {
        typedef NCorbaHelpers::CAutoPtr<CTrafficTuner> PTrafficTuner;
        typedef NCorbaHelpers::CWeakPtr<CTrafficTuner> PWeakTrafficTuner;

        class CTrafficFilter
            : public virtual NMMSS::IFilter
            , public virtual NCorbaHelpers::CWeakReferableImpl
        {
            class CSinkProxy : public NMMSS::IPullStyleSink, public virtual NCorbaHelpers::COwnerRefcountedImpl
            {
                CTrafficFilter*               m_owner;
                NMMSS::SAllocatorRequirements m_allocReq;

            public:

                CSinkProxy(CTrafficFilter* owner, const NMMSS::SAllocatorRequirements& req)
                    : NCorbaHelpers::COwnerRefcountedImpl(owner)
                    , m_owner(owner)
                    , m_allocReq(req)
                {
                }

                NMMSS::SAllocatorRequirements GetAllocatorRequirements() override
                {
                    return m_allocReq;
                }

                void OnConnected(TConnection* connection) override
                {
                    m_owner->OnProxyConnected(connection);
                }

                void OnDisconnected(TConnection* connection) override
                {
                    m_owner->OnProxyDisconnected(connection);
                }

                void Receive(NMMSS::ISample* sample) override
                {
                    m_owner->OnReceive(sample);
                }
            };

            class CSourceProxy : public NMMSS::IPullStyleSource, public virtual NCorbaHelpers::COwnerRefcountedImpl
            {
                CTrafficFilter* m_owner;

            public:

                CSourceProxy(CTrafficFilter* owner)
                    : NCorbaHelpers::COwnerRefcountedImpl(owner)
                    , m_owner(owner)
                {
                }

                NMMSS::SAllocatorRequirements GetAllocatorRequirements() override
                {
                    return NMMSS::SAllocatorRequirements(0);
                }

                void OnConnected(TConnection* connection) override
                {
                    m_owner->OnProxyConnected(connection);
                }

                void OnDisconnected(TConnection* connection) override
                {
                    m_owner->OnProxyDisconnected(connection);
                }

                void Request(unsigned int count) override
                {
                    m_owner->OnRequested(count);
                }
            };

            class CCalculator
            {
                float  m_currFpsLimit = 0;
                size_t m_currTraffic = 0;
                size_t m_currFrames = 0;
                size_t m_statTraffic = 0;
                size_t m_statFrames = 0;
                size_t m_statTrafficLimit = std::numeric_limits<size_t>::max();
                float  m_statFpsLimit = std::numeric_limits<float>::max();
                std::time_t m_logTime;

            public:

                CCalculator() : m_logTime(std::time(0)) {}

                float GetFpsLimit() const
                {
                    return m_currFpsLimit;
                }

                size_t GetTraffic()
                {
                    return m_currTraffic;
                }

                void RegFrame(size_t bytes)
                {
                    m_currTraffic += bytes;
                    m_currFrames++;
                }

                void SetTrafficLimit(size_t bytesPerSecond, DECLARE_LOGGER_ARG)
                {
                    if (m_currTraffic)
                    {
                        m_currFpsLimit = float(bytesPerSecond) * m_currFrames / m_currTraffic;
                    }
                    else
                    {
                        m_currFpsLimit = m_statFpsLimit != std::numeric_limits<float>::max() ? m_statFpsLimit : 0;
                    }

                    _dbg_ << "limit=" << bytesPerSecond << " traffic=" << m_currTraffic << " frames=" << m_currFrames << " fps=" << m_currFpsLimit;

                    m_statTraffic += m_currTraffic;
                    m_statFrames += m_currFrames;

                    m_currTraffic = 0;
                    m_currFrames = 0;

                    m_statTrafficLimit = m_statTrafficLimit != std::numeric_limits<size_t>::max() ? (m_statTrafficLimit + bytesPerSecond) >> 1 : bytesPerSecond;
                    m_statFpsLimit = m_statFpsLimit != std::numeric_limits<float>::max() ? (m_statFpsLimit + m_currFpsLimit) * 0.5f : m_currFpsLimit;
                }

                void Log(DECLARE_LOGGER_ARG)
                {
                    std::time_t now = std::time(0);
                    auto elapsed = now - m_logTime;

                    if (elapsed)
                    {
                        _log_ << "speed: " << float(m_statTraffic) / elapsed / MEGA * 8 << "/" << float(m_statTrafficLimit) / MEGA * 8 << " Mb/s, fps: " << float(m_statFrames) / elapsed << "/" << m_statFpsLimit;

                        m_statTrafficLimit = std::numeric_limits<size_t>::max();
                        m_statTraffic = 0;
                        m_statFrames = 0;
                        m_logTime = now;
                    }
                }
            };

            typedef NCorbaHelpers::CAutoPtr<CTrafficFilter> PSelf;
            typedef NCorbaHelpers::CWeakPtr<CTrafficFilter> PWeakSelf;

        public:

            CTrafficFilter(DECLARE_LOGGER_ARG, PReactor reactor, const std::string& id, const NMMSS::SAllocatorRequirements& req)
                : m_needs(0)
                , m_debts(0)
                , m_skips(0)
                , m_lastRequest(boost::posix_time::min_date_time)
                , m_sinkProxy(this, req)
                , m_sourceProxy(this)
                , m_reactor(reactor)
                , m_requestor(reactor->GetIO())
                , m_timer(reactor->GetIO())
            {
                CLONE_TO_THIS_LOGGER;
                ADD_THIS_LOG_PREFIX(boost::str(boost::format("[%s]") % id).c_str());

                _this_log_ << "created";
            }

            ~CTrafficFilter()
            {
                _log_ << "destroyed";
            }

            NMMSS::IPullStyleSink* GetSink() override
            {
                return &m_sinkProxy;
            }

            NMMSS::IPullStyleSource* GetSource() override
            {
                return &m_sourceProxy;
            }

            void AddUnfilteredFrameType(uint32_t major, uint32_t minor) override {}
            void RemoveUnfilteredFrameType(uint32_t major, uint32_t minor) override {}

            void OnProxyConnected(NMMSS::IConnection<NMMSS::IPullStyleSink>* connection)
            {
                if (connection)
                {
                    boost::mutex::scoped_lock lock(m_mutex);
                    m_source = NMMSS::PPullStyleSource(connection->GetOtherSide(), NCorbaHelpers::ShareOwnership());

                    m_needs += m_debts;
                    m_skips = 0;
                    m_debts = 0;

                    if (m_needs)
                        ScheduleRequest(lock);
                }
            }

            void OnProxyDisconnected(NMMSS::IConnection<NMMSS::IPullStyleSink>* connection)
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_source = 0;
            }

            void OnProxyConnected(NMMSS::IConnection<NMMSS::IPullStyleSource>* connection)
            {
                if (connection)
                {
                    boost::mutex::scoped_lock lock(m_mutex);
                    m_sink = NMMSS::PPullStyleSink(connection->GetOtherSide(), NCorbaHelpers::ShareOwnership());

                    m_skips += m_debts;
                    m_debts = 0;
                    m_needs = 0;
                }
            }

            void OnProxyDisconnected(NMMSS::IConnection<NMMSS::IPullStyleSource>* connection)
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_sink = 0;
            }

            void OnRequested(unsigned int count)
            {
                boost::mutex::scoped_lock lock(m_mutex);

                m_needs += count;

                if (m_needs == count)
                    ScheduleRequest(lock);
            }

            void OnReceive(NMMSS::ISample* sample)
            {
                boost::mutex::scoped_lock lock(m_mutex);

                if (sample)
                    m_calc.RegFrame(sample->Header().nHeaderSize + sample->Header().nBodySize);

                if (m_skips == 0)
                {
                    if (m_debts > 0)
                    {
                        --m_debts;
                    }
                    else
                    {
                        _wrn_ << "unexpected scenario";
                    }

                    if (m_sink)
                    {
                        NMMSS::PPullStyleSink sink = m_sink;
                        boost::reverse_lock<boost::mutex::scoped_lock> unlock(lock);
                        sink->Receive(sample);
                    }
                }
                else
                    --m_skips;
            }

            void SetTrafficLimit(size_t bytesPerSecond)
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_calc.SetTrafficLimit(bytesPerSecond, GET_LOGGER_PTR);
            }

            size_t GetLastTraffic()
            {
                boost::mutex::scoped_lock lock(m_mutex);
                return m_calc.GetTraffic();
            }

            void Log()
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_calc.Log(GET_LOGGER_PTR);
            }

        protected:

            void ScheduleRequest(boost::mutex::scoped_lock& lock)
            {
                float fps = m_calc.GetFpsLimit();
                PWeakSelf weak(this);

                boost::posix_time::ptime deadline = m_lastRequest + boost::posix_time::microseconds(fps > 0 ? uint32_t(MICROSECONDS_PER_SECOND / fps) : 0);
                m_timer.expires_at(deadline);
                m_timer.async_wait([weak](const boost::system::error_code& error)
                {
                    if (!error)
                    {
                        PSelf ptr(weak);
                        if (ptr)
                            ptr->Request();
                    }
                });
            }

            void Request()
            {
                boost::mutex::scoped_lock lock(m_mutex);

                if (m_source && m_needs)
                {
                    boost::posix_time::ptime now = boost::posix_time::microsec_clock::universal_time();

                    unsigned int request = m_needs;

                    float fps = m_calc.GetFpsLimit();
                    if (fps > 0)
                    {
                        request = std::max(
                            1u,
                            std::min(m_needs, uint32_t(fps * (now - m_lastRequest).total_microseconds() / MICROSECONDS_PER_SECOND))
                        );
                    }

                    m_needs -= request;
                    m_debts += request;
                    m_lastRequest = now;

                    _trc_ << "Request " << fps << "/" << request;
                    m_requestor.post(boost::bind(&NMMSS::IPullStyleSource::Request, m_source, request));

                    if (m_needs)
                        ScheduleRequest(lock);
                }
            }

        private:

            DECLARE_LOGGER_HOLDER;

            CCalculator                     m_calc;
            unsigned int                    m_needs;
            unsigned int                    m_debts;
            unsigned int                    m_skips;
            boost::posix_time::ptime        m_lastRequest;
            CSinkProxy                      m_sinkProxy;
            CSourceProxy                    m_sourceProxy;
            NMMSS::PPullStyleSink           m_sink;
            NMMSS::PPullStyleSource         m_source;
            PReactor                        m_reactor;
            boost::asio::io_service::strand m_requestor;
            boost::asio::deadline_timer     m_timer;
            boost::mutex                    m_mutex;
        };

        typedef NCorbaHelpers::CAutoPtr<CTrafficFilter> PTrafficFilter;
        typedef NCorbaHelpers::CWeakPtr<CTrafficFilter> PWeakTrafficFilter;

    protected:

        void Tune()
        {
            boost::mutex::scoped_lock lock(m_mutex);

            // calculating traffic
            size_t totalTraffic = 0;
            auto iter = m_channels.begin();
            while (iter != m_channels.end())
            {
                PTrafficFilter channel(iter->first);
                if (channel)
                {
                    iter->second = channel->GetLastTraffic();
                    totalTraffic += iter->second;
                    ++iter;
                }
                else
                {
                    iter = m_channels.erase(iter);
                }
            }

            std::time_t now = std::time(0);
            std::time_t elapsed = now - m_tuneTime;
            if (elapsed >= m_tuneDeadline || totalTraffic / elapsed > m_limit)
            {
                // logging traffic
                m_traffic += totalTraffic;

                std::time_t logspan = now - m_logTime;
                if (logspan >= m_tuneDeadline)
                {
                    auto iter = m_channels.begin();
                    while (iter != m_channels.end())
                    {
                        PTrafficFilter channel(iter->first);
                        if (channel)
                        {
                            channel->Log();
                        }
                        ++iter;
                    }

                    _log_ << "traffic: " << float(m_traffic) / logspan / MEGA * 8 << "/" << m_limit / MEGA * 8 << " Mb/s";

                    m_traffic = 0;
                    m_logTime = now;
                }

                // tunning traffic
                size_t limitPerChannel = m_channels.size() ? m_limit / m_channels.size() : m_limit;
                iter = m_channels.begin();
                while (iter != m_channels.end())
                {
                    PTrafficFilter channel(iter->first);
                    if (channel)
                    {
                        float channelShare = totalTraffic ? float(iter->second) / totalTraffic : 0;
                        size_t bytesPerSecond = size_t(channelShare * m_limit);

                        if (bytesPerSecond < limitPerChannel)
                            bytesPerSecond = limitPerChannel;

                        channel->SetTrafficLimit(bytesPerSecond);
                    }
                    ++iter;
                }

                m_tuneTime = now;
            }
        }

        void StartTuneTimer()
        {
            boost::mutex::scoped_lock lock(m_mutex);
            if (m_limit > 0)
            {
                m_tuneTimer.expires_from_now(boost::posix_time::seconds(m_tunePeriod));
                PWeakTrafficTuner weak(this);
                m_tuneTimer.async_wait([weak](const boost::system::error_code& error)
                    {
                        if (!error)
                        {
                            PTrafficTuner tuner(weak);
                            if (tuner)
                            {
                                tuner->Tune();
                                tuner->StartTuneTimer();
                            }
                        }
                    });
            }
            else
            {
                auto iter = m_channels.begin();
                while (iter != m_channels.end())
                {
                    PTrafficFilter channel(iter->first);
                    if (channel)
                        channel->SetTrafficLimit(0);
                    ++iter;
                }
            }
        }

      public:

        CTrafficTuner(DECLARE_LOGGER_ARG, PReactor reactor, size_t bytesPerSecond, size_t tunePeriod)
            : m_tunePeriod(tunePeriod)
            , m_tuneDeadline(tunePeriod * 5)
            , m_limit(bytesPerSecond)
            , m_logTime(std::time(0))
            , m_tuneTime(std::time(0))
            , m_reactor(reactor)
            , m_tuneTimer(m_reactor->GetIO())
        {
            CLONE_TO_THIS_LOGGER;
            ADD_THIS_LOG_PREFIX(boost::str(boost::format("TrafficTuner.%08p") % this).c_str());

            _this_log_ << "traffic_limit: " << m_limit / MEGA * 8 << " Mb/s";

            if (m_limit > 0)
            {
                StartTuneTimer();
            }
        }

        NMMSS::IFilter* CreateChannel(const std::string& id, const NMMSS::SAllocatorRequirements& req) override
        {
            boost::mutex::scoped_lock lock(m_mutex);

            PTrafficFilter channel(new CTrafficFilter(GET_LOGGER_PTR, m_reactor, id, req));
            m_channels.insert(std::make_pair(PWeakTrafficFilter(channel), 0));
            channel->SetTrafficLimit(m_channels.size() ? m_limit / m_channels.size() : m_limit);
            return channel.Dup();
        }

        void SetTrafficLimit(size_t bytesPerSecond) override
        {
            boost::mutex::scoped_lock lock(m_mutex);

            std::swap(m_limit, bytesPerSecond);
            if (m_limit && !bytesPerSecond)
            {
                StartTuneTimer();
            }

            _log_ << "traffic_limit: " << m_limit / MEGA * 8 << " Mb/s";
        }

    private:

        DECLARE_LOGGER_HOLDER;

        int64_t m_tunePeriod = 0;
        int64_t m_tuneDeadline = 0;
        size_t m_limit = 0;
        size_t m_traffic = 0;
        std::time_t m_logTime;
        std::time_t m_tuneTime;
        PReactor m_reactor;
        boost::asio::deadline_timer m_tuneTimer;
        std::map<PWeakTrafficFilter, size_t> m_channels;
        boost::mutex m_mutex;
    };
}


namespace NMMSS
{
    ITrafficTuner* CreateTrafficTuner(DECLARE_LOGGER_ARG, size_t bytesPerSecond, size_t tunePeriod)
    {
        PReactor reactor(NExecutors::CreateReactor("TrafficTuner", GET_LOGGER_PTR));
        return new CTrafficTuner(GET_LOGGER_PTR, reactor, bytesPerSecond, tunePeriod);
    }
}
