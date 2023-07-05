#include <ItvSdkWrapper.h>
#include "CAudioSource.h"
#include "CIpInt30.h"
#include "Notify.h"
#include "Utility.h"
#include "../MMClient/MMClient.h"
#include "../MMTransport/SourceFactory.h"
#include "../MMTransport/MMTransport.h"
#include "../ItvSdkUtil/CAudioG7xxBuffer.h"

#include "ParamContext.h"

namespace
{
const std::string AUDIO_SOURCE_ENDPOINT("SourceEndpoint.audio:%d");
const char* const AUDIO_CHANNEL_FORMAT = "Microphone.%d";
}

namespace IPINT30
{
//TODO: Rewrite class like the CVideoSource
CAudioSource::CAudioSource(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
    const SMicrophoneParam& micSettings, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
    const char* objectContext, NCorbaHelpers::IContainerNamed* container, bool breakUnusedConnection)
    : WithLogger(GET_LOGGER_PTR)
    , CAsyncChannelHandlerImpl<ITV8::GDRV::IAudioSourceHandler>(GET_LOGGER_PTR, dynExec, parent, 0)
    , CSourceImpl(GET_LOGGER_PTR, boost::str(boost::format(objectContext + ("/" + AUDIO_SOURCE_ENDPOINT)) % micSettings.id), container, breakUnusedConnection)
    , m_micSettings(micSettings)
    , m_callback(callback)
    , m_objectContext(objectContext)
    , m_container(container)
    , m_sampleSeqChecker(new CSampleSequenceChecker(GET_LOGGER_PTR, ToString()))
    , m_accessPoint(boost::str(boost::format(AUDIO_SOURCE_ENDPOINT)%micSettings.id))
    , m_context(new CParamContext)
    , m_asyncPushSinkHelper(GET_LOGGER_PTR, NCorbaHelpers::GetReactorFromPool(), boost::bind(&CAudioSource::DoProcessFrame, this, _1))
{
    const std::string channel(boost::str(boost::format(AUDIO_CHANNEL_FORMAT)%micSettings.id));
    m_context->AddContext(channel.c_str(), MakeContext(m_micSettings));
    m_asyncPushSinkHelper.Activate(true);
    CSourceImpl::SetTimeCorrector(CreateTimeCorrector(true, PTimeCorrectorSyncPoint()));
}

void CAudioSource::OnStopped()
{
    m_audioSource.reset();
}

void CAudioSource::OnFinalized()
{
    SetEnabled(false);
    m_asyncPushSinkHelper.Activate(false);
}

void CAudioSource::OnEnabled()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    boost::mutex::scoped_lock lock(m_switchMutex);

    NMMSS::PSourceFactory factory(
        NMMSS::CreateDefaultSourceFactory(GET_LOGGER_PTR, this, NMMSS::NAugment::UnbufferedDistributor{}, this));
    PortableServer::Servant audioServant = NMMSS::CreatePullSourceEndpoint(GET_LOGGER_PTR, cont.Get(), factory.Get());
    m_servant.reset(NCorbaHelpers::ActivateServant(cont.Get(), audioServant, m_accessPoint.c_str()));

    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        std::string(m_objectContext + "/" + m_accessPoint), m_reactOnRegistrations));
}

void CAudioSource::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    m_servant.reset();
    this->SetNotifier(0);
}

//Starts audio source in separate thread
void CAudioSource::DoStart()
{
    if (!initializeAudioSource())
    {
        return;
    }

    if (m_applyFromStart.test_and_set())
    {
        m_blockApplyFromStart.exchange(true);
    }

    ApplyChanges(0);
    m_audioSource->Start();
    CChannel::DoStart();
}

//Stops audio source in separate thread
void CAudioSource::DoStop()
{
    IAudioSourcePtr audioSource(m_audioSource);
    if(0 != audioSource.get())
    {
        WaitForApply();
        audioSource->Stop();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

void CAudioSource::Failed(ITV8::IContract* source, ITV8::hresult_t error)
{
    std::string message = get_last_error_message(source, error);
    _err_ << ToString() << " Failed. It's unexpected event, err:" << message;

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

void CAudioSource::MultimediaFrameReceived(ITV8::GDRV::IAudioSource* pSource,
    ITV8::MFF::IMultimediaBuffer* pBuffer)
{
    m_asyncPushSinkHelper.Enqueue(pBuffer);
}

void CAudioSource::DoProcessFrame(ITV8::MFF::IMultimediaBuffer* pBuffer)
{
    bool discontinue = false;
    if (!GetFlag(cfSignal))
    {
        m_sampleSeqChecker->Reset();
        this->Notify(NMMSS::IPDS_SignalRestored);
        discontinue = true;
    }

    m_sampleSeqChecker->Update(pBuffer->GetTimeStamp());

    SetFlag(cfSignal, true);
    try
    {
        //удалено по договоренности с ITV
        //CAudioG7xxBuffer *pG7xxBuffer = (CAudioG7xxBuffer*) dynamic_cast<CAudioG7xxBuffer*>(pBuffer);
        //if(pG7xxBuffer) pG7xxBuffer->ConvertToHostBitOrder();

        auto pSampleContainer = ITV8::contract_cast<ITVSDKUTILES::ISampleContainer>(pBuffer);
        NMMSS::PSample sample(pSampleContainer->Detach());
        pBuffer->Destroy();

        PushToSink(sample, discontinue);
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << "Error in PushToSink: " << e.what();
    }
    catch(...)
    {
        _err_ << ToString() << "Unknown error in PushToSink.";
        throw;
    }

}

std::string CAudioSource::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "audioSource:" << m_micSettings.id;
    return stream.str();
}

void CAudioSource::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
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

    if (!m_audioSource)
    {
        _log_ << ToString() << " audio source doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_audioSource.get());
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_audioSource.get(), ITV8::EUnsupportedCommand);
}

bool CAudioSource::initializeAudioSource()
{
    if (m_audioSource.get())
    {
        return true;
    }

    try
    {
        ITV8::GDRV::IDevice* device = getDevice();
        m_audioSource = IAudioSourcePtr(
            device->CreateAudioSource(this, m_micSettings.id, getMultimediaFrameFactory()),
            ipint_destroyer<ITV8::GDRV::IAudioSource>());

        if (!m_audioSource.get())
        {
            _err_ << ToString() << " Audio source was not created. msg="<< get_last_error_message(device);
            return false;
        }
    }
    catch (const std::runtime_error& e)
    {
        _err_ << ToString() << __FUNCTION__ << " std::runtime_error: " << e.what();
        return false;
    }

    return true;
}

void CAudioSource::SwitchEnabled()
{
    SetEnabled(m_micSettings.enabled);
}

void CAudioSource::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (GetFlag(cfEnvironmentReady) && m_audioSource.get())
    {
        ITV8::IAsyncAdjuster* asyncAj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_audioSource.get());
        if (!asyncAj)
        {
            _wrn_ << ToString() << " couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped.";
            return;
        }

        auto blockConfiguration = m_parent->BlockConfiguration();
        if (m_reactOnRegistrations && m_blockApplyFromStart.load())
        {
            m_blockApplyFromStart.exchange(false);
            blockConfiguration = true;
        }

        SetBlockConfiguration(asyncAj, blockConfiguration);

        //Sets properties of audio source.
        SetValues(asyncAj, m_micSettings.publicParams);

        //Applies new settings.
        ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
    }
}

IParamContextIterator* CAudioSource::GetParamIterator()
{
    return m_context.get();
}

std::string CAudioSource::ToString() const
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
    str << "\\" << ".audio:"<< m_micSettings.id;
    return str.str();
}

}
