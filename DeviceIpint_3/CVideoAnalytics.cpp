#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <future>

#include "../ItvSdkUtil/ItvSdkUtil.h"
#include "../ItvSdkUtil/CDetectorEventRaiser.h"
#include "../ItvSdkUtil/CDetectorEventFactory.h"
#include "../Grabber/Grabber.h"
#include "../MMClient/MMClient.h"
#include "../MMClient/DetectorEventFactory.h"
#include <CommonNotificationCpp/StateControlImpl.h>
#include "CIpInt30.h"
#include "CVideoAnalytics.h"
#include "CVisualElementContext.h"
#include "Notify.h"
#include "TimeStampHelpers.h"
#include "Utility.h"

#include <ItvFramework/VisualPrimitives.h>
#include <ItvFramework/VisualPrimitivesFactory.h>

namespace
{
    const char* const DETECTOR_EVENT_SUPPLIER_ID = "analytics:%s:%s:%s";
    const char* const ANALYTICS_CATEGORY_FORMAT = "VideoChannel.%d/VideoAnalytics.%s.%s";
    const char* const VISUAL_ELEMENT_GROUP_PREFIX_FORMAT = "VideoChannel.%d/VideoAnalytics.%s.%s/VisualElement.";

    // minimal duration of the event
    const boost::posix_time::time_duration EVENT_DURATION = boost::posix_time::milliseconds(3000);

    struct FakeVideoAnalytics : public ITV8::GDRV::IVideoAnalytics
    {
        ITV8_BEGIN_CONTRACT_MAP()
            ITV8_CONTRACT_ENTRY(ITV8::GDRV::IVideoAnalytics)
        ITV8_END_CONTRACT_MAP()

        FakeVideoAnalytics(ITV8::GDRV::IVideoAnalyticsHandler* handler) : m_handler(handler) {}
        void Start() override
        {
            std::async(std::bind(&ITV8::GDRV::IAsyncDeviceChannelHandler::Started, m_handler, this));
        }
        void Stop() override
        {
            std::async(std::bind(&ITV8::GDRV::IAsyncDeviceChannelHandler::Stopped, m_handler, this));
        }
        void Destroy() override
        {
            delete this;
        }
        ITV8::GDRV::IVideoAnalyticsHandler* m_handler;
    };

}

