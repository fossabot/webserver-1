#pragma once

#include "HWAccelerated.h"
#include "HWCodecsDeclarations.h"
#include <mutex>
#include <vector>

class HWDeviceId
{
public:
    NMMSS::EHWDeviceType Type{};
    int Index{};

    bool operator==(const HWDeviceId& id) const
    {
        return Type == id.Type && Index == id.Index;
    }

    bool operator!=(const HWDeviceId& id) const
    {
        return !(*this == id);
    }
};

class HWDevicePool
{
public:
    HWDeviceSP NextDevice(HWDeviceSP prevDevice, const NMMSS::HWDecoderRequirements& requirements);
    HWDeviceSP GetPrimaryDevice();
    HWDeviceSP GetDevice(const HWDeviceId& id);
    MMCODING_CLASS_DECLSPEC void SetExternalDevice(HWDeviceSP device);

public:
    MMCODING_CLASS_DECLSPEC static std::shared_ptr<HWDevicePool> Instance();
    static void Deinitialize();

private:
    void checkDevices();
    void completeInitialization();
    bool deviceOk(HWDeviceSP device, const NMMSS::HWDecoderRequirements& requirements) const;
    static void addDevices(std::vector<HWDeviceSP>& devices);

private:
    std::mutex m_lock;
    std::vector<HWDeviceSP> m_devices;
    HWDeviceSP m_primaryDevice;
    bool m_devicesInitialized = false;
    bool m_mixedDecoding = false;
};
