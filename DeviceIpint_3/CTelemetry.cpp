#include <ItvSdkWrapper.h>
#include "CTelemetry.h"
#include "CIpInt30.h"
#include "Utility.h"
#include "Observer.h"
#include "Notify.h"
#include "FakeDeviceManager.h"
#include <../Primitives/CorbaHelpers/Unicode.h>
#include <CorbaHelpers/ResolveServant.h>
#include <Logging/Log3.h>

#include "../MMClient/MMClient.h"
#include "DeviceSettings.h"
#include "ItvDeviceSdk/include/infoWriters.h"

#include <boost/make_shared.hpp>
#include <boost/format.hpp>

#include <InfraServer_IDL/LicenseChecker.h>
#include <InfraServer_IDL/LicenseServiceC.h>
#include <ctime>

namespace
{
const char* const TELEMETRY_CONTROL = "TelemetryControl.%d";
const char* const STATE_CONTROL_TELEMETRY = "StateControl.telemetry:%d";
const char* const TELEMETRY_FORMAT = "Telemetry.%d";
const char* const TAG_ANG_TRACK = "Observer.%d";

const int TIMEOUT = 10000; // 10 seconds.

namespace continuousOperations
{
const char MOVE = 0;
const char ZOOM = 1;
const char FOCUS = 2;
const char IRIS = 3;
};

const char META_ENABLE_MULTIPLE_CONTROL[] = "enableMultipleControl";
const char META_PATROL_SPEED[] = "partolSpeed";
const char META_ENABLE_TELEMETRY2[] = "enableTelemetry2";

}

namespace IPINT30
{

class CPatrolContext : public std::enable_shared_from_this <CPatrolContext>
                     , public NLogging::WithLogger
{
public:
    CPatrolContext(DECLARE_LOGGER_ARG, IPINT30::CTelemetry& holder, int patrolTimeout)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , m_rHolder(holder)
        , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
        , m_currentPos(0)
        , m_patrolSpeed(0)
        , m_patrolTimeout(patrolTimeout)
        , m_isTimerActive(false)
    {
    }

    ~CPatrolContext()
    {
        boost::mutex::scoped_lock lock(m_lock);
        Stop(lock);
    }

    void SwitchPatrol(bool enabled)
    {
        boost::mutex::scoped_lock lock(m_lock);
        Stop(lock);

        if (!m_isTimerActive && enabled && m_patrolTimeout > 0)
        {
            std::weak_ptr<CPatrolContext> weak(shared_from_this());
            m_patrolTimer.reset(new NCorbaHelpers::CTimer(m_reactor->GetIO()));
            m_patrolTimer->Start(boost::posix_time::seconds(m_patrolTimeout),
                [weak]() -> NCorbaHelpers::CTimerState
                {
                    auto strong = weak.lock();

                    if (!strong)
                        return NCorbaHelpers::CTimerState::TS_Stop;

                    strong->doPatrol();
                    return NCorbaHelpers::CTimerState::TS_Continue;
                });

            m_isTimerActive = true;
            _inff_("{} Patrol started with patrolTimeout {} seconds", m_rHolder.ToString(), m_patrolTimeout);
        }
    }

    void SetSpeed(int speed)
    {
        m_patrolSpeed = speed;
    }

    int GetSpeed() const
    {
        return m_patrolSpeed;
    }

    // Sets the timeout (sec) between switching presets in patrol mode
    void SetPatrolTimeout(int value)
    {
        m_patrolTimeout = value;
    }

    // Gets the timeout (sec) between switching presets in patrol mode
    int GetPatrolTimeout() const
    {
        return m_patrolTimeout;
    }

private:
    void doPatrol()
    {
        NCorbaHelpers::CAutoPtr<NMMSS::IPresetIterator> pIter(m_rHolder.GetPresetsInfo());
        if (0 != pIter.Get())
        {
            NCorbaHelpers::CAutoPtr<NMMSS::IPreset> pPreset(pIter->GetPresetByHint(m_currentPos));
            if (!pPreset.Get())
            {
                m_currentPos = 0;
                pPreset = pIter->Next();
            }
            if (pPreset.Get())
            {
                m_currentPos = pPreset->Position();
                m_rHolder.GoPreset(Equipment::Telemetry::SERVER_SESSION_ID, m_currentPos++, m_patrolSpeed / 100.0f);
            }
        }
    }

    void Stop(boost::mutex::scoped_lock& lock)
    {
        if (m_isTimerActive)
        {
            m_patrolTimer.reset();
            m_isTimerActive = false;
            _inff_("{} Patrol stopped", m_rHolder.ToString());
        }
    }

private:
    IPINT30::CTelemetry& m_rHolder;
    boost::mutex m_lock;
    NCorbaHelpers::PReactor m_reactor;
    unsigned long m_currentPos;
    int m_patrolSpeed;
    int m_patrolTimeout;
    NCorbaHelpers::PTimer m_patrolTimer;
    bool m_isTimerActive;
};

typedef boost::function<void()> releaseHandler_t;

class TelemetryConnectionKeeper : public boost::enable_shared_from_this<TelemetryConnectionKeeper>
{
public:
    static TelemetryConnectionKeeperWP Create(const NMMSS::UserSessionInformation& userSession, boost::asio::io_service& service,
        long sessionId, long priority, releaseHandler_t releaseHandler, time_t& expirationTime)
    {
        boost::shared_ptr<TelemetryConnectionKeeper> keeper(
            new TelemetryConnectionKeeper(userSession, service, sessionId, priority, releaseHandler));
        expirationTime = keeper->ScheduleExpiration();
        return TelemetryConnectionKeeperWP(keeper);
    }

    NMMSS::UserSessionInformation GetUserSession() const
    {
        return m_userSession;
    }

    long GetSessionId() const
    {
        return m_sessionId;
    }

    long GetPriority() const
    {
        return m_priority;
    }

    time_t ScheduleExpiration()
    {
        if (m_released.load())
            return boost::posix_time::to_time_t(m_timer.expires_at());

        static boost::posix_time::seconds ALIVE_TIME(10);
        m_timer.expires_from_now(ALIVE_TIME);
        auto sharedThis = shared_from_this();
        m_timer.async_wait([sharedThis](const boost::system::error_code& ec)
            {
                if (!ec)
                {
                    if (!sharedThis->m_released.exchange(true))
                        sharedThis->m_releaseHandler();

                }
            });

        return boost::posix_time::to_time_t(m_timer.expires_at());
    }

    time_t GetSessionExpirationTime()
    {
        return boost::posix_time::to_time_t(m_timer.expires_at());
    }

    void Release()
    {
        if (m_released.exchange(true))
            return;
        boost::system::error_code ec;
        m_timer.cancel(ec);
        m_releaseHandler();
    }

private:
    TelemetryConnectionKeeper(const NMMSS::UserSessionInformation& userSession, boost::asio::io_service& service,
        long sessionId, long priority, releaseHandler_t releaseHandler)
        : m_userSession(userSession)
        , m_timer(service)
        , m_sessionId(sessionId)
        , m_priority(priority)
        , m_releaseHandler(releaseHandler)
        , m_released()
    {
    }

private:
    const NMMSS::UserSessionInformation m_userSession;
    boost::asio::deadline_timer m_timer;
    const long m_sessionId;
    const long m_priority;
    releaseHandler_t m_releaseHandler;
    std::atomic_bool m_released;
};

namespace
{
bool isSessionAvailable(IPINT30::TelemetryConnectionKeeperSP keeper, long priority)
{
    return !keeper || keeper->GetPriority() > priority;
}

bool isSessionAvailableWithMultipleControl(IPINT30::TelemetryConnectionKeeperSP keeper, bool multipleControl, long priority)
{
    return multipleControl && keeper && keeper->GetPriority() == priority;
}


class ToursEventHandler : public ITV8::GDRV::IToursEventHandler
{
public:
    ToursEventHandler(CTelemetry* parent)
        : m_parent(parent)
    {}

