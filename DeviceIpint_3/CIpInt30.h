#ifndef DEVICEIPINT3_CIPINT30_H
#define DEVICEIPINT3_CIPINT30_H

#include <unordered_map>
#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition.hpp>
#include <CorbaHelpers/RefcountedImpl.h>

#include <ItvSdkWrapper.h>
#include <DeviceManager/IDeviceManager.h>

#include <CommonNotificationCpp/StateControlImpl.h>
#include <InfraServer_IDL/ConfigurableImpl.h>

#include "../Grabber/Grabber.h"
#include "DeviceSettings.h"
#include "IIPManager3.h"
#include "DeviceInfo.h"
#include "ParamContext.h"
#include "AsyncActionHandler.h"
#include "DeviceInformation.h"
#include "DeviceControl.h"
#include "DeviceNode.h"
#include "Notify.h"

typedef std::vector<std::string> TCategoryPathParts;
typedef std::vector<TCategoryPathParts> TCategoryPath;
using statesMap_t = std::unordered_map<std::string, NMMSS::EIpDeviceState>;

namespace IPINT30
{

class EquipmentNotifier: public NLogging::WithLogger
{
    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;
    statesMap_t m_lastEquipmentStates;
    std::string m_lastTS;
public:
    EquipmentNotifier(DECLARE_LOGGER_ARG, boost::shared_ptr<NMMSS::IGrabberCallback> callback, const statesMap_t& m)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , m_callback(callback)
        , m_lastEquipmentStates(m)
    {
    }

    void notify(const std::string& unitId, NMMSS::EIpDeviceState state)
    {
        if (unitId.empty())
        {
            _wrn_ << "EquipmentNotifier: notify skipped, empty unit id.";
            return;
        }

        //skip unchanged state notification
        auto it = m_lastEquipmentStates.find(unitId);
        if (it == m_lastEquipmentStates.cend() || it->second == state)
            return;

        std::string now = boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time());
        try
        {
            m_callback->OnNotify(unitId.c_str(), state, now.c_str(), m_lastTS.c_str());
            _log_ << "EquipmentNotifier: OnNotify(" << unitId << ", " << EIpDeviceStateToString(state) << ", " << now << ")";
        }
        catch (const std::logic_error& e)
        {
            _err_ << "EquipmentNotifier: OnNotify(" << unitId << ", "
                  << EIpDeviceStateToString(state) << ", " << now << ") - failed. Error=" << e.what();
        }

        it->second = state;
        m_lastTS = now;
    }
};

class CAudioSource;
class CTelemetry;
class CVideoSource;
class CVideoAnalytics;
class CIoPanel;
class CLoudSpeaker;
class CTextEventSource;
class EmbeddedStorage;
class CAdaptiveSourceFactory;

using CVideoSourcePtr = NCorbaHelpers::CAutoPtr<CVideoSource>;
using CVideoAnalyticsPtr = NCorbaHelpers::CAutoPtr<CVideoAnalytics>;
using CAudioSourcePtr = NCorbaHelpers::CAutoPtr<CAudioSource>;
using CTelemetryPtr = NCorbaHelpers::CAutoPtr<CTelemetry>;
using CIoPanelPtr = NCorbaHelpers::CAutoPtr<CIoPanel>;
using CLoudSpeakerPtr = NCorbaHelpers::CAutoPtr<CLoudSpeaker>;
using CTextEventSourcePtr = NCorbaHelpers::CAutoPtr<CTextEventSource>;
using EmbeddedStoragePtr = NCorbaHelpers::CAutoPtr<EmbeddedStorage>;
using DeviceNodePtr = NCorbaHelpers::CAutoPtr<DeviceNode>;
using PAdaptiveSourceFactory = NCorbaHelpers::CAutoPtr<CAdaptiveSourceFactory>;

//Implements the functional to support ITV8::GDRV::IDevice
class CDevice : public IPINT30::IIpintDevice
              , public CAsyncHandlerBase
              , public IDeviceInformationProvider
              , public ITV8::GDRV::IDeviceControl
              , public ITV8::GDRV::IDeviceEquipmentStatusProvider
{
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IDeviceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDeviceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDeviceEquipmentStatusProvider)
    ITV8_END_CONTRACT_MAP()

    CDevice(DECLARE_LOGGER_ARG, const char* szObjectID, NCorbaHelpers::IContainerNamed* pContainer,
        bool callbackNeeded, boost::shared_ptr<SDeviceSettings> settingsPtr, NExecutors::PDynamicThreadPool threadPool);

    //Checks settings.
    void ValidateSettings();// throw(std::runtime_error);

    virtual ~CDevice();

//IPINT30::IIPManager implementation
public:
    virtual void Open();
    virtual void Close();
    virtual void Connect();
    virtual bool IsGeneric() const;
    virtual bool BlockConfiguration() const;

