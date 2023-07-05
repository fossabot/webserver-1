#include <ace/OS.h>
#include <ItvSdkWrapper.h>
#include "AudioDestination.h"
#include "CIpInt30.h"
#include "sdkHelpers.h"
#include "Notify.h"
#include "Utility.h"
#include "../ConnectionBroker.h"
#include "../ItvSdkUtil/ItvSdkUtil.h"
#include "../ItvSdkUtil/CFrameFactory.h"
#include "../MMCoding/Transforms.h"
#include "../ItvSdkUtil/CAudioG7xxBuffer.h"
#include <ItvMediaSdk/include/codecConstants.h>
#include <boost/foreach.hpp>
#include <boost/function.hpp>

#include <boost/nondet_random.hpp>
#include <boost/scope_exit.hpp>

#include <MMIDL/SinkEndpointC.h>
#include <MMIDL/SinkEndpointS.h>

#include "SinkEndpointImpl.h"

namespace
{
    const char* const AUDIO_DESTINATION = "AudioDestination.%d";
    const char* const SPEAKER_FORMAT = "Speaker.%d";
    const char* const SINK_ENDPOINT_FORMAT = "SinkEndpoint.%d";

    const char* const AUDIO_CODEC_TAG = "audioCodec";
    const char* const BITRATE_TAG = "bitRate";
    const char* const ENCODING_TAG = "encoding";
    const char* const CODEC_ALGORITHM_TAG = "algorithm";

    const unsigned short DATA_TIMEOUT = 5;
    const uint32_t MAX_REQUEST_COUNT = 100u;
}

namespace IPINT30
{

typedef MMSS::SinkEndpoint::XBadConnecton XBadConnecton;
typedef MMSS::SinkEndpoint::XConnectFailed XConnectFailed;
typedef MMSS::SinkEndpoint::XSinkIsBusy XSinkIsBusy;

class SinkEndpointServant : public POA_MMSS::SinkEndpoint
{
public:
    SinkEndpointServant(NCorbaHelpers::PContainerNamed container, NMMSS::IPullStyleSink* sink) :
        m_container(container),
        m_sink(sink, NCorbaHelpers::ShareOwnership()),
        m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
        m_impl(m_reactor->GetIO())
    {
        INIT_LOGGER_HOLDER_FROM_CONTAINER(container);
    }

    ~SinkEndpointServant()
    {
        _log_ << "Destroying  SinkEndpointServant";
    }

    virtual ::CORBA::Long KeepAliveMilliseconds() 
    { 
        return static_cast< ::CORBA::Long >(KEEP_ALIVE_TIME.total_milliseconds()); 
    }

    virtual ::CORBA::Long ConnectByObjectRef(
        ::MMSS::Endpoint_ptr objref, CORBA::Long priority)
    {
        try
        {
            return m_impl.create( [this,objref](){
                return NMMSS::CreatePullConnectionByObjref(
                    GET_LOGGER_PTR, objref, m_sink.Get(), MMSS::EAUTO);
                }, priority );
        }
        catch (const SinkEndpointImpl::XInvalidOperation& e)
        {
            _err_ << "SinkEndpointServant::ConnectByObjectRef failed. Error: " << e.what();
            throw XSinkIsBusy();
        } 
    }

    virtual ::CORBA::Long ConnectByNameRef(const char * nsref, CORBA::Long priority)
    {
        try
        {
            return m_impl.create( [this,nsref](){
                return NMMSS::CreatePullConnectionByNsref(
                    GET_LOGGER_PTR, nsref, m_container->GetRootNC(), m_sink.Get(), MMSS::EAUTO);
                }, priority );
        }
        catch (const SinkEndpointImpl::XInvalidOperation& e)
        {
            _err_ << "SinkEndpointServant::ConnectByNameRef failed. Error: " << e.what();
            throw XSinkIsBusy();
        }
    }

    virtual ::CORBA::Boolean isBusy(CORBA::Long priority)
    {
        return m_impl.isBusy(priority);
    }

    virtual void KeepAlive(::CORBA::Long connectionHandle)
    {
        try
        {
        	m_impl.keepAlive(connectionHandle);
        }
        catch (const SinkEndpointImpl::XInvalidOperation& e)
        {
            _err_ << "SinkEndpointServant::KeepAlive failed. Connection handle:" << connectionHandle
            << " Error: " << e.what();
        	throw XBadConnecton();
        }
    }

