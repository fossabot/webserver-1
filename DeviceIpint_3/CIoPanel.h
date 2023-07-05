#ifndef DEVICEIPINT3_CIOPANEL_H
#define DEVICEIPINT3_CIOPANEL_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <CorbaHelpers/RefcountedImpl.h>

#include <ItvDeviceSdk/include/ItvDeviceSdk.h>
#include <ItvDeviceSdk/include/infoWriters.h>
#include <ItvDeviceSdk/include/IJoystick.h>
#include "../ItvSdkUtil/ItvSdkUtil.h"
#include "../../mmss/DeviceInfo/include/PropertyContainers.h"

#include "../Grabber/Grabber.h"
#include <CommonNotificationCpp/StateControlImpl.h>

#include "CAsyncChannel.h"
#include "IIPManager3.h"
#include "ParamContext.h"
#include "DeviceInformation.h"

namespace NCorbaHelpers
{
    class CResourceSet;
}

namespace IPINT30
{

class CDevice;

// �� ���� IoPanel ��� ���������� ������ � ����.
class CIoPanel :
    public CAsyncChannelHandlerImpl<ITV8::GDRV::IIODeviceHandler>,
    public IPINT30::IIOPanel,
    public IObjectParamContext,
    public IDeviceInformationProvider,
    public ITV8::GDRV::IJoystickStateNotify,
    public ITV8::GDRV::IDigitalOutputHandler,
    public ITV8::GDRV::IDigitalInputHandler,
    public ITV8::Analytics::IEventFactory
{
public:
    CIoPanel(DECLARE_LOGGER_ARG, const SIoDeviceParam& settings, bool blockingConfiguration, NExecutors::PDynamicThreadPool dynExec,
        const std::string& eventChannel, boost::shared_ptr<IPINT30::IIpintDevice> parent, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
        const char* objectContext, NCorbaHelpers::IContainerNamed* container);

    //IPINT30::IIOPanel implementation
    virtual void SetRelayState(unsigned short contact, EIOState state);
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IIODeviceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IIODeviceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IJoystickStateNotify)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDigitalOutputHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDigitalInputHandler)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventFactory)
    ITV8_END_CONTRACT_MAP()

    //ITV8::GDRV::IDigitalInputHandler implementation
public:
    virtual void OnInputSignalLevelChanged(ITV8::GDRV::IIODevice* source, uint32_t inputNumber,
        const char* label, ITV8::timestamp_t timestamp, float level, float min, float max);

    //ITV8::GDRV::IDigitalOutputHandler implementation
public:
    virtual void OnRelayStateChanged(ITV8::GDRV::IIODevice* source, uint32_t relayNumber, ITV8::bool_t state);

    //ITV8::GDRV::IIODeviceHandler implementation
public:
    virtual void Alarmed(ITV8::GDRV::IIODevice* source, ITV8::uint32_t rayNumber, ITV8::bool_t status);
    
    //ITV8::GDRV::IJoystickStateNotify implementation
public:
    virtual void StateChanged(unsigned char* stateMask, int32_t maskSize, ITV8::GDRV::IAxisEnumerator* axisEnumerator);
    
    //ITV8::IEventHandler implementation
public:
    virtual void Failed(ITV8::IContract* pSource, ITV8::hresult_t error);

    //ITV8::IEventFactory implementation
public:
    ITV8::Analytics::IDetectorEventRaiser* BeginOccasionalEventRaising(ITV8::Analytics::IDetector* sender, 
        const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase) override;

    // TODO:
    ITV8::Analytics::IDetectorEventRaiser* BeginPeriodicalEventRaising(ITV8::Analytics::IDetector* /*sender*/,
        const char* /*metadataType*/, ITV8::timestamp_t /*time*/) override { return nullptr; }

    //IPINT30::IObjectParamContext implementation
public:
    void SwitchEnabled() override;
    void ApplyChanges(ITV8::IAsyncActionHandler* handler) override;
    void ApplyMetaChanges(const NStructuredConfig::TCustomParameters& meta) override;
    IParamContextIterator* GetParamIterator() override;

// Overrides public virtual methods of CChannel class
public:
    std::string ToString() const;

// Overrides virtual methods of CChannel class
protected:
    virtual void DoStart();

    virtual void DoStop();

// Overrides virtual methods of CChannel template
protected:
    virtual void OnStopped();
    virtual void OnFinalized();
    virtual void OnEnabled();
    virtual void OnDisabled();

private:
    void Prepare();
    void PrepareForTheRay(const SRayParam& rayParam, const std::string& isoTime);

    void changeRelayNormalState(const std::string& accessPoint, int normalState);

    bool getRayNormalState(int contact) const;

    void Notify(const std::string& accessPoint, NMMSS::EIpDeviceState state,
        const std::string& isoTime = std::string());

    void callbackAbsenceHandling(ITV8::int32_t rayNumber);

    typedef boost::shared_ptr<ITV8::GDRV::IIODevice> IIODevicePtr;
    IIODevicePtr m_ioDevice;

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
    void initializeDetectorEventFactory(NCorbaHelpers::PContainerNamed container, const std::string& originId);

private:
    const std::string m_eventChannel;
    SIoDeviceParam m_settings;
    bool m_blockingConfiguration;
    ITVSDKUTILES::IEventFactoryPtr m_eventFactory;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;

    std::map<int, std::pair<boost::shared_ptr<NMMSS::IGrabberCallback>, std::string>> m_rayCallbacks;

    typedef std::map<int, NMMSS::EIOAlarmState> TAlarmedRays;
    typedef TAlarmedRays::iterator TAlarmedRaysIterator;
    TAlarmedRays m_alarmedRays;

    boost::shared_ptr<CParamContext> m_context;

    typedef std::set<std::string> TAccessPointList;
    typedef TAccessPointList::iterator TAccessPointIterator;
    TAccessPointList m_accessPoints;

    typedef std::map<std::string, boost::shared_ptr<NCorbaHelpers::IResource> > TResourceMap;
    TResourceMap m_resources;

    const std::string m_accessPoint;
};
typedef NCorbaHelpers::CAutoPtr<CIoPanel> CIoPanelPtr;


}//namespace IPINT30

#endif // DEVICEIPINT3_CIOPANEL_H