//ITV8::IEventHandler implementation
public:
    //Signals that last call to ITV8::GDRV::IDevice - failed with error.
    virtual void Failed(ITV8::IContract* pSource, ITV8::hresult_t error);

//ITV8::GDRV::IDeviceHandler implementation
public:
    virtual void Connected(ITV8::GDRV::IDevice* pSource);
    virtual void StateChanged(ITV8::GDRV::IDevice* pSource, ITV8::uint32_t state);
    virtual void Disconnected(ITV8::GDRV::IDevice* pSource);

//ITV8::GDRV::IDeviceEquipmentStatusProvider implementation
public:
    void EquipmentStatusChanged(ITV8::GDRV::IDevice* source, const char* data) override;

// IObjectParamContext implementation
public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();

// IDeviceControl implementation
public:
    virtual void Reboot(IAsyncActionHandler* handler);
    virtual void SoftwareDefault(IAsyncActionHandler* handler);
    virtual void FactoryDefault(IAsyncActionHandler* handler);
    virtual void FirmwareUpgrade(const char* fwFile, IAsyncActionHandler* handler);
    virtual void GetSystemDateTime(ITV8::GDRV::IGetSystemDateTimeHandler* handler);
    virtual void SetSystemDateTime(const ITV8::GDRV::ISystemDateAndTimeInfo& dateTimeInfo, IAsyncActionHandler* handler);
    virtual void GetNTPServer(ITV8::GDRV::IGetNTPServerHandler* handler);
    virtual void SetNTPServer(const ITV8::GDRV::INTPServerInfo& ntpServerInfo, IAsyncActionHandler* handler);
    virtual void GetNetworkParams(ITV8::GDRV::IGetNetworkParamsHandler* handler);
    virtual void SetNetworkParams(const ITV8::GDRV::INetworkParamsInfo& networkParams, IAsyncActionHandler* handler);

// IParameterHolder implementation
public:
    void GetInitialState(NStructuredConfig::TCategoryParameters& params) const override;
    void OnChanged(const NStructuredConfig::TCategoryParameters& params, const NStructuredConfig::TCategoryParameters& removed) override;
    void OnChanged(const NStructuredConfig::TCategoryParameters& meta) override;
    void ResetToDefaultProperties(std::string& deviceDescription) override;

// CAsyncHandlerBase implementation
protected:
    virtual void ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code);

public:
    //Gets the Ipint device interface.
    virtual ITV8::GDRV::IDevice* getDevice() const
    {
        return m_device;
    }

    virtual std::string ToString() const;

    void onChannelSignalRestored() override;

private:
    typedef std::map<std::string, NStructuredConfig::SCustomParameter> TCustomParametersMap;

    //Signals that operation of autodetecting finished.
    void OnAutodetectionFinished();

    void CreateVideoSource(const SVideoChannelParam& channelSettings);

    void CreateVideoStreaming(NCorbaHelpers::IContainerNamed* container, const SVideoChannelParam& channelSettings, 
        std::size_t streamType, PAdaptiveSourceFactory adaptiveFactory);

    void CreateVideoAnalytics(int videoChannelId, const SEmbeddedDetectorSettings& detectorSettings);
    void DestroyVideoAnalytics(int videoChannelId, const std::string& detectorType, const std::string& detectorId);

    void CreateAudioSource(const SMicrophoneParam& micSettings);

    void CreateTelemetryChannel(const STelemetryParam& telSettings);

    void CreateIoDevice(const SIoDeviceParam& ioDeviceSettings);

    void CreateAudioDestination(const SAudioDestinationParam& settings);

    void CreateEmbeddedStorage(const SEmbeddedStoragesParam& settings, bool isOfflineAnalytics);

    void CreateTextEventSource(const STextEventSourceParam& settings);

    void CreateDeviceNode(const SDeviceNodeParam& settings, DeviceNode* owner);
    void DestroyDeviceNode(const std::string& id);

    template<class T>
    void OnChannelCreated(T channel, std::vector<T>& collection);

    // Registers all parameter context for object (driver channel).
    void RegisterObjectParamContext(IPINT30::IObjectParamContext* oc);
    void UnregisterObjectParamContext(IPINT30::IObjectParamContext* oc);

    void UpdateChannelParameters(const std::string& groupKey, const std::string& name, 
        const NStructuredConfig::TCustomParameters& parameters, IPINT30::IObjectParamContext* channel);

    void UpdateVideoAnalyticsExistence(const std::string& name, const TCategoryPath& path, bool isRemoved, const std::string& grpId);
    void ClearInternalState(const std::string&, const std::string& uid, const NStructuredConfig::TCustomParameters&);
    void UpdateBlockingConfigurationProp(const std::string& name, const NStructuredConfig::TCustomParameters& parameters);

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

    template<typename TAction> void handleChildAction(TAction action);
    PTimeCorrectorSyncPoint getSyncPointByChannelId(int channelId);

