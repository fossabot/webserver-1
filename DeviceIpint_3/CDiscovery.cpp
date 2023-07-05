#include <atomic>

#include <json/json.h>

#include <ItvSdkWrapper.h>
#include "CDiscovery.h"

#include <Lifecycle/ImplementApp.h>

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/xpressive/xpressive_static.hpp>
#include <boost/range/algorithm/remove_if.hpp>

#include <CorbaHelpers/Reactor.h>
#include <Crypto/Crypto.h>

#include "../../mmss/DeviceInfo/include/PropertyContainers.h"

#include "sdkHelpers.h"
#include "FakeDeviceManager.h"

namespace IPINT30
{
const char PROP_DRIVER[] = "driver";
const char PROP_DRIVER_VERSION[] = "driver_version";
const char PROP_MAC_ADDRESS[] = "mac_address";
const char PROP_IP_ADDRESS[] = "ip_address";
const char PROP_IP_PORT[] = "ip_port";
const char PROP_VENDOR[] = "vendor";
const char PROP_MODEL[] = "model";
const char PROP_FIRMWARE_VERSION[] = "firmware_version";
const char PROP_WAN_ADDRESS[] = "wan_address";
const char PROP_DEVICE_DESCRIPTION[] = "device_description";

const std::string OFFLINE_GENERIC_SIGN = "OFFLINE_GENERIC";

namespace
{
const int REGULAR_DEVICE_PROP_COUNT = 9;
const int GENERIC_DEVICE_PROP_COUNT = 10;

typedef boost::function<void(ITV8::IEventHandler*)> cleanFunc_t;

template <typename TEventHandlerWrapper, typename TEventHandler>
typename TEventHandlerWrapper::interface_t* createEventHandler(DECLARE_LOGGER_ARG, TEventHandler handler,
    eventHandlerList_t& container, boost::mutex& mutex, boost::function<void()> clearFunc = []() {})
{
    EventHandlerSP handlerPtr(new TEventHandlerWrapper(GET_LOGGER_PTR, handler, [&container, &mutex, clearFunc](ITV8::IEventHandler* eh)
        {
            clearFunc();
            boost::mutex::scoped_lock lock(mutex);
            container.erase(std::remove_if(container.begin(), container.end(), [eh](const eventHandlerList_t::value_type& value)
                {
                    return eh == value.get();
                }), container.end());
        }));
    boost::mutex::scoped_lock lock(mutex);
    container.push_back(handlerPtr);
    return static_cast<typename TEventHandlerWrapper::interface_t*>(handlerPtr.get());
}

NDiscovery::EDiscoveryModuleCallbackError convertErrorCode(ITV8::hresult_t code)
{
    switch (code)
    {
    case ITV8::EAuthorizationFailed:
        return NDiscovery::EAuthorizationFailed;
    case ITV8::EGeneralConnectionError:
        return NDiscovery::EConnectionError;
    default:
        return NDiscovery::EGeneralError;
    }
}

std::string safeGetString(const char* value)
{
    return value == nullptr ? std::string() : std::string(value);
}
std::string getStringWithoutCntrl(const char* value)
{
    std::string cpy(value);
    cpy.erase(boost::remove_if(cpy, ::iscntrl), cpy.end());
    return cpy;
}

std::string getFakeGenericDescription(const std::string& brand, const std::string& firmware, const std::string& model)
{
    Json::Value root{Json::objectValue};

    root["brand"] = brand;
    root["firmware"] = firmware;
    root["model"] = model;
    root["schemaVersion"] = 1;

    Json::Value& videoSourceElement = root["videoSources"]["items"][0U];
    Json::Value& videoStreamingElement = videoSourceElement["videoStreamings"]["items"][0U];
    videoStreamingElement["name"] = "profile";

    return std::move(root.toStyledString());
}

bool fillDiscoveryOffer(DECLARE_LOGGER_ARG, const ITV8::GDRV::IDeviceSearchResult& data, CosTrading::Offer& offer)
{
    const ITV8::GDRV::IIpDeviceSearchResult* ipdata = ITV8::contract_cast<ITV8::GDRV::IIpDeviceSearchResult>(&data);

    if (!ipdata)
    {
        _err_ << "CIPINT30Discovery: Illegal information about IP device" << std::endl;
        return false;
    }

    const ITV8::GDRV::IDeviceDescriptionProvider* deviceDescriptionProvider = ITV8::contract_cast<ITV8::GDRV::IDeviceDescriptionProvider>(&data);

    std::string description;

    if (deviceDescriptionProvider)
    {
        description = safeGetString(deviceDescriptionProvider->GetDeviceDescription());
    }

    const std::string brand = getStringWithoutCntrl(ipdata->GetBrand());
    const std::string model = getStringWithoutCntrl(ipdata->GetModel());
    const std::string firmware = getStringWithoutCntrl(safeGetString(ipdata->GetFirmware()).c_str());
    const std::string driverName = getStringWithoutCntrl(ipdata->GetDriverName());
    const ITV8::uint32_t driverVersionNum = ipdata->GetDriverVersion();
    const std::string lanAddress = getStringWithoutCntrl(ipdata->GetLANAddress());
    const ITV8::uint32_t port = ipdata->GetPort();
    const std::string macAddress = getStringWithoutCntrl(ipdata->GetMac());
    const std::string wanAddress = getStringWithoutCntrl(safeGetString(ipdata->GetWANAddress()).c_str());

    ITV8::GDRV::Version driverVersion(driverVersionNum);

    //creating description for offline generic device (ACR-42569)
    bool isOfflineGeneric = description == OFFLINE_GENERIC_SIGN;
    if (isOfflineGeneric)
    {
        description.clear();
        description = getFakeGenericDescription(brand, firmware, model);
    }

    _log_ << "CIPINT30Discovery: IP device was found" << std::endl <<
        "\tBrand:\t" << brand << std::endl <<
        "\tModel:\t" << model << std::endl <<
        "\tFirmware:\t" << firmware << std::endl <<
        "\tDriverName:\t" << driverName << std::endl <<
        "\tDriverVersion:\t" << driverVersion.toString() << std::endl <<
        "\tLANAddress:\t" << lanAddress << std::endl <<
        "\tPort:\t" << port << std::endl <<
        "\tMac:\t" << macAddress << std::endl <<
        "\tWANAddress:\t" << wanAddress << std::endl <<
        "\tDevice description:\t" << description << std::endl;

    if ((driverName.empty() || !driverVersion) && !isOfflineGeneric)
    {
        _err_ << "CIPINT30Discovery: IIpDeviceSearchResult is not filled by driver properly" << std::endl;
        return false;
    }

    offer.properties.length(deviceDescriptionProvider ? GENERIC_DEVICE_PROP_COUNT : REGULAR_DEVICE_PROP_COUNT);

    int propId = 0;

    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_DRIVER, driverName);
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_DRIVER_VERSION, driverVersion.toString());
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_MAC_ADDRESS, macAddress);
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_IP_ADDRESS, lanAddress);

    std::ostringstream portStream;
    portStream << port;
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_IP_PORT, portStream.str());
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_VENDOR, brand);
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_MODEL, model);
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_FIRMWARE_VERSION, firmware);
    NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_WAN_ADDRESS, wanAddress);

    if (deviceDescriptionProvider)
    {
        NDiscoveryHelpers::SetProperty(offer.properties[propId++], PROP_DEVICE_DESCRIPTION, NCrypto::ToBase64Padded(description.data(), description.size()));
    }

    return true;
}

