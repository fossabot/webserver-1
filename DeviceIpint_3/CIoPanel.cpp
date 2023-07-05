#include <ItvSdkWrapper.h>
#include <CorbaHelpers/CorbaStl.h>
#include "CIoPanel.h"
#include "CIpInt30.h"
#include "Notify.h"
#include "TimeStampHelpers.h"
#include "CRelayStateControl.h"

#include "sdkHelpers.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

#include "../MMClient/GrabberCallback.h"
#include "../MMClient/DetectorEventFactory.h"
#include "Utility.h"

namespace
{
    const char* const RELAY_CONTROL = "StateControl.relay%d:%d";

    const char* const IO_FORMAT = "IO.%d";
    const char* const IO_RAY_FORMAT = "IORay.%d:%d";
    const char* const IO_RELAY_FORMAT = "IORelay.%d:%d";

    const char* const RAY_ID_FORMAT = "EventSupplier.ray%d:%d";
    const char* const IO_EVENT_FORMAT = "%s/EventSupplier.ioDevice:%d";

    const std::string META_OFFERS_CONFIG_CHANGED_PROP("metaOffersConfigChanged");
}

namespace IPINT30
{

CIoPanel::CIoPanel(DECLARE_LOGGER_ARG, const SIoDeviceParam& settings, bool blockingConfiguration, NExecutors::PDynamicThreadPool dynExec,
    const std::string& eventChannel, boost::shared_ptr<IPINT30::IIpintDevice> parent, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
    const char* objectContext, NCorbaHelpers::IContainerNamed* container)
    : WithLogger(GET_LOGGER_PTR)
    , CAsyncChannelHandlerImpl<ITV8::GDRV::IIODeviceHandler>(GET_LOGGER_PTR, dynExec, parent, nullptr)
    , m_eventChannel(eventChannel)
    , m_settings(settings)
    , m_blockingConfiguration(blockingConfiguration)
    , m_callback(callback)
    , m_objectContext(objectContext)
    , m_container(container)
    , m_context(new CParamContext)
    , m_accessPoint(boost::str(boost::format(IO_EVENT_FORMAT) % m_objectContext % m_settings.id))
{
    //There is no data receiver for the relay/beam panel. Set the default flag.
    SetFlag(cfSinkConnected, true);

    const std::string io(boost::str(boost::format(IO_FORMAT)%m_settings.id));
    m_context->AddContext(io.c_str(), MakeContext(m_settings));

    TRayParams& rays = m_settings.rays;
    TRayParams::iterator rayIt1 = rays.begin(), rayIt2 = rays.end();
    for (; rayIt1 != rayIt2; ++rayIt1)
    {
        TCustomProperties properties;
        SRayParam& rp = *rayIt1;

        RegisterProperty<int>(properties, "normalState", "int",
            boost::bind(&SRayParam::get_normalState, &rp),
            boost::bind(&SRayParam::set_normalState, &rp, _1));

        const std::string ioRay(boost::str(boost::format(IO_RAY_FORMAT)%m_settings.id %(rp.contact)));
        m_context->AddContext(ioRay.c_str(), MakeContext(rp, properties));
    }

    TRelayParams& relays = m_settings.relays;
    TRelayParams::iterator relayIt1 = relays.begin(), relayIt2 = relays.end();
    for (; relayIt1 != relayIt2; ++relayIt1)
    {
        TCustomProperties properties;
        SRelayParam& rp = *relayIt1;

        RegisterProperty<int>(properties, "normalState", "int",
            boost::bind(&SRelayParam::get_normalState, &rp),
            boost::bind(&SRelayParam::set_normalState, &rp, _1));

        const std::string ioRelay(boost::str(boost::format(IO_RELAY_FORMAT)%m_settings.id %(rp.contact)));
        m_context->AddContext(ioRelay.c_str(), MakeContext(rp, properties));
    }
}

void CIoPanel::OnStopped()
{
    m_ioDevice.reset();
    try
    {
        m_eventFactory.reset();
    }
    catch (const CORBA::Exception &e)
    {
        _err_ << "CORBA exception occurred (" << e._name() << ") during m_eventFactory.reset()";
    }
   
}

void CIoPanel::OnFinalized()
{
    m_settings.enabled = false;
    SwitchEnabled();
}

namespace
{

class CAsyncActionHandlerWait : public ITV8::IAsyncActionHandler
{
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IAsyncActionHandler)
    ITV8_END_CONTRACT_MAP()

