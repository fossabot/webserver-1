#include "CIpInt30.h"

#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/split.hpp>

#include "IIPManager3.h"
#include "CVideoSource.h"
#include "CVideoAnalytics.h"
#include "CAudioSource.h"
#include "CTelemetry.h"
#include "CIoPanel.h"
#include "AudioDestination.h"
#include "CTextEventSource.h"
#include "DeviceSettings.h"
#include "sdkHelpers.h"
#include "Notify.h"
#include "EmbeddedStorage.h"
#include "Utility.h"
#include "CAdaptiveSource.h"

#include "../MMClient/GrabberCallback.h"

#include <fmt/format.h>

#include <itv-sdk/ItvMediaSdk/include/codecConstants.h>

namespace
{
const char BREAK_UNUSED_CONNECTIONS[] = "breakUnusedConnections";
const char LOW_GOP[] = "lowGOP";
const char DEVICE_SERIAL_NUMBER[] = "deviceSerialNumber";
const char CONNECTION_STRING_FORMAT[] = "{0}:{1}:{2}:{3}|{4}";
const std::chrono::milliseconds FAKE_STATS_TTL(60 * 1000 * 2);

std::string getConnectionString(boost::shared_ptr<IPINT30::SDeviceSettings> settings)
{
    if (!settings)
    {
        return std::string();
    }

    return (fmt::format(CONNECTION_STRING_FORMAT, settings->host, settings->port, settings->login,
        settings->password, settings->vendor));
}

class CDeviceSerialNumberHandler : public IPINT30::CBaseBlockingHandler
                                 , public ITV8::GDRV::IDeviceSerialNumberHandler
{
public:
    CDeviceSerialNumberHandler() :
        IPINT30::CBaseBlockingHandler()
    {
    }

    std::string GetSerialNumber() const
    {
        return m_serialNumber;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDeviceSerialNumberHandler)
    ITV8_END_CONTRACT_MAP()

private:
    void SerialNumberReceived(ITV8::GDRV::IDeviceSerialNumberProvider*, ITV8::hresult_t result,
        const char* serialNumber) override
    {
        m_result = result;
        if (m_result == ITV8::ENotError)
        {
            m_serialNumber = std::string(serialNumber);
        }

        CBaseBlockingHandler::ReleaseCondition();
    }

private:
    std::string                     m_serialNumber;
};

NMMSS::EIpDeviceState getUnitStatus(const int state)
    /*throw(std::runtime_error)*/
{
    switch (state)
    {
    case ITV8::EActive: return NMMSS::IPDS_SignalRestored;
    case ITV8::EInactive: return NMMSS::IPDS_SignalLost;
    default: throw std::runtime_error("Unknown status received!");
    }
}

uint32_t getStreamType(const std::string& codec)
{
    if (codec == ITV8_VIDEO_CODEC_MJPEG)
    {
        return NMMSS::NMediaType::Video::fccJPEG::ID;
    }
    else if (codec == ITV8_VIDEO_CODEC_MPEG4)
    {
        return NMMSS::NMediaType::Video::fccMPEG4::ID;
    }
    else if (codec == ITV8_VIDEO_CODEC_H264)
    {
        return NMMSS::NMediaType::Video::fccH264::ID;
    }
    else if (codec == ITV8_VIDEO_CODEC_H265)
    {
        return NMMSS::NMediaType::Video::fccH265::ID;
    }
    else if (codec == ITV8_VIDEO_CODEC_VP8)
    {
        return NMMSS::NMediaType::Video::fccVP8::ID;
    }
    else if (codec == ITV8_VIDEO_CODEC_VP9)
    {
        return NMMSS::NMediaType::Video::fccVP9::ID;
    }
    else
    {
        throw std::runtime_error("Unexpected codec: " + codec);
    }
}

inline std::string concatVideoSourceId(const std::string& objId, const int ch, const int s)
{
    return objId + "/SourceEndpoint.video:" + std::to_string(ch) + ':' + std::to_string(s);
}
}

namespace IPINT30
{

typedef std::set<IPINT30::IObjectParamContext*> TObjectParamContexts;

CDevice::CDevice(DECLARE_LOGGER_ARG,
                 const char* szObjectID, NCorbaHelpers::IContainerNamed* pContainer, 
                 bool callbackNeeded, boost::shared_ptr<SDeviceSettings> settingsPtr,
                 NExecutors::PDynamicThreadPool threadPool):
    NLogging::WithLogger(GET_LOGGER_PTR),
    CAsyncHandlerBase(GET_LOGGER_PTR),
    m_objectID(szObjectID),
    m_container(pContainer),
    m_dynExecutor(threadPool),
    m_aggregator(NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container)),
    m_settings(*settingsPtr.get()),
    m_settingsPtr(settingsPtr),
    m_device(0),
    m_connected(false),
    m_readyToDestroy(true),
    m_deviceInfo(*settingsPtr.get()),
    m_context(new CParamContext),
    m_deviceInformationManager(GET_LOGGER_PTR, pContainer, getConnectionString(settingsPtr)),
    m_deviceControlManager(GET_LOGGER_PTR),
    m_deviceSerialNumberValidationFailed(false),
    m_blockingConfiguration(settingsPtr->blockingConfiguration)
{
    if (callbackNeeded)
    {
        m_callback.reset(NMMSS::CreateEventNotificationGrabberCallback(szObjectID,
            pContainer, settingsPtr->eventChannel.c_str(), "EventSupplier"));
    }
    m_notifier.reset(new NotifyStateImpl(GET_LOGGER_PTR, m_callback, m_aggregator, szObjectID));

    // if device manager not initialized
    m_deviceManager = ITV8::MMD::GetDeviceManager();
    if (0 == m_deviceManager)
    {
        throw std::runtime_error("CDevice: DeviceManager wasn't initialized.");
    }

    try
    {
        ValidateSettings();

        for (auto param : m_settings.metaParams)
        {
            if (param.name == BREAK_UNUSED_CONNECTIONS)
                m_breakUnusedConnections = (0 == param.ValueUtf8().compare("1"));
            if (param.name == LOW_GOP)
                m_useVideoBuffersWithSavingPrevKeyFrame = (0 == param.ValueUtf8().compare("1"));
            if (param.name == DEVICE_SERIAL_NUMBER)
            {
                m_deviceSerialNumber = param.ValueUtf8();
                if (m_deviceSerialNumber == "-")
                {
                    m_deviceSerialNumber.clear();
                }
            }
        }
    }
    catch (const std::runtime_error& e)
    {
        _err_ << "ValidateSettings exception : (" << e.what() << ") for device: " << ToString();
        throw;
    }

    m_deviceInformationManager.RegisterProvider(this);
    m_deviceControlManager.RegisterProvider(this);
}

