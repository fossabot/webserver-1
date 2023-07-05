#ifndef DEVICEIPINT3_DEVICECONTROL_H
#define DEVICEIPINT3_DEVICECONTROL_H

#include <mutex>

#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>

#include "IIPManager3.h"

#include <MMIDL/DeviceInformationS.h>
#include <CorbaHelpers/RefcountedServant.h>

namespace IPINT30
{

class DeviceControlManager : private boost::noncopyable
{
public:
    DeviceControlManager(DECLARE_LOGGER_ARG);

    void RegisterProvider(ITV8::GDRV::IDeviceControl* provider);
    Equipment::ESetNTPServerResult SetNTPServer(const Equipment::NTPServerInfo& ntpServerInfo);

private:
    DECLARE_LOGGER_HOLDER;
    ITV8::GDRV::IDeviceControl*     m_provider;
};

class DeviceControlServant : public virtual POA_Equipment::DeviceControl, public NCorbaHelpers::CRefcountedServant
{
public:
    DeviceControlServant(boost::shared_ptr<IIpintDevice> parentDevice, DeviceControlManager& deviceControlManager);

    virtual Equipment::ESetNTPServerResult SetNTPServer(const Equipment::NTPServerInfo& ntpServerInfo);
    void ReleaseParent();

private:
    boost::weak_ptr<IIpintDevice>   m_parentDeviceWp;
    DeviceControlManager&           m_deviceControlManager;
    std::mutex                      m_releaseParentDeviceMutex;
};

typedef NCorbaHelpers::CAutoPtr<DeviceControlServant> PDeviceControlServant;

class DeviceControlServantHolder
{
public:
    DeviceControlServantHolder();
    void Activate(boost::shared_ptr<IIpintDevice> parentDevice, 
        DeviceControlManager& deviceControlManager, NCorbaHelpers::PContainerNamed container);
    void Reset();
private:
    NCorbaHelpers::PResource m_resource;
    PDeviceControlServant m_impl;
};

}

#endif

