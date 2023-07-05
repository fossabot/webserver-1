// documentation https://doc.axxonsoft.com/confluence/pages/viewpage.action?pageId=138449538

#include <string>

#include <boost/thread.hpp> 
#include <boost/asio.hpp>
#include <boost/thread/reverse_lock.hpp>
#include <boost/smart_ptr/scoped_ptr.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <CorbaHelpers/RefcountedImpl.h>
#include <Utils/TimedExecution.h>
#include <Utils/TimeJitter.h>
#include "../Sample.h"
#include "../PtimeFromQword.h"
#include "../ConnectionBroker.h"
#include "../MediaType.h"
#include "SourceFactory.h"
#include "MMTransport.h"
#include "StatisticsCollectorImpl.h"
#include "../EndOfStreamSample.h"

namespace
{
    const unsigned long MAX_INTERVALS = 32768;

    class CSequencedSource : 
        public virtual NMMSS::IStatisticsProvider, 
        public virtual NMMSS::ISeekableSource, 
        public virtual NCorbaHelpers::CWeakReferableImpl,
        public NLogging::WithLogger
    {
        using PWeakSequencedSource = NCorbaHelpers::CWeakPtr<CSequencedSource>;
        using PSequencedSource = NCorbaHelpers::CAutoPtr<CSequencedSource>;
        using TTimePeriodList = std::vector<boost::posix_time::time_period>;
        using TLock = boost::mutex::scoped_lock;
        using PStatisticsCollector = boost::scoped_ptr<NMMSS::IStatisticsCollectorImpl>;

        struct SSharedData
        {
            boost::mutex                Mutex;
            int                         RequestedCount = 0;
            NMMSS::EPlayModeFlags       Mode = NMMSS::PMF_NONE;
            boost::posix_time::ptime    TimePosition;
            NMMSS::EEndpointStartPosition FramePosition = NMMSS::espExactly;
            std::uint32_t               SessionOut = 0;
            std::uint32_t               SessionIn = 0;
            TConnection*                Connection = nullptr;

            bool IsRunnable(std::uint32_t sessionId) const { return SessionOut == sessionId && Connection; }
        };
        using PSharedData = std::shared_ptr<SSharedData>;

        /// The structure is designed to store the time interval and its Storage Source processor
        struct SIntervalInfo
        {
            boost::posix_time::time_period Interval = { boost::posix_time::ptime(boost::date_time::min_date_time), boost::posix_time::ptime(boost::date_time::min_date_time) };
            size_t Processor = std::numeric_limits<size_t>::max();
        };
        typedef std::vector<SIntervalInfo>  TPlanedIntervals;

        class CStorageSourceProcessor:
            public NMMSS::IPullStyleSink,
            public virtual NCorbaHelpers::CRefcountedImpl,
            public NLogging::WithLogger
        {
        public:
            CStorageSourceProcessor(DECLARE_LOGGER_ARG, NMMSS::PStorageSourceClient storageSource, PWeakSequencedSource owner, PSharedData sharedData, size_t index):
                NLogging::WithLogger(GET_LOGGER_PTR),
                m_storageSource(storageSource),
                m_sharedData(sharedData),
                m_index(index),
                m_interval(boost::posix_time::ptime(boost::date_time::min_date_time), boost::posix_time::ptime(boost::date_time::min_date_time)),
                m_owner(owner)
            {
            }

            void Destroy()
            {
                try
                {
                    if (m_sinkEP)
                    {
                        m_sinkEP->Destroy();

                        m_sourceEP.reset();
                        m_sinkEP.Reset();
                    }
                }
                catch (...) {}
            }

            ~CStorageSourceProcessor()
            {
                Destroy();
            }

            NMMSS::SAllocatorRequirements GetAllocatorRequirements()
            {
                static const NMMSS::SAllocatorRequirements var(0);
                return var;
            }

            void OnConnected(TConnection* connection) override
            {
                if (!connection)
                    return;

                TLock lock(m_sharedData->Mutex);
                m_pullSrc = NMMSS::PPullStyleSource(connection->GetOtherSide(), NCorbaHelpers::ShareOwnership());
                DoRequest(lock, 0);
            }

            void OnDisconnected(TConnection*) override
            {
                TLock lock(m_sharedData->Mutex);
                m_pullSrc.Reset();
            }

            bool CheckSampleSessionId(NMMSS::ISample *sample)
            {
                if (NMMSS::SessionIdExHeader *ext = NMMSS::FindExtensionHeader<NMMSS::SessionIdExHeader>(sample))
                {
                    return ext->SessionId == m_sharedData->SessionIn;
                }
                return false;
            }

            bool CheckIfEndReached(NMMSS::ISample* sample) const
            {
                boost::posix_time::ptime timestamp = NMMSS::PtimeFromQword(sample->Header().dtTimeBegin);
                bool reverse = m_sharedData->Mode & NMMSS::PMF_REVERSE;
                bool eos = NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&sample->Header());
                return eos || (reverse ? m_interval.begin() > timestamp : m_interval.end() < timestamp);
            }

            void Receive(NMMSS::ISample* sample) override
            {
                TLock lock(m_sharedData->Mutex);
                if (CheckSampleSessionId(sample) && !m_endReached)
                {
                    if (PSequencedSource owner = m_owner)
                    {
                        if (CheckIfEndReached(sample))
                        {
                            m_endReached = true;
                            owner->postRerunnable(m_sharedData->SessionOut, &CSequencedSource::tryNextInterval);
                        }
                        else
                        {
                            owner->onReceive(lock, sample);
                        }
                    }
                }
            }

            void BuildPeriod(const boost::posix_time::time_period& totalPeriod)
            {
                NMMSS::IStorageSourceClient::IntervalList_t intervals;
                GetHistory(totalPeriod.begin(), totalPeriod.end(), MAX_INTERVALS, intervals);

                m_intervals.clear();
                for (const auto& interval : intervals)
                {
                    auto period = totalPeriod.intersection({ NMMSS::PtimeFromQword(interval.beginTime), NMMSS::PtimeFromQword(interval.endTime) });
                    if (!period.is_null())
                    {
                        m_intervals.push_back(period);
                    }
                }
            }

            template<typename F>
            void execAndCheckExceptions(const char* location, const F& f)
            {
                try
                {
                    f();
                }
                catch (const CORBA::Exception& e)
                {
                    _wrn_ << location << ". " << e._name();
                }
                catch (const NMMSS::XServerError& e)
                {
                    _wrn_ << location << ". " << e.what();
                    if (e.Status() == NMMSS::EServerStatus::BUSY_TRY_LATER)
                    {
                        throw;
                    }
                }
                catch (const std::exception& e)
                {
                    _wrn_ << location << ". " << e.what();
                }
            }

            void GetHistory(const boost::posix_time::ptime& beginTime, const boost::posix_time::ptime& endTime,
                std::uint32_t maxCount, NMMSS::IStorageSourceClient::IntervalList_t& intervals)
            {
                execAndCheckExceptions(__FUNCTION__, [&]
                {
                    std::string sTime = boost::posix_time::to_iso_string(beginTime);
                    std::string eTime = boost::posix_time::to_iso_string(endTime);
                    if (m_storageSource)
                    {
                        m_storageSource->GetHistory(sTime, eTime, maxCount, 0, intervals);
                    }
                });
            }

            void GetBoundary(boost::posix_time::ptime& lower, boost::posix_time::ptime& upper)
            {
                NMMSS::IStorageSourceClient::IntervalList_t intervals;
                GetHistory(boost::posix_time::min_date_time, boost::posix_time::max_date_time, 1, intervals);
                if (!intervals.empty())
                {
                    lower = NMMSS::PtimeFromQword(intervals[0].beginTime);
                    upper = NMMSS::PtimeFromQword(intervals[0].endTime);
                }
            }

            boost::posix_time::time_duration getIntervalShift(const boost::posix_time::time_period& interval, const boost::posix_time::ptime& t)
            {
                return (t < interval.begin()) ? interval.begin() - t : boost::posix_time::seconds(0);
            }

            bool OfferBestInterval(SIntervalInfo* srcInterval)
            {
                boost::posix_time::time_period interval = GetSuitableEnumeratorInterval();
                if (!interval.is_null())
                {
                    auto currShift = getIntervalShift(interval, m_sharedData->TimePosition);
                    auto prevShift = getIntervalShift(srcInterval->Interval, m_sharedData->TimePosition);
                    if (std::numeric_limits<size_t>::max() == srcInterval->Processor || currShift < prevShift
                        || (currShift == prevShift && interval.end() > srcInterval->Interval.end()))
                    {
                        srcInterval->Processor = m_index;
                        srcInterval->Interval = interval;
                        return true;
                    }
                }
                return false;
            }

            void Start(TLock& lock, const boost::posix_time::time_period& interval)
            {
                _log_ << "Sequence planner " << m_sharedData.get() << ": interval " << interval << " processor " << m_index;
                m_interval = interval;
                m_endReached = false;
                resetSinkConnection(lock);
            }

            void DoRequest(TLock& lock, int count)
            {
                if (m_sharedData->IsRunnable(m_savedSessionOut) && m_pullSrc)
                {
                    if (m_justStarted)
                    {
                        count = m_sharedData->RequestedCount;
                        m_justStarted = false;
                    }
                    if (count)
                    {
                        boost::reverse_lock<TLock> unlock(lock);
                        m_pullSrc->Request(count);
                    }
                }
            }

        private:
            void createConnection(TLock& lock, const std::string& sTime, NMMSS::EEndpointStartPosition startPos, NMMSS::EPlayModeFlags mode)
            {
                if(!m_sinkEP)
                {
                    NMMSS::PStorageEndpointClient sourceEP;
                    NMMSS::PSinkEndpoint sinkEP;

                    {
                        boost::reverse_lock<TLock> unlock(lock);
                        if (m_storageSource)
                        {
                            sourceEP = m_storageSource->GetSourceReaderEndpoint(sTime, startPos, false, mode, NMMSS::IStorageSourceClient::ERP_High);
                        }

                        if (sourceEP)
                        {
                            sinkEP = NMMSS::PSinkEndpoint(NMMSS::CreatePullConnectionByEndpointClient(GET_LOGGER_PTR,
                                sourceEP, this, MMSS::EAUTO, nullptr, NMMSS::EFrameBufferingPolicy::Unbuffered));
                        }
                    }
                    if (!m_sinkEP)
                    {
                        m_sourceEP = sourceEP;
                        m_sinkEP = sinkEP;
                    }
                }
            }

            void resetSinkConnection(TLock& lock)
            {
                std::string sTime = boost::posix_time::to_iso_string(m_sharedData->Mode & NMMSS::PMF_REVERSE ? m_interval.end() : m_interval.begin());
                NMMSS::EPlayModeFlags mode = m_sharedData->Mode;
                std::uint32_t sessionId = ++m_sharedData->SessionIn;
                std::uint32_t sessionOutId = m_sharedData->SessionOut;
                NMMSS::EEndpointStartPosition startPos = m_sharedData->FramePosition;
                auto retryConnection = false;

                auto connectAndRequest = [&]()
                {
                    createConnection(lock, sTime, startPos, mode);
                    {
                        auto sourceEP = m_sourceEP;
                        boost::reverse_lock<TLock> unlock(lock);
                        if (!sourceEP)
                        {
                            _wrn_ << __FUNCTION__ << ". Nil StorageEndpoint reference aquired";
                            retryConnection = true;
                            return;
                        }

                        try
                        {
                            sourceEP->Seek(sTime, startPos, mode, sessionId);
                        }
                        catch (const NMMSS::XServerError& e)
                        {
                            if (e.Status() == NMMSS::NOT_FOUND)
                            {
                                retryConnection = true;
                                return;
                            }
                        }
                    }

                    if (m_sourceEP)
                    {
                        m_savedSessionOut = sessionOutId;
                        m_justStarted = true;
                        m_sharedData->FramePosition = NMMSS::EEndpointStartPosition::espExactly;
                        DoRequest(lock, 0);
                    }
                };

                execAndCheckExceptions(__FUNCTION__, connectAndRequest);
                if (retryConnection)
                {
                    if (m_sinkEP)
                    {
                        m_sinkEP->Destroy();
                        m_sinkEP.Reset();
                    }

                    execAndCheckExceptions(__FUNCTION__, connectAndRequest);
                }
            }

            boost::posix_time::time_period GetSuitableEnumeratorInterval()
            {
                auto it = std::upper_bound(m_intervals.begin(), m_intervals.end(), m_sharedData->TimePosition,
                    [](const boost::posix_time::ptime& t, const boost::posix_time::time_period& period) { return t < period.end(); });
                return (it != m_intervals.end()) ? *it : boost::posix_time::time_period(boost::posix_time::ptime(), boost::posix_time::ptime());
            }

            NMMSS::PStorageSourceClient     m_storageSource;
            PSharedData                     m_sharedData;
            size_t                          m_index;
            NMMSS::PStorageEndpointClient   m_sourceEP;
            NMMSS::PSinkEndpoint            m_sinkEP;
            TTimePeriodList                 m_intervals;
            NMMSS::PPullStyleSource         m_pullSrc;
            boost::posix_time::time_period  m_interval;
            bool                            m_endReached = false;
            bool m_justStarted = false;
            PWeakSequencedSource m_owner;
            std::uint32_t m_savedSessionOut = 0;
        };
        typedef NCorbaHelpers::CAutoPtr<CStorageSourceProcessor> PStorageSourceProcessor;