namespace IPINT30
{
CVideoAnalytics::CVideoAnalytics(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
    int videoChannel, const SEmbeddedDetectorSettings &settings, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
    const char* objectContext, const char* eventChannel, NCorbaHelpers::IContainerNamed* container, PTimeCorrectorSyncPoint syncPoint)
    : WithLogger(GET_LOGGER_PTR),
    CAsyncChannelHandlerImpl<ITV8::GDRV::IVideoAnalyticsHandler>(GET_LOGGER_PTR, dynExec, parent, 0),
    m_videoChannel(videoChannel),
    m_settings(settings),
    m_callback(callback),
    m_objectContext(objectContext),
    m_container(container),
    m_accessPointSuffix(boost::str(boost::format(DETECTOR_EVENT_SUPPLIER_ID)%m_videoChannel%getId()%settings.name)),
    m_eventChannel(eventChannel),
    m_context(new CParamContext),
    m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
    m_eventTimeCorrector(CreateTimeCorrector(false, syncPoint))
{
    SetFlag(cfSinkConnected, true);
    CLONE_LOGGER;
    ADD_LOG_PREFIX(ToString()+" ");
    INIT_LOGGER_HOLDER;

    auto detectorId = getId();
    const auto analytics(boost::str(boost::format(ANALYTICS_CATEGORY_FORMAT) % videoChannel % m_settings.name % detectorId));
    boost::shared_ptr<IPINT30::IParamsContext> dc(new CDetectorContext(m_settings));
    m_context->AddContext(analytics.c_str(), dc);

    const auto visualElements(boost::str(boost::format(VISUAL_ELEMENT_GROUP_PREFIX_FORMAT) % videoChannel % m_settings.name % detectorId));
    boost::shared_ptr<IPINT30::IParamsContext> context(new CVisualElementContext(m_settings));
    m_context->AddContext(visualElements.c_str(), context);
}

ITV8::Analytics::IDetectorEventRaiser* CVideoAnalytics::BeginOccasionalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)
{
    m_eventTimeCorrector->SetTimestamps(ipintTimestampToPtime(time), [&time, this](bpt::ptime time_, bool)
        {
            _dbg_ << "CVideoAnalytics::BeginOccasionalEventRaising, event time: "
                  << ipintTimestampToIsoString(time) << " -> " << boost::posix_time::to_iso_string(time_);
            time = toIpintTime(time_);
        });
          
    boost::shared_ptr<ITimedEventFactory> eventFactory = m_eventFactory;
    if(!eventFactory)
    {
        throw std::runtime_error((boost::format("m_eventFactory for %1% was not initialized.")
            %ToString()).str());
    }
    if (ITV8::Analytics::esEnd == phase)
    {
        boost::posix_time::ptime eventStartTime = boost::posix_time::min_date_time;
        {
            boost::mutex::scoped_lock timeLock(m_timeMutex);
            TEventTimesIterator it = m_eventStartTimes.find(name);
            if (m_eventStartTimes.end() != it)
                eventStartTime = it->second;
        }

        boost::posix_time::time_duration td = boost::posix_time::microsec_clock::universal_time() - eventStartTime;
        if (EVENT_DURATION > td)
        {
            boost::mutex::scoped_lock raiserLock(m_raiserMutex);
            TEventRaisersIterator it = m_timedEventRaisers.find(name);
            if (m_timedEventRaisers.end() == it)
            {
                TTimedEventRaiser ter(eventFactory->BeginTimedEventRaising(sender, name, time, phase, m_reactor->GetIO()));
                m_timedEventRaisers.insert(std::make_pair(name, ter));
                ter->SetCommitTime(EVENT_DURATION - td);
                return ter.get();
            }
            else if (it->second->DelayCommit(EVENT_DURATION - td))
            {
                return eventFactory->BeginNoOpEventRaising(name, time);
            }
            return it->second.get();
        }
        else
        {
            {
                boost::mutex::scoped_lock raiserLock(m_raiserMutex);
                TEventRaisersIterator it = m_timedEventRaisers.find(name);
                if ((m_timedEventRaisers.end() != it) && (0 != it->second.get()))
                {
                    it->second->Stop();
                    m_timedEventRaisers.erase(it);
                    return eventFactory->BeginNoOpEventRaising(name, time);
                }
            }
            return eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
        }
    }
    else if (ITV8::Analytics::esBegin == phase)
    {
        {
            boost::mutex::scoped_lock timeLock(m_timeMutex);
            m_eventStartTimes[name] = boost::posix_time::microsec_clock::universal_time();
        }
        {
            boost::mutex::scoped_lock raiserLock(m_raiserMutex);
            TEventRaisersIterator it = m_timedEventRaisers.find(name);
            if (m_timedEventRaisers.end() != it)
            {
                if ((0 != it->second.get()) && it->second->Prolongate())
                {
                    return eventFactory->BeginNoOpEventRaising(name, time);
                }
                m_timedEventRaisers.erase(it);
            }
        }
        return eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
    }
    // esMomentary?
    return eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
}

ITV8::Analytics::IDetectorEventRaiser* CVideoAnalytics::BeginPeriodicalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time)
{
    m_eventTimeCorrector->SetTimestamps(ipintTimestampToPtime(time), [&time, this](bpt::ptime time_, bool)
    {
        _dbg_ << "CVideoAnalytics::BeginPeriodicalEventRaising, event time: "
            << ipintTimestampToIsoString(time) << " -> " << boost::posix_time::to_iso_string(time_);
        time = toIpintTime(time_);
    });
    boost::shared_ptr<ITimedEventFactory> eventFactory = m_eventFactory;
    if(!eventFactory)
    {
        throw std::runtime_error((boost::format("m_eventFactory for %1% was not initialized.")
            %ToString()).str()) ;
    }
    return eventFactory->BeginPeriodicalEventRaising(sender, metadataType, time);
}

