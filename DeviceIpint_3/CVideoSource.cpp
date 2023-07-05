#include <ItvSdkWrapper.h>
#include "CVideoSource.h"
#include "CIpInt30.h"
#include "Notify.h"
#include "Utility.h"
#include "CAdaptiveSource.h"
#include "../MMClient/MMClient.h"
#include "../MMTransport/SourceFactory.h"
#include "../MMTransport/MMTransport.h"

#include <InfraServer_IDL/LicenseChecker.h>

namespace
{
    const char* const VIDEO_CHANNEL_FORMAT = "VideoChannel.%d";
    
    // The name of runtime config context for video stream where:
    // "VideoStream.VideoChannel.id:VideoStreamIndex" see the comments in struct SVideoChannelParam.
    const char* const VIDEO_STREAM_FORMAT = "VideoStream.%d:%d";

    constexpr const char* USE_FOR_GREEN_STREAM = "useForGreenStream";

    class CDefaultIpintQoSPolicy : public NMMSS::CDefaultQoSPolicy< NMMSS::CDefaultAugmentationPolicy >
    {
        std::mutex m_sourcesLock;
        std::list<NMMSS::IQoSAwareSource*> m_sources;
        bool m_ignorePeriod = true;
        bool m_needPrevKeyFrame = false;
    public:
        CDefaultIpintQoSPolicy(bool needPrevKeyFrame)
            : m_needPrevKeyFrame(needPrevKeyFrame)
        {
        }

        AugmentedSourceConfiguration PrepareConfiguration(MMSS::QualityOfService const& qos) override
        {
            auto qosCopy = qos;
            if (!m_ignorePeriod)
            {
                auto rate = NMMSS::GetRequest<MMSS::QoSRequest::FrameRate>(qosCopy);
                if (rate != nullptr && rate->fps != 0 && rate->preroll) // Decimation should be made after decoding.
                    rate->preroll = false;
            }

            auto result = NMMSS::CDefaultQoSPolicy< NMMSS::CDefaultAugmentationPolicy >::PrepareConfiguration(qosCopy);
            if (m_needPrevKeyFrame)
            {
                // To force distributor copy buffer to attached source.
                result.start = NMMSS::EStartFrom::PrevKeyFrame;
            }
            return result;
        }
        void SubscribePolicyChanged(NMMSS::IQoSAwareSource* source) override
        {
            std::lock_guard<std::mutex> lock(m_sourcesLock);
            m_sources.push_back(source);
        }
        void UnsubscribePolicyChanged(NMMSS::IQoSAwareSource* source) override
        {
            std::lock_guard<std::mutex> lock(m_sourcesLock);
            m_sources.remove(source);
        }
        void NotifySourcesToUpdate(bool ignorePeriod, NExecutors::PDynamicThreadPool dynExec)
        {
            if (m_ignorePeriod != ignorePeriod)
            {
                m_ignorePeriod = ignorePeriod;
                std::lock_guard<std::mutex> lock(m_sourcesLock);
                for (const auto source : m_sources)
                {
                    NMMSS::PQoSAwareSource sourseHolder(source, NCorbaHelpers::ShareOwnership());
                    if (!dynExec->Post([sourseHolder]() { sourseHolder->ReprocessQoS(); }))
                        break;
                }
            }
        }
    };

    bool isKeyFrame(NMMSS::ISample* s)
    {
        return !(s->Header().eFlags & (NMMSS::SMediaSampleHeader::EFNeedKeyFrame
                                    |  NMMSS::SMediaSampleHeader::EFNeedPreviousFrame
                                    |  NMMSS::SMediaSampleHeader::EFNeedInitData));
    }

    enum EKeyFrameStreamCheckType : int
    {
        DontNeedToCheck = -1,
        NeedToCheck = 0,
        FirstSampleIsKeyFrame = 1,
        TwoLeadingSamplesAreKeyFrame = 2,
        ThreeLeadingSamplesAreKeyFrame = 3,
        NonKeyFrameFound = 4
    };
}