    public:
        CSequencedSource(DECLARE_LOGGER_ARG, const NMMSS::StorageSourcesList_t& storageSources, const std::string& beginTime,
            NMMSS::EEndpointStartPosition position, NMMSS::EPlayModeFlags mode):
                NLogging::WithLogger(GET_LOGGER_PTR),
                m_measurer(NMMSS::CreateStreamQualityMeasurer())
        {
            const int MAX_QUEUE_LENGTH = 5;
            auto executor = NExecutors::CreateDynamicThreadPool(GET_THIS_LOGGER_PTR, "SeqPlanner", MAX_QUEUE_LENGTH, 0, 1);
            m_timer = NUtils::CreateSteadyTimerExecutor(executor);

            _log_ << "Create sequence planner " << m_sharedData.get();

            m_sharedData->TimePosition = boost::posix_time::from_iso_string(beginTime);
            m_sharedData->FramePosition = position;
            m_sharedData->Mode = mode;

            for (size_t i = 0; i < storageSources.size(); i++)
            {
                PStorageSourceProcessor procPtr(new CStorageSourceProcessor(GET_THIS_LOGGER_PTR, storageSources[i], PWeakSequencedSource(this), m_sharedData, i));
                m_storageProcs.push_back(procPtr);
            }
        }

        virtual ~CSequencedSource()
        {
            _log_ << "Delete sequence planner " << m_sharedData.get();

            for (auto v : m_storageProcs)
            {
                v->Destroy();
            }
        }