CDevice::~CDevice()
{
    _log_ << ToString() << " ~CDevice() begin...";

    if (0 != m_device)
    {
        WaitForApply();

        m_device->Disconnect();

        _log_ << ToString() << "  ~CDevice() wait Disconnected() ...";
        {
            boost::mutex::scoped_lock locker(m_connectedMutex);

            if(!m_readyToDestroy)
            {
                if(!m_connectedCondition.timed_wait(locker, 
                    boost::posix_time::seconds(int(CChannel::DRIVER_OPERATION_CONFIRM_TIMEOUT)),
                    [this]() { return m_readyToDestroy; }))
                {
                    _err_ << ToString() <<  " The timeout period of Disconnect() operation elapsed. "\
                        "The ITV8::GDRV::IDeviceHandler::Disconnected(...) wasn't called by driver.";

                    while (!m_readyToDestroy)
                        m_connectedCondition.wait(locker);
                }

            }
        }

        _log_ << ToString() << " ~CDevice() Disconnection() - OK";

        _log_ << ToString() << " ~CDevice() Destroy()...";

        m_device->Destroy();

        _log_ << ToString() << " ~CDevice() Destroy() - OK";
    }
}

void CDevice::Open()
{
    NCorbaHelpers::PContainerNamed containerPtr = m_container;
    if (containerPtr)
    {
        m_deviceInformationServantHolder.Activate(shared_from_this(), m_deviceInformationManager, containerPtr);
        m_deviceControlServantHolder.Activate(shared_from_this(), m_deviceControlManager, containerPtr);
    }
    else
    {
        _err_ << "Can't activate DeviceInformation servant. Container doesn't exist";
    }

    for_each(m_settingsPtr->videoChannels.begin(), m_settingsPtr->videoChannels.end(), 
        boost::bind(&CDevice::CreateVideoSource, this, _1));

    for_each(m_settingsPtr->microphones.begin(), m_settingsPtr->microphones.end(), 
        boost::bind(&CDevice::CreateAudioSource, this, _1));

    for_each(m_settingsPtr->telemetries.begin(), m_settingsPtr->telemetries.end(), 
        boost::bind(&CDevice::CreateTelemetryChannel, this, _1));

    for_each(m_settingsPtr->ioDevices.begin(), m_settingsPtr->ioDevices.end(), 
        boost::bind(&CDevice::CreateIoDevice, this, _1));

    for_each(m_settingsPtr->speakers.begin(), m_settingsPtr->speakers.end(), 
        boost::bind(&CDevice::CreateAudioDestination, this, _1));

    for_each(m_settingsPtr->textEventSources.begin(), m_settingsPtr->textEventSources.end(),
        boost::bind(&CDevice::CreateTextEventSource, this, _1));

    bool isOfflineAnalytics = (m_settingsPtr->vendor == "AxxonSoft" || m_settingsPtr->vendor == "Virtual") && m_settingsPtr->driverName == "Virtual"; // magic criteria for offline analytics functionality with external files
    for_each(m_settingsPtr->embeddedStorages.begin(), m_settingsPtr->embeddedStorages.end(), [this, isOfflineAnalytics](const SEmbeddedStoragesParam& settings)
    {
        CreateEmbeddedStorage(settings, isOfflineAnalytics);
    });

    for (const auto& settings : m_settingsPtr->deviceNodes)
        CreateDeviceNode(settings, nullptr);

    m_context->AddContext("Device", MakeContext(m_settings));
    RegisterObjectParamContext(this);
}

void CDevice::Close()
{
    m_deviceInformationServantHolder.Reset();

    //Signals to every child object to begin finalization.
    handleChildAction(boost::bind(&CChannel::BeginFinalization, _1));

    m_deviceInformationManager.UnregisterAllProviders();

    //Signals to every child object to end finalization.
    handleChildAction(boost::bind(&CChannel::EndFinalization, _1));

    m_speakers.clear();
    m_ioPanels.clear();
    m_telemetryChannels.clear();
    m_audioSources.clear();
    m_videoSources.clear();
    m_videoAnalytics.clear();
    m_textEventSources.clear();
    m_embededStorages.clear();
    m_deviceNodes.clear();

    m_deviceControlServantHolder.Reset();
}

bool CDevice::IsGeneric() const
{
    return m_settings.model == "generic";
}

bool CDevice::BlockConfiguration() const
{
    return m_blockingConfiguration;;
}

void CDevice::Connect()
{
    ITV8::GDRV::Version driverVersion;
    if( !driverVersion.fromString(m_deviceInfo.driverVersion) )
    {
        _err_ << ToString() << " Invalid version format: " << m_deviceInfo.driverVersion;
    }

    m_device = m_deviceManager->CreateDevice(this, m_deviceInfo.vendor.c_str(), 
        m_deviceInfo.model.c_str(), nullptr,
        m_deviceInfo.driverName.c_str(), (ITV8::uint32_t)driverVersion);

    if(!m_device)
    {
        throw std::runtime_error(fmt::format("Device {0} was not created.", ToString()));
    }

    ApplyChanges(0);
    {
        boost::mutex::scoped_lock lock(m_connectedMutex);
        m_readyToDestroy = false;
    }
    m_device->Connect(getConnectionInfo().c_str());

    _log_ << ToString() << " connecting...";
}

std::string CDevice::GetDynamicParamsContextName() const
{
    return "device";
}

void CDevice::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (!handler)
    {
        _log_ << ToString() << " IDynamicParametersHandler doesn't exist";
        return;
    }

    if (!m_device)
    {
        _log_ << ToString() << " device doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_device);
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_device, ITV8::EUnsupportedCommand);
}

void CDevice::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (m_device != nullptr)
    {
        auto* asyncAj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_device);
        if(asyncAj)
        {
            SetBlockConfiguration(asyncAj, m_blockingConfiguration);
            for (const auto& param : m_settings.publicParams)
            {
                try
                {
                    _inf_ << ToString() << fmt::format(" SetValue(\"{0}\", {1}:{2})", param.name,
                        param.ValueUtf8(), param.Type());
                    ITV8::set_value(asyncAj, param.Type(), param.name, param.ValueUtf8());
                }
                catch (const std::runtime_error& e)
                {
                    _err_ << ToString() << " ITV8::set_value threw runtime_error:" << e.what();
                }
                catch (const boost::bad_lexical_cast& e)
                {
                    _err_ << ToString() << " ITV8::set_value threw bad_lexical_cast:" << e.what();
                }
            }

            //Applies new settings.
            _dbg_ << ToString() << " CDevice::ApplyChanges " << handler << " : " << static_cast<ITV8::IAsyncActionHandler*>(this);
            ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() << " couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped.";
        }
    }
}

void CDevice::SwitchEnabled()
{
    // Do nothing because the device enables/disables with the whole application.
}

IParamContextIterator* CDevice::GetParamIterator()
{
    return m_context.get();
}

// ITV8::IAsyncActionHandler implementation
void CDevice::ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code)
{
    if (code != ITV8::ENotError)
    {
        _err_ << ToString() << " ITV8::IAsyncAdjuster::ApplySettings(); return err:" << code
            << "; msg: " << get_last_error_message(source, code);
    }
    else
    {
        _inf_ << ToString() << " Async operation completed.";
    }
}

template<class T>
void CDevice::OnChannelCreated(T channel, std::vector<T>& collection)
{
    //Sets connected flag for a new channel if necessary.
    {
        boost::mutex::scoped_lock lock(m_connectedMutex);
        if(m_connected)
        {
            channel->SetDeviceConnected(true);
        }
    }

    //Copy the AutoPtr of new channel to channel collection
    collection.push_back(channel);

    _log_ << static_cast<CChannel*>(channel.Get())->ToString() << " created.";

    RegisterObjectParamContext(channel.Get());
    m_deviceInformationManager.RegisterProvider(channel.Get());

    channel->SwitchEnabled();
}

