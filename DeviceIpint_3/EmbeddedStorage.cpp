#include "EmbeddedStorage.h"
#include "Notify.h"
#include "Utility.h"

#include <algorithm>

#include <InfraServer_IDL/LicenseChecker.h>

#include <boost/make_shared.hpp>
#include <boost/thread/mutex.hpp>

#include "RecordingsInfoRequester.h"
#include "StorageSource.h"
#include <CommonNotificationCpp/StatisticsAggregator.h>
#include <ItvDeviceSdk/include/IRecordingPlayback.h>

namespace IPINT30
{

class StartedStateHolder : boost::noncopyable
{
public:
    explicit StartedStateHolder(ITV8::GDRV::IAsyncDeviceChannelHandler* parent) :
        m_parent(parent)
    {
        if (m_parent)
            m_parent->Started(0);
    }

    ~StartedStateHolder()
    {
        if (m_parent)
            m_parent->Stopped(0);
    }

private:
    ITV8::GDRV::IAsyncDeviceChannelHandler* const m_parent;
};


namespace
{
    const char* const EMBEDDED_STORAGE_CONTEXT_NAME = "EmbeddedStorage.%d";
    const char* const PLAYBACK_SPEED = "playbackSpeed";
    const char* const SUPPORTS_RATE_CONTROL_OFF = "supportsTurnOffRateControl";
    const char* const SUPPORTS_AUDIO = "supportsAudio";
    const char* const VALUE_ON = "1";
    const uint32_t MAX_SUPPORTED_ENDPOINTS_COUNT = 1000u;

    int getMaximalPlaybackSpeed(const SEmbeddedStoragesParam& storageParam)
    {
        int speed = ITV8::GDRV::TurnOffRateControl;
        using namespace NStructuredConfig;
        const auto& params = storageParam.privateParams;
        if (!std::any_of(params.begin(), params.end(), [](const SCustomParameter& item)
        {
            return (item.name == SUPPORTS_RATE_CONTROL_OFF) &&
                (item.ValueUtf8() == VALUE_ON);
        }))
        {
            speed = 1;
            try
            {
                for (const auto& x : params)
                {
                    if (x.name == PLAYBACK_SPEED)
                        speed = boost::lexical_cast<int>(x.ValueUtf8().c_str());
                }
            }
            catch (const boost::system::system_error&)
            {

            }
        }
        return speed;
    }
    bool supportsAudio(const SEmbeddedStoragesParam& storageParam)
    {
        using namespace NStructuredConfig;
        const auto& params = storageParam.privateParams;
        return std::any_of(params.begin(), params.end(), [](const SCustomParameter& item)
        {
            return (item.name == SUPPORTS_AUDIO) &&
                (item.ValueUtf8() == VALUE_ON);
        });
    }
}

class CEndpointConnectionsManager : public virtual NLogging::WithLogger
{
public:
    CEndpointConnectionsManager(DECLARE_LOGGER_ARG) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_maxSupportedEndpointsCount(MAX_SUPPORTED_ENDPOINTS_COUNT),
        m_currentEndpointsCount(0)
    {
    }

    void UpdateAllowedConnectionsCount(const uint32_t connections)
    {
        m_maxSupportedEndpointsCount = connections;
    }

    bool IncreaseEndpointConnection()
    {
        auto result = true;
        auto currentValue = m_currentEndpointsCount.load(std::memory_order_relaxed);
        do
        {
            if (currentValue + 1 > m_maxSupportedEndpointsCount)
            {
                result = false;
                break;
            }
        }
        while (!m_currentEndpointsCount.compare_exchange_weak(currentValue, currentValue + 1, std::memory_order_relaxed));

        return result;
    }

