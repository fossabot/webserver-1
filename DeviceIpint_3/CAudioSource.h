#ifndef DEVICEIPINT3_CAUDIOSOURCE_H
#define DEVICEIPINT3_CAUDIOSOURCE_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Resource.h>

#include "IIPManager3.h"
#include "PullStyleSourceImpl.h"
#include "CAsyncChannel.h"
#include "ParamContext.h"

#include "AsyncPushSinkHelper.h"
#include "AsyncActionHandler.h"
#include "DeviceInformation.h"

namespace IPINT30
{
class CDevice;
class CSampleSequenceChecker;

class CAudioSource : 
    public CAsyncChannelHandlerImpl<ITV8::GDRV::IAudioSourceHandler>,
    public CSourceImpl,
    public IObjectParamContext,
    public IDeviceInformationProvider
{
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IAudioSourceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IAudioSourceHandler)
    ITV8_END_CONTRACT_MAP()

	CAudioSource(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
        const SMicrophoneParam& micSettings, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
        const char* objectContext, NCorbaHelpers::IContainerNamed* container, bool breakUnusedConnection);

	//ITV8::GDRV::IAudioSourceHandler
public:
	virtual void Failed(ITV8::IContract* source, ITV8::hresult_t error);
    virtual void MultimediaFrameReceived(ITV8::GDRV::IAudioSource* pSource,
        ITV8::MFF::IMultimediaBuffer* pBuffer);

public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();

//Overrides public virtual methods of CChannel class
public:
    virtual std::string ToString() const;

//Overrides protected virtual methods of CChannel class
protected:
    virtual void DoStart();

    virtual void DoStop();

    virtual void OnStopped();
    virtual void OnFinalized();

    virtual void OnEnabled();
    virtual void OnDisabled();

    virtual void SetSinkConnected(bool conn)
    {
        CChannel::SetSinkConnected(conn);
    }
    virtual ITV8::ILogger* GetLogger(){ return 0; }

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);
    bool initializeAudioSource();

private:
	// Audio source settings.
	SMicrophoneParam m_micSettings;
    typedef boost::shared_ptr<ITV8::GDRV::IAudioSource> IAudioSourcePtr;
    IAudioSourcePtr m_audioSource;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;
    NCorbaHelpers::PResource m_servant;

    std::unique_ptr<CSampleSequenceChecker> m_sampleSeqChecker;

    std::string m_accessPoint;

    boost::shared_ptr<CParamContext> m_context;

    CAsyncPushSinkHelper m_asyncPushSinkHelper;
    void DoProcessFrame(ITV8::MFF::IMultimediaBuffer* pBuffer);
};
}

#endif // DEVICEIPINT3_CAUDIOSOURCE_H