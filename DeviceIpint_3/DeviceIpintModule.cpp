#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <boost/asio.hpp>
#include <ItvSdkWrapper.h>
#include "DeviceIpint_Exports.h"
#include <Lifecycle/ModuleImpl.h>
#include "../ItvSdkUtil/ItvSdkUtil.h"
#include <DeviceManager/CDeviceManagerSettings.h>

#include <boost/thread.hpp>
#include <boost/thread/mutex.hpp>

class CDeviceIpintModule : public CModuleImpl
{
    DECLARE_LOGGER_HOLDER;
public:
    CDeviceIpintModule()
    {
    }

    void Initialize(NCorbaHelpers::IContainer* hostProcessContainer)
    {
        INIT_LOGGER_HOLDER_FROM_CONTAINER(hostProcessContainer);
        
        _log_ << "CDeviceIpintModule::Initialize";
        if (m_deviceManagerSettings)
        {
            _wrn_ << "Device ipint module is not released yet!";
            return;
        }
        // Creates service provider and IDeviceManagerSettings instance
        m_deviceManagerSettings.reset(new ITV8::MMD::CDeviceManagerSettings());

        // Creates and registers logger
        ITVSDKUTILES::ILoggerPtr logger = ITVSDKUTILES::CreateLogger(GET_LOGGER_PTR);
        m_deviceManagerSettings->RegisterService(logger);

        // Creates and registers TargetEnumeratorFactory service
        ITVSDKUTILES::ITargetEnumeratorFactoryPtr factory =
            ITVSDKUTILES::CreateTargetEnumeratorFactory(GET_LOGGER_PTR, 0, "TargetEnumeratorFactory");

        m_deviceManagerSettings->RegisterService(factory);

        ITV8::MMD::IDeviceManager* deviceManager = ITV8::MMD::GetDeviceManager(m_deviceManagerSettings.get());
        if(0 == deviceManager)
        {
            throw std::runtime_error("ITV8::MMD::GetDeviceManager() returned null");
        }
    }

    void Release()
    {
        _log_ << "CDeviceIpintModule::Release";
        // Destroy services after drivers finalization.
        m_deviceManagerSettings.reset();
    }
    
    ~CDeviceIpintModule() 
    {
		_log_ << "~CDeviceIpintModule begin..." << std::endl;
        Release();
		_log_ << "~CDeviceIpintModule finished." << std::endl;
    }
private:
    ITV8::MMD::CDeviceManagerSettings::Ptr m_deviceManagerSettings;
};

namespace NLifecycle
{
    namespace NInternals
    {
        NLifecycle::NInternals::IAppFactoryRegistry* CAppFactoryRegistrator::GetRegistry()
        {
            return GetModuleImpl<CDeviceIpintModule>().get();
        }
    }
}

static boost::mutex initMutex;

DEVICEIPINT_DECLSPEC NLifecycle::IModule* NGP_GetModule(NCorbaHelpers::IContainer* hostProcessContainer)
{
    boost::mutex::scoped_lock lock(initMutex);
    std::auto_ptr<CDeviceIpintModule>& module = GetModuleImpl<CDeviceIpintModule>();
    module->Initialize(hostProcessContainer);
    return module.get();
}
DEVICEIPINT_DECLSPEC void NGP_ReleaseModule()
{
    boost::mutex::scoped_lock lock(initMutex);
    ITV8::MMD::ReleaseDeviceManager();
    std::auto_ptr<CDeviceIpintModule>& module = GetModuleImpl<CDeviceIpintModule>(false);
    if (nullptr != module.get())
        module->Release();

    // FIXME(a-zagaevskiy): Don't release the module (with all its app-factories) till we honestly manage to unload shared libraries or
    // replace the existing mechanism of registration via initialization of global variables (see REGISTER_APP_IMPL).
    //
    // GetModuleImpl<CDeviceIpintModule>(false).reset();
}