void CDevice::CreateVideoSource(const SVideoChannelParam& channelSettings)
{
    if (NCorbaHelpers::PContainerNamed container = m_container)
    {
        PAdaptiveSourceFactory factory(new CAdaptiveSourceFactory(GET_LOGGER_PTR, container.Get(), ToVideoAccessPoint(channelSettings.id, "*")));
        for (std::size_t streamType = 0; streamType < channelSettings.streamings.size(); ++streamType)
        {
            CreateVideoStreaming(container.Get(), channelSettings, streamType, factory);
        }

        for_each(channelSettings.detectors.begin(), channelSettings.detectors.end(),
            bind(&CDevice::CreateVideoAnalytics, this, channelSettings.id, _1));
    }
}

void CDevice::CreateVideoStreaming(NCorbaHelpers::IContainerNamed* container, const SVideoChannelParam& channelSettings, 
    std::size_t streamType, PAdaptiveSourceFactory adaptiveFactory)
{
    try
    {
        CVideoSourcePtr videoSource(new CVideoSource(GET_LOGGER_PTR, m_dynExecutor, shared_from_this(),
            channelSettings, channelSettings.streamings[streamType], m_settings.videoChannels.size(), (EStreamType)streamType, m_callback,
            m_objectID.c_str(), container, m_breakUnusedConnections, m_useVideoBuffersWithSavingPrevKeyFrame, adaptiveFactory));

        OnChannelCreated(videoSource, m_videoSources);
    }
    catch (const std::exception& e)
    {
        _err_ << "new CVideoSource threw exception: " << e.what();
        //TODO: It is unknown how to process exception.
        throw;
    }
}

void CDevice::CreateVideoAnalytics(int videoChannelId, const SEmbeddedDetectorSettings& detectorSettings)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    try
    {
        CVideoAnalyticsPtr videoAnalytics(new CVideoAnalytics(GET_LOGGER_PTR, m_dynExecutor, shared_from_this(), videoChannelId,
            detectorSettings, m_callback, m_objectID.c_str(), m_settings.eventChannel.c_str(), cont.Get(), getSyncPointByChannelId(videoChannelId)));

        OnChannelCreated(videoAnalytics, m_videoAnalytics);
    }
    catch (const std::exception& e)
    {
        _err_ << "new CVideoAnalytics threw exception: " << e.what();
        //TODO: It is unknown how to process exception.
        throw;
    }
}

void CDevice::DestroyVideoAnalytics(int videoChannelId, const std::string& detectorType, const std::string& detectorId)
{
    _inf_ << ToString() << " DestroyVideoAnalytics(" << videoChannelId << ", "
        << detectorType << "." << detectorId << ")...";

    TVideoAnalyticses::iterator it = std::find_if(m_videoAnalytics.begin(), m_videoAnalytics.end(),
        boost::bind(&CVideoAnalytics::getVideoChannelId, *&_1) == videoChannelId &&
        boost::bind(&CVideoAnalytics::getType, *&_1) == detectorType &&
        boost::bind(&CVideoAnalytics::getId, *&_1) == detectorId);
    
    if(it != m_videoAnalytics.end())
    {
        // Удаляем детектор
        CVideoAnalyticsPtr videoAnalytics(*it);
        m_videoAnalytics.erase(it);
        m_deviceInformationManager.UnregisterProvider(videoAnalytics.Get());
        std::string realType = videoAnalytics->getType();
        videoAnalytics->BeginFinalization();
        videoAnalytics->EndFinalization();
        UnregisterObjectParamContext(videoAnalytics.Get());
        videoAnalytics.Reset();

        _inf_ << ToString() << " DestroyVideoAnalytics(" << videoChannelId << ", "
            << detectorType << "." << detectorId << ") OK. VideoAnalytics had type=" << realType;
    }
    else
    {
        _wrn_ << ToString() << " DestroyVideoAnalytics(" << videoChannelId << ", " 
            << detectorType << "." << detectorId << ") failed. VideoAnalytics was not found.";
    }

}

void CDevice::CreateAudioSource(const SMicrophoneParam& micSettings)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    try
    {
        CAudioSourcePtr audioSource(new CAudioSource(GET_LOGGER_PTR, m_dynExecutor,
            shared_from_this(), micSettings, m_callback, m_objectID.c_str(), cont.Get(), m_breakUnusedConnections));

        OnChannelCreated(audioSource, m_audioSources);
    }
    catch(const std::exception& e)
    {
        _err_ << "new CAudioSource threw exception: " << e.what();
        //TODO: It is unknown how to process exception.
        throw;
    }
}

void CDevice::CreateTelemetryChannel(const STelemetryParam& telSettings)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    try
    {
        CTelemetryPtr telChannel(new CTelemetry(GET_LOGGER_PTR, m_dynExecutor, shared_from_this(),
            telSettings, m_callback, m_objectID.c_str(), cont.Get()));

        OnChannelCreated(telChannel, m_telemetryChannels);
    }
    catch(const std::exception& e)
    {
        _err_ << "new CTelemetry threw exception: " << e.what();
        //TODO: It is unknown how to process exception.
        throw;
    }
}

void CDevice::CreateIoDevice(const SIoDeviceParam& ioDeviceSettings)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    try
    {
        CIoPanelPtr ioPanel(new CIoPanel(GET_LOGGER_PTR, ioDeviceSettings, m_blockingConfiguration, m_dynExecutor,
            m_settings.eventChannel.c_str(), shared_from_this(), m_callback, m_objectID.c_str(), cont.Get()));

        OnChannelCreated(ioPanel, m_ioPanels);
    }
    catch(const std::exception& e)
    {
        _err_ << "new CIoPanel threw exception: " << e.what();
        //TODO: It is unknown how to process exception.
        throw;
    }
}

void CDevice::CreateAudioDestination(const SAudioDestinationParam& settings)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    try
    {
        CLoudSpeakerPtr loudSpeaker(new CLoudSpeaker(GET_LOGGER_PTR, m_dynExecutor, shared_from_this(), 
            settings, m_callback, m_objectID.c_str(), cont.Get()));
        
        OnChannelCreated(loudSpeaker, m_speakers);
    }
    catch(const std::exception& e)
    {
        _err_ << "Creating of the audio destination failed by reason: " << e.what();
        throw;
    }
}

void CDevice::CreateEmbeddedStorage(const SEmbeddedStoragesParam& settings, bool isOfflineAnalytics)
{
    const char* const SOURCES_CONTAINER_NAME = "Sources";

    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
        return;

    try
    {
        if (!m_sourcesContainer)
            m_sourcesContainer = cont->CreateContainer(SOURCES_CONTAINER_NAME, NCorbaHelpers::IContainer::EA_Advertise);
        EmbeddedStoragePtr embeddedStorage(new EmbeddedStorage(GET_LOGGER_PTR, m_dynExecutor, shared_from_this(),
            settings, isOfflineAnalytics, m_callback, m_objectID.c_str(), cont.Get(), m_sourcesContainer, m_settingsPtr->videoChannels.size(), m_settings.eventChannel));

        OnChannelCreated(embeddedStorage, m_embededStorages);
    }
    catch (const std::exception& e)
    {
        _err_ << "Creating of the audio destination failed by reason: " << e.what();
        throw;
    }
}