    virtual void Finished(ITV8::IContract *, ITV8::hresult_t)
    {
    }
};
}//namespace

void CIoPanel::SetRelayState(unsigned short contact, IPINT30::EIOState state)
{
    //TODO: save the state of the relay in a class field, and set it when calling Init()
    if (!m_ioDevice.get())
    {
        _err_ << ToString() << "Error: can't SetRelayState because m_ioDevice==0. " << std::endl;
        return;
    }

    ITV8::bool_t status = (IPINT30::EIO_Closed == state);

    //send a command to ipint to switch the relay.
    //wait for command to complete.
    //TODO: maybe forget about IAsyncActionHandler result and don't wait for anything?
    //TODO: understand the threads. in which context we start and in which we wait!
    //TODO: don't send something to a stopped m_ioDevice. Implement synchronization
    _log_ << ToString() << ". Contact: " << contact << ". SetRelayStatus (" << status << ")..." << std::endl;
    m_ioDevice->SetRelayStatus(new CAsyncActionHandlerWait(), contact, status);
    _log_ << ToString() << ". Contact: " << contact << ". SetRelayStatus done." << std::endl;
    return;
}

std::string CIoPanel::ToString() const
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
    str << "\\" << ".ioDevice:"<< m_settings.id;
    return str.str();
}

//Starts IIODevice in separate thread
void CIoPanel::DoStart()
{
    _log_ << ToString() <<  " CIoPanel::DoStart()"<<std::endl;
    if(m_ioDevice.get() == 0)
    {
        ITV8::GDRV::IDevice *device = 0;
        try
        {
            device = getDevice();
        }
        catch (const std::runtime_error& e)
        {
            _err_ << ToString() <<  " Exception: Couldn't getDevice(). msg=" <<e.what()<<std::endl;
            return;
        }

        _log_ << ToString() <<  " device->CreateIODevice()..."<<std::endl;

        m_ioDevice = IIODevicePtr(device->CreateIODevice(this, m_settings.id), 
            ipint_destroyer<ITV8::GDRV::IIODevice>());

        if(m_ioDevice.get()== 0)
        {
            std::string message = ITV8::get_last_error_message(device);
            _err_ << ToString() <<  "Exception: ioDevice was not created. msg=" 
                <<message<<std::endl;
            return;
        }
        else
        {
            _log_ << ToString() <<  " device->CreateIODevice()...succeed."<<std::endl;
        }
    }

    if (m_ioDevice && ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_ioDevice.get()))
        ApplyChanges(nullptr);
    m_ioDevice->Start();
    CChannel::DoStart();
}

