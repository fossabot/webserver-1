#ifndef DEVICEIPINT3_DEVICEINFO_H
#define DEVICEIPINT3_DEVICEINFO_H

#include <string>

namespace ITV8
{
    namespace GDRV 
    {
        struct IDeviceSearchResult; 
    }
}
namespace IPINT30
{

struct SDeviceSettings;

// Identifies the information about detected device.
class DeviceInfo 
{
public:

    DeviceInfo();

    DeviceInfo(const ITV8::GDRV::IDeviceSearchResult& data);

    DeviceInfo(const DeviceInfo& src);

    DeviceInfo(const SDeviceSettings& src);

    bool IsEmpty() const;

    void Clear();

    inline bool operator != (const DeviceInfo& src) const
    {
        return !operator ==(src);
    }

    bool operator == (const DeviceInfo& src) const;

    // The name of driver which has detected / found device.
    std::string driverName;
    // The version of driver which has detected / found device.
    std::string driverVersion;
    // The device vendor name.
    std::string vendor;
    // The device model name.
    std::string model;
    // The device firmware version.
    std::string firmware;
};

//Compares the SDeviceSettings instance with the DeviceInfo instance
bool operator == (const SDeviceSettings& lhs, const DeviceInfo& rhs);

//Compares the DeviceInfo instance with the SDeviceSettings instance
inline bool operator == (const DeviceInfo& lhs, const SDeviceSettings& rhs)
{
    return operator == (rhs, lhs);
}


}//namespace IPINT30

#endif //DEVICEIPINT3_DEVICEINFO_H