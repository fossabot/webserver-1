#ifndef DEVICEIPINT3_DEVICEINFORMATION_H
#define DEVICEIPINT3_DEVICEINFORMATION_H

#include <atomic>
#include <mutex>

#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>

#include "IIPManager3.h"
#include "CAsyncChannel.h"
#include "ParamContext.h"
#include "AsyncActionHandler.h"

#include <MMIDL/DeviceInformationS.h>
#include <CorbaHelpers/RefcountedServant.h>

namespace IPINT30
{
struct IDeviceInformationProvider
{
    virtual std::string GetDynamicParamsContextName() const = 0;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler) = 0;

protected:
    ~IDeviceInformationProvider() {}
};

class DeviceInformationManager : private boost::noncopyable
{
public:
    DeviceInformationManager(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed* container, const std::string& connectionString);

    void RegisterProvider(IDeviceInformationProvider* provider);
    void UnregisterProvider(IDeviceInformationProvider* provider);
    void UnregisterAllProviders();

    Equipment::EAcquiringDynamicParametersResult AcquireDynamicParameters(const std::string& contextName, std::string& jsonData);
    std::string AcquireActualFirmware();

private:
    typedef std::map<std::string, IDeviceInformationProvider*> providerMap_t;

private:
    DECLARE_LOGGER_HOLDER;
    NCorbaHelpers::WPContainerNamed m_container;
    std::string                     m_connectionString;

    boost::mutex                    m_providerMapGuard;
    providerMap_t                   m_providerMap;

    boost::mutex                    m_actualFirmwareGuard;
    std::string                     m_cachedActualFirmware;
};

class DeviceInformationServant : public virtual POA_Equipment::DeviceInformation, public NCorbaHelpers::CRefcountedServant
{
public:
    DeviceInformationServant(boost::shared_ptr<IIpintDevice> parentDevice, DeviceInformationManager& deviceInformationManager);

    virtual Equipment::EAcquiringDynamicParametersResult AcquireDynamicParameters(const char* accessPoint, CORBA::String_out parameters);
    virtual Equipment::EAcquiringFirmwareResult AcquireActualFirmware(CORBA::String_out deviceFirmware);
    void ReleaseParent();

private:
    boost::weak_ptr<IIpintDevice>   m_parentDeviceWp;
    DeviceInformationManager&       m_deviceInformationManager;
    std::atomic_flag                m_acquiringFirmwareFlag;
    std::mutex                      m_releaseParentDeviceMutex;
};

typedef NCorbaHelpers::CAutoPtr<DeviceInformationServant> PDeviceInformationServant;

class DeviceInformationServantHolder
{
public:
    DeviceInformationServantHolder();
    void Activate(boost::shared_ptr<IIpintDevice> parentDevice, 
        DeviceInformationManager& deviceInformationManager, NCorbaHelpers::PContainerNamed container);
    void Reset();
private:
    NCorbaHelpers::PResource m_resource;
    PDeviceInformationServant m_impl;
};

}

#endif