        const NMMSS::IStatisticsCollector* GetStatisticsCollector() const
        {
            return m_measurer.get();
        }

        NMMSS::SAllocatorRequirements GetAllocatorRequirements()
        {
            static const NMMSS::SAllocatorRequirements var(0);
            return var;
        }

        void OnConnected(TConnection* connection) override
        {
            if (!connection)
                return;

            TLock lock(m_sharedData->Mutex);
            m_sharedData->Connection = connection;
            seek(lock);
        }

        void OnDisconnected(TConnection*) override
        {
            TLock lock(m_sharedData->Mutex);
            m_sharedData->Connection = 0;
            ++m_sharedData->SessionOut;
            ++m_sharedData->SessionIn;
        }

        void Request(unsigned int count) override
        {
            TLock lock(m_sharedData->Mutex);
            m_sharedData->RequestedCount += count;
            if (!m_planedIntervals.empty() && m_activeInterval >= 0 && m_activeInterval < (int)m_planedIntervals.size())
            {
                m_storageProcs[m_planedIntervals[m_activeInterval].Processor]->DoRequest(lock, count);
            }
        }

        void Seek(boost::posix_time::ptime const& startTime, NMMSS::EEndpointStartPosition startPos, NMMSS::EPlayModeFlags mode, std::uint32_t sessionId) override
        {
            _log_ << "Seek sequence planner " << m_sharedData.get() << " to " << startTime << "|" << startPos << "|" << mode << "|" << sessionId;

            TLock lock(m_sharedData->Mutex);

            m_sharedData->RequestedCount = 0;
            m_sharedData->SessionOut = sessionId;
            m_sharedData->TimePosition = startTime;
            m_sharedData->FramePosition = startPos;
            m_sharedData->Mode = mode;

            if (m_sharedData->Connection)
                seek(lock);
        }