//ITV8::GDRV::IEventHandler implementation
void CVideoAnalytics::Failed(ITV8::IContract* source, ITV8::hresult_t error)
{
    TRACE_BLOCK;
    std::string message = get_last_error_message(source, error);
    _log_ << ToString() << " Failed. It's unexpected event, err:" << message;

    CChannel::RaiseChannelStarted();
    CChannel::RaiseChannelStopped();

    NMMSS::EIpDeviceState st;
    switch(error)
    {
    case ITV8::EAuthorizationFailed:
        st = NMMSS::IPDS_AuthorizationFailed;
        break;
    case ITV8::EDeviceReboot:
        st = NMMSS::IPDS_Rebooted;
        break;
    case ITV8::EHostUnreachable:
    case ITV8::ENetworkDown:
        st = NMMSS::IPDS_NetworkFailure;
        break;
    case ITV8::EGeneralConnectionError:
        st = NMMSS::IPDS_ConnectionError;
        break;
    default:
        st = NMMSS::IPDS_IpintInternalFailure;
        break;
    }

    this->Notify(st);
}

void CVideoAnalytics::OnFinalized()
{
    SetEnabled(false);
}

void CVideoAnalytics::OnEnabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        std::string(m_objectContext + "/EventSupplier." + m_accessPointSuffix)));
}

void CVideoAnalytics::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    this->SetNotifier(0);
}

std::string CVideoAnalytics::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "detector:" << m_videoChannel << ":" << getId() << ':' << m_settings.name;
    return stream.str();
}

void CVideoAnalytics::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
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

    if (!m_analytics)
    {
        _log_ << ToString() << " video analytics doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_analytics.get());
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_analytics.get(), ITV8::EUnsupportedCommand);
}

//Starts video analytics in separate thread
void CVideoAnalytics::DoStart()
{
    TRACE_BLOCK;

    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    // Reset previous instance of m_analytics because it can reference to invalid m_eventFactory.
    m_analytics.reset();
    ITV8::GDRV::IDevice *device = 0;
    try
    {
        device = getDevice();
    }
    catch (const std::runtime_error& e)
    {
        _err_ << " Exception: Couldn't getDevice(). msg=" <<e.what()<<std::endl;
        return;
    }
    
    std::string videoSource = ToVideoAccessPoint(m_videoChannel, 0, m_objectContext);

    try
    {
        m_factory = 
            NMMSS::CreateDetectorEventFactory((boost::format("%1%/EventSupplier.%2%") % m_objectContext % m_accessPointSuffix).str().c_str(), videoSource.c_str(),
            cont.Get(), m_eventChannel.c_str(), m_accessPointSuffix.c_str());

        m_eventFactory = 
            ITVSDKUTILES::CreateEventFactory(GET_THIS_LOGGER_PTR, m_factory, ToString().c_str());

        auto analytics = device->CreateVideoAnalytics(this, m_videoChannel, m_settings.name.c_str());
        m_analytics = IVideoAnalyticsPtr(analytics ? analytics : new FakeVideoAnalytics(this), ipint_destroyer<ITV8::GDRV::IVideoAnalytics>());
        if (nullptr == analytics)
        {
            std::string message = get_last_error_message(device);
            _err_ << "Exception: Video analytics was not created. msg="
                << message << std::endl << "; detector_id: " << m_settings.name.c_str();

            CChannel::RaiseChannelStarted();
            CChannel::RaiseChannelStopped();
            return;
        }
    }
    catch (const CORBA::Exception& ex)
    {
        _err_ << "Corba Exception occured on CreateDetectorEventFactory " << ex 
            << " Object context:" << m_objectContext.c_str() << " m_accessPointSuffix:" << m_accessPointSuffix.c_str() << " videoSource:" << videoSource.c_str();
        return;
    }
    catch (const std::exception& e)
    {
        _err_ << " std::exception: occured on CreateDetectorEventFactory. msg=" << e.what()
            << " Object context:" << m_objectContext.c_str() << " m_accessPointSuffix:" << m_accessPointSuffix.c_str() << " videoSource:" << videoSource.c_str();
        return;
    }

    ApplyChanges(0);

    _log_ << "Start " << m_videoChannel << ":" << getId() << ", is alive=" << m_analytics.get();
    m_analytics->Start();

    this->Notify(NMMSS::IPDS_SignalRestored); // because ipint driver do not call the Started method inspite of this is CAsyncChannel<T>. It is wrong abstraction, isn't it?

    CChannel::DoStart();
}