void CDevice::CreateTextEventSource(const STextEventSourceParam& settings)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
        return;

    try
    {
        CTextEventSourcePtr textEventSource(new CTextEventSource(GET_LOGGER_PTR, m_dynExecutor, shared_from_this(), settings,
            m_callback, m_objectID.c_str(), m_settings.eventChannel.c_str(), cont.Get()));

        OnChannelCreated(textEventSource, m_textEventSources);
    }
    catch (const std::exception& e)
    {
        _err_ << "Creating of the text event source failed by reason: " << e.what();
        throw;
    }
}

void CDevice::CreateDeviceNode(const SDeviceNodeParam& settings, DeviceNode* owner)
{
    std::string deviceNodeObjectId = DeviceNode::createObjectId(settings.deviceNodeId, settings.id);
    if (m_deviceNodes.end() != findDeviceNode(deviceNodeObjectId))
    {
        _err_ << "Attemt to create existing device node " << deviceNodeObjectId;
        return;
    }

    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
        return;

    DeviceNode* childOwner = nullptr;
    try
    {
        _dbg_ << "Create the device node " << deviceNodeObjectId;
        auto deviceNode = NCorbaHelpers::MakeRefcounted<DeviceNode>(GET_LOGGER_PTR,
                                                m_dynExecutor, 
                                                shared_from_this(), 
                                                settings,
                                                m_callback, 
                                                m_objectID.c_str(), 
                                                cont.Get(),
                                                m_settings.eventChannel.c_str(),
                                                owner);

        OnChannelCreated(deviceNode, m_deviceNodes);

        childOwner = deviceNode.Get();
    }
    catch (const std::exception& e)
    {
        _err_ << "Creating of the device node '" << deviceNodeObjectId << "' failed by reason: " << e.what();
        throw;
    }

    for (const auto& deviceNodeSettings : settings.children)
        CreateDeviceNode(deviceNodeSettings, childOwner);
}

void CDevice::DestroyDeviceNode(const std::string& deviceNodeObjectId)
{
    std::vector<std::string> children;
    getDeviceNodeChildren(deviceNodeObjectId, children);

    for (const std::string& childGuid : children)
    {
        DestroyDeviceNode(childGuid);
    }

    auto it = findDeviceNode(deviceNodeObjectId);
    if (it == m_deviceNodes.end())
    {
        _err_ << " Can not destroy device node '" << deviceNodeObjectId << "'";
        return;
    }

    DeviceNodePtr sp(*it);
    _dbg_ << " Destroy device node '" << sp->getObjectId() << "'";
    m_deviceNodes.erase(it);
    m_deviceInformationManager.UnregisterProvider(sp.Get());
    sp->BeginFinalization();
    sp->EndFinalization();
    UnregisterObjectParamContext(sp.Get());
    sp.Reset();
}

template<typename TAction>
void IPINT30::CDevice::handleChildAction(TAction action)
{
    for_each(m_videoAnalytics.begin(), m_videoAnalytics.end(), action);
    for_each(m_videoSources.begin(), m_videoSources.end(), action);
    for_each(m_audioSources.begin(), m_audioSources.end(), action);
    for_each(m_telemetryChannels.begin(), m_telemetryChannels.end(), action);
    for_each(m_ioPanels.begin(), m_ioPanels.end(), action);
    for_each(m_speakers.begin(), m_speakers.end(), action);
    for_each(m_textEventSources.begin(), m_textEventSources.end(), action);
    for_each(m_embededStorages.begin(), m_embededStorages.end(), action);
    for_each(m_deviceNodes.begin(), m_deviceNodes.end(), action);
}

PTimeCorrectorSyncPoint CDevice::getSyncPointByChannelId(int channelId)
{
    auto it = std::find_if(m_videoSources.begin(), m_videoSources.end(),
        [&channelId](const CVideoSourcePtr& vs)
        {
            return vs->IsOrigin() && vs->GetChannelId() == channelId;
        });

    if (it == m_videoSources.end())
        return PTimeCorrectorSyncPoint();

    return it->Get()->GetSyncPoint();
}

void CDevice::SetNTPServer(const ITV8::GDRV::INTPServerInfo& ntpServerInfo, ITV8::IAsyncActionHandler* handler)
{
    if (m_device)
    {
        ITV8::GDRV::IDeviceControl* provider = ITV8::contract_cast<ITV8::GDRV::IDeviceControl>(m_device);
        if (provider)
        {
            provider->SetNTPServer(ntpServerInfo, handler);
        }
    }
}

void CDevice::Reboot(IAsyncActionHandler* handler) {};
void CDevice::SoftwareDefault(IAsyncActionHandler* handler) {};
void CDevice::FactoryDefault(IAsyncActionHandler* handler) {};
void CDevice::FirmwareUpgrade(const char* fwFile, IAsyncActionHandler* handler) {};
void CDevice::GetSystemDateTime(ITV8::GDRV::IGetSystemDateTimeHandler* handler) {};
void CDevice::SetSystemDateTime(const ITV8::GDRV::ISystemDateAndTimeInfo& dateTimeInfo, IAsyncActionHandler* handler) {};
void CDevice::GetNTPServer(ITV8::GDRV::IGetNTPServerHandler* handler) {};
void CDevice::GetNetworkParams(ITV8::GDRV::IGetNetworkParamsHandler* handler) {};
void CDevice::SetNetworkParams(const ITV8::GDRV::INetworkParamsInfo& networkParams, IAsyncActionHandler* handler) {};

// ITV8::GDRV::IDeviceHandler Implementation
void CDevice::Connected(ITV8::GDRV::IDevice* pSource)
{
    _log_ << ToString() << " Connected.";

    {
        boost::mutex::scoped_lock lock(m_connectedMutex);
        m_connected = true;
    }
    // Signal child objects that device is connected.
    handleChildAction(bind(&CChannel::SetDeviceConnected, _1, true));
    m_notifier->Notify(NMMSS::IPDS_Connected);

    if (!m_dynExecutor->Post(boost::bind(&CDevice::scheduleDeviceSerialNumberValidation, this)))
        _err_ << "Can't post scheduleDeviceSerialNumberValidation() to thread pool!";
}

void CDevice::StateChanged(ITV8::GDRV::IDevice* pSource, ITV8::uint32_t state)
{
    //Signal to BL about state changing.
    NMMSS::EIpDeviceState st;
    switch(state)
    {
    case ITV8::GDRV::IDevice::STATE_DISCONNECTED:
        st = NMMSS::IPDS_Disconnected;
        break;
    case ITV8::GDRV::IDevice::STATE_CONNECTING:
        return;
    case ITV8::GDRV::IDevice::STATE_CONNECTED:
        st = NMMSS::IPDS_Connected;
        break;
    case ITV8::GDRV::IDevice::STATE_REBOOT:
        st = NMMSS::IPDS_Rebooted;
        break;
    case ITV8::GDRV::IDevice::STATE_NETWORK_FAILURE:
        st = NMMSS::IPDS_NetworkFailure;
        break;
    default:
        _err_ << ToString() << " Unknown state: " << state;
        return;
    }

    // Signal child objects about new state
    handleChildAction(bind(&CChannel::SetDeviceStateChanged, _1, st));

    _log_ << ToString() << " StateChanged(..., " << state << ").";

    // Rebooted state will likely confuse BL and other clients, so it is better not to send it.
    if (st != NMMSS::IPDS_Rebooted)
    {
        m_notifier->Notify(st);
    }
}

