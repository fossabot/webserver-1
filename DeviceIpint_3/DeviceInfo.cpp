#include "DeviceInfo.h"
#include <ItvDeviceSdk/include/IDeviceSearch.h>
//for ITV8::GDRV::Version
#include "../../mmss/DeviceInfo/include/PropertyContainers.h"

#include "DeviceSettings.h"

namespace IPINT30
{

DeviceInfo::DeviceInfo()
{
}

DeviceInfo::DeviceInfo(const ITV8::GDRV::IDeviceSearchResult& data)
    :driverName(data.GetDriverName()),
    driverVersion(ITV8::GDRV::Version(data.GetDriverVersion()).toString()),
    vendor(data.GetBrand()),
    model(data.GetModel()),
    firmware(data.GetFirmware())
{
}

DeviceInfo::DeviceInfo(const DeviceInfo& src)
    :driverName(src.driverName), driverVersion(src.driverVersion),
    vendor(src.vendor), model(src.model), firmware(src.firmware)
{
}

DeviceInfo::DeviceInfo(const SDeviceSettings& src)
    :driverName(src.driverName), driverVersion(src.driverVersion),
    vendor(src.vendor), model(src.model), firmware(src.firmware)
{
}

bool DeviceInfo::IsEmpty() const
{
    return vendor.empty() && model.empty() && 
        firmware.empty() && driverName.empty() &&
        driverVersion.empty();
}

void DeviceInfo::Clear()
{
    vendor.clear();
    model.clear();
    firmware.clear();
    driverName.clear();
    driverVersion.clear();
}

bool DeviceInfo::operator == (const DeviceInfo& src) const
{
    return vendor == src.vendor && 
        model == src.model && 
        firmware == src.firmware && 
        driverName == src.driverName &&
        driverVersion == src.driverVersion;
}

bool operator == (const SDeviceSettings& lhs, const DeviceInfo& rhs)
{
    return rhs.vendor == lhs.vendor && 
        rhs.model == lhs.model && 
        rhs.firmware == lhs.firmware && 
        rhs.driverName == lhs.driverName &&
        rhs.driverVersion == lhs.driverVersion;
}


}//namespace IPINT30

