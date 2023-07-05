#ifndef DEVICEIPINT3_STORAGEENDPOINT_H
#define DEVICEIPINT3_STORAGEENDPOINT_H

#include <MMSS.h>
#include <ConnectionBroker.h>

#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/smart_ptr/scoped_ptr.hpp>
#include <boost/date_time/posix_time/time_parsers.hpp>
#include <CorbaHelpers/Reactor.h>
#include <MMTransport/StatisticsCollectorImpl.h>
#include "RecordingPlaybackFactory.h"
#include "StorageEndpointImpl.h"
#include "IObjectsGroupHolder.h"
#include "../ItvSdkUtil/ItvSdkUtil.h"

namespace IPINT30
{

typedef boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> IMultimediaBufferSP;
typedef boost::scoped_ptr<NMMSS::IStatisticsCollectorImpl> PStatisticsCollector;

class StorageSource;
using WPStorageSource = NCorbaHelpers::CWeakPtr<StorageSource>;
using PStorageSource = NCorbaHelpers::CAutoPtr<StorageSource>;

// This class works through video StorageEndpoint 'pair'.
class AudioStorageEndpointProxy : public NMMSS::ISeekableSource,
    public virtual NMMSS::IStatisticsProvider, public NCorbaHelpers::CWeakReferableImpl,
    boost::noncopyable

{
    typedef boost::shared_ptr<ITV8::MFF::IMultimediaFrameFactory> IMultimediaFrameFactorySP;

public:
    AudioStorageEndpointProxy(PStorageSource parent);

public:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements();
    virtual void OnConnected(TConnection* connection);
    virtual void OnDisconnected(TConnection* connection);
    virtual void Request(unsigned int count = 1);
    virtual void Seek(boost::posix_time::ptime const& startTime, NMMSS::EEndpointStartPosition startPos, NMMSS::EPlayModeFlags mode, std::uint32_t sessionId);
    virtual const NMMSS::IStatisticsCollector* GetStatisticsCollector() const { return m_measurer.get(); }
    void Push(NMMSS::PSample sample);
private:
    WPStorageSource         m_parent;
    PStatisticsCollector    m_measurer;
    boost::mutex            m_sourceMutex;
    TConnection*            m_sourceConnection;
    bool                    m_isFirstFrame;
    ITV8::timestamp_t       m_lastTs;
    std::once_flag          m_proxySetted;
};

class StorageEndpoint : public NMMSS::ISeekableSource,
                        public virtual NMMSS::IStatisticsProvider, 
                        public NCorbaHelpers::CWeakReferableImpl,
                        boost::noncopyable
{
    typedef boost::shared_ptr<ITV8::MFF::IMultimediaFrameFactory> IMultimediaFrameFactorySP;
    typedef boost::function<void()> deactivateHandler_t;

    deactivateHandler_t     m_deactivateEndpointHandler;
    errorNotifyHandler_t    m_errorNotifyHandler;
public:
    typedef boost::function<void(ITV8::hresult_t)> errorCallback_t;
    StorageEndpoint(DECLARE_LOGGER_ARG,
        const RecordingPlaybackFactory& factory,
        ITV8::timestamp_t begintime,
        std::string readerName,
        IObjectsGroupHolderSP groupHolder,
        int playbackSpeed,
        NMMSS::EPlayModeFlags mode,
        NMMSS::EEndpointStartPosition startPos,
        boost::shared_ptr<ITimedEventFactory> eventFactory,
        deactivateHandler_t handler,
        errorNotifyHandler_t errHandler);
    ~StorageEndpoint();

public:
    // NMMSS::IPullStyleSource interface
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements();
    virtual void OnConnected(TConnection* connection);
    virtual void OnDisconnected(TConnection* connection);
    virtual void Request(unsigned int count = 1);
    virtual void Seek(boost::posix_time::ptime const& startTime, NMMSS::EEndpointStartPosition startPos, NMMSS::EPlayModeFlags mode, std::uint32_t sessionId);
    virtual const NMMSS::IStatisticsCollector* GetStatisticsCollector() const { return m_measurer.get(); }
    void RequestAudio(unsigned int count = 1);
    void SetAudioEndpointProxy(NCorbaHelpers::CWeakPtr<AudioStorageEndpointProxy> audioSource);
    void SetWithAudio(bool state);
private:
    void start();
    void handleFrame(const BufferWrapper& buffer, bool firstFrame);
    void push(NMMSS::PSample sample, bool isDiscontinuity);
    ITV8::Analytics::IDetectorEventRaiser* handleOccasionalEventRaising(ITV8::IContract* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase);
    ITV8::Analytics::IDetectorEventRaiser* handlePeriodicalEventRaising(ITV8::IContract* sender, const char* metadataType, ITV8::timestamp_t time);
    ITV8::Analytics::IDetectorEventRaiser* handleEventRaising(bool isOccasional, ITV8::IContract* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase);
    std::string ToString() const;

private:
    DECLARE_LOGGER_HOLDER;
    RecordingPlaybackFactory                    m_playbackFactory;
    std::atomic_ullong                          m_beginTime;

    std::atomic_ullong                          m_lastStartTime;
    NMMSS::EPlayModeFlags                       m_mode;
    NMMSS::EEndpointStartPosition               m_startPos;
    uint32_t                                    m_sessionId;
    const std::string                           m_readerName;

    IObjectsGroupHolderSP                       m_groupHolder;
    static const NMMSS::SAllocatorRequirements  m_nullAllocatorRequirements;
    
    boost::mutex                                        m_sourceMutex;
    uint64_t                                            m_sampleCount;
    TConnection*                                        m_sourceConnection;
    IMultimediaFrameFactorySP                           m_frameFactory;
    boost::mutex                                        m_endpointMutex;
    StorageEndpointImpl                                 m_endpointImpl;
    ITV8::timestamp_t                                   m_lastTs;
    NCorbaHelpers::CWeakPtr<AudioStorageEndpointProxy>  m_audioEndpointProxy;
    ITVSDKUTILES::IEventFactoryPtr                      m_eventFactory;
    PStatisticsCollector                                m_measurer;
    bool                                                m_needMarkFirstVideoFrameAsDiscontinuity;

    friend class AudioStorageEndpointProxy;
};

}

#endif

