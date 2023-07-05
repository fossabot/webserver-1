#ifndef DEVICEIPINT3_STORAGESOURCE_H
#define DEVICEIPINT3_STORAGESOURCE_H

#include <boost/shared_ptr.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/detail/atomic_count.hpp>

#include "CIpInt30.h"
#include <MMIDL/MMStorageS.h>
#include "../mmss/MMSS.h"
#include "../ItvSdkUtil/ItvSdkUtil.h"

#include <CommonNotificationCpp/StatisticsAggregator.h>
#include <CommonNotificationCpp/StateControlImpl.h>
#include <MMTransport/QualityOfService.h>
#include <CorbaHelpers/Container.h>

#include <ItvDeviceSdk/include/IStorageDevice.h>

#include "StorageDataTypes.h"
#include "IObjectsGroupHolder.h"
#include "CachedHistoryRequester.h"

namespace IPINT30
{
typedef boost::detail::atomic_count TAtomicCount;
namespace NAtomic = boost::interprocess::ipcdetail;

typedef boost::intrusive_ptr<PortableServer::ServantBase> ServantBaseSP;
class EmbeddedStorage;
class CFairPresentationRangePolicy;
typedef std::unique_ptr<CFairPresentationRangePolicy> PFairPresentationRangePolicy;
class StorageSource;

using WPStorageSource = NCorbaHelpers::CWeakPtr<StorageSource>;
using PStorageSource = NCorbaHelpers::CAutoPtr<StorageSource>;
using WPSeekableSource = NCorbaHelpers::CWeakPtr<NMMSS::ISeekableSource>;

class DEVICEIPINT_TESTABLE_DECLSPEC StorageSource : public virtual POA_MMSS::StorageSource,
    public virtual NCommonNotification::CLegacyStateControl
{
public:
    StorageSource(NCorbaHelpers::IContainerNamed* container, 
            const std::string& id,
            ITV8::Utility::RecordingInfoSP recordingsInfo,
            boost::shared_ptr<IPINT30::IIpintDevice> parent,
            EmbeddedStorage* embeddedStorage,
            ITV8::GDRV::IStorageDevice* storageDevice,
            NExecutors::PDynamicThreadPool dynExec,
            int playbackSpeed,
            bool requestAudio,
            const std::string& objectId,
            const std::string& eventChannel,
            bool useCachedHistoryRequester = true);

    ~StorageSource();

public:
    // Interfaces virtual methods
    virtual void Name(const char* Name);
    virtual char* Name();
    virtual ::MMSS::UUID_slice* ID();
    virtual char* AccessPoint();

    //attribute QualityOfService QoS; 
    
    virtual ::MMSS::StorageEndpoint_ptr GetSourceReaderEndpoint(const char * ptBeginTime,
        ::MMSS::EStartPosition startPos, ::CORBA::Boolean bIsRealtime, 
        ::CORBA::Long mode, ::MMSS::EEndpointReaderPriority priority);

    virtual ::MMSS::StorageEndpoint_ptr GetSourceReaderEndpoint2(
        ::MMSS::EStartPosition startPos, ::CORBA::Boolean bIsRealtime,
        ::CORBA::Long mode, ::MMSS::EEndpointReaderPriority priority,
        const ::MMSS::StorageSource::IntervalSeq& intervals);

    virtual ::MMSS::Endpoint_ptr GetBlockReaderEndpoint(const char* ptBeginTime) { return 0; }

    virtual void DoStart();
    virtual void DoStop();

    void GetHistory(const char* beginTime, const char* endTime, CORBA::ULong maxCount,
        CORBA::ULong minGap, MMSS::StorageSource::IntervalSeq_out intervals);

    void GetCalendar(::CORBA::LongLong timeFrom, ::CORBA::LongLong timeTo,
        ::MMSS::StorageSource::DateTimeSeq_out dates) final override;

    bool GetRelevantHistoryRange(MMSS::Days const& relevanceThreshold,
        ::MMSS::StorageSource::Interval& range) /*throw (CORBA::SystemException)*/ final override;

    void GetSize(const char *,const char *,
        CORBA::LongLong_out,CORBA::LongLong_out);

    virtual ::CORBA::ULong Prerecording() { return 0; }
    virtual void Prerecording(::CORBA::ULong ms) { }

    virtual ::CORBA::Float Fps() { return 0; }
    virtual void SetFps(const char *, ::CORBA::Float fps) { }
    virtual void DropFps(const char *) { }

    virtual void UpdateSettings(::CORBA::ULong prerecording, ::CORBA::Float fps) { }

    virtual void ClearInterval(const char* beginTime, const char* endTime) { }
    virtual CORBA::LongLong GetRewriteBoundary() { return 0; }
    
    virtual ::MMSS::StorageLocation* GetLocationInfo();

    ::MMSS::HistoryResult GetHistory2(::MMSS::HistoryScanMode mode, ::CORBA::LongLong timeFrom,
        ::CORBA::LongLong timeTo, ::CORBA::ULong maxCount, ::CORBA::ULongLong minGapMs,
        ::MMSS::StorageSource::IntervalSeq_out intervals) final override;

    void deactivateReader(const std::string& endpointName, bool needDecrease);

    void ClearCache();
    void SetParent(PStorageSource source);
    WPSeekableSource GetVideoSource() { return m_videoSourceImplWeek; }

    void Stop();

    // Now it required only for UT
    void waitHistoryRequester2BackgroundActions();
    void UT_tweakCachedHistoryRequester2(const CachedHistoryRequester2::STweaks& tweaks);
    //

    void fetchParams(uint32_t& cacheDepthMs, uint32_t& cacheUpdatePeriodSeconds);
private:
    std::string checkAbilityToRegisterReader(const std::string& readerName, MMSS::EEndpointReaderPriority priority);
    void fillIntervals(const IPINT30::historyIntervalSet_t intervals, MMSS::StorageSource::IntervalSeq_out out);
    historyIntervalSet_t requestHistory(historyInterval_t searchRange);
    MMSS::HistoryResult requestHistory2(historyInterval_t searchRange, unsigned int maxCount,
                                        unsigned long long minGapMs, historyIntervalSet_t& result);
    void handlePresentationRange(MMSS::StorageSource::IntervalSeq_out intervals);
    void terminate();
    std::string getReaderName();
    void addStatistics(historyInterval_t searchRange, int64_t callDuration, bool isFromCache);
    std::string ToString() const;
    void initializeHistoryRequester(bool useCachedHistoryRequester);
    void performAsyncRecordingSearch(historyInterval_t searchRange, historyIntervalSetHandler_t handler,
        finishedHandler_t finishedHandler, boost::signals2::signal<void()>& stopSignal);

private:
    DECLARE_LOGGER_HOLDER;
    std::string                                                  m_id;
    std::string                                                  m_objId;
    ITV8::Utility::RecordingInfoSP                               m_recordingsInfo;
    boost::weak_ptr<IPINT30::IIpintDevice>                       m_parent;
    EmbeddedStorage* const                                       m_embeddedStorage;
    ITV8::GDRV::IStorageDevice* const                            m_storageDevice;
    NExecutors::PDynamicThreadPool                               m_dynExec;
    const int                                                    m_playbackSpeed;
    const bool                                                   m_requestAudio;

    boost::mutex                                                 m_containerGuard;
    NCorbaHelpers::PContainerTrans                               m_endpointsContainer;
    NCorbaHelpers::CAutoPtr<NCommonNotification::CEventSupplier> m_connector;
    NCorbaHelpers::PResource                                     m_eventServantRegistration;

    typedef std::tuple<int64_t, int64_t, int> statistics_format_t;
    NStatisticsAggregator::PStatisticsAggregator                 m_aggregator;
    boost::mutex                                                 m_statMutex;
    std::vector<statistics_format_t>                             m_statistics;
    std::chrono::system_clock::time_point                        m_lastHistoryRequestTime;

    struct Endpoint
    {
        MMSS::EEndpointReaderPriority m_prior;
        ServantBaseSP m_servant;
    };

    typedef std::map<std::string, Endpoint> endpointSevantsMap_t;
    typedef std::shared_ptr<CachedHistoryRequester<>> PCachedHistoryRequester;
    typedef std::shared_ptr<CachedHistoryRequester2> PCachedHistoryRequester2;
    boost::mutex                    m_servantsMapGuard;
    bool                            m_terminated;
    endpointSevantsMap_t            m_endpointsServantsMap;
    TAtomicCount                    m_readerNameCount;
    OperationCompletion             m_completion;
    PCachedHistoryRequester         m_historyRequester;
    PCachedHistoryRequester2        m_historyRequester2;
    PFairPresentationRangePolicy    m_fairPresentationRangePolicy;
    WPStorageSource                 m_parentSource; // Audio source should have 'parent' video source.
    WPSeekableSource                m_videoSourceImplWeek;
    ITVSDKUTILES::IEventFactoryPtr  m_eventFactory;   
};

}

#endif