namespace IPINT30
{

CVideoSource::CVideoSource(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
    const SVideoChannelParam &channelSettings, const SVideoStreamingParam &streamingSettings, size_t videoChannelsCount,
    EStreamType streamType, boost::shared_ptr<NMMSS::IGrabberCallback> callback, const char* objectContext, NCorbaHelpers::IContainerNamed* container,
    bool breakUnusedConnection, bool useVideoBuffersWithSavingPrevKeyFrame, PAdaptiveSourceFactory adaptiveSourceFactory)
    : WithLogger(GET_LOGGER_PTR)
    , CAsyncChannelHandlerImpl<ITV8::GDRV::IVideoSourceHandler>(GET_LOGGER_PTR, dynExec, parent, 0)
    , CSourceImpl(GET_LOGGER_PTR, ToVideoAccessPoint(channelSettings.id, (int)streamType, objectContext), container, breakUnusedConnection)
    , m_settings(channelSettings)
    , m_streamSettings(streamingSettings)
    , m_videoChannelsCount(videoChannelsCount)
    , m_isOrigin(streamType == EStreamType::STRegistrationStream)
    , m_callback(callback)
    , m_objectContext(objectContext)
    , m_container(container)
    , m_accessPoint(ToVideoAccessPoint(m_settings.id, (int)streamType))
    , m_context(new CParamContext)
    , m_streamType(streamType)
    , m_sampleSeqChecker(new CSampleSequenceChecker(GET_LOGGER_PTR, ToString()))
    , m_asyncPushSinkHelper(GET_LOGGER_PTR, NCorbaHelpers::GetReactorFromPool(), boost::bind(&CVideoSource::DoProcessFrame, this, _1))
    , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
    , m_timerLicense(m_reactor->GetIO())
    , m_useVideoBuffersWithSavingPrevKeyFrame(useVideoBuffersWithSavingPrevKeyFrame)
    , m_adaptiveSourceFactory(adaptiveSourceFactory)
    , m_syncPoint(std::make_shared<CTimeCorrectorSyncPoint>())
{
    const std::string channel(boost::str(boost::format(VIDEO_CHANNEL_FORMAT) % m_settings.id));
    m_context->AddContext(channel.c_str(), MakeContext(m_settings));

    const std::string stream(boost::str(boost::format(VIDEO_STREAM_FORMAT) % m_settings.id % (int)streamType));

    TCustomProperties properties;

    RegisterProperty<int>(properties, "id", "int",
        boost::bind(&CVideoSource::GetId, this),
        boost::bind(&CVideoSource::SetId, this, _1));

    m_context->AddContext(stream.c_str(), MakeContext(m_streamSettings, properties));

    m_asyncPushSinkHelper.Activate(true);

    CSourceImpl::SetTimeCorrector(CreateTimeCorrector(true, m_syncPoint));
    CSourceImpl::setStreamTypeChangedCallback(std::bind(&CVideoSource::onStreamTypeChanged, this));
}

int CVideoSource::GetChannelId() const
{
    return m_settings.id;
}

bool CVideoSource::IsChannelEnabled() const
{
    return m_settings.enabled;
}

int CVideoSource::GetId() const
{
    return m_streamSettings.id;
}

void CVideoSource::SetId(int newVal)
{
    if (m_streamSettings.id != newVal)
    {
        m_streamSettings.id = newVal;
        SetEnabled(false);
    }
}

void CVideoSource::OnStopped()
{
    m_videoSource.reset();
}

void CVideoSource::OnFinalized()
{
    SetEnabled(false);
    m_asyncPushSinkHelper.Activate(false);
}

void CVideoSource::OnEnabled()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
        return;

    boost::mutex::scoped_lock lock(m_switchMutex);