//Stops IIODevice in separate thread
void CIoPanel::DoStop()
{
    IIODevicePtr ioDevice(m_ioDevice);

    if(0 != ioDevice)
    {
        ioDevice->Stop();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

// ITV8::GDRV::IDigitalInputHandler implementation
void CIoPanel::OnInputSignalLevelChanged(ITV8::GDRV::IIODevice* source, uint32_t inputNumber, const char* label,
                                         ITV8::timestamp_t timestamp, float level, float min, float max)
{
    if (!GetFlag(cfSignal))
    {
        CChannel::Notify(NMMSS::IPDS_SignalRestored);
        SetFlag(cfSignal, true);
    }
    if(m_rayCallbacks.end() != m_rayCallbacks.find(inputNumber))
    {
        _dbg_ << "OnInputSignalLevelChanged - " << inputNumber << ", label - " << label
            << ", level - " << level << ", min - " << min << ", max - " << max;
        const auto& callbackPair = m_rayCallbacks[inputNumber];
        callbackPair.first->OnInputSignalLevelChanged(inputNumber, label,
            IPINT30::ipintTimestampToIsoString(timestamp), level, min, max, callbackPair.second.c_str());
    }
    else
    {
        callbackAbsenceHandling(inputNumber);
    }
}

// ITV8::GDRV::IDigitalOutputHandler implementation
void CIoPanel::OnRelayStateChanged(ITV8::GDRV::IIODevice* source, uint32_t relayNumber, ITV8::bool_t state)
{
    if (!m_callback)
    {
        return;
    }

    TRelayParams& relays = m_settings.relays;
    if (relayNumber >= relays.size())
    {
        _err_ << "Incorrect relay number provided " << relayNumber;
        return;
    }
    SRelayParam& rp = relays[relayNumber];

    std::string accessPoint(m_objectContext);
    accessPoint.append("/").append(boost::str(boost::format(RELAY_CONTROL) % m_settings.id % rp.contact));

    _dbg_ << "Relay " << accessPoint << " status has been changed to " << state;
    m_callback->OnRelayStateChanged(accessPoint.c_str(), state);
}

// ITV8::GDRV::IIODeviceHandler implementation
void CIoPanel::Alarmed(ITV8::GDRV::IIODevice* source, ITV8::uint32_t rayNumber, ITV8::bool_t status)
{
    if (!GetFlag(cfSignal))
    {
        CChannel::Notify(NMMSS::IPDS_SignalRestored);
        SetFlag(cfSignal, true);
    }

    std::string now(boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time()));

    _log_ << ToString() << " Alarmed. ray = "<< rayNumber << " status=" << status << " iodevice=" << m_settings.id 
        << " time=" << now;

    NMMSS::EIOAlarmState state;
    try
    {
        bool normalState = getRayNormalState(rayNumber);
        state = (normalState == status) ? NMMSS::EIO_AlarmEnded : NMMSS::EIO_AlarmBegan;
    }
    catch(const std::exception &e)
    {
        _err_ << ToString() << " Alarmed: std::exception: " << e.what() << " (contact number '"<< rayNumber << "')";
        return;
    }

    auto alarmedRayIt = m_alarmedRays.find(rayNumber);
    if (state == NMMSS::EIO_AlarmEnded && 
        alarmedRayIt != m_alarmedRays.end() && alarmedRayIt->second == state)
        return; // ending phase is belated

    const auto& callbackPairIt = m_rayCallbacks.find(rayNumber);
    if(callbackPairIt == m_rayCallbacks.end())
    {
        callbackAbsenceHandling(rayNumber);
        return;
    }

    callbackPairIt->second.first->OnRayStateChanged(state, now.c_str(), callbackPairIt->second.second.c_str());
    alarmedRayIt->second = state;
}

void CIoPanel::callbackAbsenceHandling(ITV8::int32_t rayNumber)
{
    const auto rayIter = std::find_if(m_settings.rays.begin(), m_settings.rays.end(), [rayNumber](const SRayParam& rayVal)
    {
        return rayNumber == rayVal.contact;
    });

    if (rayIter == m_settings.rays.end())
    {
        _err_ << ToString() << " CIoPanel: unexpected ray contact number '" << rayNumber << "'";
        return;
    }

    _wrn_ << ToString() << " CIoPanel: Can't find ray grabber callback for contact number '" << rayNumber
        << "', so try to create a new";
    const auto now(boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time()));
    PrepareForTheRay(*rayIter, now);
}