    virtual void Disconnect(::CORBA::Long connectionHandle)
    {
        try
        {
            m_impl.destroy(connectionHandle);
        }
        catch (const SinkEndpointImpl::XInvalidOperation& e)
        {
            _err_ << "SinkEndpointServant::Disconnect failed. Connection handle:" << connectionHandle
                << " Error: " << e.what();
            throw XBadConnecton();
        }
    }

private:
    DECLARE_LOGGER_HOLDER;
    NCorbaHelpers::PContainerNamed m_container;
    NCorbaHelpers::CAutoPtr<NMMSS::IPullStyleSink> m_sink;
    NCorbaHelpers::PReactor m_reactor;
    SinkEndpointImpl m_impl;
};

CLoudSpeaker::CLoudSpeaker(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
    const SAudioDestinationParam& params, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
    const char* objectContext, NCorbaHelpers::IContainerNamed* container)
    :WithLogger(GET_LOGGER_PTR),
    CChannel(GET_LOGGER_PTR, dynExec, parent, 0),
    CAsyncHandlerBase(GET_LOGGER_PTR),
    m_callback(callback),
    m_objectContext(objectContext),
    m_container(container),
    m_accessPoint(boost::str(boost::format(AUDIO_DESTINATION)%params.id)),
    m_bitrate(0),
    m_connection(0),
    m_context(new CParamContext),
    m_dataFlowEstablished(false),
    m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
    m_timeoutTimer(m_reactor->GetIO()),
    m_dataReceived(false),
    m_params(params),
    m_operationInProgress(false),
    m_callAccepted(false),
    m_debt(0),
    m_stopCalled(false)
{
    const std::string speaker(boost::str(boost::format(SPEAKER_FORMAT)%m_params.id));
    m_context->AddContext(speaker.c_str(), MakeContext(m_params));
}

void CLoudSpeaker::OnStopped()
{
    m_speaker.reset();
}

void CLoudSpeaker::OnFinalized()
{
    SetEnabled(false);
}

void CLoudSpeaker::InitCodec()
{
    int bitrate = 0;
    std::string codec, encoding;
    GetEncoderParams(m_params, codec, encoding, bitrate);
    if(m_codec==codec && m_encoding==encoding && m_bitrate==bitrate)
        return;

    DestroyCodec();
    if (ITV8_AUDIO_CODEC_G711 == codec)
    {
        if (encoding.empty())
        {
            _err_ << ToString() << " Encoding parameter must be set for G.711 audio codec";
            return;
        }

        if (ITV8_G7XX_ENCODING_ULAW == encoding)
            m_codecFilter = NMMSS::CreateAudioEncoderFilter_G711_U(GET_LOGGER_PTR);
        else if (ITV8_G7XX_ENCODING_ALAW == encoding)
            m_codecFilter = NMMSS::CreateAudioEncoderFilter_G711_A(GET_LOGGER_PTR);
        else
        {
            _err_ << ToString() << " G.711 audio codec doesn't support '" << encoding << "' encoding";
            return;
        }
    }
    else if (ITV8_AUDIO_CODEC_G726 == codec)
    {
        if (0 == bitrate)
        {
            _err_ << ToString() << " Bitrate must be set for G.726 audio codec";
            return;
        }
        m_codecFilter = NMMSS::CreateAudioEncoderFilter_G726(GET_LOGGER_PTR, bitrate);
    }
    else if (ITV8_AUDIO_CODEC_AAC == codec)
    {
        m_codecFilter = NMMSS::CreateAudioEncoderFilter_AAC(GET_LOGGER_PTR, 0);
    }
    else if (ITV8_AUDIO_CODEC_MP2 == codec)
    {
        m_codecFilter = NMMSS::CreateAudioEncoderFilter_MP2(GET_LOGGER_PTR, 0);
    }
    else
    {
        _err_ << ToString() << " Unsupported codec type";
        return;
    }

    if(NCorbaHelpers::PContainerNamed cont = m_container)
    {
        PortableServer::ServantBase_var sinkEnpointServant(
            new SinkEndpointServant(cont, m_codecFilter->GetSink()));
        m_sinkEnpointPublication.reset(NCorbaHelpers::ActivateServant(cont.Get(), sinkEnpointServant._retn(), 
            str(boost::format(SINK_ENDPOINT_FORMAT) % m_params.id).c_str()));
    }
    else
    {
        _err_ << ToString() << " Critical error! Can't acquire NamedContainer!";
    }

 
    m_connection = NMMSS::GetConnectionBroker()->SetConnection(this, m_codecFilter->GetSource(), GET_LOGGER_PTR);

    m_codec=codec;
    m_encoding=encoding;
    m_bitrate=bitrate;
}
void CLoudSpeaker::OnEnabled()
{
    {
        boost::mutex::scoped_lock lock(m_notifyMutex);
        m_notifier.reset(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
            NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
            std::string(m_objectContext + "/" 
            + str(boost::format(SINK_ENDPOINT_FORMAT) % m_params.id))));
    }