    virtual void Updated(ITV8::GDRV::INamedTourController*)
    {
        Json::Value data;
        data["event"] = "tour_list_changed";
        m_parent->NotifyTourState(std::move(data));
    }
    virtual void StateChanged(ITV8::GDRV::INamedTourController*, const char* name, ITV8::GDRV::TourState state)
    {
        Json::Value data;
        data["event"] = "tour_state_changed";
        data["name"] = std::string(name);
        data["status"] = (int)state;
        m_parent->NotifyTourState(std::move(data));
    }
    virtual void Failed(IContract* pSource, ITV8::hresult_t error)
    {
    }
private:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IToursEventHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IToursEventHandler)
    ITV8_END_CONTRACT_MAP()

    CTelemetry* m_parent;
};

}

CTelemetry::CTelemetry(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
    const STelemetryParam& telSettings, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
    const char* objectContext, NCorbaHelpers::IContainerNamed* container)
    : WithLogger(GET_LOGGER_PTR)
    , CChannelHandlerImpl(GET_LOGGER_PTR, dynExec, parent, 0)
    , m_settings(telSettings)
    , m_recheckPresetsNeeded(true)
    , m_enableMultipleControl(true)
    , m_patrolEnabled(telSettings.defaultEnablePatrol!=0)
    , m_callback(callback)
    , m_objectContext(objectContext)
    , m_container(container)
    , m_context(new CParamContext)
    , m_lastSessionId(0)
    , m_patrolContext(std::make_shared<CPatrolContext>(GET_LOGGER_PTR, *this, m_settings.patrolTimeout))
    , m_accessPoint(boost::str(boost::format(TELEMETRY_CONTROL) % telSettings.id))
    , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
    , m_homePresetTimer(m_reactor->GetIO())
    , m_homePresetIndex(0)
    , m_homePresetTimeout(0)
    , m_enableTelemetry2(false)
    , m_homePresetSpeed(std::max<ITV8::uint32_t>(1, static_cast<ITV8::uint32_t>(GetIntParamSafe(ITV8_PROP_PRESET_SPEED))))
    , m_getToursInProcess(false)
{
    NCorbaHelpers::PContainerNamed c(m_container);

    const std::string telemetry(boost::str(boost::format(TELEMETRY_FORMAT)%telSettings.id));
    
    if (0 != c.Get())
        m_observerContainer = c->CreateContainer();
    
    m_enableMultipleControl = GetCustomParameter("enableMultipleControl", m_settings.metaParams, true);
    m_homePresetIndex = GetCustomParameter("homePreset", m_settings.metaParams, 0u);
    m_homePresetTimeout = GetCustomParameter("homePresetTimeout", m_settings.metaParams, 0u);
    m_enableTelemetry2 = GetCustomParameter(META_ENABLE_TELEMETRY2, m_settings.metaParams, false);

    TCustomProperties properties;

    RegisterProperty<int>(properties, "defaultEnablePatrol", "int",
        boost::bind(&CTelemetry::GetDefaultEnablePatrol, this),
        boost::bind(&CTelemetry::SetDefaultEnablePatrol, this, _1));

    RegisterProperty<int>(properties, META_PATROL_SPEED, "int",
        [this]() -> int { return m_patrolContext->GetSpeed(); },
        [this](int value) { m_patrolContext->SetSpeed(value); });

    RegisterProperty<int>(properties, "patrolTimeout", "int",
        [this]() -> int { return m_patrolContext->GetPatrolTimeout(); },
        [this](int value)
        {
            m_patrolContext->SetPatrolTimeout(value);
            ReenableControl();
        });

    RegisterProperty<int>(properties, "savePresetsOnDevice", "int",
        [this]() -> int { return this->m_settings.savePresetsOnDevice; },
        [this](int value) { this->m_settings.savePresetsOnDevice = value; });

    RegisterProperty<bool>(properties, META_ENABLE_MULTIPLE_CONTROL, "bool",
        [this]() -> bool { return this->m_enableMultipleControl; },
        [this](bool value) { this->m_enableMultipleControl = value; });

    RegisterProperty<uint32_t>(properties, "homePreset", "int",
        [this]() -> uint32_t { return this->m_homePresetIndex; },
        [this](uint32_t value) { this->m_homePresetIndex = value; });

    RegisterProperty<uint32_t>(properties, "homePresetTimeout", "int",
        [this]() -> uint32_t { return this->m_homePresetTimeout; },
        [this](uint32_t value) { this->m_homePresetTimeout = value; });

    RegisterProperty<bool>(properties, META_ENABLE_TELEMETRY2, "bool",
        [this]() -> bool { return this->m_enableTelemetry2; },
        [this](bool value) { this->m_enableTelemetry2 = value; });

    m_context->AddContext(telemetry.c_str(), MakeContext(m_settings, properties));

    initializeMetaParams();

    //Для телеметрии нет получателя данных. Взводим флаг по умолчанию.
    SetFlag(cfSinkConnected, true);
}

ITV8::GDRV::ITelemetry2* CTelemetry::castToTelemetry2()
{
    if (m_enableTelemetry2)
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        return ITV8::contract_cast<ITV8::GDRV::ITelemetry2>(adj);
    }
    return nullptr;
}

void CTelemetry::initializeMetaParams()
{
    initMetaParameter(m_settings.metaParams, META_ENABLE_MULTIPLE_CONTROL,
        [this] (const std::string& value)
        {
            this->m_enableMultipleControl = value == "1";
        });

    initMetaParameter(m_settings.metaParams, META_PATROL_SPEED,
        [this](const std::string& value)
        {
            m_patrolContext->SetSpeed(boost::lexical_cast<int>(value));
        });
}

void CTelemetry::initMetaParameter(const NStructuredConfig::TCustomParameters& metaParams, const char* name, std::function<void(const std::string& value)> initializer)
{
    auto param = std::find_if(metaParams.begin(), metaParams.end(), SCustomParamFindName(name));
    if (param != metaParams.end())
    {
        try
        {
            initializer(param->ValueUtf8());
        }
        catch (const std::exception& ex)
        {
            _err_ << "std::exception: can't initialize meta parameter '" << name << "' - " << ex.what();
        }
    }
}

CTelemetry::~CTelemetry()
{
    releaseActiveSession();
}

// Gets the flag indicates whether the patrol should be enabled with telemetry enabling.
int CTelemetry::GetDefaultEnablePatrol()  const
{
    return m_settings.defaultEnablePatrol;
}

// Sets the flag indicates whether the patrol should be enabled with telemetry enabling.
void CTelemetry::SetDefaultEnablePatrol(int newVal)
{
    if(m_settings.defaultEnablePatrol != newVal)
    {
        m_settings.defaultEnablePatrol = newVal;
        ReenableControl();
    }
}
void CTelemetry::ReenableControl()
{
    if(m_telemetryChannel.get() != 0)
    {
        // After update configuration the channel will be enabled (if it's necessary) with new parameter(s)
        SetEnabled(false);
    }
}
void CTelemetry::OnEnabled()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    boost::mutex::scoped_lock lock(m_switchMutex);

    std::string stateControlAccessPoint(boost::str(boost::format(STATE_CONTROL_TELEMETRY)%m_settings.id));

    std::string nsName(m_objectContext + "/" + m_accessPoint);

    NCommonNotification::PEventSupplier supplier = m_callback
        ? m_callback->GetConnector()
        : NCommonNotification::PEventSupplier();
    PortableServer::Servant telemetryServant = NMMSS::CreateTelemetryEndpoint(GET_LOGGER_PTR,
        this, supplier, nsName.c_str());

    m_telemetryServant.reset(NCorbaHelpers::ActivateServant(cont.Get(), telemetryServant,
        m_accessPoint.c_str()));

    if (m_settings.observer.trackers.size() > 0)
    {
        try
        {
            m_observerServant.reset(NCorbaHelpers::ActivateServant(cont.Get(),
                IPINT30::CreateObserverServant(GET_LOGGER_PTR, NCorbaHelpers::WPContainerTrans(m_observerContainer)
                , m_settings.observer, nsName.c_str(), this, supplier)
                , boost::str(boost::format(TAG_ANG_TRACK) % m_settings.id).c_str()));
            try
            {
                NLicenseService::PLicenseChecker pLC = NLicenseService::GetLicenseChecker(cont.Get());
                m_lease.reset(pLC->Acquire("TagAndTrackPro", std::string(m_objectContext + "/" + m_accessPoint)));
            }
            catch (const InfraServer::LicenseService::InvalidArgument& /*e*/)
            {
                _wrn_ << "License has no position for TagAndTrackPro. Functionality wasn't released before 4.0. Wrong key file";
            }
            catch (const InfraServer::LicenseService::LicenseFailed& /*e*/)
            {
                _err_ << "No free leases for TagAndTrackPro";
                throw;
            }
        }
        catch (std::runtime_error&)
        {
        }
    }

    m_patrolEnabled = m_settings.defaultEnablePatrol != 0;
    m_stateControlServantName = m_objectContext + "/" + stateControlAccessPoint;
    PortableServer::Servant stateServant = NMMSS::CreateTelemetryStateControl(GET_LOGGER_PTR, 
        m_stateControlServantName.c_str(), supplier, this, m_patrolEnabled);
    m_stateServant.reset(NCorbaHelpers::ActivateServant(cont.Get(), stateServant, 
        stateControlAccessPoint.c_str()));

    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        std::string(m_objectContext + "/" + m_accessPoint)));
}