//Starts video analytics in separate thread
void CVideoAnalytics::DoStop()
{
    TRACE_BLOCK;

    _log_ << "Stop " << m_videoChannel << ":" << getId() << ", is alive=" << m_analytics.get();

    IVideoAnalyticsPtr analytics(m_analytics);
    if(0 != analytics.get())
    {
        WaitForApply();

        ITV8::IAsyncAdjuster* asyncAj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_analytics.get());
        if(asyncAj)
        {
            ITV8::Analytics::IVisualAsyncAdjuster* asyncVisAj = 
                ITV8::contract_cast<ITV8::Analytics::IVisualAsyncAdjuster>(asyncAj);
            if(asyncVisAj)
            {
                //Reset visual elements
                ResetVisualElements(asyncVisAj, m_settings.visual_elements);
            }
        }

        analytics->Stop();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }

    this->Notify(NMMSS::IPDS_SignalLost); // because ipint driver do not call the Started method inspite of this is CAsyncChannel<T>. It is wrong abstraction, isn't it?
}

void CVideoAnalytics::OnStopped()
{
    TRACE_BLOCK;
    try
    {
        boost::mutex::scoped_lock raiserLock(m_raiserMutex);
        for (auto& raiser : m_timedEventRaisers)
            raiser.second->Stop();
        m_timedEventRaisers.clear();
        m_eventFactory.reset();
        m_factory.Reset();
    }
    catch (const CORBA::Exception &e)
    {
        _log_ << "CORBA exception occurred (" << e._name() << ") during m_eventFactory.reset()" 
            << std::endl;
    }
    m_analytics.reset();
}

void CVideoAnalytics::SwitchEnabled()
{
    SetEnabled(m_settings.enabled);
}

void CVideoAnalytics::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    TRACE_BLOCK;
    if (GetFlag(cfEnvironmentReady) && (0 != m_analytics))
    {
        ITV8::IAsyncAdjuster* asyncAj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_analytics.get());
        if(asyncAj)
        {
            //Sets properties of video analytics.
            SetValues(asyncAj, m_settings.publicParams);
            SetBlockConfiguration(asyncAj, m_parent->BlockConfiguration());

            ITV8::Analytics::IVisualAsyncAdjuster* asyncVisAj = 
                ITV8::contract_cast<ITV8::Analytics::IVisualAsyncAdjuster>(asyncAj);
            if(asyncVisAj)
            {
                //Set visual elements
                try
                {
                    SetVisualElementsValues(asyncVisAj, m_settings.visual_elements);
                }
                catch (const std::exception& e)
                {
                    _err_ << "SetVisualElementsValues returned error: " << e.what();
                }
            }

            //Applies new settings.
            _dbg_ << "CVideoAnalytics::ApplyChanges " << handler << " : " << static_cast<ITV8::IAsyncActionHandler*>(this);
            ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() <<" couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped." <<std::endl;
        }
    }
}

void CVideoAnalytics::ApplyMetaChanges(const NStructuredConfig::TCustomParameters& params)
{
    for (auto param : params)
    {
        if ("enabledMaskEvents" == param.name)
        {
            std::string sesisonId(param.ValueUtf8());
            if (m_factory)
                m_factory->EnableMotionMask(NCorbaHelpers::UuidFromString(param.ValueUtf8().c_str()), true);
            m_maskedEventSessions.push_back(sesisonId);
        }
        else if (std::find(m_maskedEventSessions.begin(), m_maskedEventSessions.end(), param.name) != m_maskedEventSessions.end())
        {
            if (m_factory)
                m_factory->EnableMotionMask(NCorbaHelpers::UuidFromString(param.name.c_str()), false);
            m_maskedEventSessions.erase(std::find(m_maskedEventSessions.begin(), m_maskedEventSessions.end(), param.name));
        }
    }
}