void CDevice::Disconnected(ITV8::GDRV::IDevice* pSource)
{
    _log_ << ToString() << " Disconnected.";
    m_notifier->Notify(NMMSS::IPDS_Disconnected);
    {
        boost::mutex::scoped_lock lock(m_connectedMutex);
        m_connected = false;
        m_readyToDestroy = true;
        m_connectedCondition.notify_all();
    }
}

void CDevice::Failed(ITV8::IContract* source, ITV8::hresult_t error)
{
    std::string message = get_last_error_message(source, error);
    _err_ << ToString() << " Failed. It's unexpected err:" << message;
    NMMSS::EIpDeviceState st;
    switch(error)
    {
    case ITV8::EAuthorizationFailed:
        st = NMMSS::IPDS_AuthorizationFailed;
        break;
    case ITV8::EDeviceReboot:
        st = NMMSS::IPDS_Rebooted;
        break;
    case ITV8::EGeneralConnectionError:
        st = NMMSS::IPDS_ConnectionError;
        break;
    default:
        st = NMMSS::IPDS_IpintInternalFailure;
        break;
    }
    m_notifier->Notify(st);
}

//Checks settings.
void CDevice::ValidateSettings()// throw(std::runtime_error)
{
    //Check version format.
    ITV8::GDRV::Version ver;
    if(!ver.fromString(m_settings.driverVersion))
    {
        throw std::runtime_error("Invalid version format.");
    }
}

std::string CDevice::ToString() const
{
    std::ostringstream str;
    str << "\"" << getDriverName() << ".ver:" << m_settings.driverVersion << "/" << getVendor() << "/"
        << getModel() << ".frw:" << getFirmware() << " [" << getHostName() << ":" << getPort() << "]\"";
    return str.str();
}

void CDevice::onChannelSignalRestored()
{
    if (!m_dynExecutor->Post(boost::bind(&CDevice::scheduleDeviceSerialNumberValidation, this)))
        _err_ << "Can't post scheduleDeviceSerialNumberValidation() to thread pool!";
}

std::string CDevice::getConnectionInfo() const
{
    std::ostringstream conInf;
    conInf << getHostName() << ":" << getPort();
    //Should add without any cases, see
    //ItvDeviceSdk/include/IDevice.h IDevice::Connect()
    conInf << ":" << getLogin() << ":" << getPassword();
    

    return conInf.str();
}

unsigned int CDevice::getDriverVersion() const{
    ITV8::GDRV::Version ver;
    ver.fromString(m_settings.driverVersion);
    return ver;
}

void CDevice::RegisterObjectParamContext(IPINT30::IObjectParamContext* oc)
{
    if (0 == oc)
        return;

    IPINT30::IParamContextIterator* paramContextIterator = oc->GetParamIterator();
    if (0 != paramContextIterator)
    {
        // Go though all groups of params of current object.
        paramContextIterator->Reset();
        while (paramContextIterator->MoveNext())
        {
            const char* ctxName = paramContextIterator->GetCurrentContextName();
            m_paramContexts.insert(std::make_pair(ctxName, oc));
            _inf_ << ToString() << " The parameter context \"" << ctxName << "\" registered.";
        }
    }
}

void CDevice::UnregisterObjectParamContext(IPINT30::IObjectParamContext* oc)
{
    if (0 == oc)
        return;

    IPINT30::IParamContextIterator* paramContextIterator = oc->GetParamIterator();
    if (0 != paramContextIterator)
    {
        // Go though all groups of params of current object.
        paramContextIterator->Reset();
        while (paramContextIterator->MoveNext())
        {
            const char* ctxName = paramContextIterator->GetCurrentContextName();
            if(m_paramContexts.erase(ctxName) != 0)
            {
                _inf_ << ToString() << " The parameter context \"" << ctxName << "\" unregistered.";
            }
            else
            {
                _wrn_ << ToString() << " the parameter context with name \"" << ctxName 
                    << "\" already unregistered.";
            }
        }
    }
}

void CDevice::GetInitialState(NStructuredConfig::TCategoryParameters& params) const
{
    params.clear();

    std::string uniqueName = "";
    for (const TParamMap::value_type& object: m_paramContexts)
    {
        IPINT30::IParamContextIterator* paramContextIterator = object.second->GetParamIterator();
        if (0 != paramContextIterator)
        {
            IPINT30::IParamsContext* pc = paramContextIterator->GetContextByName(object.first);

            // To avoid videoChannel:0 duplicates while parameters gathering.
            if (uniqueName == object.first)
                continue;
            uniqueName = object.first; 
            // Get current values of params from context.
            pc->GatherParameters(object.first, params);
        }
    }
}

bool CDevice::checkParamContext(const std::string& groupName,
                                const TCategoryPath& path,
                                const NStructuredConfig::TCategoryParameters::value_type& group,
                                TParamMap::const_iterator& it)
{
    bool isVideoAnalytics = false;
    for (auto subPath : path)
    {
        if (std::find(subPath.begin(), subPath.end(), "VideoAnalytics") != subPath.end())
        {
            isVideoAnalytics = true;
            break;
        }
    }

    // If it is properties of non-existent object.
    if (it == m_paramContexts.end())
    {
        if (isVideoAnalytics)
        {
            UpdateVideoAnalyticsExistence(groupName, path, false, group.first);
            it = m_paramContexts.find(groupName);
        }
        _err_if_(it == m_paramContexts.end()) << "New object " << groupName << " was not created.";
        return (it != m_paramContexts.end());
    }
    else if (isVideoAnalytics && IsDisabled(group.second))
    {
        UpdateVideoAnalyticsExistence(groupName, path, true, group.first);
        return false;
    }

    return true;
}

void CDevice::scheduleDeviceSerialNumberValidation()
{
    if (m_deviceSerialNumberValidationFailed || m_deviceSerialNumber.empty())
    {
        return;
    }

    if (!m_device)
    {
        _log_ << ToString() << " device doesn't exist";
        return;
    }

    ITV8::GDRV::IDeviceSerialNumberProvider* provider =
        ITV8::contract_cast<ITV8::GDRV::IDeviceSerialNumberProvider>(m_device);
    if (!provider)
    {
        return;
    }

    CDeviceSerialNumberHandler handler;
    provider->GetSerialNumber(&handler);
    handler.WaitForResult();

    _log_ << ToString() << " IDeviceSerialNumberHandler finished with error code " << handler.GetResult();
    if (handler.GetResult() == ITV8::ENotError)
    {
        const auto received = handler.GetSerialNumber();
        _dbg_ << ToString() << " IDeviceSerialNumberHandler returns serialNumber: '"
            << received << "'";
        if (m_deviceSerialNumber != received)
        {
            _wrn_ << ToString() << " valid device serial number '" << m_deviceSerialNumber
                << "' changed on device side to '" << received << "'";

            m_deviceSerialNumberValidationFailed = true;

            m_notifier->Notify(NMMSS::IPDS_SerialNumberValidationFailed);
        }
    }
}