void CTelemetry::OnDisabled()
{
    releaseActiveSession();
    boost::mutex::scoped_lock lock(m_switchMutex);
    try
    {
        m_stateServant.reset();
        m_telemetryServant.reset();
        m_observerServant.reset();
        m_lease.reset();
    }
    catch(const CORBA::Exception&)
    {
    }
    
    this->SetNotifier(0);
}

void CTelemetry::OnFinalized()
{
    SetEnabled(false);
}

bool CTelemetry::isSessionActive(long sessionId)
{
    if (sessionId == Equipment::Telemetry::SERVER_SESSION_ID)
        return true;
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();
    return keeper && keeper->GetSessionId() == sessionId;
}

bool CTelemetry::IsSessionAvailable(long priority, NMMSS::UserSessionInformation& blockingUserInformation)
{
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();
    auto isAvailable = isSessionAvailable(keeper, priority) ||
        isSessionAvailableWithMultipleControl(keeper, m_enableMultipleControl, priority);

    if (keeper)
    {
        blockingUserInformation = keeper->GetUserSession();
    }

    return isAvailable;
}

long CTelemetry::AcquireSessionId(const NMMSS::UserSessionInformation& userSession,
    long priority, long& result, NMMSS::UserSessionInformation& blockingUserInformation, time_t& expirationTime)
{
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();

    if(keeper)
        expirationTime = keeper->GetSessionExpirationTime();

    if (isSessionAvailable(keeper, priority))
    {
        ++m_lastSessionId;
        if (m_lastSessionId == Equipment::Telemetry::SERVER_SESSION_ID)
        {
            ++m_lastSessionId;
        }
        m_connectionKeeper = TelemetryConnectionKeeper::Create(userSession, m_reactor->GetIO(),
            m_lastSessionId, priority, boost::bind(&CTelemetry::sessionReleaseHandler, this), expirationTime);
        result = m_lastSessionId;
        return Equipment::Telemetry::ENotError;
    }
    if (isSessionAvailableWithMultipleControl(keeper, m_enableMultipleControl, priority))
    {
        result = m_lastSessionId;
        return Equipment::Telemetry::ENotError;
    }
    blockingUserInformation = keeper->GetUserSession();
    return Equipment::Telemetry::ESessionUnavailable;
}

long CTelemetry::ReleaseSessionId(long sessionId)
{
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();
    if (keeper && keeper->GetSessionId() == sessionId)
    {
        m_connectionKeeper.reset();
        lock.unlock();
        keeper->Release();

        m_patrolEnabled = m_settings.defaultEnablePatrol != 0;

        NCorbaHelpers::PContainerNamed cont = m_container;
        if (!cont)
            _err_ << ToString() << "Configuration is unavailable";
        else
        {
            namespace ntf = Notification;
            ntf::StateControl_var etp = NCorbaHelpers::ResolveServant<ntf::StateControl>(cont.Get(), m_stateControlServantName, TIMEOUT);
            if (!CORBA::is_nil(etp))
                etp->SetState("", ntf::StateControl::DEFAULT_STATE,
                    m_patrolEnabled 
                    ? ntf::StateControl::ON
                    : ntf::StateControl::OFF);
            else
                _err_ << ToString() << " Can't set default patrol state to "<< m_patrolEnabled << ". Can't resolve reference to " << m_stateControlServantName;
        }

        m_patrolContext->SwitchPatrol(m_patrolEnabled);

        return Equipment::Telemetry::ENotError;
    }
    return Equipment::Telemetry::ESessionUnavailable;
}

bool CTelemetry::KeepAlive(long sessionId)
{
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();
    if (keeper && keeper->GetSessionId() == sessionId)
    {
        keeper->ScheduleExpiration();
        return true;
    }
    return false;
}

time_t CTelemetry::GetSessionExpirationTime(long sessionId)
{
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();
    if (keeper && keeper->GetSessionId() == sessionId)
    {
        return keeper->GetSessionExpirationTime();
    }
    return 0;
}

void CTelemetry::releaseActiveSession()
{
    boost::mutex::scoped_lock lock(m_connectionKeeperGuard);
    auto keeper = m_connectionKeeper.lock();
    if (keeper)
    {
        keeper->Release();
    }
}

void CTelemetry::sessionReleaseHandler()
{
    ITelemetryPtr telemetryPtr = m_telemetryChannel;
    if (!telemetryPtr)
    {
        return;
    }

    ITV8::hresult_t res = 0;

    if (m_continuousOperationFlags.test(continuousOperations::MOVE))
    {
        res = telemetryPtr->MoveContinuous(0, 0);
        if (ITV8::ENotError != res)
        {
            _err_ << ToString() << " MoveContinuous returns " << ITV8::get_last_error_message(res) << std::endl;
        }
    }
    
    if (m_continuousOperationFlags.test(continuousOperations::ZOOM))
    {
        res = telemetryPtr->ZoomContinuous(0);
        if (ITV8::ENotError != res)
        {
            _err_ << ToString() << " ZoomContinuous returns " << ITV8::get_last_error_message(res) << std::endl;
        }
    }

    if (m_continuousOperationFlags.test(continuousOperations::FOCUS))
    {
        res = telemetryPtr->FocusContinuous(0);
        if (ITV8::ENotError != res)
        {
            _err_ << ToString() << " FocusContinuous returns " << ITV8::get_last_error_message(res) << std::endl;
        }
    }

    if (m_continuousOperationFlags.test(continuousOperations::IRIS))
    {
        res = telemetryPtr->IrisContinuous(0);
        if (ITV8::ENotError != res)
        {
            _err_ << ToString() << " IrisContinuous returns " << ITV8::get_last_error_message(res) << std::endl;
        }
    }

    if (m_homePresetTimeout)
    {
        boost::system::error_code ec;
        m_homePresetTimer.cancel(ec);
        res = GoPreset(Equipment::Telemetry::SERVER_SESSION_ID, m_homePresetIndex, m_homePresetSpeed);
        if (ITV8::ENotError != res)
        {
            _err_ << ToString() << " HomePreset returns " << ITV8::get_last_error_message(res) << std::endl;
        }
    }
}

