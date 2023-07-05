#ifndef DEVICEIPINT3_EMBEDDEDSTORAGE_H
#define DEVICEIPINT3_EMBEDDEDSTORAGE_H

#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread.hpp>

#include <ItvDeviceSdk/include/IStorageDevice.h>
#include <CorbaHelpers/Reactor.h>

#include "IIPManager3.h"
#include "CAsyncChannel.h"
#include "ParamContext.h"
#include "AsyncActionHandler.h"
#include "StorageDataTypes.h"
#include "DeviceInformation.h"

namespace IPINT30
{
class CDevice;
class StorageSource;
class StartedStateHolder;
class CEndpointConnectionsManager;
typedef boost::shared_ptr<void> StartedStateHolderSP;
class RecordingsInfoRequester;
using WPStorageSource = NCorbaHelpers::CWeakPtr<StorageSource>;
using PStorageSource = NCorbaHelpers::CAutoPtr<StorageSource>;


class EmbeddedStorage : public CAsyncChannelHandlerImpl<ITV8::GDRV::IAsyncDeviceChannelHandler>,
    public IObjectParamContext,
    public IDeviceInformationProvider
{
public:
    EmbeddedStorage(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec,
        boost::shared_ptr<IPINT30::IIpintDevice> parent,
        const SEmbeddedStoragesParam& storageParam, bool isOfflineAnalytics,
        boost::shared_ptr<NMMSS::IGrabberCallback> callback,
        const char* objectContext, NCorbaHelpers::IContainerNamed* container,
        NCorbaHelpers::PContainerNamed sourcesContainer,
        size_t videoChannelsCount,
        const std::string& eventChannel);

    ~EmbeddedStorage();

    bool IncreaseEndpointConnection();
    void DecreaseEndpointConnection();
    void NotifyError(ITV8::hresult_t err);

public:
    // From IObjectParamContext
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();

public:
    const SEmbeddedStoragesParam& storageParams() const noexcept;

public:
    //From CChannel class
    virtual std::string ToString() const;

protected:
    //From CChannel class
    virtual void DoStart();
    virtual void DoStop();

    virtual void OnStopped();
    virtual void OnFinalized();

    virtual void OnEnabled();
    virtual void OnDisabled();

    virtual void SetDeviceStateChanged(NMMSS::EIpDeviceState state);

private:
    inline ITV8::GDRV::IStorageDevice*    getStorageDevice();

    void handleRecordingsInfo(const ITV8::Utility::recordingInfoList& recordingsInfo,
        StartedStateHolderSP);

    ITV8_BEGIN_CONTRACT_MAP()
    ITV8_END_CONTRACT_MAP()
    virtual void Failed(ITV8::IContract*, ITV8::hresult_t) {}

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
    SEmbeddedStoragesParam                              m_storageParam;
    boost::shared_ptr<NMMSS::IGrabberCallback>          m_callback;
    std::string                                         m_objectContext;
    std::string                                         m_name;
    NCorbaHelpers::PContainerNamed                      m_sourcesContainer;
    boost::shared_ptr<CParamContext>                    m_context;
    StartedStateHolderSP                                m_startedState;
    boost::scoped_ptr<RecordingsInfoRequester>          m_recordingsInfoManager;
    std::unique_ptr<CEndpointConnectionsManager>        m_endpointConnectionsManager;

    typedef boost::shared_ptr<NCorbaHelpers::IResource> IResourceSP;
    typedef std::vector<IResourceSP> resourcesList_t;
    typedef std::vector<WPStorageSource> sourcesList_t;
    resourcesList_t                           m_sourcesServant;
    sourcesList_t                             m_sourcesList;
    boost::mutex                              m_sourcesGuard;
    bool                                      m_sourcesCleared;
    bool                                      m_isOfflineAnalytics;
    IResourceSP                               m_lease;
    size_t                                    m_videoChannelsCount;
    std::string                               m_eventChannel;
    NCorbaHelpers::PReactor                   m_reactor;
};

}

#endif