//ITV8::GDRV::IJoystickStateNotify implementation
void CIoPanel::StateChanged(unsigned char* stateMask, int32_t maskSize, ITV8::GDRV::IAxisEnumerator* axisEnumerator)
{
    if (!m_callback)
    {
        return;
    }

    std::vector<NMMSS::JoystickAxisState> axes;
    if (axisEnumerator)
    {
        axisEnumerator->Reset();
        while (axisEnumerator->MoveNext())
        {
            NMMSS::JoystickAxisState axis;
            axis.name = axisEnumerator->GetName();
            axis.value = axisEnumerator->GetValue();
            axis.type = axisEnumerator->GetType() != nullptr
                ? axisEnumerator->GetType()
                : std::string();

            axes.push_back(axis);
            _dbg_ << ToString() << " IJoystickStateNotify::StateChanged '" << axis.name << "' = " << axis.value << " type = " << axis.type;
        }
        axisEnumerator->Destroy();
    }

    std::vector<int> pressedButtons;
    if (stateMask && maskSize > 0)
    {
        for (int i = 0; i < maskSize; ++i)
        {
            for (int j = 0; j < 8; ++j)
            {
                if (stateMask[i] & (1 << j))
                {
                    pressedButtons.push_back(i * 8 + j);
                    _dbg_ << ToString() << " IJoystickStateNotify:StateChanged pressed button: " << i * 8 + j;
                }
            }
        }
    }
    m_callback->OnJoystickStateChanged(m_accessPoint.c_str(), axes, pressedButtons);
}

void CIoPanel::Failed(ITV8::IContract* source, ITV8::hresult_t errorCode)
{
    std::string message = ITV8::get_last_error_message(source, errorCode);
    _log_ << ToString() << " Failed, err:" << message;

    CChannel::RaiseChannelStarted();
    CChannel::RaiseChannelStopped();

    SetFlag(cfSignal, false);
}

ITV8::Analytics::IDetectorEventRaiser* CIoPanel::BeginOccasionalEventRaising(ITV8::Analytics::IDetector* sender,
        const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)
{
    if (!sender || !m_eventFactory)
    {
        return nullptr;
    }

    return m_eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
}

void CIoPanel::OnEnabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        m_accessPoint));
}

void CIoPanel::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    m_resources.clear();
    
    for (TAlarmedRaysIterator it = m_alarmedRays.begin(); it != m_alarmedRays.end(); ++it)
    {
        if (it->second == NMMSS::EIO_AlarmBegan)
        {
            this->Alarmed(0, it->first, getRayNormalState(it->first));
        }
    }
    
    m_alarmedRays.clear();
    m_rayCallbacks.clear();

    const auto now(boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time()));
    for (const auto& ap : m_accessPoints)
    {
        Notify(ap, NMMSS::IPDS_SignalLost, now);
    }

    m_accessPoints.clear();
    this->SetNotifier(nullptr);
}

void CIoPanel::SwitchEnabled()
{
    SetEnabled(m_settings.enabled);
}

void CIoPanel::changeRelayNormalState(const std::string& accessPoint, int normalState)
{
    try
    {
        NCorbaHelpers::PContainerNamed cont = m_container;
        if(!cont)
            return;

        using namespace Notification;
        CORBA::Object_var object = cont->GetRootNC()->resolve_str(accessPoint.c_str());
        StateControl_var sc = StateControl::_narrow(object);
        if(!CORBA::is_nil(sc))
            sc->SetState("", StateControl::DEFAULT_STATE, normalState != 0 ? StateControl::ON : StateControl::OFF);
    }
    catch(const CORBA::Exception &e)
    {
        _inf_ << "Could not resolve Host process object (" << accessPoint << "), CORBA exception: " << e._name();
    }
}

std::string CIoPanel::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "ioDevice:" << m_settings.id;
    return stream.str();
}

void CIoPanel::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (!handler)
    {
        _log_ << ToString() << " IDynamicParametersHandler doesn't exist";
        return;
    }

    // If source is not active than we should not do any call to driver.
    if (!GetFlag(cfStarted))
    {
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    if (!m_ioDevice)
    {
        _log_ << ToString() << " IODevice doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_ioDevice.get());
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_ioDevice.get(), ITV8::EUnsupportedCommand);
}