long CTelemetry::Move(long sessionId, NMMSS::ERangeFlag flag, long panSpeedOrStep, long tiltSpeedOrStep)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    ITV8::hresult_t res = ITV8::ENotError;
    //На самом деле я так понимаю, сюда передаются не только скорости но и шаги
    _inf_ << ToString() << "Move flag(" << (int)flag << ") pan(" << panSpeedOrStep << ") tilt(" << tiltSpeedOrStep << ")"<<std::endl;
    
    if (NMMSS::ECONTINUOUS == flag)
    {
        m_continuousOperationFlags.set(continuousOperations::MOVE, panSpeedOrStep || tiltSpeedOrStep);
        res = m_telemetryChannel->MoveContinuous(panSpeedOrStep, tiltSpeedOrStep);
    }
    else if (NMMSS::EABSOLUTE == flag)
    {
        //т.к. NGP не озабочено вопросом передачи скорости - берём из настроек IPINT
        int panPos = panSpeedOrStep;
        int tiltPos = tiltSpeedOrStep;
        res = m_telemetryChannel->MoveDirect(panPos, tiltPos, GetIntParamSafe(ITV8_PROP_DIRECT_MOVE_SPEED));
    }
    else if (NMMSS::ERELATIVE == flag)
    {
        int panStep = panSpeedOrStep;
        int tiltStep = tiltSpeedOrStep;
        res = m_telemetryChannel->MoveDiscrete(panStep, GetIntParamSafe(ITV8_PROP_PAN_SPEED), 
            tiltStep, GetIntParamSafe(ITV8_PROP_TILT_SPEED));
    }

    if (ITV8::ENotError != res && ITV8::EAlready != res)
    {
        _err_ << ToString() << " Move returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    if (NMMSS::ECONTINUOUS == flag)
    {
        if (panSpeedOrStep == 0 && tiltSpeedOrStep == 0)
            scheduleReturnToHomePreset(); // Do not return untill continuos move stopped.
        else
            m_homePresetTimer.cancel();
    }
    else
        scheduleReturnToHomePreset();

    return Equipment::Telemetry::ENotError;
}

long CTelemetry::Zoom(long sessionId, NMMSS::ERangeFlag flag, long speedOrStep)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    ITV8::hresult_t res = ITV8::ENotError;
    _inf_ << ToString() << "Zoom flag(" << (int)flag << ") speed(" << speedOrStep << ")"<<std::endl;
    
    if (NMMSS::ECONTINUOUS == flag)
    {
        m_continuousOperationFlags.set(continuousOperations::ZOOM, speedOrStep);
        res = m_telemetryChannel->ZoomContinuous(speedOrStep);
    }
    else if (NMMSS::EABSOLUTE == flag)
    {
        int zoomPos = speedOrStep;
        res = m_telemetryChannel->ZoomDirect(zoomPos, GetIntParamSafe(ITV8_PROP_DIRECT_ZOOM_SPEED));
    }
    else if (NMMSS::ERELATIVE == flag)
    {
        int zoomStep = speedOrStep;
        res = m_telemetryChannel->ZoomDiscrete(zoomStep, GetIntParamSafe(ITV8_PROP_ZOOMSPEED));
    }

    if (ITV8::ENotError != res)
    {
        _err_ << ToString() << " Zoom returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    if (NMMSS::ECONTINUOUS == flag)
    {
        if (speedOrStep == 0)
            scheduleReturnToHomePreset(); // Do not return untill continuos zoom stopped.
        else
            m_homePresetTimer.cancel();
    }
    else
        scheduleReturnToHomePreset();

    return Equipment::Telemetry::ENotError;
}

long CTelemetry::Focus(long sessionId, NMMSS::ERangeFlag flag, long speedOrStep)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    ITV8::hresult_t res = ITV8::ENotError;
    _inf_ << ToString() << "Focus flag(" << (int)flag << ") speed(" << speedOrStep << ")"<<std::endl;
    
    if (NMMSS::ECONTINUOUS == flag)
    {
        m_continuousOperationFlags.set(continuousOperations::FOCUS, speedOrStep);
        res = m_telemetryChannel->FocusContinuous(speedOrStep);
    }
    else if (NMMSS::EABSOLUTE == flag)
    {
        int focusPos = speedOrStep;
        res = m_telemetryChannel->FocusDirect(focusPos, GetIntParamSafe(ITV8_PROP_DIRECT_FOCUS_SPEED));
    }
    else if (NMMSS::ERELATIVE == flag)
    {
        int focusStep = speedOrStep;
        res = m_telemetryChannel->FocusDiscrete(focusStep, GetIntParamSafe(ITV8_PROP_FOCUS_SPEED));
    }

    if (ITV8::ENotError != res)
    {
        _err_ << ToString() << " Focus returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    if (NMMSS::ECONTINUOUS == flag)
    {
        if (speedOrStep == 0)
            scheduleReturnToHomePreset(); // Do not return untill continuos focus stopped.
        else
            m_homePresetTimer.cancel();
    }
    else
        scheduleReturnToHomePreset();

    return Equipment::Telemetry::ENotError;
}

long CTelemetry::FocusAuto(long sessionId)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    _inf_ << ToString() << "FocusAuto"<<std::endl;

    const ITV8::hresult_t res = m_telemetryChannel->OnePushAutofocus();
    if (ITV8::ENotError != res)
    {
        _err_ << ToString() << " OnePushAutofocus returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    scheduleReturnToHomePreset();
    return Equipment::Telemetry::ENotError;
}

long CTelemetry::Iris(long sessionId, NMMSS::ERangeFlag flag, long speedOrStep)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    ITV8::hresult_t res = ITV8::ENotError;
    _inf_ << ToString() << "Iris flag(" << (int)flag << ") speed(" << speedOrStep << ")"<<std::endl;
   
    
    if (NMMSS::ECONTINUOUS == flag)
    {
        m_continuousOperationFlags.set(continuousOperations::IRIS, speedOrStep);
        res = m_telemetryChannel->IrisContinuous(speedOrStep);
    }
    else if (NMMSS::EABSOLUTE == flag)
    {
        int irisPos = speedOrStep;
        res = m_telemetryChannel->IrisDirect(irisPos, GetIntParamSafe(ITV8_PROP_dIRECT_IRIS_SPEED));
    }
    else if (NMMSS::ERELATIVE == flag)
    {
        int irisStep = speedOrStep;
        res = m_telemetryChannel->IrisDiscrete(irisStep, GetIntParamSafe(ITV8_PROP_IRIS_SPEED));
    }

    if (ITV8::ENotError != res)
    {
        _err_ << ToString() << " Iris returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    if (NMMSS::ECONTINUOUS == flag)
    {
        if (speedOrStep == 0)
            scheduleReturnToHomePreset(); // Do not return untill continuos iris stopped.
        else
            m_homePresetTimer.cancel();
    }
    else
        scheduleReturnToHomePreset();

    return Equipment::Telemetry::ENotError;
}

long CTelemetry::IrisAuto(long sessionId)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    _inf_ << ToString() << "IrisAuto"<<std::endl;

    const ITV8::hresult_t res = m_telemetryChannel->OnePushAutoiris();
    if (ITV8::ENotError != res)
    {
        _err_ << ToString() << " OnePushAutoiris returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    scheduleReturnToHomePreset();
    return Equipment::Telemetry::ENotError;
}

long CTelemetry::SetPreset(long sessionId, uint32_t pos, const wchar_t* label, NMMSS::SPreset& newPreset)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }

    newPreset.savedOnDevice = m_settings.savePresetsOnDevice;
    _inff_("{} SetPreset({}) saveOnDevice={}", ToString(), pos, newPreset.savedOnDevice);
    ITV8::hresult_t res = ITV8::ENotError;
    if (newPreset.savedOnDevice)
    {
        auto newTelemetry = castToTelemetry2();
        if (newTelemetry)
        {
            std::string presetName(NCorbaHelpers::ToUtf8(label));
            res = newTelemetry->SetPreset(presetName.c_str());
            if (ITV8::EUnsupportedCommand == res)
                res = m_telemetryChannel->SetPreset(pos);
        }
        else
            res = m_telemetryChannel->SetPreset(pos);
    }
    else
    {
        res = GetPositionInformation(newPreset.position);
    }
  
    if(ITV8::ENotError != res)
    {
        _err_ << ToString() << "SetPreset returns " << ITV8::get_last_error_message(res);
    }
    else
    {
        boost::recursive_mutex::scoped_lock lock(m_presetsLock);
        newPreset.label = (label ? std::wstring(label) : L"");
        m_settings.presets[pos] = newPreset;
    }

    return res;
}

void CTelemetry::ConfigurePreset(uint32_t index, NMMSS::SPreset& preset)
{
    boost::recursive_mutex::scoped_lock lock(m_presetsLock);
    m_settings.presets[index] = preset;
}

void CTelemetry::scheduleReturnToHomePreset()
{
    if (m_homePresetTimeout)
    {
        CTelemetryPtr strongThis(this, NCorbaHelpers::ShareOwnership());
        m_homePresetTimer.expires_from_now(boost::posix_time::millisec(m_homePresetTimeout));
        m_homePresetTimer.async_wait(boost::bind(&GoHomePreset, _1, strongThis, m_homePresetIndex, m_homePresetSpeed));
    }
}

