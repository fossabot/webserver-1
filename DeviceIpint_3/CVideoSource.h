#ifndef DEVICEIPINT3_CVIDEOSOURCE_H
#define DEVICEIPINT3_CVIDEOSOURCE_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.

#include <boost/asio.hpp>
#include <boost/thread/condition.hpp>
#include <boost/shared_ptr.hpp>
#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Resource.h>
#include <CorbaHelpers/Reactor.h>

#include "PullStyleSourceImpl.h"
#include "CAsyncChannel.h"
#include "ParamContext.h"

#include "AsyncPushSinkHelper.h"
#include "AsyncActionHandler.h"
#include "DeviceInformation.h"

#include "../MMTransport/QoSPolicyImpl.h"

namespace IPINT30
{

class CSampleSequenceChecker;
class CAdaptiveSourceFactory;
using PAdaptiveSourceFactory = NCorbaHelpers::CAutoPtr<CAdaptiveSourceFactory>;

class CVideoSource :
    public CAsyncChannelHandlerImpl<ITV8::GDRV::IVideoSourceHandler>,
    public CSourceImpl,
    public IObjectParamContext,
    public IDeviceInformationProvider
{
public:
    CVideoSource(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
        const SVideoChannelParam &channelSettings, const SVideoStreamingParam &streamingSettings, size_t videoChannelsCount,
        EStreamType streamType, boost::shared_ptr<NMMSS::IGrabberCallback> callback, const char* objectContext, NCorbaHelpers::IContainerNamed* container,
        bool breakUnusedConnection, bool useVideoBuffersWithSavingPrevKeyFrame, PAdaptiveSourceFactory adaptiveSourceFactory);

    int GetChannelId() const override;
    bool IsChannelEnabled() const override;
// ITV8::IContract implementation
public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IVideoSourceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IVideoSourceHandler)
    ITV8_END_CONTRACT_MAP()

// ITV8::GDRV::IVideoSourceHandler implementation
public:
    virtual void MultimediaFrameReceived(ITV8::GDRV::IVideoSource* pSource, ITV8::MFF::IMultimediaBuffer* pBuffer);

    virtual void Failed(ITV8::IContract* pSource, ITV8::hresult_t error);

public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();

public:
    // Gets text description of the video source.
    virtual std::string ToString() const;
    virtual void SetSinkConnected(bool conn)
    {
        CChannel::SetSinkConnected(conn);
    }

    bool IsOrigin() const;
    PTimeCorrectorSyncPoint GetSyncPoint();

// Overrides virtual methods of CChannel class
protected:
    void DoStart() override;

    void DoStop() override;

    void OnStopped() override;

    void OnFinalized() override;

    void OnEnabled() override;

    void OnDisabled() override;

    void BeginFinalization() override;

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
    int GetId() const;
    void SetId(int newVal);
    bool initializeVideoSource();
    void licenseObserve();
    void licenseObserve(const boost::system::error_code& error);
    void setTimerLicense();
    void doStart();
    std::string fullAccessPoint() const;
    void onStreamTypeChanged();

private:
    typedef boost::shared_ptr<ITV8::GDRV::IVideoSource> IVideoSourcePtr;
    // The driver video source.
    IVideoSourcePtr m_videoSource;
    // Video source settings.
    SVideoChannelParam m_settings;
    // Video streaming settings.
    SVideoStreamingParam m_streamSettings;

    //std::string m_vendor;
    size_t m_videoChannelsCount;
    const bool m_isOrigin;

    boost::shared_ptr<NCorbaHelpers::IResource> m_lease;
    bool m_doStartCalled = false;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;
    NCorbaHelpers::PResource m_servant;

    std::string m_accessPoint;

    boost::shared_ptr<CParamContext> m_context;

    const EStreamType m_streamType;
    std::unique_ptr<CSampleSequenceChecker> m_sampleSeqChecker;

    CAsyncPushSinkHelper m_asyncPushSinkHelper;
    void DoProcessFrame(ITV8::MFF::IMultimediaBuffer* pBuffer);

    NCorbaHelpers::PReactor m_reactor;
    boost::asio::deadline_timer m_timerLicense;
    bool m_useVideoBuffersWithSavingPrevKeyFrame;

    uint32_t m_lastMediaType = 0;
    int32_t m_keyFrameCounter = 0;
    NCorbaHelpers::CAutoPtr<NMMSS::CDefaultQoSPolicy< NMMSS::CDefaultAugmentationPolicy >> m_qosPolicy;

    PAdaptiveSourceFactory m_adaptiveSourceFactory;
    PTimeCorrectorSyncPoint m_syncPoint;
};

}

#endif // DEVICEIPINT3_CVIDEOSOURCE_H