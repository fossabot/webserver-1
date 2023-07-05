#pragma once

#include "../MMCoding/Points.h"
#include "../Sample.h"
#include <CorbaHelpers/RefcountedImpl.h>
#include <vector>

namespace NMMSS
{
    class CDeferredAllocSampleHolder;
}

class VideoMemorySample
{
public:
    VideoMemorySample(int32_t type, int deviceIndex = 0);

    NMMSS::PSample Sample() const;
    bool IsReady() const;
    bool SetupSystemMemory(const SurfaceSize& size, const uint8_t* src, const SurfaceSize& uvSize, const uint8_t* uvSrc, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);

protected:
    bool Setup(const SurfaceSize& size, const SurfaceSize& uvSize, uint64_t timestamp, const Point& cropSize = {});
    bool CreateSample(NMMSS::CDeferredAllocSampleHolder& holder, int64_t size, bool systemMemory);

protected:
    NMMSS::PSample m_sample;
    int32_t m_videoMemoryType;
    int m_deviceIndex;
};

using VideoMemorySampleSP = std::shared_ptr<VideoMemorySample>;

class BaseVideoMemorySampleHolder
{
public:
    virtual ~BaseVideoMemorySampleHolder();
    virtual VideoMemorySampleSP GetFreeSampleBase();

protected:
    virtual VideoMemorySampleSP CreateSample();
    void SetMaxSampleCount(int sampleCount);
    void ClearAllSamples();

protected:
    std::vector<VideoMemorySampleSP> m_samples;
    int m_maxSampleCount{};
};

template<typename TSample>
class VideoMemorySampleHolder : public BaseVideoMemorySampleHolder
{
public:
    std::shared_ptr<TSample> GetFreeSample()
    {
        return std::static_pointer_cast<TSample>(GetFreeSampleBase());
    }
};