    {
        boost::mutex::scoped_lock lock(m_codecMutex);
        InitCodec();
    }
}
void CLoudSpeaker::DestroyCodec()
{
    if (0 != m_connection)
    {
        NMMSS::GetConnectionBroker()->DestroyConnection(m_connection);
        m_connection = 0;
    }
    m_sinkEnpointPublication.reset();
    m_codecFilter.Reset();
    clearState();
}
void CLoudSpeaker::OnDisabled()
{
    {
        boost::mutex::scoped_lock lock(m_codecMutex);
        DestroyCodec();
    }

    boost::mutex::scoped_lock lock(m_notifyMutex);
    if (0 != m_notifier.get())
        m_notifier->Notify(NMMSS::IPDS_SignalLost);
    m_notifier.reset();
}

void CLoudSpeaker::onConnected(boost::mutex::scoped_lock& lock)
{
    try
    {
        m_debt = MAX_REQUEST_COUNT;
        requestNextSamples(lock, m_debt, false);
    }
    catch (const std::exception& ex)
    {
        _err_ << ToString() << " Failed to request samples for AudioDestination: std::exception - " << ex.what();
    }

    SetSinkConnected(true);
}

void CLoudSpeaker::onDisconnected(boost::mutex::scoped_lock& lock)
{
    m_timeoutTimer.cancel();
    m_debt = 0;
    SetSinkConnected(false);
}

void CLoudSpeaker::Receive(NMMSS::ISample* pSample)
{
    if (0 == m_speaker.get() || m_stopCalled)
        return;

    {
        boost::mutex::scoped_lock dataLock(m_dataMutex);
        if (!m_dataFlowEstablished)
        {
            m_dataFlowEstablished = true;

            AddRef();
            m_timeoutTimer.expires_from_now(boost::posix_time::seconds(DATA_TIMEOUT));
            m_timeoutTimer.async_wait(boost::bind(&CLoudSpeaker::handle_timer, this,
                boost::asio::placeholders::error)); // It's wrong

            dataLock.unlock();

            {
                boost::mutex::scoped_lock callLock(m_callMutex);
                WaitAsyncOperationCompletion(callLock);
                m_operationInProgress = true;
            }

            _dbg_ << ToString() << " make call";
            m_speaker->Call();
        }
    }

    bool started = GetFlag(cfStarted);

    boost::mutex::scoped_lock lock(mutex());

    m_dataReceived = true;

    NMMSS::PSample tempSample(pSample, NCorbaHelpers::ShareOwnership());
    m_bufferedSamples.push_back(tempSample);
    m_debt = (m_debt > 0) ? (m_debt - 1) : 0;
    uint32_t reqCount = 0;
    if (m_debt < MAX_REQUEST_COUNT)
    {
        reqCount = MAX_REQUEST_COUNT - m_debt;
        m_debt = MAX_REQUEST_COUNT;
    }

    if (started)
    {
        for (const auto& item : m_bufferedSamples)
        {
            PushSample(item.Get());
        }
        m_bufferedSamples.clear();
    }

    try
    {
        requestNextSamples(lock, reqCount, false);
    }
    catch (const std::exception& ex)
    {
        _err_ << ToString() << " Failed to request samples for AudioDestination: std::exception - " << ex.what();
    }
}

bool CLoudSpeaker::initializeAudioDestination()
{
    ITV8::GDRV::IDevice *device = 0;
    try
    {
        device = getDevice();
    }
    catch (const std::runtime_error& e)
    {
        _err_ << ToString() << " Exception: Couldn't getDevice(). Message: " << e.what();
        return false;
    }

    m_speaker = IAudioDestinationPtr(device->CreateAudioDestination(this, m_params.id),
        ipint_destroyer<ITV8::GDRV::IAudioDestination>());

    if (!m_speaker.get())
    {
        std::string message = get_last_error_message(device);
        _err_ << ToString() << " Exception: ITV8::GDRV::IDevice::CreateAudioDestination(this,"
            << m_params.id << ") return 0. Message: " << message;
        return false;
    }
    return true;
}


