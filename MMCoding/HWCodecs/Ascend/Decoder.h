#ifndef HA_DECODER_H
#define HA_DECODER_H

#include "HuaweiAscend/Decoder.h"

#include "HWCodecs/IHWDecoder.h"
#include "HWAccelerated.h"
#include "FrameGeometryAdvisor.h"

#include "FrameLagHandler.h"

#include "Logging/log2.h"

#include <map>

extern "C"
{
#include <libswscale/swscale.h>
}

namespace NMMSS
{
    class CDeferredAllocSampleHolder;
    class IFrameGeometryAdvisor;
    class HWDecoderRequirements;
}

class HADecoder : public NLogging::WithLogger, public IHWDecoder
{
public:
    HADecoder(DECLARE_LOGGER_ARG, AscendDeviceSP device, const VideoPerformanceInfo& info, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements);
    ~HADecoder();

    HADecoder(const HADecoder& other) = delete;
    HADecoder(HADecoder&& other) = delete;
    HADecoder& operator=(const HADecoder& other) = delete;
    HADecoder& operator=(HADecoder&& other) = delete;

    void DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll) override;
    bool IsValid() const override;
    void ReleaseSamples() override;
    const VideoPerformanceInfo& GetPerformanceInfo(std::chrono::milliseconds recalc_for_period) const override;
    HWDeviceSP Device() const override;

private:
    AscendDeviceSP m_device;
    mutable bool m_decoderErrorObserved;
    mutable uint8_t m_nrErrorsGrowthAge = 0;
    mutable decltype(AscendDecoder::StreamContext::Stats::nr_errors) m_nrErrorsReportedPreviously = 0;
    SwsContext * m_swsctx;
    uint32_t m_swsWidth = 0u;
    uint32_t m_swsHeight = 0u;
    std::shared_ptr<AscendDecoder> m_decoder;
    mutable VideoPerformanceInfo m_performanceInfo;
    std::shared_ptr<AscendDecoder::StreamContext> m_streamCtx;
    NMMSS::PFrameGeometryAdvisor m_advisor;

    void WriteNV12ToHolderAsYUV420(const VDecOutSample * frame, NMMSS::CDeferredAllocSampleHolder * holder);
};

#endif