    int factor = m_useVideoBuffersWithSavingPrevKeyFrame ? 2 : 1;
    auto const limits = NMMSS::SBufferLimits(
        factor * 1024,                      // 1024 number of samples (twice for rare key frame)
        factor * 64 * 1024 * 1024,          // 64 MiB (twice for rare key frame)
        std::chrono::seconds(0),            // start from last key sample
        std::chrono::seconds(factor * 30)); // store no more than 30 seconds (twice for rare key frame)

    m_qosPolicy =
        NCorbaHelpers::MakeRefcounted<CDefaultIpintQoSPolicy>(m_useVideoBuffersWithSavingPrevKeyFrame);
    NMMSS::PSourceFactory factory(NMMSS::CreateQoSAwareSourceFactory(
        GET_LOGGER_PTR, this, m_qosPolicy.Get(), NMMSS::NAugment::GreedyDistributor{ limits }, this));
    PortableServer::Servant videoServant = 
        NMMSS::CreatePullSourceEndpoint(GET_LOGGER_PTR, cont.Get(), factory.Get());

    m_servant.reset(NCorbaHelpers::ActivateServant(cont.Get(), videoServant, m_accessPoint.c_str()));

    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        fullAccessPoint(), m_reactOnRegistrations));

    if (m_videoChannelsCount > 1 && m_isOrigin) // Only for ip server. For cameras see DeviceIpintApp.cpp
    {
        if (m_settings.enabled && m_streamSettings.enabled)
        {
            if (!m_dynExec->Post(boost::bind(&CVideoSource::licenseObserve, NCorbaHelpers::CAutoPtr<CVideoSource>(this, NCorbaHelpers::ShareOwnership()))))
                _err_ << ToString() << " Can't post licenseObserve() to thread pool!";
        }
    }

    if (m_adaptiveSourceFactory)
    {
        m_adaptiveSourceFactory->Enable(fullAccessPoint(), GetCustomParameter(USE_FOR_GREEN_STREAM, m_streamSettings.metaParams, true));
    }
}

void CVideoSource::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);

    if (m_adaptiveSourceFactory)
    {
        m_adaptiveSourceFactory->Disable(fullAccessPoint());
    }

    m_lease.reset();
    boost::system::error_code ignore;
    m_timerLicense.cancel(ignore);

    m_servant.reset();
    m_qosPolicy.Reset();
    this->SetNotifier(0);
}

void CVideoSource::BeginFinalization()
{
    auto needToNotifyAboutFictiveStart = false;
    boost::mutex::scoped_lock lock(m_switchMutex);
    if (!m_lease && m_doStartCalled && m_isOrigin)
    {
        boost::system::error_code ignore;
        m_timerLicense.cancel(ignore);
        needToNotifyAboutFictiveStart = true;
    }
    lock.unlock();

    if (needToNotifyAboutFictiveStart)
    {
        CChannel::RaiseChannelStarted();
    }

    CChannel::BeginFinalization();
}

bool CVideoSource::initializeVideoSource()
{
    if (m_videoSource.get())
    {
        return true;
    }

    try
    {
        ITV8::GDRV::IDevice* device = getDevice();
        m_videoSource =
            IVideoSourcePtr(device->CreateVideoSource(this, m_settings.id,
            m_streamSettings.id, getMultimediaFrameFactory()), ipint_destroyer<ITV8::GDRV::IVideoSource>());

        if (!m_videoSource.get())
        {
            std::string message = get_last_error_message(device);
            _err_ << ToString() << " Exception: Video source was not created. msg="
                << message << std::endl;
            return false;
        }
    }
    catch (const std::runtime_error& e)
    {
        _err_ << ToString() << " Exception: Couldn't getDevice(). msg=" << e.what() << std::endl;
        return false;
    }

    return true;
}

//Starts video source in separate thread
void CVideoSource::DoStart()
{
    if (!initializeVideoSource())
        return;

    boost::mutex::scoped_lock lock(m_switchMutex);
    m_doStartCalled = true;
    CChannel::DoStart();

    if (m_videoChannelsCount == 1 || !m_isOrigin)
        doStart();
    else // multichannel device
    {
        if (m_lease) // lease already acquired successfully, else do nothing because of doStart() will be call in licenseObserve
            doStart();
    }
}

