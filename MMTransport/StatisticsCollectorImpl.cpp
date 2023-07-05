#include "StatisticsCollectorImpl.h"

#include "../MediaType.h"
#include "../FpsCounter.h"
#include "../FilterImpl.h"

#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>


namespace NMMSS
{
    namespace
    {
        const size_t SAMPLE_COUNT_FOR_CALCULATE = 250;
        const int64_t MIN_STATS_SEND_DELAY_MS = 15000;
        std::chrono::milliseconds STAT_TTL{ MIN_STATS_SEND_DELAY_MS * 2 };

        class StatisticsCollector : public NMMSS::IStatisticsCollectorImpl
        {
        public:
            StatisticsCollector(const std::string& federalName, NStatisticsAggregator::PStatisticsAggregator aggregator, bool calcKeyFps);
            ~StatisticsCollector();

            // IStatisticsCollectorImpl implementation.
            virtual NMMSS::StatisticsInfo GetStatistics() const override;
            void Update(NMMSS::ISample* sample) override;
        
        private:
            void processCodedHeader(const NMMSS::NMediaType::Video::SCodedHeader* sh, const uint8_t*);

        private:
            const std::string m_federalName;
            mutable boost::mutex  m_statisticsInfoGuard;
            mutable NMMSS::StatisticsInfo m_statisticsInfo;
            uint16_t m_sampleCount;
            uint64_t m_dataSize;
            boost::posix_time::ptime m_last;
            std::unique_ptr<NMMSS::IFpsCounter> m_fps;
            std::unique_ptr<NMMSS::IFpsCounter> m_keyFps;
            NStatisticsAggregator::PStatisticsAggregator m_statAggregator;
            bool m_firstStatisitcsOrReportImmediate;
            uint32_t m_lastStreamType;
        };


        class CStreamQualityMeasurer : public NMMSS::IStatisticsCollectorImpl
        {
            NMMSS::StatisticsInfo m_stats;
            uint64_t              m_firstTs = 0;
            uint64_t              m_lastTs = 0;
            uint16_t              m_frameCount = 0;
            uint64_t              m_dataSize = 0;
            uint32_t              m_width = 0;
            uint32_t              m_height = 0;
            uint32_t              m_mediaType = 0;
            uint32_t              m_streamType = 0;
            mutable boost::mutex  m_mutex;

        protected:

            void UpdateStats()
            {
                uint64_t span = m_lastTs - m_firstTs;
                if (span > 0)
                {
                    boost::mutex::scoped_lock lock(m_mutex);

                    m_stats.fps = 1000.0f * m_frameCount / span;
                    m_stats.bitrate = m_dataSize * 8 * 1000 / span;

                    m_stats.mediaType = m_mediaType;
                    m_stats.streamType = m_streamType;
                    m_stats.width = m_width;
                    m_stats.height = m_height;
                }
            }

            void Reset()
            {
                m_frameCount = 0;
                m_dataSize = 0;
                m_firstTs = 0;
                m_lastTs = 0;
                m_mediaType = 0;
                m_streamType = 0;
                m_width = 0;
                m_height = 0;
            }

        public:

            CStreamQualityMeasurer() : m_stats({0, 0.0f, 0, 0, 0, 0})
            {
            }

            NMMSS::StatisticsInfo GetStatistics() const override
            {
                boost::mutex::scoped_lock lock(m_mutex);
                return m_stats;
            }