    private:
        void seek(const TLock& lock)
        {
            ++m_sharedData->SessionIn;
            clearIntervals();
            postRerunnable(m_sharedData->SessionOut, &CSequencedSource::startPlaying);
        }

        void clearIntervals()
        {
            m_activeInterval = -1;
            m_planedIntervals.clear();
        }

        class TimerState;
        using PTimerState = std::shared_ptr<TimerState>;

        class TimerState
        {
        public:
            using Duration = std::chrono::milliseconds;
            const Duration INITIAL_RETRY_PERIOD = std::chrono::milliseconds(500);
            const Duration MAX_RETRY_PERIOD = std::chrono::milliseconds(3000);
            static const int MAX_ATTEMPTS = 4;

            static bool GetNextTimeout(const PTimerState& state, Duration& timeout)
            {
                if (state && (++(state->m_attempts) > MAX_ATTEMPTS))
                {
                    return false;
                }

                timeout = state ? state->m_timeout.GetTimeoutAndBackoff() : Duration(0);
                return true;
            }

        private:
            int m_attempts{};
            NUtils::TimeoutGenerator<Duration> m_timeout = 
                NUtils::CreateTimeoutGeneratorWithBackoff(INITIAL_RETRY_PERIOD, MAX_RETRY_PERIOD, NUtils::JitterType::Equal);
        };

