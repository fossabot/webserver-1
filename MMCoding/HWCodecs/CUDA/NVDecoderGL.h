#pragma once

#include "HWCodecs/IHWDecoder.h"
#include "FrameLagHandler.h"

#include "Logging/log2.h"

#include "dynlink_nvcuvid.h"

#include <queue>

namespace NMMSS
{
    class CDeferredAllocSampleHolder;
    class IFrameGeometryAdvisor;
    class IHWDecoderAdvisor;
    class HWDecoderRequirements;
}

class MemorySampleTransformer;
class CudaSample;

class NVDecoderGL : public NLogging::WithLogger, public IAsyncHWDecoder
{
public:
    NVDecoderGL(DECLARE_LOGGER_ARG, CudaDeviceSP device, DecoderPerformanceSP performance, const VideoPerformanceInfo& info, 
        NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements);
    ~NVDecoderGL();

    NVDecoderGL(const NVDecoderGL& other) = delete;
    NVDecoderGL(NVDecoderGL&& other) = delete;
    NVDecoderGL& operator=(const NVDecoderGL& other) = delete;
    NVDecoderGL& operator=(NVDecoderGL&& other) = delete;

    void DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll) override;
    bool IsValid() const override;
    void ReleaseSamples() override;
    const VideoPerformanceInfo& GetPerformanceInfo(std::chrono::milliseconds /*recalc_for_period*/) const override;
    HWDeviceSP Device() const override;

    void SetAdvisor(NMMSS::IHWDecoderAdvisor* advisor) override;

    int HandleVideoSequence(CUVIDEOFORMAT*);
    int HandlePictureDecode(CUVIDPICPARAMS*);
    int HandlePictureDisplay(CUVIDPARSERDISPINFO*);

    void Decode(const CompressedData& data, bool preroll) override;
    bool GetDecodedSamples(NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) override;

private:
    void init();
    void stop();
    void stopDecoder(bool reset);
    void initCudaVideo();
    void output(const CUVIDPARSERDISPINFO& frame, NMMSS::CDeferredAllocSampleHolder& holder, uint64_t timestamp);
    bool check(CUresult code, const char* message) const;
    void unmapFrame();
    void sendSample(NMMSS::CDeferredAllocSampleHolder& holder);
    bool checkDecoderCaps(cudaVideoCodec codec, uint32_t width, uint32_t height, cudaVideoChromaFormat chromaFormat);

private:
    CUvideoparser m_parser{};
    CUvideodecoder m_decoder{};
    CUvideoctxlock m_cuCtxLock{};
    CUVIDEOFORMAT m_format{};
    std::deque<CUVIDPARSERDISPINFO> m_frameQueue;

    CudaDeviceSP m_device;
    CudaSampleHolderSP m_sampleHolder;
    std::unique_ptr<MemorySampleTransformer> m_sampleTransformer;
    DecoderPerformanceSP m_performance;
    VideoPerformanceInfo m_performanceInfo;

    NMMSS::FrameLagHandler m_lagHandler;
    CUdeviceptr m_mappedFrame{};
    std::shared_ptr<CudaSample> m_sample;
    mutable bool m_stopRequested;
    CudaMemoryLeaseSP m_memoryLease;
    bool m_prerollFlag{};
    ::uint64_t m_timestamp{};
};

class NVReceiver : public NLogging::WithLogger, public IHWReceiver
{
public:
    NVReceiver(DECLARE_LOGGER_ARG, CudaDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements);
    ~NVReceiver();
    void ProcessSample(NMMSS::ISample& sample, NMMSS::CDeferredAllocSampleHolder& holder) override;

private:
    void sendSample(NMMSS::CDeferredAllocSampleHolder& holder);

private:
    CudaDeviceSP m_device;
    CudaSampleHolderSP m_sampleHolder;
    std::unique_ptr<MemorySampleTransformer> m_sampleTransformer;
    CudaSharedMemorySP m_sharedMemory;
    void* m_originalHandle{};
    std::shared_ptr<CudaSample> m_sample;
};
