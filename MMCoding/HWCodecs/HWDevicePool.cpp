#include "HWDevicePool.h"
#include "IHWDevice.h"
#include <CorbaHelpers/Envar.h>
#include <Logging/NgpAssert.h>


HWDeviceSP HWDevicePool::NextDevice(HWDeviceSP prevDevice, const NMMSS::HWDecoderRequirements& requirements)
{
    std::lock_guard<std::mutex> lock(m_lock);
    checkDevices();
    auto it = prevDevice ? std::next(std::find(m_devices.begin(), m_devices.end(), prevDevice)) : m_devices.begin();
    while (it != m_devices.end() && !deviceOk(*it, requirements))
    {
        ++it;
    }
    return it != m_devices.end() ? *it : nullptr;
}

HWDeviceSP HWDevicePool::GetDevice(const HWDeviceId& id)
{
    std::lock_guard<std::mutex> lock(m_lock);
    checkDevices();
    for (const auto& device : m_devices)
    {
        if (device->GetDeviceType() == id.Type && device->GetDeviceIndex() == id.Index)
        {
            return device;
        }
    }
    return nullptr;
}

bool CheckVendor(HWDeviceSP device, const NMMSS::HWDecoderRequirements& requirements)
{
    return requirements.DeviceType & device->GetDeviceType();
}

bool CheckGpuId(HWDeviceSP device, const NMMSS::HWDecoderRequirements& requirements)
{
    return requirements.GpuIdMask & (1 << device->GetDeviceIndex());
}

bool CheckCopyToPrimary(HWDeviceSP device, const NMMSS::HWDecoderRequirements& requirements, bool allowMixedDecoding)
{
    return (requirements.Destination != NMMSS::EMemoryDestination::ToPrimaryVideoMemory) || allowMixedDecoding || device->IsPrimary();
}

bool CheckOutputProcessing(HWDeviceSP device, const NMMSS::HWDecoderRequirements& requirements)
{
    return ((requirements.Destination == NMMSS::EMemoryDestination::Auto) && (device->GetDeviceType() == NMMSS::EHWDeviceType::NvidiaCUDA)) ||
        device->CanProcessOutput();
}

bool HWDevicePool::deviceOk(HWDeviceSP device, const NMMSS::HWDecoderRequirements& requirements) const
{
    return CheckVendor(device, requirements) &&
        CheckGpuId(device, requirements) &&
        CheckCopyToPrimary(device, requirements, m_mixedDecoding) &&
        CheckOutputProcessing(device, requirements);
}

void HWDevicePool::checkDevices()
{
    if (!m_devicesInitialized)
    {
        m_mixedDecoding = NCorbaHelpers::CEnvar::NgpMixedDecoding();
        addDevices(m_devices);
        completeInitialization();
    }
}

void HWDevicePool::completeInitialization()
{
    NGP_ASSERT(!m_devicesInitialized);
    for (auto device : m_devices)
    {
        if (device->IsPrimary())
        {
            m_primaryDevice = device;
        }
    }
    m_devicesInitialized = true;
}

void HWDevicePool::SetExternalDevice(HWDeviceSP device)
{
    m_devices.emplace_back(device);
    completeInitialization();
}

HWDeviceSP HWDevicePool::GetPrimaryDevice()
{
    std::lock_guard<std::mutex> lock(m_lock);
    checkDevices();
    return m_primaryDevice;
}

std::shared_ptr<HWDevicePool> g_DevicePool = std::make_shared<HWDevicePool>();

std::shared_ptr<HWDevicePool> HWDevicePool::Instance()
{
    return g_DevicePool;
}

void HWDevicePool::Deinitialize()
{
    g_DevicePool = std::make_shared<HWDevicePool>();
}