void CLoudSpeaker::DoStart()
{
    _dbg_ << ToString() << " DoStart";
    if (0 == m_speaker.get() && !initializeAudioDestination())
    {
        return;
    }

    ApplyChanges(0);

    CChannel::DoStart();
    CChannel::RaiseChannelStarted();

    {
        boost::mutex::scoped_lock lock(m_notifyMutex);
        if (0 != m_notifier.get())
            m_notifier->Notify(NMMSS::IPDS_SignalRestored);
    }
}

//Stops in separate thread
void CLoudSpeaker::DoStop()
{
    _dbg_ << ToString() << " DoStop";
    m_stopCalled = true;
    IAudioDestinationPtr speaker(m_speaker);
    if (0 != speaker.get())
    {
        {
            boost::mutex::scoped_lock callLock(m_callMutex);
            if (!m_callAccepted)
                WaitAsyncOperationCompletion(callLock);
            if (!m_callAccepted)
            {
                CChannel::RaiseChannelStopped();
                return;
            }
            m_operationInProgress = true;
        }
        WaitForApply();

        _dbg_ << ToString() << " make hangup";
        speaker->HangUp();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

std::string CLoudSpeaker::ToString() const
{
    std::ostringstream str;
    if (!m_parent)
    {
        str << "DeviceIpint.Unknown";
    }
    else
    {
        str << m_parent->ToString();
    }
    str << ".speaker:" << m_params.id;
    return str.str();
}

void CLoudSpeaker::CallAccepted(ITV8::GDRV::IAudioDestination*)
{
    _log_ << ToString() << " Call accepted.";
    SetFlag(cfStarted, true);

    boost::mutex::scoped_lock callLock(m_callMutex);
    m_operationInProgress = false;
    m_callAccepted = true;
    m_callCondition.notify_all();

    callLock.unlock();
}

void CLoudSpeaker::HungUp(ITV8::GDRV::IAudioDestination*)
{
    {
        boost::mutex::scoped_lock dataLock(m_dataMutex);
        m_dataFlowEstablished = false;
    }

    _log_ << ToString() << " Hung up.";
    SetFlag(cfStarted, false);

    {
        boost::mutex::scoped_lock callLock(m_callMutex);
        m_operationInProgress = false;
        m_callAccepted = false;
        m_callCondition.notify_all();
    }

    CChannel::RaiseChannelStopped();
}

void CLoudSpeaker::ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code)
{
    if(ITV8::ENotError != code)
    {
        _err_ << ToString() << " ITV8::IAsyncAdjuster::ApplySettings(); return err:"
            << code << "; msg: " << get_last_error_message(source, code);

        boost::mutex::scoped_lock lock(m_notifyMutex);
        if (0 != m_notifier.get())
            m_notifier->Notify(NMMSS::IPDS_AcceptSettingsFailure);
    }
    else
    {
        _inf_ << ToString() <<" Async operation completed.";
    }
}

void CLoudSpeaker::Failed(ITV8::IContract* source, ITV8::hresult_t errorCode)
{
    std::string message = ITV8::get_last_error_message(source, errorCode);
    _log_ << ToString() << " Failed, err:" << message;

    CChannel::RaiseChannelStarted();
    CChannel::RaiseChannelStopped();

    NMMSS::EIpDeviceState st;
    switch(errorCode)
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

    boost::mutex::scoped_lock lock(m_notifyMutex);
    if (0 != m_notifier.get())
        m_notifier->Notify(st);
}

std::string CLoudSpeaker::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "audioDestination:" << m_params.id;
    return stream.str();
}

void CLoudSpeaker::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (!handler)
    {
        _log_ << ToString() << " IDynamicParametersHandler doesn't exist";
        return;
    }

    if (!m_speaker && !initializeAudioDestination())
    {
        _log_ << ToString() << " audio destination doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_speaker.get());
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_speaker.get(), ITV8::EUnsupportedCommand);
}

void CLoudSpeaker::SwitchEnabled()
{
    SetEnabled(m_params.enabled);
}

