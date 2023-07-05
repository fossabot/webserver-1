#include "DecoderPerformance.h"
#include "HWAccelerated.h"
#include "IHWDevice.h"
#include <algorithm>

const DecoderPerformancePool::Duration_t SINGLE_COLLECTION_TIME = std::chrono::seconds(1);
const double FULL_LOAD_RATIO = 0.95;
const int COLLECTION_COUNT = 3;


DecoderPerformancePool::DecoderPerformance::DecoderPerformance(DecoderPerformancePoolSP pool, int64_t performance, int64_t framePerformance, bool forced) :
    Pool(pool),
    Performance(performance),
    FramePerformance(framePerformance),
    Forced(forced)
{
}

DecoderPerformancePool::DecoderPerformance::~DecoderPerformance()
{
    Pool->Release(this);
}

void DecoderPerformancePool::DecoderPerformance::StartDecoding()
{
    Pool->startDecoding(FramePerformance);
}

void DecoderPerformancePool::DecoderPerformance::StopDecoding()
{
    Pool->stopDecoding();
}

bool DecoderPerformancePool::DecoderPerformance::IsLegal() const
{
    return Legal;
}

void DecoderPerformancePool::DecoderPerformance::MarkIllegal()
{
    Legal = false;
}



DecoderPerformancePool::DecoderPerformancePool(const Codec2PerformanceMap_t& performanceMap, bool cutExcessiveChannels, double fillLimit):
    m_performanceMap(performanceMap),
    m_fillLimit(fillLimit)
{
    if(!cutExcessiveChannels)
    {
        m_performance = m_totalPerformance = std::numeric_limits<int64_t>::max();
    }
}

DecoderPerformanceSP DecoderPerformancePool::Acquire(const VideoPerformanceInfo& info)
{
    auto it = m_performanceMap.find(info.Codec);
    double performanceFactor = (it != m_performanceMap.end()) ? it->second : 1.0;
    int64_t framePerformance = int64_t(performanceFactor * (int64_t)info.Width * (int64_t)info.Height);
    int64_t requiredPerformance = int64_t(double(info.Fps) * framePerformance);

    std::lock_guard<std::mutex> lock(m_lock);
    if (performanceUnknown() || m_performance > requiredPerformance || info.Forced)
    {
        m_performance -= requiredPerformance;
        auto result = std::make_shared<DecoderPerformance>(shared_from_this(), requiredPerformance, framePerformance, info.Forced);
        m_leases.push_back(result.get());
        return result;
    }
    return nullptr;
}

void DecoderPerformancePool::Release(const DecoderPerformance* performance)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_performance += performance->Performance;
    m_leases.erase(std::find(m_leases.begin(), m_leases.end(), performance));
}

bool DecoderPerformancePool::performanceUnknown() const
{
    return !m_totalPerformance;
}

void DecoderPerformancePool::startDecoding(int64_t framePerformance)
{
    if (performanceUnknown())
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (!m_decodeCounter || !m_accumulatedPerformance)
        {
            m_busyStart = Clock_t::now();
            m_busyTime = Duration_t::zero();
            if (!m_accumulatedPerformance)
            {
                m_totalStart = m_busyStart;
            }
        }
        ++m_decodeCounter;
        m_accumulatedPerformance += framePerformance;
    }
}

void DecoderPerformancePool::stopDecoding()
{
    if (performanceUnknown())
    {
        std::lock_guard<std::mutex> lock(m_lock);
        --m_decodeCounter;
        if (m_accumulatedPerformance)
        {
            auto stop = Clock_t::now();
            auto totalTime = stop - m_totalStart;
            bool collectionFinished = totalTime > SINGLE_COLLECTION_TIME;
            if (!m_decodeCounter || collectionFinished)
            {
                m_busyTime += stop - m_busyStart;
            }
            if (collectionFinished)
            {
                tryCalculatePerformance(m_busyTime, totalTime);
                m_accumulatedPerformance = 0;
            }
        }
    }
}

void DecoderPerformancePool::tryCalculatePerformance(const Duration_t& busyTime, const Duration_t& totalTime)
{
    double ratio = (double)busyTime.count() / totalTime.count();
    if (ratio > FULL_LOAD_RATIO && performanceUnknown())
    {
        Duration_t unit = std::chrono::seconds(1);
        double unitPerformance = (double)m_accumulatedPerformance / ((double)totalTime.count() / unit.count());
        m_performanceData.push_back(unitPerformance);
        if ((int)m_performanceData.size() >= COLLECTION_COUNT)
        {
            //double averagePerformance = std::accumulate(m_performanceData.begin(), m_performanceData.end(), 0.0) / m_performanceData.size();
            double averagePerformance = *std::max_element(m_performanceData.begin(), m_performanceData.end());
            m_totalPerformance = (int64_t)(m_fillLimit * averagePerformance);
            m_performance += m_totalPerformance;
            int64_t performance = m_performance;
            for(auto it = m_leases.rbegin(); it != m_leases.rend() && performance < 0; ++it)
            {
                if (!(*it)->Forced)
                {
                    performance += (*it)->Performance;
                    (*it)->MarkIllegal();
                }
            }
        }
    }
    else
    {
        m_performanceData.clear();
    }
}

const float DEFAULT_FPS = 25.f;
float GetFps(uint32_t numerator, uint32_t denominator)
{
    float fps = 0;
    if (denominator > 0)
    {
        fps = float(numerator) / denominator;
    }
    return fps ? fps : DEFAULT_FPS;
}
