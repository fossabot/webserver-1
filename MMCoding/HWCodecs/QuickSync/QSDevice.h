#pragma once

#include "HWCodecs/IHWDevice.h"

#include "mfxcommon.h"

#include <mutex>

class QSDecoderLease
{
public:
    QSDecoderLease(QSDeviceSP device, ::int64_t size);
    ~QSDecoderLease();
    QSDeviceSP Device() const;

private:
    QSDeviceSP m_device;
    ::int64_t m_size;
};

class QSDevice : public IHWDevice, public std::enable_shared_from_this<QSDevice>
{
public:
    QSDevice();
    ~QSDevice();

    bool IsPrimary() const override;
    int GetPitch(int width) const override;
    NMMSS::EHWDeviceType GetDeviceType() const override;
    int GetDeviceIndex() const override;
    IHWDecoderSP CreateDecoder(NLogging::ILogger* logger, VideoPerformanceInfo info, 
        NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) override;
    bool CanProcessOutput() const override;

    virtual void Init(int adapterNum);
    virtual bool IsValid() const = 0;

    bool SupportsHEVC() const;
    IHWDecoderSP CreateDecoderInternal(NLogging::ILogger* logger, VideoPerformanceInfo info,
        NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements);

private:
    void relaseMemory(::int64_t size);

protected:
    HWDecoderFactorySP m_sharedDecoderHolder;
    bool m_primary;
    bool m_supportsHEVC;

private:
    ::int64_t m_availableMemory;
    std::mutex m_memoryLock;

    friend class QSDecoderLease;
};

std::string MfxStatusToString(mfxStatus status);