void CLoudSpeaker::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (GetFlag(cfEnvironmentReady) && (0 != m_speaker.get()))
    {
        ITV8::IAsyncAdjuster* asyncAj =
            ITV8::contract_cast<ITV8::IAsyncAdjuster>(static_cast<ITV8::GDRV::IDeviceChannel*>(m_speaker.get()));
        if(asyncAj)
        {
            SetBlockConfiguration(asyncAj, m_parent->BlockConfiguration());
            //Sets properties of speaker.
            SetValues(asyncAj, m_params.publicParams);

            //Applies new settings.
            ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() <<" couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped.";
        }
    }
    boost::mutex::scoped_lock lock(m_codecMutex);
    InitCodec();
}

IParamContextIterator* CLoudSpeaker::GetParamIterator()
{
    return m_context.get();
}

void CLoudSpeaker::handle_timer(const boost::system::error_code& error)
{
    _dbg_ << ToString() << " handle timer, err - " << error;
    if (!error)
    {
        bool started = GetFlag(cfStarted);

        boost::mutex::scoped_lock lock(mutex());
        bool dataReceived = m_dataReceived;
        if (!started)
        {
            m_bufferedSamples.clear();
        }
        else
        {
            m_dataReceived = false;
            if (!dataReceived)
            {
                IAudioDestinationPtr speaker(m_speaker);
                if (0 != speaker.get())
                {
                    {
                        boost::mutex::scoped_lock callLock(m_callMutex);
                        if (!m_callAccepted)
                            WaitAsyncOperationCompletion(callLock);
                        m_operationInProgress = true;
                    }

                    _dbg_ << ToString() << " make hangup";
                    speaker->HangUp();
                }
                Release();
                return;
            }
        }
        lock.unlock();

        m_timeoutTimer.expires_from_now(boost::posix_time::seconds(DATA_TIMEOUT));
        m_timeoutTimer.async_wait(boost::bind(&CLoudSpeaker::handle_timer, this,
            boost::asio::placeholders::error));
    }
    else
    {
        Release();
    }
}

void CLoudSpeaker::PushSample(NMMSS::ISample* pSample)
{
    ITV8::MFF::IMultimediaBuffer* mmBuffer = CreateFrameFromSample(GET_LOGGER_PTR, pSample, m_codec.c_str());
    if (0 == mmBuffer)
    {
        _log_ << ToString()<< " Cannot convert multimedia sample to IPINT30 multimedia buffer";
    }
    else
    {
        ITV8::MFF::IAudioBuffer* audioBuffer = ITV8::contract_cast<ITV8::MFF::IAudioBuffer>(mmBuffer);
        if (0 == audioBuffer)
        {
            _log_ << ToString() << " Multimedia buffer doesn't contain audio data";
        }
        else
        {
            if (0 != m_speaker.get())
            {
                //удалено по договоренности с ITV
                //CAudioG7xxBuffer *pG7xxBuffer = (CAudioG7xxBuffer*) dynamic_cast<CAudioG7xxBuffer*>(mmBuffer);
                //if(pG7xxBuffer) pG7xxBuffer->ConvertToNetworkBitOrder();
                m_speaker->PushAudioFrame(audioBuffer);
            }
        }
    }
}

void CLoudSpeaker::GetEncoderParams(const SAudioDestinationParam& settings, std::string& codec,
                                    std::string& encoding, int& bitrate)
{
    BOOST_FOREACH(const NStructuredConfig::TCustomParameters::value_type& param, settings.publicParams)
    {
        if(BITRATE_TAG == param.name)
            bitrate = atoi(param.ValueUtf8().c_str());
        else if(ENCODING_TAG == param.name)
            encoding.assign(param.ValueUtf8());
        else if (CODEC_ALGORITHM_TAG == param.name)
            codec.assign(param.ValueUtf8());
    }
    BOOST_FOREACH(const NStructuredConfig::TCustomParameters::value_type& param, settings.privateParams)
    {
        if (CODEC_ALGORITHM_TAG == param.name)
            codec.assign(param.ValueUtf8());
    }

    _inf_ << ToString() << " GetEncoderParams: codec = " << codec << ", encoding = " << encoding << ", bitrate = " << bitrate;
}

void CLoudSpeaker::WaitAsyncOperationCompletion(boost::mutex::scoped_lock& lock)
{
    while (m_operationInProgress)
        m_callCondition.wait(lock);
}

void CLoudSpeaker::clearState()
{
    m_codec.clear();
    m_encoding.clear();
    m_bitrate = 0;
}

}