        using TRerunnableMethod = void(CSequencedSource::*)(TLock&, std::uint32_t);

        void rerunIfBusy(std::uint32_t sessionId, TRerunnableMethod method, PTimerState timerState = {})
        {
            try
            {
                TLock lock(m_sharedData->Mutex);
                if (m_sharedData->IsRunnable(sessionId))
                {
                    (this->*method)(lock, sessionId);
                }
            }
            catch (const NMMSS::XServerError& e)
            {
                if (e.Status() == NMMSS::EServerStatus::BUSY_TRY_LATER)
                {
                    delayedPostRerunnable(sessionId, method, timerState);
                }
            }
        }

        void delayedPostRerunnable(std::uint32_t sessionId, TRerunnableMethod method, PTimerState timerState = {})
        {
            postRerunnable(sessionId, method, timerState ? timerState : std::make_shared<TimerState>());
        }

        void postRerunnable(std::uint32_t sessionId, TRerunnableMethod method, PTimerState timerState = {})
        {
            TimerState::Duration timeout;
            if (!TimerState::GetNextTimeout(timerState, timeout))
            {
                _err_ << "CSequencedSource failed to retry server request after " << TimerState::MAX_ATTEMPTS << " attempts";
                return;
            }

            auto pThis = PSequencedSource(this, NCorbaHelpers::ShareOwnership());
            checkTimerError(m_timer->Start(timeout, [pThis, sessionId, method, timerState](const boost::system::error_code& error)
            {
                if(pThis->checkTimerError(error))
                {
                    pThis->rerunIfBusy(sessionId, method, timerState);
                }
            }));
        }

        bool checkTimerError(const boost::system::error_code& error)
        {
            if (error && error != boost::system::errc::operation_canceled)
                _wrn_ << "CSequencedSource failed to post and exec operation: " << error.message();

            return (error == boost::system::errc::success || error == boost::system::errc::operation_canceled);
        }

        void startPlaying(TLock& lock, std::uint32_t sessionId)
        {
            buildIntervalsSequence(lock, sessionId); 
            postRerunnable(sessionId, &CSequencedSource::tryNextInterval);
        }

        bool startNextInterval(TLock& lock)
        {
            if (m_activeInterval + 1 < (int)m_planedIntervals.size())
            {
                auto interval = m_planedIntervals[m_activeInterval + 1];
                m_storageProcs[interval.Processor]->Start(lock, interval.Interval);
                ++m_activeInterval;
                m_discontinuity = true;
                return true;
            }
            clearIntervals();
            return false;
        }

        void buildIntervalsSequence(TLock& lock, std::uint32_t sessionId)
        {
            clearIntervals();

            if (!canBuildIntervals(lock, sessionId))
            {
                return;
            }

            static const boost::posix_time::time_duration HISTORY_PORTION = boost::posix_time::hours(24);

            if (m_sharedData->Mode & NMMSS::PMF_REVERSE)
            {
                m_sharedData->TimePosition -= HISTORY_PORTION;
            }
            if (m_sharedData->FramePosition == NMMSS::espOneFrameBack)
            {
                m_sharedData->TimePosition = std::min(m_sharedData->TimePosition, m_upperTime - boost::posix_time::milliseconds(1));
            }

            boost::posix_time::ptime beginTime = m_sharedData->TimePosition;
            boost::posix_time::ptime endTime = beginTime + HISTORY_PORTION;

            for (auto proc : m_storageProcs)
            {
                boost::reverse_lock<TLock> unlock(lock);
                proc->BuildPeriod({ beginTime, endTime });
            }

            if (m_sharedData->IsRunnable(sessionId))
            {
                bool offered;
                do
                {
                    offered = false;
                    SIntervalInfo info;

                    for (auto proc : m_storageProcs)
                        offered |= proc->OfferBestInterval(&info);

                    if (offered)
                    {
                        m_sharedData->TimePosition = info.Interval.end();

                        if (!m_planedIntervals.empty())
                        {
                            boost::posix_time::time_period& last = m_planedIntervals.back().Interval;
                            if (last.end() > info.Interval.begin())
                            {
                                last = boost::posix_time::time_period(last.begin(), info.Interval.begin());
                            }
                        }

                        m_planedIntervals.push_back(info);
                    }
                }
                while (offered);

                if (m_sharedData->Mode & NMMSS::PMF_REVERSE)
                {
                    m_sharedData->TimePosition = std::max(beginTime, m_lowerTime);
                    std::reverse(m_planedIntervals.begin(), m_planedIntervals.end());
                }
                else
                {
                    m_sharedData->TimePosition = std::min(endTime, m_upperTime);
                }
            }
        }