void CTelemetry::GoHomePreset(const boost::system::error_code& err, NCorbaHelpers::CAutoPtr<CTelemetry> strongThis, uint32_t pos, uint32_t speed)
{
    if (err)
        return;

    if (strongThis)
    {
        NLogging::PLogger ngp_Logger_Ptr_ = strongThis->ngp_Logger_Ptr_;
        _inf_ << strongThis->ToString() << "CTelemetry::GoHomePreset(" << pos << ", " << speed << ")" << std::endl;

        strongThis->GoPreset(Equipment::Telemetry::SERVER_SESSION_ID, pos, speed);
    }
}

long CTelemetry::GoPreset(long sessionId, uint32_t pos, double speed)
{
    boost::system::error_code err;
    m_homePresetTimer.cancel(err);

    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }

    NMMSS::SPreset preset;
    {
        boost::recursive_mutex::scoped_lock lock(m_presetsLock);
        const auto cit = m_settings.presets.find(pos);
        if (cit == m_settings.presets.end())
        {
            _wrn_ << ToString() << " CTelemetry::GoPreset(" << pos << ") failed! Can't find preset." << std::endl;
            return Equipment::Telemetry::EGeneralError;
        }
        preset = cit->second;
    }

    auto presetSpeed = std::max<ITV8::uint32_t>(1, static_cast<ITV8::uint32_t>(speed * GetIntParamSafe(ITV8_PROP_PRESET_SPEED)));

    _inf_ << ToString() << " GoPreset(" << pos << ", " << presetSpeed << ") savedOnDevice=" << preset.savedOnDevice << std::endl;
    ITV8::hresult_t res = ITV8::ENotError;
    if (preset.savedOnDevice)
    {
        auto newTelemetry = castToTelemetry2();
        if (newTelemetry)
        {
            std::string presetName(NCorbaHelpers::ToUtf8(preset.label));
            res = newTelemetry->GoPreset(presetName.c_str(), presetSpeed);
            if (ITV8::EUnsupportedCommand == res)
                res = m_telemetryChannel->GoPreset(pos, presetSpeed);
        }
        else
            res = m_telemetryChannel->GoPreset(pos, presetSpeed);
    }
    else
    {
        auto telemetryError = AbsoluteMove(sessionId, preset.position); //Equipment::Telemetry::ErrorCode
        switch (telemetryError)
        {
            case Equipment::Telemetry::ErrorCode::ENotError:           res = ITV8::ENotError;      break;
            case Equipment::Telemetry::ErrorCode::ESessionUnavailable: res = ITV8::EInternalError; break;
            case Equipment::Telemetry::ErrorCode::EGeneralError:       res = ITV8::EGeneralError;  break;
            case Equipment::Telemetry::ErrorCode::EPresetError:        res = ITV8::EInternalError; break;
            default: break;
        }
    }

    if(ITV8::ENotError != res)
    {
        _err_ << ToString() << " GoPreset returns " << ITV8::get_last_error_message(res)
              << " savedOnDevice = " << preset.savedOnDevice << std::endl;
        return Equipment::Telemetry::EPresetError;
    }

    if (m_homePresetIndex != pos)
        scheduleReturnToHomePreset();

    return Equipment::Telemetry::ENotError;
}

long CTelemetry::RemovePreset(long sessionId, uint32_t pos)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }

    ITV8::hresult_t res = ITV8::ENotError;
    _inff_("{} RemovePreset({}) saveOnDevice={}", ToString(), pos, m_settings.savePresetsOnDevice);
    if (m_settings.savePresetsOnDevice)
    {
        auto newTelemetry = castToTelemetry2();
        if (newTelemetry)
        {
            auto i = m_settings.presets.find(pos);
            if (i != m_settings.presets.end())
            {
                std::string presetName(NCorbaHelpers::ToUtf8(i->second.label));
                res = newTelemetry->ClearPreset(presetName.c_str());
                if (ITV8::EUnsupportedCommand == res)
                    res = m_telemetryChannel->ClearPreset(pos);
            }
        }
        else
            res = m_telemetryChannel->ClearPreset(pos);
    }

    if (ITV8::ENotError != res && ITV8::EUnsupportedCommand != res)
    {
        _err_ << ToString() << "RemovePreset returns " << ITV8::get_last_error_message(res);
        return Equipment::Telemetry::EPresetError;
    }
    else
    {
        boost::recursive_mutex::scoped_lock lock(m_presetsLock);
        auto i = m_settings.presets.find(pos);
        if(i != m_settings.presets.end())
        {
            m_settings.presets.erase(i);
        }
    }
    
    return Equipment::Telemetry::ENotError;
}

int CTelemetry::GetMaxPresetsCount()
{
	_inf_ << ToString() << "GetMaxPresetsCount"<<std::endl;
	if (0 != m_telemetryChannel.get())
	{
		return GetIntParamUnsafe(ITV8_PROP_PRESET_COUNT);
	}
	return 0;
}

NMMSS::IPresetIterator* CTelemetry::GetPresetsInfo()
{
    if (m_telemetryChannel.get() == nullptr)
        return 0;

    boost::recursive_mutex::scoped_lock lock(m_presetsLock);
    auto newTelemetry = castToTelemetry2();
    _inff_("{} GetPresetsInfo usingITelemety2={} m_recheckPresetsNeeded={}", ToString(), (newTelemetry != nullptr), m_recheckPresetsNeeded);
    if (newTelemetry && m_recheckPresetsNeeded)
    {
        STelemetryParam::TPresets& oldPresets = m_settings.presets;
        auto enumerator = newTelemetry->GetPresets();
        if (enumerator)
        {
            enumerator->Reset();
            while (enumerator->MoveNext())
            {
                auto info = enumerator->GetCurrent();
                std::wstring name = std::wstring();
                try
                {
                    name = NCorbaHelpers::FromUtf8(Utility::safeCopyString(info->GetName()));
                }
                catch (const std::runtime_error& e)
                {
                    _errf_("{} std::exception while converting name '{}': {}", ToString(), Utility::safeCopyString(info->GetName()), e.what());
                    continue;
                }

                _dbgf_("{} GetPresetsInfo name={}", ToString(), info->GetName());

                bool isOld = true;
                for (auto& preset : oldPresets)
                    if (preset.second.savedOnDevice && preset.second.label == name)
                    {
                        isOld = false;
                        break;
                    }
                if (isOld)
                    oldPresets[oldPresets.size()].label = name;

            }
            enumerator->Destroy();
        }

        m_recheckPresetsNeeded = false;
    }

    return new CPresetIterator(m_settings.presets);
}

std::vector<std::string> CTelemetry::GetAuxiliaryOperations()
{
    _log_ << ToString() << " GetAuxiliaryOperations";

    if (m_auxOperations.empty() && 0 != m_telemetryChannel.get())
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto newTelemetry = ITV8::contract_cast<ITV8::GDRV::ITelemetry2>(adj);

        if (newTelemetry)
        {
            auto enumerator = newTelemetry->GetAuxiliaryOperations();
            if (enumerator)
            {
                enumerator->Reset();
                while (enumerator->MoveNext())
                    m_auxOperations.push_back(enumerator->GetCurrent());
                enumerator->Destroy();
            }
        }
    }
    return m_auxOperations;
}

long CTelemetry::PerformAuxiliaryOperation(long sessionId, const char* operation)
{
    _log_ << ToString() << " PerformAuxiliaryOperation";

    if (!isSessionActive(sessionId))
        return Equipment::Telemetry::ESessionUnavailable;

    if (0 != m_telemetryChannel.get() && std::find(m_auxOperations.begin(), m_auxOperations.end(), operation) != m_auxOperations.end())
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto newTelemetry = ITV8::contract_cast<ITV8::GDRV::ITelemetry2>(adj);

        if (newTelemetry)
            return newTelemetry->PerformAuxiliaryOperation(operation) == ITV8::ENotError ? Equipment::Telemetry::ENotError
                                                                                         : Equipment::Telemetry::EGeneralError;
    }
    return Equipment::Telemetry::EGeneralError;
}