class AutodetectHandler : public ITV8::MMD::IAutodetectHandler
{
public:
    typedef ITV8::MMD::IAutodetectHandler interface_t;

    AutodetectHandler(DECLARE_LOGGER_ARG, NDiscovery::IProbeHandler* handler, cleanFunc_t cleaner)
        : m_handler(handler)
        , m_offerCount(0)
        , m_cleaner(cleaner)
    {
        INIT_LOGGER_HOLDER;
    }

    ~AutodetectHandler()
    {
        boost::mutex::scoped_lock lock(m_conditionMutex);
        while (m_handler != nullptr)
        {
            m_finishedCondition.wait(lock);
        }
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MMD::IAutodetectHandler)
        ITV8_CONTRACT_ENTRY(ITV8::MMD::IAutodetectHandler)
    ITV8_END_CONTRACT_MAP()

private:
    virtual void Autodetected(ITV8::MMD::IDeviceManager* source, const ITV8::GDRV::IDeviceSearchResult& data)
    {
        CosTrading::Offer offer;
        if (!fillDiscoveryOffer(GET_LOGGER_PTR, data, offer))
        {
            return;
        }

        {
            boost::mutex::scoped_lock lock(m_devFoundMutex);
            m_offers.length(m_offerCount + 1);
            m_offers.operator[](m_offerCount++) = offer;
        }
        
        _log_ << "CIPINT30Discovery: m_offerCount=" << m_offerCount << std::endl;
    }

    virtual void Done(IContract* source)
    {
        NDiscovery::IProbeHandler* handler = nullptr;
        {
            boost::mutex::scoped_lock lock(m_conditionMutex);
            std::swap(m_handler, handler);
        }

        if (m_offerCount)
        {
            handler->Finished(m_offers);
        }
        else
        {
            handler->Failed(convertErrorCode(m_lastError));
        }
        m_finishedCondition.notify_all();
        m_cleaner(this);
    }

