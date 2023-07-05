#pragma once

#include "HWCodecsDeclarations.h"

namespace NLogging
{
    class ILogger;
};

namespace NMMSS
{
    class IFrameGeometryAdvisor;
    class HWDecoderRequirements;
    enum EHWDeviceType : uint32_t;
};

class IHWDecoderFactory
{
public:
    virtual ~IHWDecoderFactory() {}

    virtual IHWDecoderSP CreateDecoder(NLogging::ILogger* logger, VideoPerformanceInfo info,
        NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) = 0;
};

class IHWDevice : public IHWDecoderFactory
{
public:
    virtual bool IsPrimary() const = 0;
    virtual int GetPitch(int width) const = 0;
    virtual NMMSS::EHWDeviceType GetDeviceType() const = 0;
    virtual int GetDeviceIndex() const = 0;
    virtual bool CanProcessOutput() const = 0;
    virtual IHWReceiverSP CreateReceiver(NLogging::ILogger* logger, NMMSS::IFrameGeometryAdvisor* advisor, 
        const NMMSS::HWDecoderRequirements& requirements)
    {
        return nullptr; 
    }
};