//Starts video source in separate thread
void CVideoSource::DoStop()
{
    TRACE_BLOCK;

    boost::mutex::scoped_lock lock(m_switchMutex);

    m_doStartCalled = false;

    IVideoSourcePtr videoSource(m_videoSource);
    if (0 != videoSource.get())
    {
        WaitForApply();
        videoSource->Stop();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

//ITV8::GDRV::IVideoSourceHandler implementation
void CVideoSource::Failed(ITV8::IContract* source, ITV8::hresult_t error)
{
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

void CVideoSource::MultimediaFrameReceived(ITV8::GDRV::IVideoSource* pSource, ITV8::MFF::IMultimediaBuffer* pBuffer)
{
    m_asyncPushSinkHelper.Enqueue(pBuffer);
}

void CVideoSource::DoProcessFrame(ITV8::MFF::IMultimediaBuffer* pBuffer)
{
    bool discontinue = false;
    if (!GetFlag(cfSignal))
    {
        m_sampleSeqChecker->Reset();
        this->Notify(NMMSS::IPDS_SignalRestored);
        discontinue = true;
        if (m_streamType == STRegistrationStream)
        {
            m_parent->onChannelSignalRestored();
        }
    }

    const auto sampleId = m_sampleSeqChecker->Update(pBuffer->GetTimeStamp());

    SetFlag(cfSignal, true);

    try
    {
        auto pSampleContainer = ITV8::contract_cast<ITVSDKUTILES::ISampleContainer>(pBuffer);
        NMMSS::PSample sample(pSampleContainer->Detach());
        pBuffer->Destroy();

        auto mediaType = sample->Header().nSubtype;
        NMMSS::SetSampleId(sample.Get(), sampleId);

        if (m_qosPolicy)
        {
            if (mediaType != m_lastMediaType)
            {
                m_lastMediaType = mediaType;
                m_keyFrameCounter = NeedToCheck;
            }

            if (m_keyFrameCounter != DontNeedToCheck)
            {
                if (isKeyFrame(sample.Get()))
                    m_keyFrameCounter++;
                else
                    m_keyFrameCounter = NonKeyFrameFound;
            }

            if (m_keyFrameCounter == ThreeLeadingSamplesAreKeyFrame || m_keyFrameCounter == NonKeyFrameFound)
            {
                bool ignorePeriod = m_keyFrameCounter == 4; // Non-key frame had appeared, decimation should be disabled.
                _dbg_ << ToString() << " The 'FrameRate' QoS requests will be " << (ignorePeriod ? "ignored" : "processed");
                dynamic_cast<CDefaultIpintQoSPolicy*>(m_qosPolicy.Get())->NotifySourcesToUpdate(ignorePeriod, m_dynExec);

                m_keyFrameCounter = DontNeedToCheck;
            }
        }

        PushToSink(sample, discontinue);
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << " Error in PushToSink: " << e.what();
    }
    catch (...)
    {
        _err_ << ToString() << " Unknown error in PushToSink.";
        throw;
    }
}

void CVideoSource::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (!handler)
    {
        _log_ << ToString() << " IDynamicParametersHandler doesn't exist";
        return;
    }

    // If source is not active than we should not do any call to driver.
    if (!GetFlag(cfStarted))
    {
        _log_ << ToString() << " video source is not started";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    if (!m_videoSource)
    {
        _log_ << ToString() << " video source doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_videoSource.get());
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_videoSource.get(), ITV8::EUnsupportedCommand);
}

std::string CVideoSource::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "videoSource:" << m_settings.id << ':' << m_streamType;
    return stream.str();
}

void CVideoSource::SwitchEnabled()
{
    SetEnabled(m_settings.enabled != 0 && m_streamSettings.enabled != 0);
}

void CVideoSource::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (GetFlag(cfEnvironmentReady) && m_videoSource.get())
    {
        auto blockConfiguration = m_parent->BlockConfiguration();
        if (m_reactOnRegistrations && m_blockApplyFromStart.load())
        {
            m_blockApplyFromStart.exchange(false);
            blockConfiguration = true;
        }
        
        m_videoSource->SetBlockConfigurating(blockConfiguration);

        ITV8::IAsyncAdjuster* asyncAj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_videoSource.get());
        if (asyncAj)
        {
            //Sets properties of video source.
            if (m_streamType == STRegistrationStream)
                SetValues(asyncAj, m_settings.publicParams);
            SetValues(asyncAj, m_streamSettings.publicParams);

            //Applies new settings.
            _dbg_ << ToString() << " CVideoSource::ApplyChanges " << handler << " : " << static_cast<ITV8::IAsyncActionHandler*>(this);
            ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() << " couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped.";
        }
    }
}