            void Update(NMMSS::ISample* sample) override
            {
                if (!sample)
                    return;

                SMediaSampleHeader& header = sample->Header();
                if (NMediaType::CheckMediaType<NMediaType::Auxiliary::EndOfStream>(&header))
                {
                    UpdateStats();
                    Reset();
                    return;
                }
                else if (header.HasFlag(NMMSS::SMediaSampleHeader::EFInitData))
                {
                    return;
                }
                
                if (header.HasFlag(NMMSS::SMediaSampleHeader::EFDiscontinuity) || (header.IsKeySample() && m_lastTs - m_firstTs >= MIN_STATS_SEND_DELAY_MS))
                {
                    UpdateStats();
                    Reset();
                }
                
                if (m_frameCount++ == 0)
                {
                    if (header.nMajor == NMMSS::NMediaType::Video::ID)
                    {
                        auto functor = [this](const NMMSS::NMediaType::Video::SCodedHeader* header, const uint8_t*)
                        {
                            m_width = header->nCodedWidth;
                            m_height = header->nCodedHeight;
                        };
                        NMMSS::NMediaType::ProcessSampleOfSubtype<NMMSS::NMediaType::Video::SCodedHeader>(sample, functor);
                    }
                    
                    m_mediaType = header.nMajor;
                    m_streamType = header.nSubtype;
                    m_firstTs = header.dtTimeBegin;
                    m_lastTs = header.dtTimeBegin;
                }
                else
                {
                    m_firstTs = std::min(m_firstTs, header.dtTimeBegin);
                    m_lastTs = std::max(m_lastTs, header.dtTimeBegin);
                }
                
                m_dataSize += header.nBodySize;

                if (m_frameCount % SAMPLE_COUNT_FOR_CALCULATE == 0)
                {
                    UpdateStats();
                }
            }
        };
    }

    IStatisticsCollectorImpl* CreateStreamQualityMeasurer()
    {
        return new CStreamQualityMeasurer();
    }

    StatisticsCollector::StatisticsCollector(const std::string& federalName, NStatisticsAggregator::PStatisticsAggregator aggregator, bool calcKeyFps)
        : m_federalName(federalName)
        , m_statisticsInfo()
        , m_sampleCount(SAMPLE_COUNT_FOR_CALCULATE - 1)
        , m_dataSize(0)
        , m_last(boost::posix_time::microsec_clock::local_time())
        , m_fps(NMMSS::CreateFpsCounter("", 0, 0))
        , m_keyFps(calcKeyFps ? NMMSS::CreateFpsCounter("", 0, 0) : nullptr)
        , m_statAggregator(aggregator)
        , m_firstStatisitcsOrReportImmediate(true)
        , m_lastStreamType(0)
    { }

    StatisticsCollector::~StatisticsCollector()
    {
        if (m_statAggregator)
        {
            using namespace NStatisticsAggregator;
            m_statAggregator->Push(std::move(StatPoint(LiveFPS, m_federalName, STAT_TTL).AddValue(0)));
            m_statAggregator->Push(std::move(StatPoint(LiveBitrate, m_federalName, STAT_TTL).AddValue(0)));
            m_statAggregator->Push(std::move(StatPoint(LiveWidth, m_federalName, STAT_TTL).AddValue(0)));
            m_statAggregator->Push(std::move(StatPoint(LiveHeight, m_federalName, STAT_TTL).AddValue(0)));
            m_statAggregator->Push(std::move(StatPoint(LiveMediaType, m_federalName, STAT_TTL).AddValue(0)));
            m_statAggregator->Push(std::move(StatPoint(LiveStreamType, m_federalName, STAT_TTL).AddValue(0)));
        }
    }

