#include "MemorySampleTransformer.h"
#include "CudaDevice.h"
#include "CudaProcessor.h"
#include "CudaSampleHolder.h"
#include "../FilterImpl.h"
#include "../MediaType.h"
#include "dynlink_cuda.h"


const bool USE_MIP_FILTER_FOR_SCALING = true;

MemorySampleTransformer::MemorySampleTransformer(CudaSampleHolderSP sampleHolder, NMMSS::IFrameGeometryAdvisor* advisor):
    m_sampleHolder(sampleHolder),
    m_advisor(advisor, NCorbaHelpers::ShareOwnership())
{
}

std::shared_ptr<CudaSample> MemorySampleTransformer::Transform(const CudaSurfaceRegion& src, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    int resultWidth = 0, resultHeight = 0;
    auto adviceType = m_advisor ? m_advisor->GetAdvice((uint32_t&)resultWidth, (uint32_t&)resultHeight) : NMMSS::IFrameGeometryAdvisor::ATLargest;
    CudaSurfaceRegion dst = src;
    if (resultWidth && resultWidth < src.Size.Width)
    {
        SurfaceSize dstSize{ resultWidth, resultHeight, device()->GetPitch(resultWidth) };
        checkSize(m_memoryDst, dstSize);
        dst = { dstSize, m_memoryDst->DevicePtr() };

        if (adviceType == NMMSS::IFrameGeometryAdvisor::ATLargest)
        {
            if (USE_MIP_FILTER_FOR_SCALING)
            {
                mipFilter(src, dst);
            }
            else
            {
                device()->Processor()->Scale2(src, dst, stream());
            }
        }
        else
        {
            device()->Processor()->Scale(src, dst, stream());
        }
    }

    return transform(dst, timestamp, holder);
}

int RoundTo2(int size)
{
    return size & ~1;
}

void MemorySampleTransformer::mipFilter(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst)
{
    int w = RoundTo2(src.Size.Width / 2);
    SurfaceSize lowerMipSize{w, RoundTo2(src.Size.Height / 2), device()->GetPitch(w)};
    checkSize(m_lowerMip, lowerMipSize);
    CudaSurfaceRegion lowerMip = {lowerMipSize, m_lowerMip->DevicePtr()};
    device()->Processor()->CreateMip(src, lowerMip, stream());

    if (lowerMip.Size.Width <= dst.Size.Width)
    {
        device()->Processor()->MipFilter(src, lowerMip, dst, stream());
    }
    else
    {
        std::swap(m_lowerMip, m_upperMip);
        mipFilter(lowerMip, dst);
    }
}

std::shared_ptr<CudaSample> MemorySampleTransformer::transform(const CudaSurfaceRegion& src, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    int rUVWidth = RoundUpTo2(src.Size.Width) / 2;
    SurfaceSize rUVSize{ rUVWidth, RoundUpTo2(src.Size.Height) / 2,  device()->GetPitch(rUVWidth) };
    checkSize(m_memoryDstUV, rUVSize.Pitch * rUVSize.Height * 2);

    CudaSurfaceRegion resultUV{ rUVSize, m_memoryDstUV->DevicePtr() };
    device()->Processor()->ExtractUV(src, resultUV, stream());

    auto sample = m_sampleHolder->GetFreeSample();
    if (sample)
    {
        sample->SetupToHostMem(src, resultUV, timestamp, holder);
    }
    return sample;
}

CudaDeviceSP MemorySampleTransformer::device()
{
    return stream()->Device();
}

void MemorySampleTransformer::checkSize(CudaMemorySP& memory, int size)
{
    if (!memory || memory->Size() < size)
    {
        memory.reset();
        memory = device()->AllocateMemory(size, false);
    }
}

void MemorySampleTransformer::checkSize(CudaMemorySP& memory, const SurfaceSize& size)
{
    checkSize(memory, GetNv12MemorySize(size));
}

CudaStreamSP MemorySampleTransformer::stream() const
{
    return m_sampleHolder->Stream();
}