namespace
{

bool getDeviceNodeOnwerId(const NStructuredConfig::TCustomParameters& parameters, std::string& result)
{
    auto it = std::find_if(parameters.begin(), parameters.end(),
        [](const NStructuredConfig::SCustomParameter& param)
    {
        return IsDeviceNodeOwnerParam(param);
    });
    if (parameters.end() == it)
        return false;
    result = it->ValueUtf8();
    return true;
}

}

bool CDevice::addDeviceNodeContext(const std::string& deviceNodeObjectId,
    TParamMap::const_iterator& it,
    const NStructuredConfig::TCustomParameters& parameters)
{
    try{
        SDeviceNodeParam settings;
        if (!DeviceNode::parseObjectId(deviceNodeObjectId, settings.deviceNodeId, settings.id))
        {
            std::string message = std::string("Incorrect device node id ='") + deviceNodeObjectId + std::string("'");
            throw std::runtime_error(message.c_str());
        }
        std::string ownerId;
        if (!getDeviceNodeOnwerId(parameters, ownerId))
            throw std::runtime_error("Missed ownerDeviceNode parameter for new device node");
        DeviceNode* deviceNodeOwner = nullptr;
        if (!ownerId.empty())
        {
            auto it = findDeviceNode(ownerId);
            if (it == m_deviceNodes.end())
            {
                std::string message = std::string("Incorrect parameter ownerDeviceNode ='") + ownerId + std::string("'");
                throw std::runtime_error(message.c_str());
            }
            deviceNodeOwner = it->Get();
        }
        CreateDeviceNode(settings, dynamic_cast<DeviceNode*>(deviceNodeOwner));
    }
    catch (const std::exception& ex)
    {
        _err_ << "New device node object " << deviceNodeObjectId << " was not created: " << ex.what();
        return false;
    }
    it = m_paramContexts.find(deviceNodeObjectId);
    if (m_paramContexts.end() == it)
    {
        _err_ << "Context for new device node object " << deviceNodeObjectId << " was not created.";
        return false;
    }

    return true;
}

CDevice::TDeviceNodes::iterator CDevice::findDeviceNode(const std::string& deviceNodeObjectId)
{
    return std::find_if(m_deviceNodes.begin(), m_deviceNodes.end(),
        [deviceNodeObjectId](const DeviceNodePtr& deviceNode)
    {
        return deviceNode->getObjectId() == deviceNodeObjectId;
    });
}

void CDevice::getDeviceNodeChildren(const std::string& deviceNodeObjectId, std::vector<std::string>& children)
{
    children.clear();
    for (const auto& child : m_deviceNodes)
    {
        if (child->getOwnerObjectId() == deviceNodeObjectId)
        {
            children.push_back(child->getObjectId());
        }
    }
}

/// Method is called when settings are changed and when something is added to configuration or removed from configuration. 
/// Method is used for add/remove objects for VisualAnalytic, Visuall element of VisualAnalytic and DeviceNodes when they are
/// added to system (either by change Enabled propery or by adding/removing it in GUI device/detector tree).
///
/// Method receives as parameter the list of changed properties separated by categories, where category name defines
/// object that was updated. Next category names are processed by the method for :
///        VideoChannel.{VideoChannelId}/VideoAnalytics.{DetectorType}.{DetectorId}
///            for process detector settings change or detector adding/removing
///        VideoChannel.{VideoChannelId}/VideoAnalytics.{DetectorType}/VisualElement.{ElementType}:{ElementId}
///            for process detector visual element settings change or detector visual element adding/removing
///        deviceNode:{DeviceNodeId}.{Id}
///            for process device node settings change or device node adding/removing
///
/// For example 
///        Adding motion_detection detector (for first video streaming):
///            CDevice::OnChanged, changes :
///                  VideoChannel.0 / VideoAnalytics.motion_detection.0
///                           'enabled' = '1'
///        Adding 2 areas for this detector:
///             CDevice::OnChanged, changes :
///                      VideoChannel.0 / VideoAnalytics.motion_detection.0 / VisualElement.window : 38749e60 - 1d67 - 466f - 897e-4eba98715838
///                               'History' = '90'
///                               'ObjectSize' = '15'
///                               'Sensitivity' = '90'
///                               'WindowType' = 'include'
///                               'window' = '0.05279831 0.1169014 0.3938754 0.587324'
///                      VideoChannel.0 / VideoAnalytics.motion_detection.0 / VisualElement.window : 70ad2830 - 9fd0 - 4feb - 90ac - eaca53206819
///                               'History' = '90'
///                               'ObjectSize' = '15'
///                               'Sensitivity' = '90'
///                               'WindowType' = 'include'
///                               'window' = '0.6979936 0.1309859 0.218585 0.2549296'
///        Removing one area and updateing parameters of second area:
///             CDevice::OnChanged, changes :
///                      VideoChannel.0 / VideoAnalytics.motion_detection.0 / VisualElement.window : 70ad2830 - 9fd0 - 4feb - 90ac - eaca53206819
///                               'window' = '0.3526927 0.09577465 0.5638859 0.2901409'
///             CDevice::OnChanged, removes :
///                      VideoChannel.0 / VideoAnalytics.motion_detection.0 / VisualElement.window : 38749e60 - 1d67 - 466f - 897e-4eba98715838
///                               'History' = '90'
///                               'ObjectSize' = '15'
///                               'Sensitivity' = '90'
///                               'WindowType' = 'include'
///                               'window' = '0.05279831 0.1169014 0.3938754 0.587324'
///        Removing detector:
///             CDevice::OnChanged, changes :
///                      VideoChannel.0 / VideoAnalytics.motion_detection.0
///                               'enabled' = '0'
///             CDevice::OnChanged, removes :
///                      VideoChannel.0 / VideoAnalytics.motion_detection.0 / VisualElement.window : 70ad2830 - 9fd0 - 4feb - 90ac - eaca53206819
///                               'History' = '90'
///                               'ObjectSize' = '15'
///                               'Sensitivity' = '90'
///                               'WindowType' = 'include'
///                               'window' = '0.3526927 0.09577465 0.5638859 0.2901409'
///       Adding Trezor B04 controller with its default child device nodes to firts bus device node as owner (supposed bus device node has been already added).
///       When new device node is added 'ownerDeviceNode' and 'enabled' properties are mandatory. The 'ownerDeviceNode' value for root device node is empty string.
///       Also 'address' property is mandatory, if this property is defined for device node in rep file (see adding channel device nodes for example).
///       For device nodes, that are added automatically (as channels of Trezor controller, for example), values of address property are expected 
///       unique for all adding device nodes with restrictions defined in rep file.:
///            deviceNode:bus485.0:controllerTrezorB04.0
///                'enabled' = '1'
///                'ownerDeviceNode' = 'busRS485.0'
///            deviceNode:bus485.0:controllerTrezorB04.0:channelTrezorB04.0
///                'ownerDeviceNode' = 'controllerTrezorB04.0'
///                'enabled' = '1'
///                'address' = '1'
///            deviceNode:bus485.0:controllerTrezorB04.0:channelTrezorB04.1
///                'ownerDeviceNode' = 'controllerTrezorB04.0'
///                'enabled' = '1'
///                'address' = '2'
///            deviceNode:bus485.0:controllerTrezorB04.0:channelTrezorB04.2
///                'ownerDeviceNode' = 'controllerTrezorB04.0'
///                'enabled' = '1'
///                'address' = '3'
///            deviceNode:bus485.0:controllerTrezorB04.0:channelTrezorB04.3
///                'ownerDeviceNode' = 'controllerTrezorB04.0'
///                'enabled' = '1'
///                'address' = '4'
///        Removing Trezor B04 controller with all its child device nodes (though they are not mentioned in parameters),
///            deviceNode:bus485.0:controllerTrezorB04.0
///                'enabled' = '0'