    void DecreaseEndpointConnection()
    {
        _dbg_ << "before DecreaseEndpointConnection() currentEndpointsCount = " << m_currentEndpointsCount;

        if(m_currentEndpointsCount != 0) //prevent overflow
            --m_currentEndpointsCount;

        _dbg_ << "after DecreaseEndpointConnection() currentEndpointsCount = " << m_currentEndpointsCount;
        return;
    }

private:
    uint32_t                      m_maxSupportedEndpointsCount;
    std::atomic<uint32_t>         m_currentEndpointsCount;
};

EmbeddedStorage::EmbeddedStorage(DECLARE_LOGGER_ARG, 
                                 NExecutors::PDynamicThreadPool dynExec,
                                 boost::shared_ptr<IPINT30::IIpintDevice> parent,
                                 const SEmbeddedStoragesParam& storageParam, bool isOfflineAnalytics,
                                 boost::shared_ptr<NMMSS::IGrabberCallback> callback,
                                 const char* objectContext,
                                 NCorbaHelpers::IContainerNamed* container,
                                 NCorbaHelpers::PContainerNamed sourcesContainer,
                                 size_t videoChannelsCount,
                                 const std::string& eventChannel) :
    NLogging::WithLogger(GET_LOGGER_PTR),
    CAsyncChannelHandlerImpl<ITV8::GDRV::IAsyncDeviceChannelHandler>(GET_LOGGER_PTR, dynExec, parent, 0),
    m_storageParam(storageParam),
    m_callback(callback),
    m_objectContext(objectContext),
    m_name(boost::str(boost::format(EMBEDDED_STORAGE_CONTEXT_NAME) % storageParam.displayId)),
    m_sourcesContainer(sourcesContainer),
    m_context(new CParamContext),
    m_endpointConnectionsManager(std::make_unique<CEndpointConnectionsManager>(GET_LOGGER_PTR)),
    m_sourcesCleared(false),
    m_isOfflineAnalytics(isOfflineAnalytics),
    m_videoChannelsCount(videoChannelsCount),
    m_eventChannel(eventChannel),
    m_reactor(NCorbaHelpers::GetReactorInstanceShared())
{
    m_context->AddContext(m_name.c_str(), MakeContext(m_storageParam));

    // EmbeddedStorage doesn't use sinks, so set connected flag by default.
    SetFlag(cfSinkConnected, true);

    _dbg_ << "EmbeddedStorage created";
}

EmbeddedStorage::~EmbeddedStorage()
{
    m_lease.reset();
    m_endpointConnectionsManager.reset();
}

std::string EmbeddedStorage::GetDynamicParamsContextName() const
{
    return std::string();
}

void EmbeddedStorage::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (handler)
        handler->Failed(0, ITV8::EUnsupportedCommand);
}

void EmbeddedStorage::SwitchEnabled()
{
    if (m_storageParam.enabled && m_isOfflineAnalytics)
    {
        NCorbaHelpers::PContainerNamed cont = m_sourcesContainer;
        if (!cont)
            return;
        try
        {
            NLicenseService::PLicenseChecker lc = NLicenseService::GetLicenseChecker(cont.Get());
            m_lease.reset(lc->Acquire("OfflineAnalytics", { m_objectContext + "/" + m_name }));
        }
        catch (const InfraServer::LicenseService::InvalidArgument& /*e*/)
        {
            _err_ << "License has no position for OfflineAnalytics";
        }
        catch (const InfraServer::LicenseService::LicenseFailed& /*e*/)
        {
            _err_ << "No free leases for OfflineAnalytics";
        }
        if (!m_lease)
        {
            return;
        }
    }

    SetEnabled(m_storageParam.enabled);
}

void EmbeddedStorage::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (handler)
        WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler)->Finished(0, ITV8::EUnsupportedCommand);
    // Implement it using device
}

IParamContextIterator* EmbeddedStorage::GetParamIterator()
{
    return m_context.get();
}

const SEmbeddedStoragesParam& EmbeddedStorage::storageParams() const noexcept
{
    return m_storageParam;
}

std::string EmbeddedStorage::ToString() const
{
    std::ostringstream str;
    if(!m_parent)
    {
        str << "DeviceIpint.Unknown";
    }
    else
    {
        str << m_parent->ToString();
    }
    str << "\\" << ".EmbeddedStorage:" << m_storageParam.displayId;
    return str.str();

}