///////////////////////////////////////////// TOURS... /////////////////////////////////////////////
void CTelemetry::NotifyTourState(Json::Value&& data)
{
    this->Notify(NMMSS::IPDS_DeviceConfigurationChanged, std::move(data));
}

NMMSS::IPresetIterator* CTelemetry::GetTourPoints(const char* tourName)
{
    _inf_ << ToString() << " GetTourPoints" << std::endl;
    if (0 != m_telemetryChannel.get())
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto ntTelemetry = ITV8::contract_cast<ITV8::GDRV::INamedTourController>(adj);

        STelemetryParam::TPresets presets;
        if (ntTelemetry)
        {
            auto enumerator = ntTelemetry->GetTourPoints(tourName);
            if (enumerator)
            {
                enumerator->Reset();
                while (enumerator->MoveNext())
                {
                    auto info = enumerator->GetCurrent();
                    presets[presets.size()].label = NCorbaHelpers::FromUtf8(Utility::safeCopyString(info->GetName()));;
                    presets[presets.size()].savedOnDevice = false;
                }
                enumerator->Destroy();
            }
        }
        _dbg_ << ToString() << " GetTourPoints size: " << presets.size() << std::endl;
        return new CPresetIterator(presets);
    }
    return 0;
}
std::vector<std::pair<std::string, int>> CTelemetry::GetTours()
{
    _log_ << ToString() << " GetTours";
    m_getToursInProcess = true;
    std::vector<std::pair<std::string, int>> availableTours;
    if (0 != m_telemetryChannel.get())
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto ntTelemetry = ITV8::contract_cast<ITV8::GDRV::INamedTourController>(adj);
        if (ntTelemetry)
        {
            auto enumerator = ntTelemetry->GetTours();
            if (enumerator)
            {
                enumerator->Reset();
                while (enumerator->MoveNext())
                    availableTours.emplace_back(enumerator->GetCurrentName(), enumerator->GetCurrentState());
                enumerator->Destroy();
            }
        }
    }
    _dbg_ << ToString() << " GetTours size: " << availableTours.size() << std::endl;

    boost::mutex::scoped_lock lock(m_getToursMutex);
    m_getToursCondition.notify_one();
    m_getToursInProcess = false;
    return availableTours;
}
long CTelemetry::runTourCommand(long sessionId, const char* tourName, const char* command, 
    boost::function<ITV8::hresult_t(ITV8::GDRV::INamedTourController*, const char*)> f)
{
    auto result = Equipment::Telemetry::ENotError;
    if (!isSessionActive(sessionId))
        result = Equipment::Telemetry::ESessionUnavailable;

    if (m_telemetryChannel.get() && result != Equipment::Telemetry::ESessionUnavailable)
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto newTelemetry = ITV8::contract_cast<ITV8::GDRV::INamedTourController>(adj);

        result = (newTelemetry && f(newTelemetry, tourName) == ITV8::ENotError)
                ? Equipment::Telemetry::ENotError
                : Equipment::Telemetry::EGeneralError;
    }

    _inff_("{} {} tourName={} sessionId={} result={}", ToString(), command, tourName, sessionId, result);
    return result;
}
long CTelemetry::PlayTour(long sessionId, const char* tourName)
{
    return runTourCommand(sessionId, tourName, __FUNCTION__, &ITV8::GDRV::INamedTourController::PlayTour);
}
long CTelemetry::PauseTour(long sessionId, const char* tourName)
{
    return runTourCommand(sessionId, tourName, __FUNCTION__, &ITV8::GDRV::INamedTourController::PauseTour);
}
long CTelemetry::StopTour(long sessionId, const char* tourName)
{
    long ret = runTourCommand(sessionId, tourName, __FUNCTION__, &ITV8::GDRV::INamedTourController::StopTour);
    scheduleReturnToHomePreset();
    return ret;
}
long CTelemetry::StartFillTour(long sessionId, const char* tourName)
{
    return runTourCommand(sessionId, tourName, __FUNCTION__, &ITV8::GDRV::INamedTourController::StartFillTour);
}
long CTelemetry::SetTourPoint(long sessionId, const char* presetName, long dwellTime, long moveSpeed)
{
    _log_ << ToString() << " SetTourPoint " << presetName;

    if (!isSessionActive(sessionId))
        return Equipment::Telemetry::ESessionUnavailable;

    if (0 != m_telemetryChannel.get())
    {
        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto newTelemetry = ITV8::contract_cast<ITV8::GDRV::INamedTourController>(adj);

        if (newTelemetry)
            return newTelemetry->SetPoint(presetName, dwellTime, moveSpeed) == ITV8::ENotError ? Equipment::Telemetry::ENotError
                                                                                               : Equipment::Telemetry::EGeneralError;
    }
    return Equipment::Telemetry::EGeneralError;
}
long CTelemetry::StopFillTour(long sessionId, const char* tourName)
{
    return runTourCommand(sessionId, tourName, __FUNCTION__, &ITV8::GDRV::INamedTourController::StopFillTour);
}
long CTelemetry::RemoveTour(long sessionId, const char* tourName)
{
    return runTourCommand(sessionId, tourName, __FUNCTION__, &ITV8::GDRV::INamedTourController::RemoveTour);
}
///////////////////////////////////////////// ...TOURS /////////////////////////////////////////////

NMMSS::IFlaggedRangeIterator* CTelemetry::GetPanInfo()
{
	_inf_ << ToString() << "GetPanInfo"<<std::endl;

	CFlaggedRangeIterator* it = new CFlaggedRangeIterator();

    if (IsSupported(ITV8_PROP_SUPPORTS_CONTINUOUS_MOVE))
    {
        int panSpeed = GetIntParamSafe(ITV8_PROP_PAN_SPEED);
        it->AddRange(NMMSS::ECONTINUOUS, -panSpeed, panSpeed);
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DIRECT_MOVE))
    {
        it->AddRange(NMMSS::EABSOLUTE, GetIntParamSafe(ITV8_PROP_DIRECT_PAN_MIN_POSITION),
            GetIntParamSafe(ITV8_PROP_DIRECT_PAN_MAX_POSITION));
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DISCRETE_MOVE))
    {
        it->AddRange(NMMSS::ERELATIVE, 0, GetIntParamSafe(ITV8_PROP_PAN_SPEED));
    }

	return it;
}

NMMSS::IFlaggedRangeIterator* CTelemetry::GetTiltInfo()
{
	_inf_ << ToString() << "GetTiltInfo"<<std::endl;

    CFlaggedRangeIterator* it = new CFlaggedRangeIterator();

    if (IsSupported(ITV8_PROP_SUPPORTS_CONTINUOUS_MOVE))
    {
        int tiltSpeed = GetIntParamSafe(ITV8_PROP_TILT_SPEED);
        it->AddRange(NMMSS::ECONTINUOUS, -tiltSpeed, tiltSpeed);
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DIRECT_MOVE))
    {
        it->AddRange(NMMSS::EABSOLUTE, GetIntParamSafe(ITV8_PROP_DIRECT_TILT_MIN_POSITION),
            GetIntParamSafe(ITV8_PROP_DIRECT_TILT_MAX_POSITION));
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DISCRETE_MOVE))
    {
        it->AddRange(NMMSS::ERELATIVE, 0, GetIntParamSafe(ITV8_PROP_TILT_SPEED));
    }

	return it;
}

NMMSS::IFlaggedRangeIterator* CTelemetry::GetZoomInfo()
{
    _inf_ << ToString() << "GetZoomInfo" << std::endl;

    CFlaggedRangeIterator* it = new CFlaggedRangeIterator();

    if (IsSupported(ITV8_PROP_SUPPORTS_CONTINUOUS_ZOOM))
    {
        int zoomSpeed = GetIntParamSafe(ITV8_PROP_ZOOMSPEED);
        it->AddRange(NMMSS::ECONTINUOUS, -zoomSpeed, zoomSpeed);
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DIRECT_ZOOM))
    {
        it->AddRange(NMMSS::EABSOLUTE, GetIntParamSafe(ITV8_PROP_DIRECT_ZOOM_MIN_POSITION), GetIntParamSafe(ITV8_PROP_DIRECT_ZOOM_MAX_POSITION));
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DISCRETE_ZOOM))
    {
        it->AddRange(NMMSS::ERELATIVE, 0, GetIntParamSafe(ITV8_PROP_ZOOMSTEP));
    }

	return it;
}

