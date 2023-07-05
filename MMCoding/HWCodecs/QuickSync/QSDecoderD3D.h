#pragma once

#include "FrameGeometryAdvisor.h"
#include "FrameLagHandler.h"
#include "HWAccelerated.h"
#include "HWCodecs/IHWDecoder.h"

#include "Logging/log2.h"

#include "mfxvideo++.h"

#include <vector>


namespace NMMSS
{
    class CDeferredAllocSampleHolder;
};

class QSSampleData;
class BaseQSAllocator;
class BaseSampleTransformer;
class IExternalSurfaces;
class QSDecoderLease;
using ExternalSurfacesSP = std::shared_ptr<IExternalSurfaces>;
using QSDecoderLeaseSP = std::shared_ptr<QSDecoderLease>;

class QSDecoderD3D : public NLogging::WithLogger, public IAsyncHWDecoder
{
public:
    QSDecoderD3D(DECLARE_LOGGER_ARG, QSDecoderLeaseSP memoryLease, const VideoPerformanceInfo& info,
        NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements);
    ~QSDecoderD3D();

    void DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll) override;
    bool IsValid() const override;
    void ReleaseSamples() override;
    const VideoPerformanceInfo& GetPerformanceInfo(std::chrono::milliseconds /*recalc_for_period*/) const override;
    HWDeviceSP Device() const override;

    void Decode(const CompressedData& data, bool preroll) override;
    bool GetDecodedSamples(NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) override;

    void SetExternalSurfaces(ExternalSurfacesSP externalSurfaces);

private:
    void decodeBitStream(mfxBitstream* bs, bool preroll, ::uint64_t timestamp);
    mfxStatus Init(mfxBitstream *pBS);
    mfxStatus InitMfxSession();
    mfxStatus decodeStreamHeader(mfxBitstream *pBS);
    void resizeStreamData(unsigned int length);
    mfxStatus allocateSurfaces();
    int getFreeSurface(int startIndex = 0);
    int findSurface(mfxFrameSurface1* surface);
    void moveBitStream(const unsigned char * pData, unsigned int nLen);
    void close();
    
private:
    bool m_bInitialized{};
    std::shared_ptr<BaseQSAllocator> m_allocator;
    std::unique_ptr<MFXVideoSession> m_mfxSession;
    std::unique_ptr<MFXVideoDECODE> m_decoder;
    mfxVideoParam m_mfxVideoParams{};
    mfxBitstream m_mfxBS{};
    std::vector<std::unique_ptr<QSSampleData>> m_samples;
    VideoPerformanceInfo m_performanceInfo;
    QSDecoderLeaseSP m_memoryLease;

    QSDeviceSP m_device;
    std::vector<unsigned char> m_streamData;

    NMMSS::FrameLagHandler m_lagHandler;
    std::shared_ptr<BaseSampleTransformer> m_sampleTransformer;

    int m_outputSurfaceIndex;
    mfxSyncPoint m_outputSyncPoint{};
    ExternalSurfacesSP m_externalSurfaces;
    NMMSS::PFrameGeometryAdvisor m_advisor;
    NMMSS::HWDecoderRequirements m_requirements;
};
