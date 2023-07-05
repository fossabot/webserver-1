#include "DeviceInformation.h"
#include "Notify.h"

#include <boost/make_shared.hpp>
#include <boost/thread/mutex.hpp>

#include <MMIDL/DeviceInformationC.h>
#include <CorbaHelpers/ResolveServant.h>
#include <Discovery_IDL/DiscoveryC.h>

#include "CDiscovery.h"
#include "Utility.h"

namespace
{

Equipment::EAcquiringDynamicParametersResult getDynamicParametersErrorCode(ITV8::hresult_t code)
{
    switch (code)
    {
    case ITV8::ENotError:
        return Equipment::adpSucceeded;
    case ITV8::EUnsupportedCommand:
        return Equipment::adpNotSupported;
    default:
        return Equipment::adpFailed;
    }
}

}

namespace IPINT30
{

DeviceInformationServant::DeviceInformationServant(boost::shared_ptr<IIpintDevice> parentDevice, DeviceInformationManager& deviceInformationManager)
    : m_parentDeviceWp(parentDevice)
    , m_deviceInformationManager(deviceInformationManager)
{
    m_acquiringFirmwareFlag.clear();
}

Equipment::EAcquiringDynamicParametersResult DeviceInformationServant::AcquireDynamicParameters(const char* accessPoint, CORBA::String_out parameters)
{
    boost::shared_ptr<IIpintDevice> parentDevice;
    {
        std::lock_guard<std::mutex> lock(m_releaseParentDeviceMutex);
        parentDevice = m_parentDeviceWp.lock();
        if (!parentDevice)
        {
            return Equipment::adpFailed;
        }
    }

    std::string dynamicParameters;
    const Equipment::EAcquiringDynamicParametersResult result = m_deviceInformationManager.AcquireDynamicParameters(accessPoint, dynamicParameters);
    if (result == Equipment::adpSucceeded)
    {
        parameters = dynamicParameters.c_str();
    }
    return result;
}

Equipment::EAcquiringFirmwareResult DeviceInformationServant::AcquireActualFirmware(CORBA::String_out deviceFirmware)
{
    boost::shared_ptr<IIpintDevice> parentDevice;
    {
        std::lock_guard<std::mutex> lock(m_releaseParentDeviceMutex);
        parentDevice = m_parentDeviceWp.lock();
        if (!parentDevice)
        {
            return Equipment::afFailed;
        }
    }

    if (m_acquiringFirmwareFlag.test_and_set())
    {
        return Equipment::EAcquiringFirmwareResult::afBusy;
    }
    const std::string actualFirmware = m_deviceInformationManager.AcquireActualFirmware();
    m_acquiringFirmwareFlag.clear();

    if (actualFirmware.empty())
    {
        return Equipment::EAcquiringFirmwareResult::afFailed;
    }
    deviceFirmware = actualFirmware.c_str();
    return Equipment::EAcquiringFirmwareResult::afSucceeded;
}

void DeviceInformationServant::ReleaseParent()
{
    std::lock_guard<std::mutex> lock(m_releaseParentDeviceMutex);
    m_parentDeviceWp.reset();
}

DeviceInformationServantHolder::DeviceInformationServantHolder()
{
}
void DeviceInformationServantHolder::Activate(boost::shared_ptr<IIpintDevice> parentDevice, 
    DeviceInformationManager& deviceInformationManager, NCorbaHelpers::PContainerNamed container)
{
    static const char DEVICE_INFORMATION_ACCESS_POINT_SUFFIX[] = "DeviceInformation";
    m_impl = PDeviceInformationServant(new DeviceInformationServant(parentDevice, deviceInformationManager));
    m_resource.reset(NCorbaHelpers::ActivateServant(container.Get(), m_impl.Dup(), DEVICE_INFORMATION_ACCESS_POINT_SUFFIX));
}
void DeviceInformationServantHolder::Reset()
{
    m_impl->ReleaseParent();
    m_impl.Reset();
    m_resource.reset();
}


DeviceInformationManager::DeviceInformationManager(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed* container, const std::string& connectionString)
    : m_container(container)
    , m_connectionString(connectionString)
{
    INIT_LOGGER_HOLDER;
}

void DeviceInformationManager::RegisterProvider(IDeviceInformationProvider* provider)
{
    const std::string accessPoint = provider->GetDynamicParamsContextName();
    if (accessPoint.empty())
        return;

    boost::mutex::scoped_lock lock(m_providerMapGuard);
    auto result = m_providerMap.insert(std::make_pair(accessPoint, provider));
    if (!result.second)
    {
        _log_ << "DeviceInformationManager: provider already exists - " << accessPoint;
    }
    else
    {
        _log_ << "DeviceInformationManager: registered provider - " << accessPoint;
    }
}

void DeviceInformationManager::UnregisterProvider(IDeviceInformationProvider* provider)
{
    boost::mutex::scoped_lock lock(m_providerMapGuard);
    m_providerMap.erase(provider->GetDynamicParamsContextName());
    _log_ << "DeviceInformationManager: unregistered provider - " << provider->GetDynamicParamsContextName();
}

void DeviceInformationManager::UnregisterAllProviders()
{
    boost::mutex::scoped_lock lock(m_providerMapGuard);
    for (auto it = m_providerMap.begin(); it != m_providerMap.end();)
    {
        _log_ << "DeviceInformationManager: unregistered provider - " << it->second->GetDynamicParamsContextName();
        it = m_providerMap.erase(it);
    }
}

Equipment::EAcquiringDynamicParametersResult DeviceInformationManager::AcquireDynamicParameters(const std::string& contextName, std::string& jsonData)
{
    IDeviceInformationProvider* provider = 0;

    {
        boost::mutex::scoped_lock lock(m_providerMapGuard);
        auto itr = m_providerMap.find(contextName);
        if (itr == m_providerMap.end())
        {
            _log_ << "DeviceInformationManager: provider doesn't exist - " << contextName;
            return Equipment::adpProviderDoesNotExist;
        }
        provider = itr->second;
    }

    if (!provider)
    {
        _log_ << "DeviceInformationManager: provider is null - " << contextName;
        return Equipment::adpProviderDoesNotExist;
    }
    
    CDynamicParametersHandler handler;
    provider->AcquireDynamicParameters(&handler);

    handler.WaitForResult();

    const ITV8::hresult_t ipintResult = handler.GetResult();
    const Equipment::EAcquiringDynamicParametersResult result = getDynamicParametersErrorCode(ipintResult);
    if (result == Equipment::adpSucceeded)
    {
        jsonData = handler.GetJsonData();
    }
    else
    {
        _err_ << "DeviceInformationManager: AcquireDynamicParameters failed with error code - " << ipintResult;
    }
    return result;
}

std::string DeviceInformationManager::AcquireActualFirmware()
{
    TRACE_BLOCK;
    boost::mutex::scoped_lock lock(m_actualFirmwareGuard);
    if (!m_cachedActualFirmware.empty())
    {
        return m_cachedActualFirmware;
    }

    NCorbaHelpers::PContainerNamed container = m_container;
    if (!container)
    {
        _log_ << "DeviceInformationManager: AcquireActualFirmware failed - container doesn't exist";
        return std::string();
    }

    Discovery::DeviceDiscovery_var discoveryServant = NCorbaHelpers::ResolveServant<Discovery::DeviceDiscovery>(container.Get(), "Discovery.0/Discovery");
    if (!discoveryServant)
    {
        _log_ << "DeviceInformationManager: AcquireActualFirmware failed - couldn't resolve Discovery servant";
        return std::string();
    }

    const size_t MAX_BUFFER_SIZE = 1024;
    wchar_t buffer[MAX_BUFFER_SIZE];
    mbstowcs(buffer, m_connectionString.c_str(), MAX_BUFFER_SIZE);

    Discovery::OfferSeq_var offers = discoveryServant->Probe("IPINT30_IP_MMSS_Device", buffer);
    if (!offers || !offers->length())
    {
        _log_ << "DeviceInformationManager: Probe failed - no offers found";
        return std::string();
    }

    const CosTrading::Offer& offer = (*offers)[0];

    unsigned int index = 0;

    while (index < offer.properties.length())
    {
        const CosTrading::Property& prop = offer.properties[index++];
        if (strcmp(PROP_FIRMWARE_VERSION, prop.name) == 0)
        {
            const char* firmware = 0;
            prop.value >>= firmware;
            if (!firmware)
            {
                _log_ << "DeviceInformationManager: can't get 'firmware_version' property value";
            }
            m_cachedActualFirmware.assign(firmware);
            return m_cachedActualFirmware;
        }
    }

    return std::string();
}

}