void EmbeddedStorage::DoStart()
{
    _dbg_ << "Starting embedded storage...";

    ITV8::GDRV::IStorageDevice* storageDevice = getStorageDevice();
    if (!storageDevice)
    {
        return;
    }
    {
        boost::mutex::scoped_lock lock(m_sourcesGuard);
        m_sourcesCleared = false;
    }

    m_startedState = boost::make_shared<StartedStateHolder>(this);
    m_recordingsInfoManager.reset(new RecordingsInfoRequester(GET_LOGGER_PTR, storageDevice, m_storageParam.displayId,
        boost::bind(&EmbeddedStorage::handleRecordingsInfo, this, _1, _2),
        m_startedState, m_reactor->GetIO(), m_dynExec));

    CChannel::DoStart();
}

void EmbeddedStorage::DoStop()
{
    _dbg_ << "Stopping embedded storage...";
    if (m_recordingsInfoManager)
    {
        m_recordingsInfoManager->cancel();
    }
    if (m_startedState)
    {
        WaitForApply();
        m_startedState.reset();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

void EmbeddedStorage::OnStopped()
{
    m_recordingsInfoManager.reset();
}

void EmbeddedStorage::OnFinalized()
{
    SetEnabled(false);
}

void EmbeddedStorage::OnEnabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    std::ostringstream str;
    str << m_objectContext << "/MultimediaStorage." << m_storageParam.displayId;
    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback, 
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_sourcesContainer.Get()),
        str.str()));
}

void EmbeddedStorage::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    
    m_lease.reset();
    resourcesList_t sourcesServant;
    sourcesList_t sourcesList;

    {
        boost::mutex::scoped_lock lock(m_sourcesGuard);
        m_sourcesCleared = true;
        m_sourcesServant.swap(sourcesServant);
        m_sourcesList.swap(sourcesList);
    }
    std::for_each(sourcesList.begin(), sourcesList.end(), [this](WPStorageSource w)
    {
        if (PStorageSource s = w)
        {
            std::string name(s->Name());
            _dbg_ << "Stop... " << name;
            s->Stop();
            _dbg_ << name << " stopped.";
        }
    });
    sourcesServant.clear();

    this->Notify(NMMSS::IPDS_SignalLost); // because ipint driver do not call the Started method inspite of this is CAsyncChannel<T>. It is wrong abstraction, isn't it?

    this->SetNotifier(0);
}

void EmbeddedStorage::SetDeviceStateChanged(NMMSS::EIpDeviceState state)
{
    if (state == NMMSS::IPDS_Rebooted)
    {
        boost::mutex::scoped_lock lock(m_sourcesGuard);
        std::for_each(m_sourcesList.begin(), m_sourcesList.end(), [] (WPStorageSource w)
            {
                if (PStorageSource s = w)
                    s->ClearCache();
            });
    }
}

ITV8::GDRV::IStorageDevice* EmbeddedStorage::getStorageDevice()
{
    try
    {
        return ITV8::contract_cast<ITV8::GDRV::IStorageDevice>(getDevice());
    }
    catch (const std::runtime_error& e)
    {
        _err_ << ToString() <<  " Exception: Couldn't getDevice(). msg=" << e.what() << std::endl;
    }
    return 0;
}

bool EmbeddedStorage::IncreaseEndpointConnection()
{
    if (m_endpointConnectionsManager)
    {
        return m_endpointConnectionsManager->IncreaseEndpointConnection();
    }

    return false;
}

void EmbeddedStorage::DecreaseEndpointConnection()
{
    if (m_endpointConnectionsManager)
    {
        m_endpointConnectionsManager->DecreaseEndpointConnection();
    }
}

void EmbeddedStorage::NotifyError(ITV8::hresult_t err)
{
    switch (err)
    {
        case ITV8::ENotEnoughBandwidth: this->Notify(NMMSS::IPDS_NotEnoughBandwidth);
        default:
            return;
    }
}