ITV8::VisualPrimitives::TProperties CVideoAnalytics::deviceParams2properties(const TDetectorParams& params)
{
    ITV8::VisualPrimitives::TProperties properties;
    for (const auto& param : params)
    {
        try
        {
            properties[param.name] = ITV8::to_any(param.Type(), param.ValueUtf8());
        }
        catch(const std::runtime_error& e)
        {
            _err_ << ToString() << " ITV8::to_any(" << param.Type() << ", " << param.ValueUtf8()
                << ") exception : " << e.what();
        }
    }
    return properties;
}

void CVideoAnalytics::SetVisualElementsValues(ITV8::Analytics::IVisualAsyncAdjuster* adjuster, 
                                  const NStructuredConfig::TVisualElementSettings& veParams)
{
    _dbg_ << "CVideoAnalytics::SetVisualElementsValues " << veParams.size();

    using namespace ITV8::VisualPrimitives;
    typedef std::map<std::string, std::list<Rectangle::Ptr> > RectanglesCollection;
    typedef std::map<std::string, std::list<Polyline::Ptr> > PolylinesCollection;

    ClearVisualElementsCache(adjuster, m_veCache);
    m_veCache.clear();

    RectanglesCollection rects;
    PolylinesCollection polylines;
    boost::optional< std::pair<std::string, Mask::Ptr> > mask;
    for (const auto& param : veParams)
    {
        TProperties properties = deviceParams2properties(param.publicParams);
        if (param.value.Type() == "rectangle")
        {
            Rectangle::Ptr p = VisualPrimitivesFactory::CreateRectangle(param.value.ValueUtf8(), properties);
            rects[param.value.name].push_back(p);

            m_veCache["rectangle"].insert(param.value.name);
        }
        else if (param.value.Type() == "polyline")
        {
            Polyline::Ptr p = VisualPrimitivesFactory::CreatePolyline(param.value.ValueUtf8(), properties);
            polylines[param.value.name].push_back(p);

            m_veCache["polyline"].insert(param.value.name);
        }
        else if (param.value.Type() == "imagemask")
        {
            mask = make_pair(param.value.name, VisualPrimitivesFactory::CreateMask(param.value.ValueUtf8(), properties));

            m_veCache["imagemask"].insert(param.value.name);
        }
        else
        {
            _err_ << "Unknown visual element:" << param.value.Type();
        }
    }
    _dbg_ << "m_veCache size " << m_veCache.size();

    _dbg_ << "IVisualAsyncAdjuster::SetRectangleArray " << rects.size();
    for(RectanglesCollection::const_iterator i = rects.begin(); i != rects.end(); ++i)
    {
        RectangleEnumerator rectEnum(i->second.begin(), i->second.end());
        for (auto& r : i->second)
            _dbg_ << "rect: " << i->first << ": " << r->ToString();

        adjuster->SetRectangleArray(i->first.c_str(), &rectEnum);
    }

    _dbg_ << "IVisualAsyncAdjuster::SetPolylineArray " << polylines.size();
    for(PolylinesCollection::const_iterator i = polylines.begin(); i != polylines.end(); ++i)
    {
        for (auto& p : i->second)
            _dbg_ << "poly: " << i->first << ": " << p->ToString();

        PolylineEnumerator polyEnum(i->second.begin(), i->second.end());
        adjuster->SetPolylineArray(i->first.c_str(), &polyEnum);
    }

    if(!!mask)
    {
        _dbg_ << "IVisualAsyncAdjuster::SetMask " << mask->first;
        adjuster->SetMask(mask->first.c_str(), mask->second.get());
    }
}

