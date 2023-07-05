#ifndef CUDA_SAMPLE_H
#define CUDA_SAMPLE_H

#include "CudaSurface.h"
#include "../MMCoding/HWCodecs/HWCodecsDeclarations.h"
#include "../MMCoding/HWCodecs/VideoMemorySample.h"
#include "../MMCoding/MMCodingExports.h"

class CudaSample : public VideoMemorySample
{
public:
    CudaSample(CudaStreamSP stream, bool inHostMemory, CudaSharedMemorySP sharedMemory);

    void Setup(const CudaSurfaceRegion& src, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);
    void SetupSystemMem(const uint8_t* src, const SurfaceSize& size, int codedHeight, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);
    void Setup(int width, int height, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);
    void SetupToHostMem(const CudaSurfaceRegion& srcY, const CudaSurfaceRegion& srcUV, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);
    void SetupSharedMemory(NMMSS::ISample& sharedSample, CudaMemorySP memory, NMMSS::CDeferredAllocSampleHolder& holder);

    CudaMemorySP Surface() const
    {
        return m_surface;
    }
    bool IsValid() const;
    CudaStreamSP Stream() const;
    CudaDeviceSP Device() const;
    CUresult Status() const;
    void ReleaseSharedSample();

    MMCODING_CLASS_DECLSPEC static CudaDeviceSP GetDevice(const NMMSS::ISample& sample);
    MMCODING_CLASS_DECLSPEC static CudaSurfaceRegion GetSurface(const NMMSS::ISample& sample, bool color);
    MMCODING_CLASS_DECLSPEC static bool IsValid(const NMMSS::ISample& sample);
    static CudaSurfaceRegion GetSurface(const NMMSS::ISample& sample, CUdeviceptr ptr, bool color);
    static CudaMemorySP GetSharedMemory(CudaDeviceSP device, const NMMSS::ISample& sample, 
        CudaSharedMemorySP& sharedMemory, void*& originalHandle);

private:
    bool setup(const SurfaceSize& size, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);
    bool setup(const SurfaceSize& size, const SurfaceSize& uvSize, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);

private:
    CudaMemorySP m_surface;
    CudaStreamSP m_stream;
    bool m_inHostMemory;
    CudaSharedMemorySP m_sharedMemory;
    NMMSS::PSample m_sharedSample;
};

#endif
