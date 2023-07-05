#ifndef DEVICEIPINT3_CTELEMETRY_H
#define DEVICEIPINT3_CTELEMETRY_H

#include <bitset>

#include <CorbaHelpers/RefcountedImpl.h>

#include <ItvDeviceSdk/include/infoWriters.h>
#include "../../mmss/DeviceInfo/include/PropertyContainers.h"
#include <CorbaHelpers/TAO.h>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Resource.h>
#include <CorbaHelpers/Timer.h>

#include "TelemetryHelper.h"
#include "IIPManager3.h"
#include "ParamContext.h"
#include "AsyncActionHandler.h"
#include "DeviceInformation.h"

namespace IPINT30
{
class CDevice;

class TelemetryConnectionKeeper;
class CPatrolContext;
typedef boost::weak_ptr<TelemetryConnectionKeeper> TelemetryConnectionKeeperWP;
typedef boost::shared_ptr<TelemetryConnectionKeeper> TelemetryConnectionKeeperSP;

class CTelemetry : 
    public CChannelHandlerImpl,
	public NMMSS::ITelemetry,
    public IObjectParamContext,
    public IDeviceInformationProvider
{
	//TODO: подумать над переносом CFlaggedRangeIterator и CPresetIterator в TelemetryHelper.h
    class CFlaggedRangeIterator : public NMMSS::IFlaggedRangeIterator
        , public virtual NCorbaHelpers::CRefcountedImpl
    {
    private:
		std::vector<NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRange> > m_ranges;
		std::vector<NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRange> >::iterator m_current;
    public:
		void AddRange(NMMSS::ERangeFlag flag, long minVal, long maxVal);
        virtual size_t Count();
        virtual NMMSS::IFlaggedRange* Next();
    };

	class CPresetIterator : 
		public NMMSS::IPresetIterator,
		public virtual NCorbaHelpers::CRefcountedImpl
	{
	private:
		size_t m_count;
		//коллекция пресетов переданная в конструкторе
        typedef std::map<int, SPreset_t> presets_t;
        presets_t m_items;
		//итератор поставленный на текущий элемент в коллекции.
        presets_t::const_iterator m_current;
	public:
        CPresetIterator(const presets_t& presets);
		virtual size_t Count();
		virtual NMMSS::IPreset* Next();
		virtual NMMSS::IPreset* GetPresetByHint(unsigned long pos);
	};

public:
	CTelemetry(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
		const STelemetryParam& telSettings, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
        const char* objectContext, NCorbaHelpers::IContainerNamed* container);
    
    ~CTelemetry();

	//NMMSS::ITelemetry implementation
public:
    virtual bool IsSessionAvailable(long priority, NMMSS::UserSessionInformation& blockingUserInformation);
    virtual long AcquireSessionId(const NMMSS::UserSessionInformation& userSession,
        long priority, long& result, NMMSS::UserSessionInformation& blockingUserInformation, time_t& expirationTime);
    virtual long ReleaseSessionId(long sessionId);
    virtual bool KeepAlive(long sessionId);
    virtual time_t GetSessionExpirationTime(long sessionId);

    virtual long Move(long sessionId, NMMSS::ERangeFlag flag, long panSpeedOrStep, long tiltSpeedOrStep);

    virtual long Zoom(long sessionId, NMMSS::ERangeFlag flag, long speedOrStep);

    virtual long Focus(long sessionId, NMMSS::ERangeFlag flag, long speedOrStep);
    virtual long FocusAuto(long sessionId);

    virtual long Iris(long sessionId, NMMSS::ERangeFlag flag, long speedOrStep);
    virtual long IrisAuto(long sessionId);

    virtual long SetPreset(long sessionId, uint32_t pos, const wchar_t*, NMMSS::SPreset& newPreset);
    virtual void ConfigurePreset(uint32_t index, NMMSS::SPreset& preset);
    virtual long GoPreset(long sessionId, uint32_t pos, double speed);
    virtual long RemovePreset(long sessionId, uint32_t pos);
    static void GoHomePreset(const boost::system::error_code& err, NCorbaHelpers::CAutoPtr<CTelemetry> strongThis, uint32_t pos, uint32_t speed);
    virtual int GetMaxPresetsCount();
    virtual NMMSS::IPresetIterator* GetPresetsInfo();

    virtual NMMSS::IFlaggedRangeIterator* GetPanInfo();
    virtual NMMSS::IFlaggedRangeIterator* GetTiltInfo();
    virtual NMMSS::IFlaggedRangeIterator* GetZoomInfo();
    virtual NMMSS::IFlaggedRangeIterator* GetFocusInfo(bool& supportAuto);
    virtual NMMSS::IFlaggedRangeIterator* GetIrisInfo(bool& supportAuto);

    virtual void StartPatrol();
    virtual void StopPatrol();
	
	// Check that this feature is supported.
    virtual bool IsAreaZoomSupported() const;
    virtual bool IsPointMoveSupported() const;

	// Zoom to specified window. Window's size can be greater than 1.
    virtual long AreaZoom(long sessionId, const NMMSS::RectangleF& area);

	// Move to specified point.
    virtual long PointMove(long sessionId, const NMMSS::PointF& center);

    virtual long AbsoluteMove(long sessionId, const NMMSS::AbsolutePositionInformation& position);
    virtual long GetPositionInformation(NMMSS::AbsolutePositionInformation& position);

    std::vector<std::string> GetAuxiliaryOperations();
    long PerformAuxiliaryOperation(long sessionId, const char* operation);

    virtual NMMSS::IPresetIterator* GetTourPoints(const char* tourName);
    virtual std::vector<std::pair<std::string, int>> GetTours();
    virtual long PlayTour(long sessionId, const char* tourName);
    virtual long PauseTour(long sessionId, const char* tourName);
    virtual long StopTour(long sessionId, const char* tourName);
    virtual long StartFillTour(long sessionId, const char* tourName);
    virtual long SetTourPoint(long sessionId, const char* presetName, long dwellTime, long moveSpeed);
    virtual long StopFillTour(long sessionId, const char* tourName);
    virtual long RemoveTour(long sessionId, const char* tourName);
    void NotifyTourState(Json::Value&& data);

private:
    void scheduleReturnToHomePreset();
    long runTourCommand(long sessionId, const char* tourName, const char* command,
        boost::function<ITV8::hresult_t(ITV8::GDRV::INamedTourController*, const char*)> f);

// IObjectParamContext implementation
public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();
public:
    virtual void OnEnabled();
    virtual void OnDisabled();

//Overrides public virtual methods of CChannel class
public:
    std::string ToString() const;

//Overrides protected virtual methods of CChannel class
protected:
    virtual void DoStart();
    virtual void DoStop();
    virtual void OnFinalized();

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
    int  GetIntParamSafe(const char* param);
    int  GetIntParamUnsafe(const char* param);

    bool IsSupported(const char* param) const;

    // Gets the flag indicates whether the patrol should be enabled with telemetry enabling.
    int GetDefaultEnablePatrol() const;
    // Sets the flag indicates whether the patrol should be enabled with telemetry enabling.
    void SetDefaultEnablePatrol(int newVal);

    void ReenableControl();

    bool isSessionActive(long sessionId);
    void releaseActiveSession();
    void sessionReleaseHandler();
    
    ITV8::GDRV::ITelemetry2* castToTelemetry2();
    void initializeMetaParams();
    void initMetaParameter(const NStructuredConfig::TCustomParameters& metaParams, const char* name, std::function<void(const std::string& value)> initializer);

private:
    // configuration
    STelemetryParam m_settings;
    bool m_recheckPresetsNeeded;

    bool m_enableMultipleControl;

    // The current state of patrolling.
    bool m_patrolEnabled;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;
    NCorbaHelpers::PContainerTrans m_observerContainer;
    NCorbaHelpers::PResource m_telemetryServant;
    NCorbaHelpers::PResource m_observerServant;

    std::string m_stateControlServantName;
    NCorbaHelpers::PResource m_stateServant;
    boost::shared_ptr<NCorbaHelpers::IResource> m_lease;

    boost::recursive_mutex m_presetsLock;

    typedef boost::shared_ptr<ITV8::GDRV::ITelemetry> ITelemetryPtr;
    ITelemetryPtr m_telemetryChannel;

    boost::shared_ptr<CParamContext> m_context;

    boost::mutex                m_connectionKeeperGuard;
    TelemetryConnectionKeeperWP m_connectionKeeper;
    CORBA::Long                 m_lastSessionId;
    std::bitset<4>              m_continuousOperationFlags;

    std::shared_ptr<CPatrolContext> m_patrolContext;
    std::string m_accessPoint;

    NCorbaHelpers::PReactor m_reactor;
    boost::asio::deadline_timer m_homePresetTimer;
    uint32_t m_homePresetIndex;
    uint32_t m_homePresetTimeout;

    std::vector<std::string> m_auxOperations;
    bool m_enableTelemetry2;
    ITV8::uint32_t m_homePresetSpeed;

    boost::mutex m_getToursMutex;
    boost::condition_variable m_getToursCondition;
    bool m_getToursInProcess;
};


}

#endif // DEVICEIPINT3_CTELEMETRY_H