void EmbeddedStorage::handleRecordingsInfo(const ITV8::Utility::recordingInfoList& recordingsInfo,
                                           StartedStateHolderSP)
{
    _dbg_ << recordingsInfo.size() << " recordings received for " << m_storageParam.displayId << " embedded storage.";

    NCorbaHelpers::PContainerNamed cont = m_sourcesContainer;
    if(!cont)
        return;

    auto storageDevice = getStorageDevice();
    if (!storageDevice)
        return;

    recordingInfoList::size_type storageSrcCount = std::min(recordingsInfo.size(), m_videoChannelsCount);
    /* We expect that driver reports summary list, which contains all recordings, for all embedded storage.
       Each embedded storage will create only part of sources, that represents video channels.
       For example, if device supports 3 channels and 2 storages, this function should get 6 recordings.
       First call will create src.0, src.1, src.2. Second call will create src.3, src.4, src.5.
    */

    auto requester = ITV8::contract_cast<ITV8::GDRV::IAllowedConnectionsRequester>(storageDevice);
    if (requester)
    {
        const auto supportedConnectionsCount = requester->GetAllowedConnectionsCount();
        m_endpointConnectionsManager->UpdateAllowedConnectionsCount(supportedConnectionsCount);
        _dbg_ << " Allowed connections count supported by driver is " << supportedConnectionsCount;
    }

    int playbackSpeed = getMaximalPlaybackSpeed(m_storageParam);
    bool supAudio = supportsAudio(m_storageParam);
    try
    {
        resourcesList_t servantList;
        sourcesList_t sources;

        for (recordingInfoList::size_type index = 0; index < storageSrcCount; ++index)
        {
            const ITV8::Utility::tracksInfoList_t& trackList = recordingsInfo[index]->tracks;
            auto videoTrackCount = std::count_if(trackList.begin(), trackList.end(),
                [](const ITV8::Utility::TrackInfo& track)
            {
                return track.GetMediaType() == ITV8::GDRV::Storage::EVideoTrack;
            });
            if (videoTrackCount > 1)
                _wrn_ << "Device reported more then one video track. Storage supports only first one!";
            else if (videoTrackCount == 0)
                _wrn_ << "There is no video track in reported list.";

            auto indexWithOffset = index + m_storageParam.displayId * m_videoChannelsCount;
            const std::string key = "src." + boost::lexical_cast<std::string>(indexWithOffset);

            const auto& info = recordingsInfo[std::min(indexWithOffset, recordingsInfo.size() - 1)];

            PStorageSource videoSource(new StorageSource(cont.Get(), key, info, m_parent, this, storageDevice,
                m_dynExec, playbackSpeed, supAudio, m_objectContext, m_eventChannel));

            IResourceSP servant((NCorbaHelpers::ActivateServant(cont.Get(), videoSource.Dup(), key.c_str())));
            sources.push_back(WPStorageSource(videoSource));
            servantList.push_back(servant);

            auto hasAudio = std::any_of(info->tracks.begin(), info->tracks.end(),
                boost::bind(&ITV8::Utility::TrackInfo::type, _1) == ITV8::GDRV::Storage::EAudioTrack);
            if (hasAudio && supAudio)
            {
                const std::string key = "src.audio:" + boost::lexical_cast<std::string>(indexWithOffset);
                PStorageSource audioSource(new StorageSource(cont.Get(), key, info, m_parent, this, storageDevice,
                    m_dynExec, playbackSpeed, supAudio, "", "", false));
                audioSource->SetParent(videoSource);

                IResourceSP servant((NCorbaHelpers::ActivateServant(cont.Get(), audioSource.Dup(), key.c_str())));
                sources.push_back(WPStorageSource(audioSource));
                servantList.push_back(servant);
            }
        }
        {
            boost::mutex::scoped_lock lock(m_sourcesGuard);
            if (!m_sourcesCleared)
            {
                m_sourcesServant.swap(servantList);
                m_sourcesList.swap(sources);
            }
        }

        this->Notify(NMMSS::IPDS_SignalRestored); // because ipint driver do not call the Started method inspite of this is CAsyncChannel<T>. It is wrong abstraction, isn't it?
    }
    catch (const CORBA::Exception& ex)
    {
        _err_ << "Fatal Error! Initialization Failed! Corba Exception: " << ex;
    }
    catch (std::exception& ex)
    {
        _err_ << "Fatal Error! Initialization Failed! std::exception: " << ex.what();
    }
}




}