    virtual void Failed(ITV8::IContract* pSource, ITV8::hresult_t error)
    {
        _err_ << "CIPINT30Discovery: Failed() err:" << error << " msg:"
                    << get_last_error_message(pSource, error)<< std::endl;
        m_lastError = error;
    }

private:
    DECLARE_LOGGER_HOLDER;
    NDiscovery::IProbeHandler*    m_handler;
    unsigned int                  m_offerCount;
    Discovery::OfferSeq           m_offers;

    boost::mutex                  m_devFoundMutex;
    ITV8::hresult_t               m_lastError;
    cleanFunc_t                   m_cleaner;

    boost::mutex                  m_conditionMutex;
    boost::condition_variable_any m_finishedCondition;
};

class DiscoveryHandler : public ITV8::MMD::IDeviceSearchHandler
{
public:
    typedef ITV8::MMD::IDeviceSearchHandler interface_t;

    DiscoveryHandler(DECLARE_LOGGER_ARG, NDiscovery::IDiscoverHandler* handler, cleanFunc_t cleaner)
        : m_handler(handler)
        , m_cleaner(cleaner)
        , m_foundedDeviceCount(0)
    {
        INIT_LOGGER_HOLDER;
    }

    ~DiscoveryHandler()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        while (m_handler != 0)
        {
            m_finishedCondition.wait(lock);
        }
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MMD::IDeviceSearchHandler)
        ITV8_CONTRACT_ENTRY(ITV8::MMD::IDeviceSearchHandler)
    ITV8_END_CONTRACT_MAP()

private:
    virtual void DeviceFound(ITV8::MMD::IDeviceManager* source, const ITV8::GDRV::IDeviceSearchResult& data)
    {
        CosTrading::Offer offer;

        if (!fillDiscoveryOffer(GET_LOGGER_PTR, data, offer))
        {
            return;
        }

        _log_ << "CIPINT30Discovery: total founded device count " << ++m_foundedDeviceCount << std::endl;

        if (auto handler = getHandler())
            handler->DeviceFound(offer);
    }

    NDiscovery::IDiscoverHandler* getHandler()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        return m_handler;
    }


    virtual void Done(IContract* source)
    {
        NDiscovery::IDiscoverHandler* handler = nullptr;
        {
            boost::mutex::scoped_lock lock(m_mutex);
            std::swap(m_handler, handler);
        }

        if (handler)
        {
            handler->Finished(NDiscovery::ENotError);
            m_finishedCondition.notify_all();
            m_cleaner(this);
        }
    }

    void Failed(ITV8::IContract* pSource, ITV8::hresult_t error)
    {
        _err_ << "CIPINT30Discovery: Failed() err:" << error << " msg:"
            << get_last_error_message(pSource, error) << std::endl;
    }

    virtual void ReportProgress(int num, int den)
    {
        if (auto handler = getHandler())
            handler->ProgressUpdated((1000 * num) / den);
    }

private:
    DECLARE_LOGGER_HOLDER;
    NDiscovery::IDiscoverHandler*    m_handler;
    cleanFunc_t                      m_cleaner;
    std::atomic<unsigned int>        m_foundedDeviceCount;
    boost::mutex                     m_mutex;
    boost::condition_variable_any    m_finishedCondition;
};

}

CIPINT30DiscoveryModule::CIPINT30DiscoveryModule(const char *globalObjectName, 
        NCorbaHelpers::IContainerNamed* container, std::wistream& isConfig)
    : m_timeout(30)
    , m_interval(300)
    , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
    , m_deviceSearchTimer(m_reactor->GetIO())
{
    INIT_LOGGER_HOLDER_FROM_CONTAINER(container);

    using namespace boost::program_options;
    variables_map vars;
    options_description optionsDescriton;
    optionsDescriton.add_options()
        ("timeout", value<int>()->default_value(m_timeout), 
        "Specifies the number of seconds for search or autodetect operation.")
        ("interval", value<int>()->default_value(m_interval), 
        "Specifies the number of seconds between search attempts.");

    try
    {
        store(parse_config_file(isConfig, optionsDescriton), vars);
        notify(vars);

        if(vars.count("timeout"))
            m_timeout = vars["timeout"].as<int>();

        if(vars.count("interval"))
            m_interval = vars["interval"].as<int>();
    }
    catch(const std::exception& e)
    {
        _err_ << "Error parsing command line arguments: " << e.what() << std::endl;
    }
}