void CDevice::UpdateBlockingConfigurationProp(const std::string& groupName, const NStructuredConfig::TCustomParameters& parameters)
{
    const auto idPos = groupName.find("Device");
    if (idPos == std::string::npos)
        return;

    const auto isBlockingConfigurationProp = [](NStructuredConfig::SCustomParameter val) {return val.name == "blockingConfiguration"; };
    const auto it = std::find_if(parameters.begin(), parameters.end(), isBlockingConfigurationProp);
    if (it != parameters.end())
        m_blockingConfiguration = it->ValueUtf8() == "1";
}

void CDevice::OnChanged(const NStructuredConfig::TCategoryParameters &parameters, const NStructuredConfig::TCategoryParameters& removed)
{
    _trc_block_(log)
    {
        log << "CDevice::OnChanged: changed " << parameters.size() << std::endl;
        for (const auto& p : parameters)
        {
            log << "\t" << p.first << std::endl;
            for (const auto& param : p.second)
            {
                log << "\t\t" << param.name << "=" << param.ValueUtf8() << std::endl;
            }
        }

        log << "CDevice::OnChanged: removed " << removed.size() << std::endl;
        for (const auto& r : removed)
        {
            log << "\t" << r.first << std::endl;
            for (const auto& param : r.second)
            {
                log << "\t\t" << param.name << "=" << param.ValueUtf8() << std::endl;
            }
        }
    }

    TObjectParamContexts changedObjects;
    const std::string VISUAL_ELEMENT_TICKET = "/VisualElement.";

    for (const auto& group : parameters)
    {
        const std::string& groupName = group.first;
        std::string::size_type pos = groupName.find(VISUAL_ELEMENT_TICKET);
        if (pos != std::string::npos)
            continue;

        if (IsGeneric())
        {
            UpdateBlockingConfigurationProp(groupName, group.second);
        }

        if (IsDeviceNodeContext(groupName))
        {// Process device nodes properties.
            // device node objectId is part of group name after last ':', i.e. for
            //    deviceNode:bus485.0:controllerTrezorB04.0:channelTrezorB04.3
            // id will be = channelTrezorB04.3
            const std::string deviceNodeObjectId = groupName.substr(groupName.rfind(DEVICE_NODE_GROUP_DELIMITER) + 1);
            const std::string contextName = DEVICE_NODE_CONTEXT_NAME_PREFIX + deviceNodeObjectId;

            TParamMap::const_iterator it = m_paramContexts.find(contextName);
            bool isAdding = (m_paramContexts.end() == it);
            if (isAdding && !addDeviceNodeContext(deviceNodeObjectId, it, group.second))
                continue;

            UpdateChannelParameters(contextName, groupName, group.second, it->second);
            changedObjects.insert(it->second);
        }
        else
        {// Process common and detector's properties.
            TCategoryPath path;
            ParsePropertyGroupName(groupName, path);

            auto range = m_paramContexts.equal_range(groupName);
            if (range.first == range.second) // For VideoAnalytics only, try to create inside checkParamContext.
            {
                TParamMap::const_iterator it = m_paramContexts.end();
                if (!checkParamContext(groupName, path, group, it))
                    continue;

                //TODO: Here we cat return a flag that indicates real change of property.
                UpdateChannelParameters(groupName, groupName, group.second, it->second);
                changedObjects.insert(it->second);
            }
            else while (range.first != range.second)
            {
                TParamMap::const_iterator it = range.first++;
                if (!checkParamContext(groupName, path, group, it))
                    continue;

                //TODO: Here we cat return a flag that indicates real change of property.
                UpdateChannelParameters(groupName, groupName, group.second, it->second);
                changedObjects.insert(it->second);
            }
        }
    }

    // Process visual element's properties.
    NStructuredConfig::TCategory2UIDs actualVisualElements;
    for (const auto& group : parameters)
    {
        const std::string& groupName = group.first;
        std::string::size_type pos = groupName.find(VISUAL_ELEMENT_TICKET);
        if (pos == std::string::npos)
            continue;
        const std::string contextKey = groupName.substr(0, pos + VISUAL_ELEMENT_TICKET.size());

        TCategoryPath path;
        ParsePropertyGroupName(groupName, path);
        TParamMap::const_iterator it = m_paramContexts.find(contextKey);
        if (!checkParamContext(contextKey, path, group, it))
            continue;

        UpdateChannelParameters(contextKey, groupName, group.second, it->second);
        changedObjects.insert(it->second);
    }

    for (const auto& group : removed)
    {
        const std::string& groupName = group.first;
        std::string::size_type pos = groupName.find(VISUAL_ELEMENT_TICKET);
        if (pos != std::string::npos)
        {// Clear visual elements that were removed.
            TCategoryPath path;
            ParsePropertyGroupName(groupName, path);
            if (path.size() >= 3 && path[2].size() >= 3)
            {
                const std::string contextKey = groupName.substr(0, pos + VISUAL_ELEMENT_TICKET.size());

                TParamMap::const_iterator it = m_paramContexts.find(contextKey);
                if (!checkParamContext(contextKey, path, group, it))
                    continue;

                ClearInternalState(contextKey, path[2][2], group.second);
                changedObjects.insert(it->second);
            }
        }
        else if (IsDeviceNodeContext(groupName))
        {//clear device nodes that were removed
            // device node objectId is part of group name after last ':', i.e. for
            //    deviceNode:bus485.0:controllerTrezorB04.0:channelTrezorB04.3
            // id will be = channelTrezorB04.3
            const std::string deviceNodeObjectId = groupName.substr(groupName.rfind(DEVICE_NODE_GROUP_DELIMITER) + 1);
            DestroyDeviceNode(deviceNodeObjectId);
        }
    }

    if (IsGeneric())
    {
        SettingsHandler settingsHandler;

        for (auto& object : changedObjects)
        {
            object->ApplyChanges(&settingsHandler);
            object->SwitchEnabled();
        }

        settingsHandler.Wait();
    }
    else
    {
        for (auto& object : changedObjects)
        {
            object->ApplyChanges(0);
            object->SwitchEnabled();
        }
    }

    if (m_breakUnusedConnections)
        m_needResetNotifier = true;
}

void CDevice::OnChanged(const NStructuredConfig::TCategoryParameters& meta)
{
    boost::mutex::scoped_lock lock(m_guardMetaSettings);
    for (auto group : meta)
    {
        const std::string& groupName = group.first;

        TCategoryPath path;
        ParsePropertyGroupName(groupName, path);

        auto range = m_paramContexts.equal_range(groupName);
        while (range.first != range.second)
        {
            TParamMap::const_iterator it = range.first++;
            if (!checkParamContext(groupName, path, group, it))
                continue;

            it->second->ApplyMetaChanges(group.second);
        }
    }
}