        bool checkTimeLimits() const
        {
            return m_sharedData->Mode & NMMSS::PMF_REVERSE ? m_lowerTime < m_sharedData->TimePosition
                : (m_upperTime > m_sharedData->TimePosition) || (!m_upperTime.is_not_a_date_time() && m_sharedData->FramePosition == NMMSS::espOneFrameBack);
        }

        bool canBuildIntervals(TLock& lock, std::uint32_t sessionId)
        {
            return buildBoundary(lock, sessionId) && m_sharedData->IsRunnable(sessionId) && checkTimeLimits();
        }

        bool buildBoundary(TLock& lock, std::uint32_t sessionId)
        {
            m_lowerTime = boost::posix_time::ptime();
            m_upperTime = boost::posix_time::ptime();

            for (auto proc : m_storageProcs)
            {
                boost::posix_time::ptime lower;
                boost::posix_time::ptime upper;

                if (m_sharedData->IsRunnable(sessionId))
                {
                    boost::reverse_lock<TLock> unlock(lock);
                    proc->GetBoundary(lower, upper);
                }

                if (!lower.is_not_a_date_time() && (m_lowerTime.is_not_a_date_time() || lower < m_lowerTime))
                    m_lowerTime = lower;

                if (!upper.is_not_a_date_time() && (m_upperTime.is_not_a_date_time() || upper > m_upperTime))
                    m_upperTime = upper;
            }

            return !m_lowerTime.is_not_a_date_time() && !m_upperTime.is_not_a_date_time();
        }

        void onReceive(TLock& lock, NMMSS::ISample* sample)
        {
            NMMSS::PPullStyleSink sink(m_sharedData->Connection->GetOtherSide(), NCorbaHelpers::ShareOwnership());
            if (sink && m_sharedData->RequestedCount > 0)
            {
                NMMSS::SetSampleSessionId(sample, m_sharedData->SessionOut);
                if (m_discontinuity)
                {
                    sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
                    m_discontinuity = false;
                }
                --m_sharedData->RequestedCount;

                m_measurer->Update(sample);

                boost::reverse_lock<TLock> unlock(lock);
                sink->Receive(sample);
            }
        }

        void tryNextInterval(TLock& lock, std::uint32_t sessionId)
        {
            if (!startNextInterval(lock))
            {
                bool reverse = m_sharedData->Mode & NMMSS::PMF_REVERSE;
                NMMSS::PSample eos(new NMMSS::EndOfStreamSample(NMMSS::PtimeToQword(m_sharedData->TimePosition) + (reverse ? -1 : 1)));
                onReceive(lock, eos.Get());

                delayedPostRerunnable(sessionId, &CSequencedSource::startPlaying);
            }
        }

        NUtils::PSteadyTimerExecutor            m_timer;
        PStatisticsCollector                    m_measurer;
        PSharedData m_sharedData = std::make_shared<SSharedData>();
        std::vector<PStorageSourceProcessor>    m_storageProcs;
        TPlanedIntervals                        m_planedIntervals;
        boost::posix_time::ptime                m_lowerTime;
        boost::posix_time::ptime                m_upperTime;
        int m_activeInterval = -1;
        bool m_discontinuity = false;
    };
}

namespace NMMSS
{
    NMMSS::ISeekableSource* CreatePlannedSequenceSource(DECLARE_LOGGER_ARG,
        const StorageSourcesList_t& storageSources,
        const std::string& beginTime,
        NMMSS::EEndpointStartPosition position,
        NMMSS::EPlayModeFlags mode)
    {
        return new CSequencedSource(GET_LOGGER_PTR, storageSources, beginTime, position, mode);
    }
}
