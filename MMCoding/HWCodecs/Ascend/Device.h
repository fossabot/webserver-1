#ifndef ASCEND_DEVICE_H
#define ASCEND_DEVICE_H

#include "HWAccelerated.h"
#include "HWCodecs/IHWDevice.h"
#include "FrameGeometryAdvisor.h"

#include "HuaweiAscend/Decoder.h"

class AscendDevice : public IHWDevice, public std::enable_shared_from_this<AscendDevice>

{
public:
    AscendDevice();
    ~AscendDevice();

    bool IsPrimary() const override;
    int GetPitch(int width) const override;
    NMMSS::EHWDeviceType GetDeviceType() const override;
    IHWDecoderSP CreateDecoder(NLogging::ILogger* logger, VideoPerformanceInfo info, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) override;
    bool CanProcessOutput() const override;
    int GetDeviceIndex() const override;

    int GetPitchH(int height) const;
    std::shared_ptr<AscendDecoder::StreamContext> CreateStreamContext(const VideoPerformanceInfo& info);

private:
    std::shared_ptr<AscendDecoder> m_ascendDecoder;
};

#endif