void CIPINT30DiscoveryModule::StartDiscover(NDiscovery::IDiscoverHandler* handler)
{
    if (!handler)
    {
        _log_ << "CIPINT30Discovery: StartDiscover handler is empty";
        return;
    }

    ITV8::MMD::IDeviceManager* deviceManager = ITV8::MMD::GetDeviceManager();

    if (!deviceManager)
    {
        _err_ << "CIPINT30Discovery: Can't GetInstance of Device Manager" << std::endl;
        handler->Finished(NDiscovery::EGeneralError);
        return;
    }

    if (m_timeout)
    {
        m_deviceSearchTimer.expires_from_now(boost::posix_time::seconds(m_timeout));
        m_deviceSearchTimer.async_wait([] (const boost::system::error_code& e)
            {
                if (e != boost::asio::error::operation_aborted)
                {
                    ITV8::MMD::IDeviceManager* deviceManager = ITV8::MMD::GetDeviceManager();
                    if (deviceManager)
                        deviceManager->StopSearch();
                }
            });
        _log_ << "CIPINT30Discovery: Set timeout:" << m_timeout << " sec. for operation." << std::endl;
    }

    deviceManager->StartSearch(createEventHandler<DiscoveryHandler>(GET_LOGGER_PTR, handler, m_eventHandlers, m_handlersGuard,
        [this]()
        {
            boost::system::error_code ec;
            this->m_deviceSearchTimer.cancel(ec);
        }), false);
}

void CIPINT30DiscoveryModule::StartProbe(const char* criteria, NDiscovery::IProbeHandler* handler)
{
    if (!criteria)
    {
        _log_ << "criteria doesn't exist";
        return;
    }

    _log_ << "CIPINT30DiscoveryModule::Probe";

    const char FAKE[] = "fake.";

    // Если это автодетектирование, и критерий поиска начинается с fake,
    // то это вызов фальшивого менеджера

    _log_ << "CIPINT30Discovery: Check Criteria." << std::endl;

    ITV8::MMD::IDeviceManager* deviceManager = nullptr;
    boost::shared_ptr<ITV8::MMD::IDeviceManager> fakeManager;

    if ((0 == strncmp(criteria, FAKE, strlen(FAKE))))
    {
        _log_ << "CIPINT30Discovery: FakeDeviceManager." << std::endl;
        fakeManager.reset(new FakeDeviceManager(GET_LOGGER_PTR));
        deviceManager = fakeManager.get();
        criteria += strlen(FAKE);
    }
    else
    {
        _log_ << "CIPINT30Discovery: RealDeviceManager." << std::endl;
        deviceManager = ITV8::MMD::GetDeviceManager();
    }

    if (!deviceManager)
    {
        _err_ << "CIPINT30Discovery: Can't GetInstance of Device Manager" << std::endl;
        return;
    }

    const std::string criteriaStr(criteria);
    using namespace boost::xpressive;
    static sregex RE_CONNECTION_INFO = (s1 = -+_) >> '|' >> (s2 = +(_w | _s));
    smatch what;
    if (regex_search(criteriaStr.begin(), criteriaStr.end(), what, RE_CONNECTION_INFO))
    {
        deviceManager->Autodetect(createEventHandler<AutodetectHandler>(GET_LOGGER_PTR, handler, m_eventHandlers, m_handlersGuard), what[1].str().c_str(), what[2].str().c_str());
    }
    else
    {
        deviceManager->Autodetect(createEventHandler<AutodetectHandler>(GET_LOGGER_PTR, handler, m_eventHandlers, m_handlersGuard), criteria);
    }
}

int CIPINT30DiscoveryModule::AdviseTimeoutForNextDiscover()
{
    return m_interval;
}

CIPINT30DiscoveryModule::~CIPINT30DiscoveryModule()
{
    _inf_ << "~CIPINT30DiscoveryModule  Stopping device discovery...";
    m_deviceSearchTimer.cancel();
    ITV8::MMD::IDeviceManager* deviceManager = ITV8::MMD::GetDeviceManager();
    if (deviceManager)
        deviceManager->StopSearch();
    
    _inf_ << "~CIPINT30DiscoveryModule  Waiting handlers...";

    eventHandlerList_t handlers;
    {
        boost::mutex::scoped_lock lock(m_handlersGuard);
        handlers.swap(m_eventHandlers);
    }
    // Destroy handler's and wait
    handlers.clear();
    _inf_ << "~CIPINT30DiscoveryModule  Stopped.";
}

}
#ifndef TEST_DEVICE_IPINT3

REGISTER_APP_IMPL(IPINT30_IP_MMSS_Device, (new CAppFactory0<IPINT30::CIPINT30DiscoveryModule, std::wistream>()));

#endif
