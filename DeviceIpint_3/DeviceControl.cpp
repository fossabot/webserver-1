#include "DeviceControl.h"

#include <future>


#include <boost/thread.hpp>

#include <MMIDL/DeviceInformationC.h>
#include <CorbaHelpers/ResolveServant.h>

namespace
{

Equipment::ESetNTPServerResult getSetNTPServerErrorCode(ITV8::hresult_t code)
{
    switch (code)
    {
    case ITV8::ENotError:
        return Equipment::Succeeded;
    case ITV8::EUnsupportedCommand:
        return Equipment::NotSupported;
    default:
        return Equipment::Failed;
    }
}

class DeviceControlHandler : public ITV8::IAsyncActionHandler
{
public:
    DeviceControlHandler()
        : m_result(ITV8::ENotError)
    {
    }
    
    void WaitForResult()
    {
        m_finishedPromise.get_future().wait();
    }

    ITV8::hresult_t GetResult() const
    {
        return m_result;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IAsyncActionHandler)
    ITV8_END_CONTRACT_MAP()

private:
    virtual void Finished(ITV8::IContract* source, ITV8::hresult_t code)
    {
        m_result = code;
        m_finishedPromise.set_value();
    }

private:
    ITV8::hresult_t     m_result;
    std::promise<void>  m_finishedPromise;
};

}

namespace IPINT30
{

DeviceControlServant::DeviceControlServant(boost::shared_ptr<IIpintDevice> parentDevice, DeviceControlManager& deviceControlManager)
    : m_parentDeviceWp(parentDevice)
    , m_deviceControlManager(deviceControlManager)
{
}

Equipment::ESetNTPServerResult DeviceControlServant::SetNTPServer(const Equipment::NTPServerInfo& ntpServerInfo)
{
    std::lock_guard<std::mutex> lock(m_releaseParentDeviceMutex);
    auto parentDevice = m_parentDeviceWp.lock();
    if (!parentDevice)
        return Equipment::Failed;

    const Equipment::ESetNTPServerResult result = m_deviceControlManager.SetNTPServer(ntpServerInfo);
    return result;
}

void DeviceControlServant::ReleaseParent()
{
    std::lock_guard<std::mutex> lock(m_releaseParentDeviceMutex);
    m_parentDeviceWp.reset();
}

DeviceControlServantHolder::DeviceControlServantHolder()
{
}
void DeviceControlServantHolder::Activate(boost::shared_ptr<IIpintDevice> parentDevice,
    DeviceControlManager& deviceControlManager, NCorbaHelpers::PContainerNamed container)
{
    static const char DEVICE_CONTROL_ACCESS_POINT_SUFFIX[] = "DeviceControl";
    m_impl = PDeviceControlServant(new DeviceControlServant(parentDevice, deviceControlManager));
    m_resource.reset(NCorbaHelpers::ActivateServant(container.Get(), m_impl.Dup(), DEVICE_CONTROL_ACCESS_POINT_SUFFIX));
}
void DeviceControlServantHolder::Reset()
{
    m_impl->ReleaseParent();
    m_impl.Reset();
    m_resource.reset();
}

DeviceControlManager::DeviceControlManager(DECLARE_LOGGER_ARG)
{
    INIT_LOGGER_HOLDER;
}

void DeviceControlManager::RegisterProvider(ITV8::GDRV::IDeviceControl* provider)
{
    m_provider = provider;
}

struct INTPServerInfoImpl : public ITV8::GDRV::INTPServerInfo
{
public: 
    INTPServerInfoImpl(const std::string& ntpAddress, const ITV8::GDRV::DeviceControl::TSetNetworkParamsType metworkParamsType) :
        m_ntpAddress(ntpAddress),
        m_metworkParamsType(metworkParamsType)
    {}

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::INTPServerInfo)
    ITV8_END_CONTRACT_MAP()

private:
    virtual const char* GetNTPAddress() const
    {
        return m_ntpAddress.c_str();
    }

    virtual ITV8::GDRV::DeviceControl::TSetNetworkParamsType GetNetworkParamsType() const
    {
        return m_metworkParamsType;
    }

private:
    std::string m_ntpAddress;
    ITV8::GDRV::DeviceControl::TSetNetworkParamsType m_metworkParamsType;
};

Equipment::ESetNTPServerResult DeviceControlManager::SetNTPServer(const Equipment::NTPServerInfo& ntpServerInfo)
{
    INTPServerInfoImpl info(ntpServerInfo.NTPAddress.in(), static_cast<ITV8::GDRV::DeviceControl::TSetNetworkParamsType>(ntpServerInfo.networkParamsType));
    DeviceControlHandler handler;
    if (!m_provider)
        return Equipment::ProviderDoesNotExist;

    m_provider->SetNTPServer(info, &handler);
    handler.WaitForResult();
    const ITV8::hresult_t ipintResult = handler.GetResult();
    return getSetNTPServerErrorCode(ipintResult);
}

}