void CDevice::ResetToDefaultProperties(std::string& deviceDescription)
{
    if (!m_device)
    {
        return;
    }

    ITV8::IDefaultPropertiesSetter* setter = ITV8::contract_cast<ITV8::IDefaultPropertiesSetter>(m_device);
    if (!setter)
    {
        _wrn_ << ToString() << " Device doesn't support IDefaultPropertiesSetter";
        return;
    }

    _log_ << ToString() << " Reset to default properties begin";
    CDynamicParametersHandler handler;
    setter->SetDefaultProperties(&handler);
    handler.WaitForResult();

    const auto ipintResult = handler.GetResult();
    if (ipintResult == ITV8::ENotError)
    {
        deviceDescription = handler.GetJsonData();
    }
    else
    {
        _err_ << ToString() << "Reset to default properties failed: errorCode=" << ipintResult;
    }
    _log_ << ToString() << " Reset to default properties end";
}

void CDevice::UpdateVideoAnalyticsExistence(const std::string& name, const TCategoryPath& path, bool isRemoved, const std::string& grpId)
{
    try
    {
        // Expect "VideoChannel.{VideoChannelId}/VideoAnalytics.{DetectorType}.{DetectorId}"
        if (path.size() != 2 || path[0].size() != 2 || path[1].size() != 3 ||
            path[0][0] != "VideoChannel" || path[1][0] != "VideoAnalytics")
        {
            throw std::runtime_error("Unexpected category format for VideoAnalytics properties.");
        }
        int videoChannelId = boost::lexical_cast<int>(path[0][1]);
        const std::string& detectorType = path[1][1];
        const std::string& detectorid = path[1][2];

        // If we are here, the type of detector was changed or it was command "enabled=0".
        DestroyVideoAnalytics(videoChannelId, detectorType, detectorid);

        if (!isRemoved)
        {
            SEmbeddedDetectorSettings detectorSettings;
            detectorSettings.name = detectorType;
            detectorSettings.enabled = false;          
            detectorSettings.privateParams.push_back(NStructuredConfig::SCustomParameter("DisplayId", "int", detectorid));

            CreateVideoAnalytics(videoChannelId, detectorSettings);
        }
    }
    catch (const std::runtime_error& e)
    {
        _err_ << ToString() << " UpdateVideoAnalyticsExistence(\"" << name
            << "\",false) - threw exception: " << e.what();
    }
}

void CDevice::UpdateChannelParameters(const std::string& groupKey, const std::string& name,
    const NStructuredConfig::TCustomParameters& parameters, IPINT30::IObjectParamContext* channel)
{
    _inf_ << ToString() << " The parameter context \"" << name << "\" changing...";

    IPINT30::IParamContextIterator* pci = channel->GetParamIterator();
    if (0 != pci)
    {
        IPINT30::IParamsContext* pc = pci->GetContextByName(groupKey);
        if (0 != pc)
        {
            pc->UpdateParameters(name, parameters);
        }
        else
        {
            _err_ << ToString() << " Context of group \"" << groupKey << "\" not found.";
        }
    }
    _inf_ << ToString() << " The parameter context \"" << name << "\" changed";
}

void CDevice::ClearInternalState(const std::string& groupKey, const std::string& uid, const NStructuredConfig::TCustomParameters& params)
{
    auto it = m_paramContexts.find(groupKey);
    if (it != m_paramContexts.end())
    {
        IPINT30::IObjectParamContext* channel = it->second;
        if (!channel)
            return;

        IPINT30::IParamContextIterator* pci = channel->GetParamIterator();
        if (0 == pci)
            return;

        IPINT30::IParamsContext* pc = pci->GetContextByName(groupKey);
        if (pc)
            pc->CleanOldParameters(uid, params);
        else
            _err_ << ToString() << " Context of group \"" << groupKey << "\" not found.";
    }
}


void CDevice::EquipmentStatusChanged(ITV8::GDRV::IDevice* /*source*/, const char* data)
{
    if (!m_breakUnusedConnections)
        return;

    if (!m_equipmentNotifier || m_needResetNotifier)
    {
        m_equipmentNotifier.reset(new EquipmentNotifier(GET_LOGGER_PTR, m_callback, resetStatesContainer(ITV8::EVideoSource)));
        m_needResetNotifier = false;
    }

    try
    {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(data, root))
        {
            _err_ << ToString() << "EquipmentStatusChanged: Can't parse json data";
            return;
        }

        for (const auto& obj : root["data"])
        {
            if (obj.isMember("streaming") && obj.isMember("codec") && obj.isMember("width")
                && obj.isMember("height"))
            {
                const auto w = obj["width"].asUInt();
                const auto h = obj["height"].asUInt();
                if (m_aggregator && w > 0 && h > 0)
                {
                    using namespace NStatisticsAggregator;
                    const auto ap(getUnitId(obj["type"].asInt(), obj["id"].asInt(), obj["streaming"].asInt()));
                    m_aggregator->Push(std::move(StatPoint(LiveWidth, ap, FAKE_STATS_TTL).AddValue(w)));
                    m_aggregator->Push(std::move(StatPoint(LiveHeight, ap, FAKE_STATS_TTL).AddValue(h)));
                    m_aggregator->Push(std::move(StatPoint(LiveMediaType, ap, FAKE_STATS_TTL).AddValue(NMMSS::NMediaType::Video::ID)));
                    m_aggregator->Push(std::move(StatPoint(LiveStreamType, ap, FAKE_STATS_TTL).AddValue(getStreamType(obj["codec"].asString()))));
                }
            }

            m_equipmentNotifier->notify(
                getUnitId(obj["type"].asInt(), obj["id"].asInt()),
                getUnitStatus(obj["status"].asInt()));
        }
    }
    catch (const std::runtime_error& e)
    {
        _err_ << ToString() << " EquipmentStatusChanged: Exception thrown: " << e.what();
    }
}

statesMap_t CDevice::resetStatesContainer(const uint32_t types)
{
    statesMap_t m;
    if (types & ITV8::EVideoSource)
    {
        for (const auto &it : m_videoSources)
        {
            if (!it->IsChannelEnabled())
                continue;

            const auto key = concatVideoSourceId(m_objectID, it->GetChannelId(), STRegistrationStream);
            m.insert(std::make_pair(key, NMMSS::IPDS_SignalLost));
        }
    }
    return m;
}

std::string CDevice::getUnitId(const int type, const int channel, const int streaming) const
{
    std::string unitId;
    switch (type)
    {
    case ITV8::EVideoSource:
    {
        unitId = concatVideoSourceId(m_objectID, channel, streaming);
        break;
    }
    case ITV8::EAudioSource:
    case ITV8::EAudioDestination:
    case ITV8::EIODevice:
    case ITV8::ETelemetry:
    case ITV8::EStorage:
    case ITV8::ETextEventSource:
    case ITV8::EVideoAnalytics:
    case ITV8::EUnit:
    default:
        break;
    }

    return unitId;
}

}
