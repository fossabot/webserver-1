#ifndef DECODER_PERFORMANCE_H
#define DECODER_PERFORMANCE_H

#include "HWCodecsDeclarations.h"

#include <mutex>
#include <map>
#include <vector>

class IDecoderPerformance
{
public:
    virtual ~IDecoderPerformance() {}
    virtual void StartDecoding() = 0;
    virtual void StopDecoding() = 0;
    virtual bool IsLegal() const = 0;
};

class DecodeWrapper
{
public:
    DecodeWrapper(IDecoderPerformance& performance) :
        m_performance(performance)
    {
        m_performance.StartDecoding();
    }
    ~DecodeWrapper()
    {
        m_performance.StopDecoding();
    }
private:
    IDecoderPerformance& m_performance;
};

using Codec2PerformanceMap_t = std::map<uint32_t, double>;

class DecoderPerformancePool : public std::enable_shared_from_this<DecoderPerformancePool>
{
private:
    class DecoderPerformance : public IDecoderPerformance
    {
    public:
        DecoderPerformance(DecoderPerformancePoolSP pool, int64_t performance, int64_t framePerformance, bool forced);
        ~DecoderPerformance();

        void StartDecoding() override;
        void StopDecoding() override;
        bool IsLegal() const override;
        void MarkIllegal();

    public:
        DecoderPerformancePoolSP Pool;
        int64_t Performance;
        int64_t FramePerformance;
        bool Legal = true;
        bool Forced;
    };

public:
    using Clock_t = std::chrono::steady_clock;
    using Time_t = Clock_t::time_point;
    using Duration_t = Clock_t::duration;

    DecoderPerformancePool(const Codec2PerformanceMap_t& performanceMap, bool cutExcessiveChannels, double fillLimit);
    DecoderPerformanceSP Acquire(const VideoPerformanceInfo& info);
    void Release(const DecoderPerformance* performance);

private:
    bool performanceUnknown() const;

    void startDecoding(int64_t framePerformance);
    void stopDecoding();
    void tryCalculatePerformance(const Duration_t& busyTime, const Duration_t& totalTime);

private:
    Codec2PerformanceMap_t m_performanceMap;
    int64_t m_totalPerformance = 0;
    int64_t m_performance = 0;
    int64_t m_accumulatedPerformance = 0;
    std::mutex m_lock;
    std::vector<DecoderPerformance*> m_leases;

    Time_t m_totalStart;
    Time_t m_busyStart;
    Duration_t m_busyTime = Duration_t::zero();
    int m_decodeCounter = 0;
    std::vector<double> m_performanceData;
    double m_fillLimit;
};

float GetFps(uint32_t numerator, uint32_t denominator);

#endif