NMMSS::IFlaggedRangeIterator* CTelemetry::GetFocusInfo(bool& supportAuto)
{
    _inf_ << ToString() << "GetFocusInfo" << std::endl;

	supportAuto = IsSupported(ITV8_PROP_SUPPORTS_ONEPUSH_AUTO_FOCUS);
    CFlaggedRangeIterator* it = new CFlaggedRangeIterator();

	if (IsSupported(ITV8_PROP_SUPPORTS_CONTINUOUS_FOCUS))
    {
        int focusSpeed = GetIntParamSafe(ITV8_PROP_FOCUS_SPEED);
        it->AddRange(NMMSS::ECONTINUOUS, -focusSpeed, focusSpeed);
    }
	if(IsSupported(ITV8_PROP_SUPPORTS_DIRECT_FOCUS))
		it->AddRange(NMMSS::EABSOLUTE, 0, GetIntParamSafe(ITV8_PROP_DIRECT_FOCUS_SPEED));
	if(IsSupported(ITV8_PROP_SUPPORTS_DISCRETE_FOCUS))
		it->AddRange(NMMSS::ERELATIVE, 0, GetIntParamSafe(ITV8_PROP_FOCUS_SPEED));

	return it;
}

NMMSS::IFlaggedRangeIterator* CTelemetry::GetIrisInfo(bool& supportAuto)
{
    _inf_ << ToString() << "GetIrisInfo" << std::endl;

	supportAuto = IsSupported(ITV8_PROP_SUPPORTS_ONEPUSH_AUTO_IRIS);
    CFlaggedRangeIterator* it = new CFlaggedRangeIterator();

	if (IsSupported(ITV8_PROP_SUPPORTS_CONTINUOUS_IRIS))
    {
        int irisSpeed = GetIntParamSafe(ITV8_PROP_IRIS_SPEED);
        it->AddRange(NMMSS::ECONTINUOUS, -irisSpeed, irisSpeed);
    }
    if (IsSupported(ITV8_PROP_SUPPORTS_DIRECT_IRIS))
		it->AddRange(NMMSS::EABSOLUTE, 0, GetIntParamSafe(ITV8_PROP_dIRECT_IRIS_SPEED));
	if(IsSupported(ITV8_PROP_SUPPORTS_DISCRETE_IRIS))
		it->AddRange(NMMSS::ERELATIVE, 0, GetIntParamSafe(ITV8_PROP_IRIS_STEP));

	return it;
}

//Starts in separate thread
void CTelemetry::DoStart()
{
	if(0 == m_telemetryChannel.get())
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
		m_telemetryChannel = ITelemetryPtr(device->CreateTelemetryChannel(m_settings.id),
            ipint_destroyer<ITV8::GDRV::ITelemetry>());

		if (0 == m_telemetryChannel.get())
        {
            std::string message = get_last_error_message(device);
            _err_ << ToString() <<  " Exception: ITV8::GDRV::IDevice::CreateTelemetryChannel("
                << m_settings.id <<") return 0. Message: " << message << std::endl;
            return;
        }

        auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
        auto newTelemetry = ITV8::contract_cast<ITV8::GDRV::INamedTourController>(adj);
        if (newTelemetry)
            newTelemetry->SubscribeToEvent(new ToursEventHandler(this));
	}

    ApplyChanges(0);

    // Set started manually because the creating telemetry doesn't need acknowledge from driver.
    CChannel::RaiseChannelStarted();
    SetFlag(cfStarted, true);
    this->Notify(NMMSS::IPDS_SignalRestored);

    CChannel::DoStart();
}

//Stops in separate thread
void CTelemetry::DoStop()
{
    boost::system::error_code err;
    m_homePresetTimer.cancel(err);

    if (0 != m_telemetryChannel.get())
        WaitForApply();

    auto* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
    auto newTelemetry = ITV8::contract_cast<ITV8::GDRV::INamedTourController>(adj);
    if (newTelemetry)
        newTelemetry->UnsubscribeFromEvent();
    
    if (m_getToursInProcess)
    {
        const boost::posix_time::milliseconds time(30000);
        boost::mutex::scoped_lock lock(m_getToursMutex);
        m_getToursCondition.timed_wait(lock, time);
    }

    m_telemetryChannel.reset();

    // Just signals that channel stopped.
    CChannel::RaiseChannelStopped();

    m_patrolContext->SwitchPatrol(false);

    this->Notify(NMMSS::IPDS_SignalLost);
}

std::string CTelemetry::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "telemetry:" << m_settings.id;
    return stream.str();
}

void CTelemetry::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
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

    if (!m_telemetryChannel)
    {
        _log_ << ToString() << " telemetry doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(
        static_cast<ITV8::GDRV::IDeviceChannel*>(m_telemetryChannel.get()));
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(static_cast<ITV8::GDRV::IDeviceChannel*>(m_telemetryChannel.get()), ITV8::EUnsupportedCommand);
}

void CTelemetry::SwitchEnabled()
{
    SetEnabled(m_settings.enabled);
}

void CTelemetry::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (GetFlag(cfEnvironmentReady) && (0 != m_telemetryChannel.get()))
    {
        ITV8::IAsyncAdjuster* asyncAj = 
		    ITV8::contract_cast<ITV8::IAsyncAdjuster>(static_cast<ITV8::GDRV::IDeviceChannel*>(m_telemetryChannel.get()));
        if(asyncAj)
        {
            //Sets properties of Ptz channel.
            SetValues(asyncAj, m_settings.publicParams);

            //Applies new settings.
            _dbg_ << ToString() << " ApplyChanges " << handler << " : " << static_cast<ITV8::IAsyncActionHandler*>(this);
            ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() <<" couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped." <<std::endl;
        }
    }

    m_patrolContext->SwitchPatrol(m_patrolEnabled);
}

IParamContextIterator* CTelemetry::GetParamIterator()
{
    return m_context.get();
}

//    CFlaggedRangeIterator
void CTelemetry::CFlaggedRangeIterator::AddRange(NMMSS::ERangeFlag flag, 
												 long minVal, long maxVal)
{
	m_ranges.push_back(NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRange>(
		CreateFlaggedRange(flag, minVal, maxVal)));

	m_current = m_ranges.begin();
}

size_t CTelemetry::CFlaggedRangeIterator::Count()
{
	return m_ranges.size();
}

NMMSS::IFlaggedRange* CTelemetry::CFlaggedRangeIterator::Next()
{
	if(m_current == m_ranges.end())
	{
		throw std::runtime_error("iterator is out of range");
	}

	NMMSS::IFlaggedRange* range = m_current->Get();
	range->AddRef();
	++m_current;
	return range;
}

CTelemetry::CPresetIterator::CPresetIterator(const presets_t& presets)
{
	m_items = presets;
	m_count = m_items.size();
	m_current = m_items.begin();
}

size_t CTelemetry::CPresetIterator::Count()
{
	return m_count;
}

NMMSS::IPreset* CTelemetry::CPresetIterator::Next()
{
	std::ostringstream ss;
	ss << "CTelemetry::CPresetIterator::Next() current(";
	if(m_current != m_items.end()) 
		ss << m_current->first;
	ss <<")"<<std::endl;
#ifdef _WIN32
	OutputDebugStringA(ss.str().c_str());
#endif
	if (m_current == m_items.end())
		return 0;
    NMMSS::IPreset* preset = 
        CreatePreset(m_current->first, m_current->second.label.c_str(), m_current->second.savedOnDevice, m_current->second.position);
	++m_current;
	return preset;
}

NMMSS::IPreset* CTelemetry::CPresetIterator::GetPresetByHint(unsigned long pos)
{
	if (m_count == 0)
		return 0;

	m_current = m_items.lower_bound(pos);
	if (m_current == m_items.end())
	{
		m_current = m_items.begin();
		return 0;
	}
    return CreatePreset(m_current->first, m_current->second.label.c_str(), m_current->second.savedOnDevice, m_current->second.position);
}