void CVideoAnalytics::ResetVisualElements(ITV8::Analytics::IVisualAsyncAdjuster *adjuster,
                                          const NStructuredConfig::TVisualElementSettings& veParams)
{
    _dbg_ << "CVideoAnalytics::ResetVisualElements " << veParams.size();
    using namespace ITV8::VisualPrimitives;
    typedef std::list<Rectangle::Ptr> RectanglesCollection;
    typedef std::list<Polyline::Ptr> PolylinesCollection;

    RectanglesCollection rects;
    PolylinesCollection polylines;
    Mask::Ptr mask;
    for (const auto& param : veParams)
    {
        TProperties properties = deviceParams2properties(param.publicParams);
        if (param.value.Type() == "rectangle")
        {
            RectangleEnumerator rectEnum(rects.begin(), rects.end());
            adjuster->SetRectangleArray(param.value.name.c_str(), &rectEnum);
        }
        else if (param.value.Type() == "polyline")
        {
            PolylineEnumerator polyEnum(polylines.begin(), polylines.end());
            adjuster->SetPolylineArray(param.value.name.c_str(), &polyEnum);
        }
        else if (param.value.Type() == "imagemask")
        {
            adjuster->SetMask(param.value.name.c_str(), mask.get());
        }
    }
}

void CVideoAnalytics::ClearVisualElementsCache(ITV8::Analytics::IVisualAsyncAdjuster* adjuster, const TVisualElementsCache& cache)
{
    _dbg_ << "CVideoAnalytics::ClearVisualElementsCache " << cache.size();

    using namespace ITV8::VisualPrimitives;
    typedef std::list<Rectangle::Ptr> RectanglesCollection;
    typedef std::list<Polyline::Ptr> PolylinesCollection;

    RectanglesCollection rects;
    PolylinesCollection polylines;
    Mask::Ptr mask;

    TVisualElementsCache::const_iterator it1 = cache.begin(), it2 = cache.end();
    for (; it1 != it2; ++it1)
    {
        if(it1->first == "rectangle")
        {
            _dbg_ << "IVisualAsyncAdjuster::ClearRectangleArray " << it1->second.size();
            TVisualElementPropertyNames::const_iterator pit1 = it1->second.begin(), pit2 = it1->second.end();
            for (; pit1 != pit2; ++pit1)
            {
                _dbg_ << "rect: " << *pit1;

                RectangleEnumerator rectEnum(rects.begin(), rects.end());
                adjuster->SetRectangleArray(pit1->c_str(), &rectEnum);
            }
        }
        else if(it1->first == "polyline")
        {
            _dbg_ << "IVisualAsyncAdjuster::ClearPolylineArray " << it1->second.size();
            TVisualElementPropertyNames::const_iterator pit1 = it1->second.begin(), pit2 = it1->second.end();
            for (; pit1 != pit2; ++pit1)
            {
                _dbg_ << "poly: " << *pit1;

                PolylineEnumerator polyEnum(polylines.begin(), polylines.end());
                adjuster->SetPolylineArray(pit1->c_str(), &polyEnum);
            }
        }
        else if(it1->first == "imagemask")
        {
            _dbg_ << "IVisualAsyncAdjuster::ClearMask " << it1->second.size();
            TVisualElementPropertyNames::const_iterator pit1 = it1->second.begin(), pit2 = it1->second.end();
            for (; pit1 != pit2; ++pit1)
                adjuster->SetMask(pit1->c_str(), mask.get());
        }
    }
}

IParamContextIterator* CVideoAnalytics::GetParamIterator()
{
    return m_context.get();
}

std::string CVideoAnalytics::ToString() const
{
    std::ostringstream str;
    str << (m_parent ? m_parent->ToString() : std::string("DeviceIpint.Unknown"))
        << "\\" << ".analytics:" << m_videoChannel << ":" << getId() << ":" << m_settings.name;
    return str.str();
}

void CVideoAnalytics::SignalLost(ITV8::IContract* source, ITV8::hresult_t errorCode)
{
    auto message = get_last_error_message(source, errorCode);
    _wrn_ << ToString() << " Signal lost. All active events will be commited.";

    boost::mutex::scoped_lock raiserLock(m_raiserMutex);
    for (auto& raiser : m_timedEventRaisers)
        raiser.second->Stop();
    m_timedEventRaisers.clear();
}

}