void CIoPanel::initializeDetectorEventFactory(NCorbaHelpers::PContainerNamed container, const std::string& originId)
{
    if (m_eventFactory)
    {
        return;
    }

    try
    {
        const std::string io(boost::str(boost::format(IO_FORMAT)%m_settings.id));
        _dbg_ << ToString() << " initializeDetectorEventFactory: sourceAccessPoint=" << m_objectContext
            << " sourceOrigin=" << (originId.empty() ? "NULL" : originId)
            << " eventChannelName=" << m_eventChannel
            << " supplierIdSuffix=" << io;
        NMMSS::PDetectorEventFactory factory(
            NMMSS::CreateDetectorEventFactory(m_objectContext.c_str(), originId.c_str(),
            container.Get(), m_eventChannel.c_str(), io.c_str()));

        m_eventFactory = ITVSDKUTILES::CreateEventFactory(GET_THIS_LOGGER_PTR, factory, m_objectContext.c_str());
    }
    catch (const CORBA::Exception& e)
    {
        _err_ << ToString() << " initializeDetectorEventFactory: CORBA::Exception: " << e;
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() <<" initializeDetectorEventFactory: std::exception: " << e.what();
    }
}

void CIoPanel::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    Prepare();

    if (GetFlag(cfEnvironmentReady) && m_ioDevice)
    {
        auto wrappedHandler = WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler);

        auto asyncAdjuster = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_ioDevice.get());
        if (!asyncAdjuster)
        {
            _wrn_ << ToString() << " couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped.";
            wrappedHandler->Finished(m_ioDevice.get(), ITV8::EUnsupportedCommand);
            return;
        }

        // Setting blocking configuration.
        SetValue(asyncAdjuster, "bool", ITV8_PROP_BLOCKCONFIGURATION, 
            m_blockingConfiguration ? "true" : "false");

        SetValues(asyncAdjuster, m_settings.publicParams);
        ApplySettings(asyncAdjuster, wrappedHandler);
    }
}

void CIoPanel::ApplyMetaChanges(const NStructuredConfig::TCustomParameters& meta)
{
}

bool CIoPanel::getRayNormalState(int contact) const
{
    const TRayParams& rays = m_settings.rays;
    TRayParams::const_iterator rayIt1 = rays.begin(), rayIt2 = rays.end();
    for(; rayIt1 != rayIt2; ++rayIt1)
    {
        const SRayParam& rp = *rayIt1;
        if (rp.contact == contact)
        {
            return (rp.normalState != 0);
        }
    }

    throw std::runtime_error("Could not found this contact in ray settings.");
}

IParamContextIterator* CIoPanel::GetParamIterator()
{
    return m_context.get();
}

void CIoPanel::Prepare()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
    {
        _wrn_ << ToString() << " CIoPanel::Prepare: The container is already dead";
        return;
    }

    const auto now(boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time()));
    for (const auto& rayParam : m_settings.rays)
    {
        PrepareForTheRay(rayParam, now);
    }

    for(auto& relayParam : m_settings.relays)
    {
        const std::string endpointName = boost::str(boost::format(RELAY_CONTROL)% m_settings.id % relayParam.contact);
        std::string accessPoint(m_objectContext);
        accessPoint.append("/").append(endpointName);

        TAccessPointIterator ap = m_accessPoints.find(accessPoint);
        if(relayParam.enabled)
        {
            // StateControl tries to pass to its inactive state when it is created
            // but, unfortunately, the device is not connected at the moment and calls to IIOPanel are senseless.
            // I think this is not a reason to be upset, because the device will pass to the necessary state
            // by forcing CIoPanel::Init()
            // Of course, in this case there is still !=0 probability that the user will click the switch and these // transitions will be forgotten
            // transitions will be forgotten, so:
            //TODO: consider executing this code after CIoDevice::Init()

            if (m_accessPoints.end() == ap) // the relay turned on
            {
                IPINT30::CRelayStateControl* pStateControl = new IPINT30::CRelayStateControl(accessPoint.c_str(),
                    m_callback ? m_callback->GetConnector() : NCommonNotification::PEventSupplier(),
                    relayParam.normalState, relayParam.contact, this, GET_LOGGER_PTR);

                m_resources.insert(
                    std::make_pair(
                        accessPoint,
                        boost::shared_ptr<NCorbaHelpers::IResource>( NCorbaHelpers::ActivateServant(cont.Get(), pStateControl, endpointName.c_str()) )
                    )
                );

                m_accessPoints.insert(accessPoint);
                Notify(accessPoint, NMMSS::IPDS_SignalRestored, now);
            }

            if (relayParam.normalStateChanged)
            {
                _log_ << "Relay normal state changed in " << accessPoint;

                changeRelayNormalState(accessPoint, relayParam.normalState);
                relayParam.normalStateChanged = false;
            }
        }
        else if (!(relayParam.enabled || (m_accessPoints.end() == ap))) // the relay turned off
        {
            m_resources.erase(accessPoint);
            m_accessPoints.erase(ap);

            Notify(accessPoint, NMMSS::IPDS_SignalLost, now);
        }
    }

    initializeDetectorEventFactory(cont, (m_rayCallbacks.empty() ? std::string() : m_rayCallbacks.begin()->second.second));
}

