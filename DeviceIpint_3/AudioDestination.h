#ifndef DEVICEIPINT3_AUDIO_DESTINATION_H
#define DEVICEIPINT3_AUDIO_DESTINATION_H

#include <boost/thread/condition.hpp>
#include <boost/asio/io_service.hpp>

#include "IIPManager3.h"
#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Reactor.h>
#include "../MMClient/MMClient.h"
#include "../PullStylePinsBaseImpl.h"

#include "CChannel.h"
#include "ParamContext.h"
#include "AsyncActionHandler.h"
#include "DeviceInformation.h"

namespace NMMSS
{
    struct SAllocatorRequirements;
}

namespace ITVSDKUTILES
{
    struct IFrameFromSampleFactory;
}

namespace IPINT30
{
class CDevice;

class CLoudSpeaker 
    : public CChannel,
      public CAsyncHandlerBase,
      public NMMSS::CPullStyleSinkBasePureRefcounted,
      public ITV8::GDRV::IAudioDestinationHandler,
      public IObjectParamContext,
      public IDeviceInformationProvider
{
public:
    CLoudSpeaker(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
        const SAudioDestinationParam& params, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
        const char* objectContext, NCorbaHelpers::IContainerNamed* container);
    
// NMMSS::IPushStyleSink, implementation
public:
	virtual void onConnected(boost::mutex::scoped_lock& lock);
    virtual void onDisconnected(boost::mutex::scoped_lock& lock);

    virtual void Receive(NMMSS::ISample*);

// ITV8::IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
	    ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IAudioDestinationHandler)
	    ITV8_CONTRACT_ENTRY(ITV8::GDRV::IAudioDestinationHandler)
    ITV8_END_CONTRACT_MAP()

// ITV8::GDRV::IAudioDestinationHandler implementation
public:
    virtual void CallAccepted(ITV8::GDRV::IAudioDestination* source);
    virtual void HungUp(ITV8::GDRV::IAudioDestination* source);

// ITV8::IEventHandler implementation
public:
    virtual void Failed(ITV8::IContract* pSource, ITV8::hresult_t error);

// IObjectParamContext implementation
public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();

// CAsyncHandlerBase implementation
public:
    virtual void ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code);

// Overrides public virtual methods of CChannel class
public:
    std::string ToString() const;

// Overrides virtual methods of CChannel class
protected:
    virtual void DoStart();

    virtual void DoStop();

    virtual void OnStopped();
    virtual void OnFinalized();

    virtual void  OnEnabled();

    virtual void  OnDisabled();

    virtual void handle_timer(const boost::system::error_code& error);

    virtual void InitCodec();
    virtual void DestroyCodec();

private:
    void PushSample(NMMSS::ISample*);
    void GetEncoderParams(const SAudioDestinationParam& settings, std::string& codec,
        std::string& encoding, int& bitrate);
    void WaitAsyncOperationCompletion(boost::mutex::scoped_lock& lock);
    void clearState();
    bool initializeAudioDestination();

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;

    std::string m_accessPoint;

    typedef boost::shared_ptr<ITV8::GDRV::IAudioDestination> IAudioDestinationPtr;
    IAudioDestinationPtr m_speaker;

    boost::mutex m_codecMutex;
    std::string m_codec, m_encoding;
    int m_bitrate;
    NMMSS::PPullFilter m_codecFilter;
    NMMSS::IConnectionBase* m_connection;

    boost::shared_ptr<CParamContext> m_context;

    boost::mutex m_dataMutex;
    bool m_dataFlowEstablished;

    typedef std::deque<NMMSS::PSample> TSampleQueue;
    TSampleQueue m_bufferedSamples;

    NCorbaHelpers::PReactor m_reactor;
    boost::asio::deadline_timer m_timeoutTimer;

    bool m_dataReceived;

    boost::mutex m_notifyMutex;
    boost::shared_ptr<INotifyState> m_notifier;
    SAudioDestinationParam m_params;

    boost::mutex m_callMutex;
    boost::condition m_callCondition;
    bool m_operationInProgress;
    bool m_callAccepted;
    uint32_t m_debt;
    std::atomic<bool> m_stopCalled;
    boost::scoped_ptr<NCorbaHelpers::IResource> m_sinkEnpointPublication;
};
}

#endif // DEVICEIPINT3_AUDIO_DESTINATION_H
