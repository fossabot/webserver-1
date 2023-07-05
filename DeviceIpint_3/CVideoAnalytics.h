#ifndef DEVICEIPINT3_CVIDEOANALYTICS_H
#define DEVICEIPINT3_CVIDEOANALYTICS_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>
#include <ItvFramework/VisualPrimitives.h>

#include "CAsyncChannel.h"
#include "ParamContext.h"

#include "AsyncActionHandler.h"
#include "DeviceInformation.h"

namespace IPINT30
{

// Device class predefinition
class CDevice;

class CVideoAnalytics :
    public CAsyncChannelHandlerImpl<ITV8::GDRV::IVideoAnalyticsHandler>,
    public IObjectParamContext,
    public IDeviceInformationProvider
{
public:
    CVideoAnalytics(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
        int videoChannel, const SEmbeddedDetectorSettings &settings, boost::shared_ptr<NMMSS::IGrabberCallback> callback, const char* objectContext, 
        const char* eventChannel, NCorbaHelpers::IContainerNamed* container, PTimeCorrectorSyncPoint syncPoint);

    inline const std::string& getType() const
    {
        return m_settings.name;
    }

    inline std::string getId() const
    {
        auto it = std::find_if(m_settings.privateParams.begin(), m_settings.privateParams.end(),
            [](const NStructuredConfig::SCustomParameter& p){ return p.name.compare("DisplayId") == 0; });
        return it == m_settings.privateParams.end() ? std::to_string(0) : it->ValueUtf8();
    }

    inline int getVideoChannelId() const
    {
        return m_videoChannel;
    }

    //ITV8::IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::Analytics::IEventFactory)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IAsyncDeviceChannelHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IVideoAnalyticsHandler)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventFactory)
        ITV8_CONTRACT_ENTRY(ITV8::IEventHandler)
    ITV8_END_CONTRACT_MAP()

// ITV8::Analytics::IEventFactory implementation
public:
    virtual ITV8::Analytics::IDetectorEventRaiser* BeginOccasionalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase);

    virtual ITV8::Analytics::IDetectorEventRaiser* BeginPeriodicalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time);

    virtual void Failed(ITV8::IContract* pSource, ITV8::hresult_t error);

// IObjectParamContext implementation
public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual void ApplyMetaChanges(const NStructuredConfig::TCustomParameters& params);
    virtual IParamContextIterator* GetParamIterator();

public:
    //Gets text description of the video source.
    virtual std::string ToString() const;

    //Overrides virtual methods of CChannel class
protected:
    virtual void DoStart();

    virtual void DoStop();

    virtual void OnFinalized();

    virtual void OnStopped();

    virtual void OnEnabled();

    virtual void OnDisabled();

private:
    virtual void SignalLost(ITV8::IContract* source, ITV8::hresult_t errorCode);

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
	// Sets to adjuster new the name-value pares from params.
    void SetVisualElementsValues(ITV8::Analytics::IVisualAsyncAdjuster *adjuster, const NStructuredConfig::TVisualElementSettings& params);
    void ResetVisualElements(ITV8::Analytics::IVisualAsyncAdjuster *adjuster, const NStructuredConfig::TVisualElementSettings& params);

	ITV8::VisualPrimitives::TProperties deviceParams2properties(const TDetectorParams& params);

    typedef boost::shared_ptr<ITV8::GDRV::IVideoAnalytics> IVideoAnalyticsPtr;
    
    // The driver video source.
    IVideoAnalyticsPtr m_analytics;

    // Video source channel number.
    int m_videoChannel;

    // Video analytics settings.
    SEmbeddedDetectorSettings m_settings;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;

    // Specifies the stream identifier from which the video source grabs data.
    unsigned int m_streamId;

    std::string m_accessPointSuffix;

    // The name (access_point) of event channel connector.
    std::string m_eventChannel;

    boost::shared_ptr<CParamContext> m_context;

    boost::shared_ptr<ITimedEventFactory> m_eventFactory;

    typedef std::set<std::string> TVisualElementPropertyNames;
    typedef std::map<std::string, TVisualElementPropertyNames> TVisualElementsCache;
    TVisualElementsCache m_veCache;

    void ClearVisualElementsCache(ITV8::Analytics::IVisualAsyncAdjuster* adjuster, const TVisualElementsCache& cache);

    typedef boost::shared_ptr<ITimedEventRaiser> TTimedEventRaiser;

    typedef std::map<std::string, TTimedEventRaiser> TEventRaisers;
    typedef TEventRaisers::iterator TEventRaisersIterator;
    TEventRaisers m_timedEventRaisers;
    boost::mutex m_raiserMutex;

    typedef std::map<std::string, boost::posix_time::ptime> TEventTimes;
    typedef TEventTimes::iterator TEventTimesIterator;
    TEventTimes m_eventStartTimes;
    boost::mutex m_timeMutex;

    NCorbaHelpers::PReactor m_reactor;
    TimeCorrectorUP m_eventTimeCorrector;
    NMMSS::PDetectorEventFactory m_factory;
    std::vector<std::string> m_maskedEventSessions;
};

}

#endif // DEVICEIPINT3_CVIDEOANALYTICS_H