void CIoPanel::PrepareForTheRay(const SRayParam& rayParam, const std::string& isoTime)
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
    {
        _wrn_ << ToString() << " CIoPanel::Prepare: The container is already dead";
        return;
    }
    const auto rayContact = rayParam.contact;
    const auto connectorRay(boost::str(boost::format(RAY_ID_FORMAT) % m_settings.id %rayContact));
    std::string accessPoint(m_objectContext);
    accessPoint.append("/").append(connectorRay);

    TAccessPointIterator ap = m_accessPoints.find(accessPoint);
    if (rayParam.enabled && (m_accessPoints.end() == ap)) // the ray turned on
    {
        boost::shared_ptr<NMMSS::IGrabberCallback> callback;
        try
        {
            callback.reset(NMMSS::CreateEventNotificationGrabberCallback(accessPoint.c_str(),
                cont.Get(), m_eventChannel.c_str(), connectorRay.c_str()));
        }
        catch (const CORBA::Exception &e)
        {
            _err_ << ToString() << " An error occurred during creating EventNotificationGrabberCallback,"
                " a CORBA error has occured : " << e._name();
            return;
        }
        catch (const std::exception &e)
        {
            _err_ << ToString() << " An error occurred during creating EventNotificationGrabberCallback,"
                " std::exception: " << e.what();
            return;
        }
        catch (...)
        {
            _err_ << ToString() << " An error occurred during creating EventNotificationGrabberCallback";
            return;
        }

        std::ostringstream componentId;
        componentId << m_objectContext << "/IoDeviceChannel." << m_settings.id;

        m_rayCallbacks.emplace(rayContact, std::make_pair(callback, componentId.str()));

        m_accessPoints.insert(accessPoint);
        Notify(accessPoint, NMMSS::IPDS_SignalRestored, isoTime);
    }
    else if (!(rayParam.enabled || (m_accessPoints.end() == ap))) // the ray turned off
    {
        if (m_alarmedRays.end() != m_alarmedRays.find(rayContact))
        {
            if (m_alarmedRays[rayContact] == NMMSS::EIO_AlarmBegan) // if the event has started but has not yet ended
            {
                this->Alarmed(0, rayContact, getRayNormalState(rayContact));
            }

            m_alarmedRays.erase(rayContact);
        }

        m_rayCallbacks.erase(rayContact);
        m_accessPoints.erase(ap);

        Notify(accessPoint, NMMSS::IPDS_SignalLost, isoTime);
    }
}

void CIoPanel::Notify(const std::string& accessPoint, NMMSS::EIpDeviceState state, const std::string& isoTime)
{
    if (!m_callback.get())
    {
        return;
    }

    using namespace boost::posix_time;
    const std::string now = isoTime.empty()
        ? to_iso_string(microsec_clock::universal_time())
        : isoTime;
    m_callback->OnNotify(accessPoint.c_str(), state, now.c_str(), "");
}

}//namespace IPINT30