IParamContextIterator* CVideoSource::GetParamIterator()
{
    return m_context.get();
}

std::string CVideoSource::ToString() const
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
    str << "\\" << ".video:"<< m_settings.id << ":" << (int)m_streamType;
    return str.str();
}

bool CVideoSource::IsOrigin() const
{
    return m_isOrigin;
}

PTimeCorrectorSyncPoint CVideoSource::GetSyncPoint()
{
    return m_syncPoint;
}

void CVideoSource::onStreamTypeChanged()
{
    this->Notify(NMMSS::IPDS_DeviceConfigurationChanged);
}

void CVideoSource::setTimerLicense()
{
    m_timerLicense.expires_from_now(boost::posix_time::seconds(60));
    m_timerLicense.async_wait(boost::bind(&CVideoSource::licenseObserve, NCorbaHelpers::CAutoPtr<CVideoSource>(this, NCorbaHelpers::ShareOwnership()), _1));
}

void CVideoSource::doStart()
{
    if (m_applyFromStart.test_and_set())
    {
        m_blockApplyFromStart.exchange(true);
    }

    ApplyChanges(0);
    m_videoSource->Start();
}

void CVideoSource::licenseObserve(const boost::system::error_code& error)
{
    if (error)
    {
        _err_ << ToString() << " CVideoSource::licenseObserve: timer error: " << error.message();
        return;
    }

    if (!m_dynExec->Post(boost::bind(&CVideoSource::licenseObserve, NCorbaHelpers::CAutoPtr<CVideoSource>(this, NCorbaHelpers::ShareOwnership()))))
        _err_ << ToString() << " Can't post licenseObserve() to thread pool!";
}

void CVideoSource::licenseObserve()
{
    boost::mutex::scoped_lock lock(m_switchMutex);

    NCorbaHelpers::PContainer cont = m_container;
    if (!cont)
        return;

    using namespace NLicenseService;
    PLicenseChecker lc = GetLicenseChecker(cont.Get());

    std::string leaseName = fullAccessPoint();

    try
    {
        m_lease.reset(lc->Acquire("DeviceNVR16", leaseName));
    }
    catch (const InfraServer::LicenseService::LicenseFailed& /*e*/)
    {
        _dbg_ << ToString() << " no free leases for DeviceNVR16";
    }
    catch (const InfraServer::LicenseService::InvalidArgument& /*e*/) {}

    if (!m_lease)
    {
        try
        {
            m_lease.reset(lc->Acquire("DeviceIpint", leaseName));
        }
        catch (const InfraServer::LicenseService::LicenseFailed& /*e*/)
        {
            _err_ << ToString() << " No free leases for DeviceIpint";
            setTimerLicense(); 
            return;
        }
        catch (const InfraServer::LicenseService::InvalidArgument& /*e*/)
        {
            _err_ << ToString() << " Wrong key";
            return;
        }
    }

    if (m_doStartCalled)
        doStart();
}

std::string CVideoSource::fullAccessPoint() const
{
    return m_objectContext + "/" + m_accessPoint;
}

}
