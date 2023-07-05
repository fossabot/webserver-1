#include <ace/OS.h>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/archive/xml_wiarchive.hpp>
#include <boost/archive/xml_woarchive.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/map.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>

//for MakeAppFactoryWrapper
#include <Lifecycle/ImplementApp.h>
#include <CorbaHelpers/Reactor.h>
#include <Primitives/Lifecycle/MainReturnGuard.h>

//Implements CreateIPINT30
#include <ItvSdkWrapper.h>
#include "DeviceIpint.h"
#include "DeviceSettings.h"
#include <InfraServer_IDL/ConfigurableImpl.h>
#include <InfraServer_IDL/LicenseChecker.h>

#include "LiveContext.h"

namespace
{
    const char* const CONFIGURABLE_CONTROL = "Configurable";

    std::once_flag initDynamicThreadPool;
    NExecutors::PDynamicThreadPool dynamicThreadPool;
}

namespace IPINT30
{

//Implements Ngp application for support Ipint devices.
class CDeviceIpintApp : public virtual NCorbaHelpers::IResource
{
    DECLARE_LOGGER_HOLDER;
public:
    CDeviceIpintApp(const char* szObjectID, NCorbaHelpers::IContainerNamed* pContainer,
         std::wistream& sConfig, NExecutors::PDynamicThreadPool dtp);
    ~CDeviceIpintApp();

private:
    boost::shared_ptr<IPINT30::IIpintDevice> m_manager;
    
    // The current ip device settings.
    IPINT30::SDeviceSettings m_settings;
    NCorbaHelpers::PResource m_confServant;

    std::string m_objectID;

    using lease_t = std::shared_ptr<NCorbaHelpers::IResource>;
    std::vector<lease_t> m_leases;
};

CDeviceIpintApp::CDeviceIpintApp(const char* szObjectID, NCorbaHelpers::IContainerNamed* pContainer,
        std::wistream& sConfig, NExecutors::PDynamicThreadPool threadPool)
    : m_objectID(szObjectID)
{
    INIT_LOGGER_HOLDER_FROM_CONTAINER(pContainer);
    TRACE_BLOCK;

    boost::shared_ptr<SDeviceSettings> settingsPtr;
    //Deserializes configuration from BLOB
    boost::archive::xml_wiarchive ia(sConfig);
    try
    {
        settingsPtr.reset(new SDeviceSettings());
        settingsPtr->Serialize(ia);
    }
    catch(const std::exception& e)
    {
        std::ostringstream msg;
        msg << "Configuration could not be parsed: " << e.what() << std::endl;
        throw std::runtime_error(msg.str());
    }

    using namespace NLicenseService;
    PLicenseChecker lc = GetLicenseChecker(pContainer);

    if (settingsPtr->videoChannels.size() == 1) // for cameras but not for video channels of video servers
    {
        for (size_t i = 0; i < settingsPtr->videoChannels.size(); ++i)
        {
            if (!settingsPtr->videoChannels[i].enabled)
                continue;

            std::string leaseName = boost::str(boost::format("%1%/SourceEndpoint.video:%2%:0") % m_objectID % i);

            try
            {
                m_leases.push_back(lease_t(lc->Acquire("DeviceIpint", leaseName)));
            }
            catch (const InfraServer::LicenseService::LicenseFailed&)
            {
                _err_ << "No free leases for DeviceIpint";
                throw;
            }
        }
    }
    else if (settingsPtr->videoChannels.size() > 1)
    {
        try
        {
            m_leases.push_back(lease_t(lc->Acquire("DeviceNVR", m_objectID)));
        }
        catch (const InfraServer::LicenseService::InvalidArgument&)
        {
        }
        catch (const InfraServer::LicenseService::LicenseFailed&)
        {
            _err_ << "No free leases for DeviceNVR";
            throw;
        }
    }

    for (size_t i = 0; i < settingsPtr->textEventSources.size(); ++i)
    {
        std::string leaseName = boost::str(boost::format("%1%/TextChannel:%2%") % m_objectID % i);
        try
        {
            if (!settingsPtr->textEventSources[i].enabled)
                continue;

            m_leases.push_back(lease_t(lc->Acquire("TextChannel", leaseName)));
        }
        catch (const InfraServer::LicenseService::InvalidArgument& /*e*/)
        {
            _err_ << "License has no position for TextChannel";
            throw;
        }
        catch (const InfraServer::LicenseService::LicenseFailed& /*e*/)
        {
            _err_ << "No free leases for TextChannel";
            throw;
        }
    }

    try
    {
        bool callbackNeeded = settingsPtr->eventChannel != "-";
        m_manager.reset(IPINT30::CreateIPINT30(GET_LOGGER_PTR,
            szObjectID, pContainer, callbackNeeded, settingsPtr, threadPool));
        if (0 != m_manager.get())
            m_manager->Open();
    }
    catch (const std::exception&)
    {
        _err_ << "Error while creating " << m_objectID;
        throw;
    }
    catch (const CORBA::Exception&)
    {
        _err_ << "Error while creating " << m_objectID;
        throw;
    }

    NCorbaHelpers::CAutoPtr<NStructuredConfig::ILiveServiceContext> liveContext(CreateLiveContext(GET_LOGGER_PTR, m_manager.get()));

    PortableServer::Servant configurable( NStructuredConfig::CreateConfigurable(pContainer, liveContext) );

    NCorbaHelpers::PResource confServant( NCorbaHelpers::ActivateServant(pContainer, configurable, CONFIGURABLE_CONTROL));

    try
    {
        m_manager->Connect();
    }
    catch(...)
    {
        throw;
    }
    m_confServant = std::move(confServant);
}

CDeviceIpintApp::~CDeviceIpintApp()
{
	_log_ << m_objectID <<" ~CDeviceIpintApp begin..." << std::endl;

    m_leases.clear();
    m_confServant.reset();
    m_manager->Close();
    m_manager.reset();

	_log_ << m_objectID <<" ~CDeviceIpintApp finished." << std::endl;
}

NCorbaHelpers::IResource* DeviceIpintFactory(const char *globalObjectName,
    NCorbaHelpers::IContainerNamed* pContainer, std::wistream& sConfig)
{
    std::call_once(initDynamicThreadPool, [&] {
        NCorbaHelpers::PContainer p = pContainer->GetParentContainer();
        const size_t MAX_QUEUE_LENGTH =
            (2 /* video channels */ +
             2 /* audio channels */ +
             1 /* telemetry */ +
             1 /* embedded storage */ +
             1 /* license */
             ) * 2048 /* max cameras per server */
        ;

        dynamicThreadPool = NExecutors::CreateDynamicThreadPool(p->GetLogger(), "DeviceIpint", MAX_QUEUE_LENGTH, 0, 1024);
        NLifecycle::AtReturnFromMain([]() { dynamicThreadPool->Shutdown(); });
    });
    return new CDeviceIpintApp(globalObjectName, pContainer, sConfig, dynamicThreadPool);
}
}

REGISTER_APP_IMPL(DeviceIpint, MakeAppFactoryWrapper<std::wistream>(IPINT30::DeviceIpintFactory, true));
