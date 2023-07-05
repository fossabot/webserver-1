#include "VideoMemorySample.h"
#include "HWUtils.h"
#include "../FilterImpl.h"
#include "../MediaType.h"
#include <CorbaHelpers/Reactor.h>
#include <boost/asio.hpp>

namespace
{

void SetupVideoHeader(NMMSS::NMediaType::Video::SVideoHeader& header, const SurfaceSize& size)
{
    header.nOffset = 0;
    header.nWidth = size.Width;
    header.nHeight = size.Height;
    header.nPitch = size.Pitch;
}

class TimerHelper
{
public:
    using TAction = std::function<bool()>;

    static void RepeatUntilSuccess(const TAction& action, std::chrono::steady_clock::duration period)
    {
        run(std::make_shared<PeriodicAction>(action, period));
    }

private:
    class PeriodicAction
    {
    public:
        PeriodicAction(const TAction& action, std::chrono::steady_clock::duration period) :
            Action(action),
            Period(period)
        {
        }

        TAction Action;
        std::chrono::steady_clock::duration Period;
        boost::asio::steady_timer Timer{ NCorbaHelpers::GetReactorInstanceShared()->GetIO() };
    };

    static void run(std::shared_ptr<PeriodicAction> action)
    {
        action->Timer.expires_after(action->Period);
        action->Timer.async_wait([action](const boost::system::error_code& error)
            {
                if (!error && !action->Action())
                {
                    run(action);
                }
            });
    }
};

bool ClearUnreferencedSamples(std::vector<VideoMemorySampleSP>& samples)
{
    samples.erase(
        std::remove_if(samples.begin(), samples.end(), [](const VideoMemorySampleSP& sample) {return sample->IsReady(); }),
        samples.end());
    return samples.empty();
}

int GetMemoryPitch(int width)
{
    return Aligned16(width);
}

void Copy(const uint8_t* pSrc, const SurfaceSize& size, uint8_t* pDst, int dstPitch)
{
    if (size.Pitch == dstPitch)
    {
        memcpy(pDst, pSrc, dstPitch * size.Height);
    }
    else
    {
        for (auto pSrcEnd = pSrc + size.Height * size.Pitch; pSrc < pSrcEnd; pSrc += size.Pitch, pDst += dstPitch)
        {
            memcpy(pDst, pSrc, size.Width);
        }
    }
}

}

VideoMemorySample::VideoMemorySample(int32_t type, int deviceIndex):
    m_videoMemoryType(type),
    m_deviceIndex(deviceIndex)
{
}

bool VideoMemorySample::SetupSystemMemory(const SurfaceSize& size, const uint8_t* src, const SurfaceSize& uvSize, const uint8_t* uvSrc, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    SurfaceSize dstSize = { size.Width, size.Height, GetMemoryPitch(size.Width) };
    SurfaceSize dstUVSize = { uvSize.Width, uvSize.Height, GetMemoryPitch(uvSize.Width) };
    if (CreateSample(holder, dstSize.MemorySize() + dstUVSize.MemorySize() * 2, true))
    {
        Copy(src, size, m_sample->GetBody(), dstSize.Pitch);
        Copy(uvSrc, { uvSize.Width, uvSize.Height * 2, uvSize.Pitch }, m_sample->GetBody() + dstSize.MemorySize(), dstUVSize.Pitch);
    }
    return Setup(dstSize, dstUVSize, timestamp);
}

bool VideoMemorySample::Setup(const SurfaceSize& size, const SurfaceSize& uvSize, uint64_t timestamp, const Point& cropSize)
{
    if (m_sample)
    {
        SurfaceSize croppedSize = cropSize.IsEmpty() ? size : SurfaceSize{cropSize.X, cropSize.Y, size.Pitch};
        if (m_sample->Header().nSubtype == NMMSS::NMediaType::Video::VideoMemory::ID)
        {
            SetupVideoHeader(m_sample->SubHeader<NMMSS::NMediaType::Video::VideoMemory::SubtypeHeader>(), croppedSize);
        }
        else if(m_sample->Header().nSubtype == NMMSS::NMediaType::Video::fccI420::ID)
        {
            auto& header = m_sample->SubHeader<NMMSS::NMediaType::Video::fccI420::SubtypeHeader>();
            SetupVideoHeader(header, croppedSize);
            header.nPitchU = header.nPitchV = uvSize.Pitch;
            header.nOffsetU = size.MemorySize();
            header.nOffsetV = header.nOffsetU + uvSize.MemorySize();
        }

        m_sample->Header().dtTimeBegin = m_sample->Header().dtTimeEnd = timestamp;
        m_sample->Header().eFlags = 0;
    }
    return !!m_sample;
}

bool VideoMemorySample::CreateSample(NMMSS::CDeferredAllocSampleHolder& holder, int64_t size, bool systemMemory)
{
    if (!m_sample || m_sample->Header().nBodySize < size)
    {
        m_sample.Reset();
        if (auto allocator = holder.GetAllocator())
        {
            m_sample = allocator->Alloc(size);
            m_sample->Header().nBodySize = static_cast<uint32_t>(size);
            if (systemMemory)
            {
                NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(m_sample->GetHeader());
            }
            else
            {
                using THeader = NMMSS::NMediaType::Video::VideoMemory;
                THeader::SubtypeHeader* header = 0;
                NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_sample->GetHeader(), &header);
                header->Type = static_cast<THeader::EVideoMemoryType>(m_videoMemoryType);
            }
        }
    }
    return !!m_sample;
}

NMMSS::PSample VideoMemorySample::Sample() const
{
    return m_sample;
}

bool VideoMemorySample::IsReady() const
{
    return m_sample && m_sample->GetCounter() == 1;
}



BaseVideoMemorySampleHolder::~BaseVideoMemorySampleHolder()
{
    ClearAllSamples();
}

void BaseVideoMemorySampleHolder::ClearAllSamples()
{
    if (!ClearUnreferencedSamples(m_samples))
    {
        auto samples = std::make_shared<std::vector<VideoMemorySampleSP>>();
        samples->swap(m_samples);
        TimerHelper::RepeatUntilSuccess([samples]() { return ClearUnreferencedSamples(*samples); }, std::chrono::milliseconds(300));
    }
}

VideoMemorySampleSP BaseVideoMemorySampleHolder::GetFreeSampleBase()
{
    for (const auto& sample : m_samples)
    {
        if (sample->IsReady())
        {
            return sample;
        }
    }
    
    if (!m_maxSampleCount || (int)m_samples.size() < m_maxSampleCount)
    {
        m_samples.push_back(CreateSample());
        return m_samples.back();
    }

    return nullptr;
}

VideoMemorySampleSP BaseVideoMemorySampleHolder::CreateSample()
{
    return std::make_shared<VideoMemorySample>(NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::Invalid);
}

void BaseVideoMemorySampleHolder::SetMaxSampleCount(int sampleCount)
{
    m_maxSampleCount = sampleCount;
}
