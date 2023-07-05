#ifndef MEMORY_SAMPLE_TRANSFORMER_H
#define MEMORY_SAMPLE_TRANSFORMER_H

#include "CudaSurface.h"
#include "FrameGeometryAdvisor.h"
#include "../MMCoding/HWCodecs/HWCodecsDeclarations.h"

class CudaSample;

namespace NMMSS
{
    class CDeferredAllocSampleHolder;
}

class MemorySampleTransformer
{
public:
    MemorySampleTransformer(CudaSampleHolderSP sampleHolder, NMMSS::IFrameGeometryAdvisor* advisor);

    std::shared_ptr<CudaSample> Transform(const CudaSurfaceRegion& src, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);

private:
    std::shared_ptr<CudaSample> transform(const CudaSurfaceRegion& src, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);
    CudaDeviceSP device();
    void checkSize(CudaMemorySP& memory, int size);
    void checkSize(CudaMemorySP& memory, const SurfaceSize& size);
    void mipFilter(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst);
    CudaStreamSP stream() const;

private:
    CudaSampleHolderSP m_sampleHolder;
    CudaMemorySP m_memoryDst, m_memoryDstUV, m_upperMip, m_lowerMip;
    NMMSS::PFrameGeometryAdvisor m_advisor;
};

#endif