private:
    std::string m_objectID;
    NCorbaHelpers::WPContainerNamed m_container;
    NExecutors::PDynamicThreadPool m_dynExecutor;
    NStatisticsAggregator::PStatisticsAggregator m_aggregator;

    SDeviceSettings m_settings;
    boost::shared_ptr<SDeviceSettings> m_settingsPtr;

    // The reference to the device (the interface is implemented in DevicePack driver.)
    ITV8::GDRV::IDevice* m_device;

    inline std::string getConnectionInfo() const;

    inline int getPort() const
    {
        return m_settings.port;
    }

    inline const std::string &getHostName() const
    {
        return m_settings.host;
    }

    inline const std::string &getLogin() const
    {
        return m_settings.login;
    }


    inline const std::string &getPassword() const
    {
        return m_settings.password;
    }

    inline const std::string &getVendor() const
    {
        return m_settings.vendor;
    }

    inline const std::string &getModel() const
    {
        return m_settings.model;
    }

    inline const std::string &getDriverName() const
    {
        return m_settings.driverName;
    }

    inline unsigned int getDriverVersion() const;

    inline const std::string &getFirmware() const
    {
        return m_settings.firmware;
    }

    ITV8::MMD::IDeviceManager* m_deviceManager;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;
    boost::shared_ptr<INotifyState> m_notifier;

    boost::mutex m_connectedMutex;
    boost::condition_variable m_connectedCondition;
    bool m_connected;
    bool m_readyToDestroy;

    boost::mutex m_deviceMutex;

    typedef std::vector<CVideoSourcePtr> TVideoSources;
    TVideoSources m_videoSources;
    typedef std::vector<CVideoAnalyticsPtr> TVideoAnalyticses;
    TVideoAnalyticses m_videoAnalytics;
    typedef std::vector<CAudioSourcePtr> TAudioSources;
    TAudioSources m_audioSources;
    typedef std::vector<CTelemetryPtr> TTelemetryChannels;
    TTelemetryChannels m_telemetryChannels;
    typedef std::vector<CIoPanelPtr> TIoPanels;
    TIoPanels m_ioPanels;
    typedef std::vector<CLoudSpeakerPtr> TLoudSpeakers;
    TLoudSpeakers m_speakers;
    
    typedef std::vector<EmbeddedStoragePtr> TEmbeddedStorages;
    TEmbeddedStorages m_embededStorages;

    typedef std::vector<CTextEventSourcePtr> TTextEventSources;
    TTextEventSources m_textEventSources;

    typedef std::vector<DeviceNodePtr> TDeviceNodes;
    TDeviceNodes m_deviceNodes;

    // Defines the information about device specified in settings (see m_settings);
    DeviceInfo m_deviceInfo;

    NStructuredConfig::TCategoryParameters m_params;

    // Map of objects for parameters apply
    typedef std::multimap<std::string, IPINT30::IObjectParamContext*> TParamMap;
    TParamMap m_paramContexts;
    boost::shared_ptr<CParamContext> m_context;
    boost::mutex m_guardMetaSettings;

    DeviceInformationManager m_deviceInformationManager;
    DeviceInformationServantHolder m_deviceInformationServantHolder;

    DeviceControlManager m_deviceControlManager;
    DeviceControlServantHolder m_deviceControlServantHolder;

    NCorbaHelpers::PContainerNamed m_sourcesContainer;

    bool addDeviceNodeContext(const std::string& deviceNodeObjectId,
                                    TParamMap::const_iterator& it,
                                    const NStructuredConfig::TCustomParameters& parameters);
    TDeviceNodes::iterator findDeviceNode(const std::string& deviceNodeObjectId);
    void getDeviceNodeChildren(const std::string& deviceNodeObjectId, std::vector<std::string>& children);

    bool checkParamContext(const std::string& groupName,
        const TCategoryPath& path,
        const NStructuredConfig::TCategoryParameters::value_type& group,
        TParamMap::const_iterator& it);

    void scheduleDeviceSerialNumberValidation();

    bool m_breakUnusedConnections = false;
    bool m_useVideoBuffersWithSavingPrevKeyFrame = false;
    std::string m_deviceSerialNumber;
    bool m_deviceSerialNumberValidationFailed;
    std::atomic_bool m_blockingConfiguration;
    bool m_needResetNotifier = false;
    std::unique_ptr<EquipmentNotifier> m_equipmentNotifier;
    statesMap_t resetStatesContainer(const uint32_t types);
    std::string getUnitId(const int type, const int id, const int streaming = STRegistrationStream) const;
};
}

#endif // DEVICEIPINT3_CIPINT30_H
