#ifndef CUDA_SAMPLE_HOLDER_H
#define CUDA_SAMPLE_HOLDER_H

#include "HWCodecs/CUDA/CudaSample.h"
#include <unordered_set>

namespace NMMSS
{
    class IHWDecoderAdvisor;
}

class CudaSampleHolder : public VideoMemorySampleHolder<CudaSample>
{
public:
    CudaSampleHolder(CudaDeviceSP device, bool inHostMemory);
    CudaStreamSP Stream() const;
    void SetDecoderAdvisor(NMMSS::IHWDecoderAdvisor* advisor);
    void ReleaseSharedSamples();
    VideoMemorySampleSP GetFreeSampleBase() override;

protected:
    VideoMemorySampleSP CreateSample() override;

private:
    CudaStreamSP m_stream;
    bool m_inHostMemory;
    NCorbaHelpers::CAutoPtr<NMMSS::IHWDecoderAdvisor> m_advisor;
    CudaSharedMemorySP m_sharedMemory;
    std::unordered_set<int32_t> m_targetProcessIds;
    int64_t m_targetIdsRevision{};
};

#endif