std::string CTelemetry::ToString() const
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
	str << "\\" << ".telemetry:"<< m_settings.id << " ";
	return str.str();
}

int  CTelemetry::GetIntParamSafe(const char* param)
{
	try
	{
		return m_settings.GetIntParam(param);
	}
	catch(const std::exception& e)
	{
		_err_ << ToString() << e.what() << std::endl;
	}
	return 0;
}

int   CTelemetry::GetIntParamUnsafe(const char* param)
{
    return m_settings.GetIntParam(param);
}

bool CTelemetry::IsSupported(const char* param) const
{
	try
	{
		return m_settings.GetIntParam(param) != 0;
	}
	catch(const std::exception& e)
	{
		_err_ << e.what() << std::endl;
	}
	return false;
}

void CTelemetry::StartPatrol()
{
    _inf_ << ToString() << " StartPatrol";
    m_patrolEnabled = true;
    m_patrolContext->SwitchPatrol(true);
}

void CTelemetry::StopPatrol()
{
    _inf_ << ToString() << " StopPatrol";
    m_patrolEnabled = false;
    m_patrolContext->SwitchPatrol(false);
    scheduleReturnToHomePreset();
}

bool CTelemetry::IsAreaZoomSupported() const
{
	return IsSupported(ITV8_PROP_SUPPORTS_AREA_ZOOM);
}

bool CTelemetry::IsPointMoveSupported() const
{
	return IsSupported(ITV8_PROP_SUPPORTS_POINT_MOVE);
}

long CTelemetry::AreaZoom(long sessionId, const NMMSS::RectangleF& area)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    _inf_ << ToString() << " AreaZoom("<<area.x<<", "<<area.y<<", "<<area.w<<", "<<area.h<<");" << std::endl;

    ITV8::GDRV::ITelemetryAdjuster* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
    ITV8::GDRV::ITelemetryPointClick* ptClick = ITV8::contract_cast<ITV8::GDRV::ITelemetryPointClick>(adj);
    if(ptClick)
    {
        ITV8::RectangleF r(area.x, area.y, area.w, area.h);
        const ITV8::hresult_t res = ptClick->AreaZoom(r);
        if(ITV8::ENotError != res)
        {
            _err_ << ToString() << " AreaZoom("<<area.x<<", "<<area.y<<", "<<area.w<<", "<<area.h<<") returns " 
                << ITV8::get_last_error_message(res) << std::endl;
            return Equipment::Telemetry::EGeneralError;
        }
    }
    else
    {
        _wrn_ << ToString() <<
            " The interface ITV8::GDRV::ITelemetryPointClick not supported by telemetry."<<std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    scheduleReturnToHomePreset();
    return Equipment::Telemetry::ENotError;
}

long CTelemetry::PointMove(long sessionId, const NMMSS::PointF& center)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    _inf_ << ToString() << " PointMove("<<center.x<<", "<<center.y<<"); " << std::endl;

    ITV8::GDRV::ITelemetryAdjuster* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
    ITV8::GDRV::ITelemetryPointClick* ptClick = ITV8::contract_cast<ITV8::GDRV::ITelemetryPointClick>(adj);
    if(ptClick)
    {
        ITV8::PointF pt(center.x, center.y);
        ITV8::hresult_t res = ptClick->PointMove(pt);
        if(ITV8::ENotError != res)
        {
            _err_ << ToString() << " PointMove("<<center.x<<", "<<center.y<<") returns " 
                << ITV8::get_last_error_message(res) << std::endl;
            return Equipment::Telemetry::EGeneralError;
        }
    }
    else
    {
        _wrn_ << ToString() <<
            " The interface ITV8::GDRV::ITelemetryPointClick not supported by telemetry." << std::endl;
        return Equipment::Telemetry::ENotError;
    }

    scheduleReturnToHomePreset();
    return Equipment::Telemetry::ENotError;
}

long CTelemetry::AbsoluteMove(long sessionId, const NMMSS::AbsolutePositionInformation& position)
{
    if (!isSessionActive(sessionId))
    {
        return Equipment::Telemetry::ESessionUnavailable;
    }
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }
    _dbg_ << ToString() << "AbsoluteMove pan(" << position.pan << ") tilt(" << position.tilt << ") zoom(" << position.zoom << ")" << std::endl;

    ITV8::hresult_t res = ITV8::EUnsupportedCommand;

    const int directMoveSpeed = GetIntParamSafe(ITV8_PROP_DIRECT_MOVE_SPEED);
    const int directZoomSpeed = GetIntParamSafe(ITV8_PROP_DIRECT_ZOOM_SPEED);

    ITV8::GDRV::ITelemetryAdjuster* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());

    ITV8::GDRV::IAbsoluteTelemetry* absTelemetry = ITV8::contract_cast<ITV8::GDRV::IAbsoluteTelemetry>(adj);
    if (absTelemetry)
    {
        auto mask = position.mask;
        if (mask == ITV8::GDRV::TNotSupported)
            mask = ITV8::GDRV::TPan | ITV8::GDRV::TTilt | ITV8::GDRV::TZoom;

        res = absTelemetry->AbsoluteMoveEx(position.pan, directMoveSpeed,
            position.tilt, directMoveSpeed, position.zoom, directZoomSpeed, mask);
    }
    else
    {
        ITV8::GDRV::ITelemetryAbsoluteMove* absMove = ITV8::contract_cast<ITV8::GDRV::ITelemetryAbsoluteMove>(adj);
        if (!absMove)
        {
            _err_ << ToString() << "AbsoluteMove failed : ITelemetryAbsoluteMove is not supported";
            return Equipment::Telemetry::EGeneralError;
        }
        res = absMove->AbsoluteMove(position.pan, directMoveSpeed, position.tilt, directMoveSpeed, position.zoom, directZoomSpeed);
    }

    if (res != ITV8::ENotError && res != ITV8::EAlready)
    {
        _err_ << ToString() << " Move returns " << ITV8::get_last_error_message(res) << std::endl;
        return Equipment::Telemetry::EGeneralError;
    }

    scheduleReturnToHomePreset();
    return Equipment::Telemetry::ENotError;
}

long CTelemetry::GetPositionInformation(NMMSS::AbsolutePositionInformation& position)
{
    if (!m_telemetryChannel.get())
    {
        return Equipment::Telemetry::EGeneralError;
    }

    ITV8::hresult_t res = ITV8::EUnsupportedCommand;

    ITV8::GDRV::ITelemetryAdjuster* adj = static_cast<ITV8::GDRV::ITelemetryAdjuster*>(m_telemetryChannel.get());
    ITV8::GDRV::IAbsoluteTelemetry* absTelemetry = ITV8::contract_cast<ITV8::GDRV::IAbsoluteTelemetry>(adj);
    if (!absTelemetry)
    {
        _err_ << ToString() << " IAbsoluteTelemetry is not supported by driver";
        return Equipment::Telemetry::EGeneralError;
    }
    
    int32_t panVal = 0;
    int32_t tiltVal = 0;
    int32_t zoomVal = 0;
    int32_t maskVal = 0;

    res = absTelemetry->GetPositionParams(panVal, tiltVal, zoomVal, maskVal);
    if (res != ITV8::ENotError)
    {
        _err_ << ToString() << "GetPositionInformation failed with error code " << res;
        return Equipment::Telemetry::EGeneralError;
    }

    if (maskVal == ITV8::GDRV::TNotSupported)
    {
        _err_ << ToString() << "GetPositionInformation have no data for current position";
        return Equipment::Telemetry::EGeneralError;
    }

    if (maskVal & ITV8::GDRV::TPan)  position.pan  = panVal;
    if (maskVal & ITV8::GDRV::TTilt) position.tilt = tiltVal;
    if (maskVal & ITV8::GDRV::TZoom) position.zoom = zoomVal;
    position.mask = maskVal;

    _dbgf_("{} GetPositionInformation pan({}) tilt({}) zoom({}) mask({})", ToString(),
        position.pan, position.tilt, position.zoom, position.mask);

    return Equipment::Telemetry::ENotError;
}

}
