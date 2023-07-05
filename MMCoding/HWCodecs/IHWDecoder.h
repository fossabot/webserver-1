#pragma once

#include "HWCodecs/HWCodecsDeclarations.h"

#include "../mIntTypes.h"

#include <chrono>

namespace NMMSS
{
    class ISample;
    class CDeferredAllocSampleHolder;
    class IHWDecoderAdvisor;
};

class CompressedData
{
public:
    const unsigned char* Ptr = nullptr;
    unsigned int Size = 0;
    ::uint64_t Timestamp = 0;
};

class IHWDecoder
{
public:
    virtual ~IHWDecoder() {}

    virtual void DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll) = 0;
    virtual bool IsValid() const = 0;
    virtual void ReleaseSamples() = 0;
    virtual const VideoPerformanceInfo& GetPerformanceInfo(std::chrono::milliseconds recalc_for_period = std::chrono::milliseconds(0)) const = 0;
    virtual HWDeviceSP Device() const = 0;
    virtual void SetAdvisor(NMMSS::IHWDecoderAdvisor* advisor) {}
};

class IAsyncHWDecoder : public IHWDecoder
{
public:
    virtual void Decode(const CompressedData& data, bool preroll) = 0;
    virtual bool GetDecodedSamples(NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) = 0;
};

class IHWReceiver
{
public:
    virtual ~IHWReceiver() {}
    virtual void ProcessSample(NMMSS::ISample& sample, NMMSS::CDeferredAllocSampleHolder& holder) = 0;
};