    void StatisticsCollector::Update(NMMSS::ISample* sample)
    {
        if (!sample)
            return;

        m_fps->Increment();
        if (m_keyFps && sample->Header().IsKeySample())
            m_keyFps->Increment();

        {
            boost::mutex::scoped_lock lock(m_statisticsInfoGuard);
            ++m_sampleCount;
            m_dataSize += sample->Header().nBodySize;
            if (sample->Header().nMajor == NMMSS::NMediaType::Video::ID)
            {
                auto handler = boost::bind(&StatisticsCollector::processCodedHeader, this, _1, _2);
                NMMSS::NMediaType::ProcessSampleOfSubtype<NMMSS::NMediaType::Video::SCodedHeader>(sample, handler);
            }
        }

        const auto now = boost::posix_time::microsec_clock::local_time();
        const auto msElapsed = (now - m_last).total_milliseconds();

        m_firstStatisitcsOrReportImmediate = m_lastStreamType != sample->Header().nSubtype;
        m_lastStreamType = sample->Header().nSubtype;
        if (!m_firstStatisitcsOrReportImmediate &&
            msElapsed < MIN_STATS_SEND_DELAY_MS &&
            m_sampleCount % SAMPLE_COUNT_FOR_CALCULATE != 0)
        {
            return;
        }

        m_fps->ForseCalcFPSNow();
        if (m_keyFps) m_keyFps->ForseCalcFPSNow();

        {
            boost::mutex::scoped_lock lock(m_statisticsInfoGuard);

            if (0 != msElapsed)
                m_statisticsInfo.bitrate = 8 * 1000 * m_dataSize / msElapsed;
            m_statisticsInfo.mediaType = sample->Header().nMajor;
            m_statisticsInfo.streamType = sample->Header().nSubtype;

            if (m_statAggregator)
            {
                using namespace NStatisticsAggregator;
                m_statAggregator->Push(std::move(StatPoint(LiveFPS, m_federalName, STAT_TTL).AddValue(m_fps->FPS())));
                if (m_keyFps)
                    m_statAggregator->Push(std::move(StatPoint(LiveKeyFPS, m_federalName, STAT_TTL).AddValue(m_keyFps->FPS())));
                m_statAggregator->Push(std::move(StatPoint(LiveBitrate, m_federalName, STAT_TTL).AddValue(m_statisticsInfo.bitrate)));
                m_statAggregator->Push(std::move(StatPoint(LiveWidth, m_federalName, STAT_TTL).AddValue(m_statisticsInfo.width)));
                m_statAggregator->Push(std::move(StatPoint(LiveHeight, m_federalName, STAT_TTL).AddValue(m_statisticsInfo.height)));
                m_statAggregator->Push(std::move(StatPoint(LiveMediaType, m_federalName, STAT_TTL).AddValue(m_statisticsInfo.mediaType)));
                m_statAggregator->Push(std::move(StatPoint(LiveStreamType, m_federalName, STAT_TTL).AddValue(m_statisticsInfo.streamType)),
                    m_firstStatisitcsOrReportImmediate ? IStatisticsAggregatorImpl::immediate : IStatisticsAggregatorImpl::deferred);

                m_firstStatisitcsOrReportImmediate = false;
            }
        }
        m_dataSize = 0;
        m_last = now;
    }


    // IStatisticsCollector implementation.
    NMMSS::StatisticsInfo StatisticsCollector::GetStatistics() const
    {
        boost::mutex::scoped_lock lock(m_statisticsInfoGuard);
        m_statisticsInfo.fps = m_fps->FPS();
        return m_statisticsInfo;
    }

    void StatisticsCollector::processCodedHeader(const NMMSS::NMediaType::Video::SCodedHeader* sh, const uint8_t*)
    {
        m_statisticsInfo.width = sh->nCodedWidth;
        m_statisticsInfo.height = sh->nCodedHeight;
    }

    NMMSS::IStatisticsCollectorImpl* CreateStatisticsCollector(const std::string& federalName,
        NStatisticsAggregator::PStatisticsAggregator aggregator, bool calcKeyFps)
    {
        return new StatisticsCollector(federalName, aggregator, calcKeyFps);
    }


    struct StatisticsCollectorFilter
    {
        StatisticsCollectorFilter(const std::string& federalName,
                NStatisticsAggregator::PStatisticsAggregator aggregator) :
            m_collector(CreateStatisticsCollector(federalName, aggregator, false))
        {
        }

        ::NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            m_collector->Update(sample);
            return NMMSS::ETHROUGH;
        }

    private:
        std::unique_ptr<IStatisticsCollectorImpl> m_collector;
    };

    NMMSS::IFilter* CreateStatisticsCollectorFilter(DECLARE_LOGGER_ARG,
        const std::string& federalName, NStatisticsAggregator::PStatisticsAggregator aggregator)
    {
        using namespace NMMSS;
        return new CPullFilterImpl<StatisticsCollectorFilter>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(),
            SAllocatorRequirements(),
            new StatisticsCollectorFilter(federalName, aggregator));
    }


}
