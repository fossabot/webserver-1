#include "StorageSource.h"

#include <algorithm>
#include <numeric>

#include <boost/date_time.hpp>
#include <boost/optional.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>

#include <PtimeFromQword.h>
#include <MMTransport/SourceFactory.h>
#include <MMTransport/MMTransport.h>
#include <CommonNotificationCpp/StatisticsAggregator.h>
#include <TimeIntervals.h>
#include <CorbaHelpers/CorbaStl.h>
#include <CorbaHelpers/Envar.h>
#include <Logging/Log3.h>
#include <Logging/Printers.h>

#include "StorageEndpoint.h"
#include "EmbeddedStorage.h"
#include "RecordingSearch.h"
#include "RecordingPlaybackFactory.h"
#include "TimeStampHelpers.h"
#include "CachedHistoryRequester.inl"
#include "../VideoFileSystem/VFS.h"
#include "../MMClient/DetectorEventFactory.h"
#include "../ItvSdkUtil/ItvSdkUtil.h"

inline void intrusive_ptr_add_ref(PortableServer::Servant s) { s->_add_ref(); }
inline void intrusive_ptr_release(PortableServer::Servant s) { s->_remove_ref(); }

namespace
{

const boost::posix_time::ptime PRESENTATION_START{ boost::gregorian::date(2010, 1, 1) };
const int DEFAULT_COMMON_EXP_TIME = 60 * 30 * 1000;
const int DEFAULT_LIVE_EXP_TIME = 60 * 1000;
boost::posix_time::milliseconds CALENDAR_REQUEST_TIMEOUT(DEFAULT_LIVE_EXP_TIME);

typedef ITV8::Utility::RecordingInfoSP RecordingInfoSP;
bool getTrackId(ITV8::Utility::RecordingInfoSP recordingInfo, ITV8::GDRV::Storage::TTrackMediaType type, std::string& ret)
{
    using namespace ITV8::Utility;
    const tracksInfoList_t& tracks = recordingInfo->tracks;
    auto cit = std::find_if(tracks.begin(), tracks.end(), boost::bind(&TrackInfo::type, _1) == type);
    if (cit == tracks.end())
    {
        return false;
    }
    ret = cit->id;
    return true;
}

boost::posix_time::milliseconds getExpirationTimeByName(const std::string& name, int defaultValue)
{
    try
    {
        std::string valueStr;
        if (NCorbaHelpers::CEnvar::Lookup(name, valueStr))
        {
            auto result = boost::posix_time::milliseconds(boost::lexical_cast<int>(valueStr));
            return result;
        }
    }
    catch (const std::exception &) {}
    return boost::posix_time::milliseconds(defaultValue);
}

class CSingleRecordingInfoHandler : public ITV8::GDRV::ISingleRecordingInfoHandler
{
public:
    CSingleRecordingInfoHandler()
        : m_finished(false)
        , m_errorCode(ITV8::ENotError)
    {
    }

    void WaitForResult()
    {
        std::unique_lock<std::mutex> lock(m_conditionMutex);
        m_condition.wait(lock, [this]() { return m_finished; });
    }

    ITV8::hresult_t GetErrorCode() const
    {
        return m_errorCode;
    }

    ITV8::GDRV::DateTimeRange GetPresentationRange() const
    {
        return m_presentationRange;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::ISingleRecordingInfoHandler)
        ITV8_CONTRACT_ENTRY(ITV8::IEventHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::ISingleRecordingInfoHandler)
    ITV8_END_CONTRACT_MAP()

private:
    void Failed(ITV8::IContract*, ITV8::hresult_t error) override
    {
        m_errorCode = error;
        std::lock_guard<std::mutex> lock(m_conditionMutex);
        m_finished = true;
        m_condition.notify_all();
    }

    void RequestRecordingDone(ITV8::GDRV::IStorageDevice*, ITV8::GDRV::Storage::IRecordingInfo& recordingInfo) override
    {
        m_presentationRange = recordingInfo.GetMediaPresentationRange();
        std::lock_guard<std::mutex> lock(m_conditionMutex);
        m_finished = true;
        m_condition.notify_all();
    }

private:
    bool                            m_finished;
    ITV8::hresult_t                 m_errorCode;
    ITV8::GDRV::DateTimeRange       m_presentationRange;
    std::mutex                      m_conditionMutex;
    std::condition_variable_any     m_condition;
};

}
namespace IPINT30
{

const char* const PlaybackPriorities[] = {
    "ERP_Low",
    "ERP_Mid",
    "ERP_High",
};

inline std::ostream& operator<< (std::ostream& s, MMSS::EEndpointReaderPriority p)
{
    return s << PlaybackPriorities[p];
}

class CFairPresentationRangePolicy
{
public:
    CFairPresentationRangePolicy(IPINT30::historyInterval_t presentation,
        const boost::posix_time::time_duration& expiration) :
        m_presentation(presentation),
        m_expTime(expiration)
    {
    }

    void Update(IPINT30::historyInterval_t newInterval)
    {
        std::lock_guard<std::mutex> lock(m_presentationGuard);
        m_presentation = newInterval;
        m_added = boost::posix_time::second_clock::universal_time();
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_presentationGuard);
        m_presentation = IPINT30::historyInterval_t();
    }

    bool TryToGetPresentationRange(IPINT30::historyInterval_t& result)
    {
        std::lock_guard<std::mutex> lock(m_presentationGuard);
        const auto now = boost::posix_time::second_clock::universal_time();
        if (boost::icl::is_empty(m_presentation) || now - m_added > m_expTime)
        {
            return false;
        }

        result = m_presentation;
        return true;
    }

private:
    mutable std::mutex                  m_presentationGuard;
    IPINT30::historyInterval_t          m_presentation;
    boost::posix_time::time_duration    m_expTime;
    boost::posix_time::ptime            m_added;
};

StorageSource::StorageSource(NCorbaHelpers::IContainerNamed* container,
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
        bool useCachedHistoryRequester) :
    m_id(id),
    m_objId(objectId + "/" + id),
    m_recordingsInfo(recordingsInfo),
    m_parent(parent),
    m_embeddedStorage(embeddedStorage),
    m_storageDevice(storageDevice),
    m_dynExec(dynExec),
    m_playbackSpeed(playbackSpeed),
    m_requestAudio(requestAudio),
    m_endpointsContainer(container->CreateContainer()),
    m_aggregator(NStatisticsAggregator::GetStatisticsAggregatorImpl(container)),
    m_lastHistoryRequestTime(std::chrono::system_clock::now()),
    m_terminated(false),
    m_readerNameCount(0)
{
    INIT_LOGGER_HOLDER_FROM_CONTAINER(container);

    initializeHistoryRequester(useCachedHistoryRequester);

    // TODO: work this out
    m_connector = NCommonNotification::CreateEventSupplierServant(container, "", NCommonNotification::RefreshCachedEvents);

    NCommonNotification::CLegacyStateControl::Init(GET_LOGGER_PTR, id.c_str(), this,
        m_connector, false, NStatisticsAggregator::PStatisticsAggregator());

    auto accessPointSuffix = objectId + "/analytics:0:0";
    auto videoSource = objectId + "/SourceEndpoint.video:0:0";

    // Check track id to prevent factory creating if metadata is not supported.
    std::string trackName;
    if (getTrackId(m_recordingsInfo, ITV8::GDRV::Storage::EMetadataTrack, trackName) && !eventChannel.empty())
    try
    {
        auto originId = objectId + "/MultimediaStorage.0/EventSupplier.analytics:0:0";

        NMMSS::PDetectorEventFactory factory(
            NMMSS::CreateDetectorEventFactory(originId.c_str(), videoSource.c_str(),
            container, eventChannel.c_str(), accessPointSuffix.c_str()));

        m_eventFactory = ITVSDKUTILES::CreateEventFactory(GET_THIS_LOGGER_PTR, factory, objectId.c_str());
    }
    catch (const CORBA::Exception& ex)
    {
        _err_ << ToString() << "Corba Exception occurred on CreateDetectorEventFactory " << ex
            << " Object context:" << objectId << " m_accessPointSuffix:" << accessPointSuffix << " videoSource:" << videoSource;
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << "std::exception: occurred on CreateDetectorEventFactory. msg=" << e.what()
            << " Object context:" << objectId << " m_accessPointSuffix:" << accessPointSuffix << " videoSource:" << videoSource;
    }
}

StorageSource::~StorageSource()
{
}

void StorageSource::Stop()
{
    // Deactivate all remain Endpoints
    terminate();
    NCorbaHelpers::PContainerTrans container;
    {
        boost::mutex::scoped_lock lock(m_containerGuard);
        std::swap(m_endpointsContainer, container);
    }
    // Destroy endpoints container
    container.Reset();
    // Wait while all detached objects destroyed.
    m_completion.waitComplete();

    if (m_historyRequester)
        m_historyRequester->stop();

    if (m_historyRequester2)
        m_historyRequester2->stop();
}

void StorageSource::Name(const char* Name)
{
}

char* StorageSource::Name()
{
    return CORBA::string_dup(m_id.c_str());
}

char* StorageSource::AccessPoint()
{
    return CORBA::string_dup(m_id.c_str());
}

::MMSS::UUID_slice* StorageSource::ID()
{
    ::MMSS::UUID_slice* r = new ::MMSS::UUID;
    ::MMSS::UUID_var res = r;
    memcpy(r, &VideoFileSystem::NULL_UUID, sizeof(VideoFileSystem::NULL_UUID));

    return res._retn();
}

::MMSS::StorageEndpoint_ptr StorageSource::GetSourceReaderEndpoint2(
    ::MMSS::EStartPosition startPos, ::CORBA::Boolean bIsRealtime,
    ::CORBA::Long mode, ::MMSS::EEndpointReaderPriority priority,
    const ::MMSS::StorageSource::IntervalSeq& intervals)
{
    // TODO: implement and remove warning.
    _wrn_ << "Uses GetSourceReaderEndpoint instead  of GetSourceReaderEndpoint2.";
    if (0 == intervals.length())
        return 0;

    const auto beginTime = boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(intervals[0].beginTime));
    return GetSourceReaderEndpoint(beginTime.c_str(), startPos, bIsRealtime, mode, priority);
}

::MMSS::StorageEndpoint_ptr StorageSource::GetSourceReaderEndpoint(const char * ptBeginTime,
    ::MMSS::EStartPosition startPos, ::CORBA::Boolean bIsRealtime, ::CORBA::Long mode, ::MMSS::EEndpointReaderPriority priority)
{
    NCorbaHelpers::PContainerTrans container;
    {
        boost::mutex::scoped_lock lock(m_containerGuard);
        container = m_endpointsContainer;
    }
    if(!container)
        return 0;

    auto startTime = toIpintTime(ptBeginTime);
    try
    {
        const std::string readerName = getReaderName();

        ServantBaseSP pullEndpoint;
        if (PStorageSource s = m_parentSource)
        {
            NMMSS::PSeekableSource audioSource(new AudioStorageEndpointProxy(s));
            pullEndpoint = NMMSS::CreateSeekableSourceEndpoint(GET_LOGGER_PTR, container.Get(),
                audioSource.Get(),  boost::bind(&StorageSource::deactivateReader, this, readerName, true));
        }
        else
        {
            auto lowerPriorReader = checkAbilityToRegisterReader(readerName, priority);
            if (!lowerPriorReader.empty())
                deactivateReader(lowerPriorReader, false);

            ITV8::Utility::tracksList_t tracks;
            std::string trackName;
            if (!getTrackId(m_recordingsInfo, ITV8::GDRV::Storage::EVideoTrack, trackName))
                throw std::runtime_error("Can't find video track");
            tracks.push_back(trackName);

            if (getTrackId(m_recordingsInfo, ITV8::GDRV::Storage::EMetadataTrack, trackName))
                tracks.push_back(trackName);

            if (m_requestAudio && getTrackId(m_recordingsInfo, ITV8::GDRV::Storage::EAudioTrack, trackName))
                tracks.push_back(trackName);

            RecordingPlaybackFactory playbackFactory(m_storageDevice, m_dynExec, m_recordingsInfo->id, tracks, m_parent);

            NMMSS::PSeekableSource videoSource(new StorageEndpoint(GET_LOGGER_PTR,
                playbackFactory, startTime, readerName, m_completion.getHolder(), m_playbackSpeed,
                (NMMSS::EPlayModeFlags)mode, (NMMSS::EEndpointStartPosition)startPos, m_eventFactory,
                boost::bind(&StorageSource::deactivateReader, this, readerName, true),
                boost::bind(&EmbeddedStorage::NotifyError, m_embeddedStorage, _1)));

            m_videoSourceImplWeek = WPSeekableSource(videoSource);

            pullEndpoint = NMMSS::CreateSeekableSourceEndpoint(GET_LOGGER_PTR,
                container.Get(), videoSource.Get(),
                boost::bind(&StorageSource::deactivateReader, this, readerName, true));
        }
        {
            boost::mutex::scoped_lock lock(m_servantsMapGuard);
            if (!m_terminated)
                m_endpointsServantsMap[readerName] = Endpoint{ priority, pullEndpoint };
        }
        
        CORBA::Object_var o = container->ActivateServant(pullEndpoint.get());
        return ::MMSS::StorageEndpoint::_narrow(o);
    }
    catch(const std::exception& ex)
    {
        _err_ << ToString() << "StorageSource::GetSourceReaderEndpoint. Can't create Endpoint. error: " << ex.what();
    }

    return ::MMSS::StorageEndpoint::_nil();
}

void StorageSource::DoStart()
{
}

void StorageSource::DoStop()
{
}

typedef ITV8::GDRV::DateTimeRange DateTimeRange;

boost::optional<DateTimeRange> getTrackRange(RecordingSearch::RecordingRangeSP range,
                                                 const std::string& trackId)
{
    using namespace ITV8::Utility;
    const tracksRangeList_t& tracksRange = range->tracksRange;
    tracksRangeList_t::const_iterator cit = 
        std::find_if(tracksRange.begin(), tracksRange.end(), 
            boost::bind(&TrackRange::id, _1) == trackId);
    if (cit == tracksRange.end())
    {
        return boost::optional<DateTimeRange>();
    }
    return cit->range;
}

void StorageSource::GetHistory(const char* beginTime, const char* endTime,
    CORBA::ULong maxCount, CORBA::ULong minGap,
    MMSS::StorageSource::IntervalSeq_out intervals)
{
    if (PStorageSource s = m_parentSource)
        return s->GetHistory(beginTime, endTime, maxCount, minGap, intervals);

    intervals = new MMSS::StorageSource::IntervalSeq();
    intervals->length(0);

    try
    {
        if (maxCount == 1)
            return handlePresentationRange(intervals);

        if (0 == beginTime || 0 == endTime)
            return;

        auto bt = boost::posix_time::from_iso_string(beginTime);
        auto et = boost::posix_time::from_iso_string(endTime);
        if (bt > et)
            std::swap(bt, et);

        // Ipint will not process so large interval anyway
        NGP_ASSERT(bt != boost::posix_time::min_date_time || et != boost::posix_time::max_date_time);

        NLogging::StopWatch sw;
        const historyInterval_t searchRange(toIpintTime(bt), toIpintTime(et));
        const auto tracksList = requestHistory(searchRange);
        fillIntervals(tracksList, intervals);

        _dbgf_("{} [GetHistory] found {}", ToString(), rangesToString(tracksList));
        _inff_("{} [GetHistory]: interval={} bt={} et={} maxCount={} minGap={} result={} elapsedMs={}", ToString(),
            size(searchRange), boost::posix_time::to_iso_string(bt), boost::posix_time::to_iso_string(et), maxCount,
            minGap, intervals->length(), NLogging::PrintDuration(sw.ElapsedMs()));
    }
    catch (const std::exception& ex)
    {
        _err_ << ToString() << "[GetHistory]: Search recordings failed: " << ex.what();
    }
}

std::string historyResultToString(const MMSS::HistoryResult& historyResult)
{
    switch (historyResult)
    {
    case MMSS::HR_FULL:
        return "FULL";
    case MMSS::HR_PARTIAL:
        return "PARTIAL";
    case MMSS::HR_TRY_LATER:
        return "TRY_LATER";

    default:
        std::abort();
    }
}

::MMSS::HistoryResult StorageSource::GetHistory2(::MMSS::HistoryScanMode mode, ::CORBA::LongLong timeFrom,
    ::CORBA::LongLong timeTo, ::CORBA::ULong maxCount, ::CORBA::ULongLong minGapMs,
    ::MMSS::StorageSource::IntervalSeq_out intervals)
{
    if (PStorageSource s = m_parentSource)
       return s->GetHistory2(mode, timeFrom, timeTo, maxCount, minGapMs, intervals);

    MMSS::HistoryResult historyResult = MMSS::HR_FULL;

    intervals = new MMSS::StorageSource::IntervalSeq();
    intervals->length(0);

    try
    {
        if (mode == MMSS::HSM_APPROXIMATE)
        {
            // TODO alexander.chernenko
            // Try to get presentation range from m_historyRequester2 cache
            // if RequestRecordingInfo fails. Remove this after ACR-68438 solved
            handlePresentationRange(intervals);
            return historyResult;
        }

        NGP_ASSERT(mode == MMSS::HSM_EXACT);

        if (0 == timeFrom || 0 == timeTo)
            return historyResult;

        auto bt = NMMSS::PtimeFromQword(timeFrom);
        auto et = NMMSS::PtimeFromQword(timeTo);
        if (bt > et)
            std::swap(bt, et);

        NLogging::StopWatch sw;
        const historyInterval_t searchRange(toIpintTime(bt), toIpintTime(et));
        historyIntervalSet_t tracksList;
        historyResult = requestHistory2(searchRange, maxCount, minGapMs, tracksList);
        fillIntervals(tracksList, intervals);

        _dbgf_("{} [GetHistory2] found {}", ToString(), rangesToString(tracksList));
        _inff_("{} [GetHistory2]: interval={} bt={} et={} maxCount={} minGap={} result={} size={} elapsedMs={}", ToString(),
            size(searchRange), boost::posix_time::to_iso_string(bt), boost::posix_time::to_iso_string(et), maxCount, minGapMs,
            historyResultToString(historyResult), intervals->length(), NLogging::PrintDuration(sw.ElapsedMs()));
    }
    catch (const std::exception& ex)
    {
        _err_ << ToString() << "[GetHistory2]: Search recordings failed: " << ex.what();
        throw MMSS::Volume::RuntimeError(ex.what());
    }

    return historyResult;
}

void StorageSource::GetCalendar(::CORBA::LongLong timeFrom, ::CORBA::LongLong timeTo, ::MMSS::StorageSource::DateTimeSeq_out dates)
{
    if (PStorageSource s = m_parentSource)
    {
        return s->GetCalendar(timeFrom, timeTo, dates);
    }

    try
    {
        dates = new ::MMSS::StorageSource::DateTimeSeq();
        dates->length(0);

        if (0 == timeFrom || 0 == timeTo || timeFrom == timeTo)
            return;

        auto bt = NMMSS::PtimeFromQword(timeFrom);
        auto et = NMMSS::PtimeFromQword(timeTo);
        if (bt > et)
        {
            std::swap(bt, et);
        }
        if (bt == boost::date_time::min_date_time)
        {
            bt = PRESENTATION_START;
        }
        if (et == boost::date_time::max_date_time)
        {
            et = boost::posix_time::second_clock::universal_time();
        }

        NLogging::StopWatch sw;
        const historyInterval_t searchRange(toIpintTime(bt), toIpintTime(et));
        auto strong = m_parent.lock();
        if (!strong)
            return;

        RecordingSearch requester(GET_LOGGER_PTR, m_storageDevice, m_recordingsInfo->id, m_dynExec,
            CALENDAR_REQUEST_TIMEOUT);
        auto calendar = m_historyRequester->getCalendar(requester, searchRange);
        for (const auto& c : calendar)
        {
            NCorbaHelpers::PushBack(*dates, static_cast<CORBA::LongLong>(ipintTimeToQword(c)));
        }

        const auto elapsed = sw.ElapsedMs();
        _inff_("{} [GetCalendar]: bt={} et={} result={} elapsedMs={}", ToString(), boost::posix_time::to_iso_string(bt),
            boost::posix_time::to_iso_string(et), calendar.size(), NLogging::PrintDuration(elapsed));

        addStatistics(searchRange, elapsed.count(), requester.isFinished());
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << "[GetCalendar]: failed with error: " << e.what();
    }
}

bool StorageSource::GetRelevantHistoryRange(MMSS::Days const& relevanceThreshold,
    ::MMSS::StorageSource::Interval& range) /*throw (CORBA::SystemException)*/
{
    _inf_ << ToString() << "Requested relevant history with relevanceThreshold = " << relevanceThreshold.value << " days";

    if (PStorageSource s = m_parentSource)
    {
        return s->GetRelevantHistoryRange(relevanceThreshold, range);
    }

    try
    {
        boost::posix_time::ptime const now = boost::posix_time::second_clock::universal_time();
        const historyInterval_t searchRange( toIpintTime(VideoFileSystem::MINUS_INFINITY), toIpintTime(now) );
        auto tracksList = requestHistory(searchRange);
        if (tracksList.empty())
            return false;

        auto const hoursPerDay = 24;
        boost::posix_time::time_duration const threshold { boost::posix_time::hours(hoursPerDay * relevanceThreshold.value) };

        auto const relevanceRangeDeadline = now - threshold;
        auto const latestTrackEnd = ipintTimestampToPtime(tracksList.rbegin()->upper());
        if (latestTrackEnd < relevanceRangeDeadline)
            return false;

        VideoFileSystem::CTimeIntervals intervals( threshold );
        intervals.Reserve(tracksList.size());
        for (auto const& track : tracksList)
        {
            VideoFileSystem::CTimeInterval trackRange{ ipintTimestampToPtime(track.lower()), ipintTimestampToPtime(track.upper()) };
            intervals.Add( trackRange );
        }
        if (!intervals.Empty())
        {
            auto const& relevantRange = intervals.Back();
            range = {
                static_cast<CORBA::LongLong>(NMMSS::PtimeToQword(relevantRange.begin)),
                static_cast<CORBA::LongLong>(NMMSS::PtimeToQword(relevantRange.end))
            };
            _inf_ << ToString() << "Found relevant range= [" << to_iso_string(relevantRange.begin)
                << ", " << to_iso_string(relevantRange.end) << "]";
            return true;
        }
    }
    catch (const std::exception& ex)
    {
        _err_ << ToString() << "Search for relevant recordings failed: " << ex.what();
    }
    return false;
}

::MMSS::StorageLocation* StorageSource::GetLocationInfo()
{
    ::MMSS::StorageLocation_var res = new ::MMSS::StorageLocation();
    res->HostName = NCorbaHelpers::GetLocalHost().c_str();
    res->ProcessId = ACE_OS::getpid();
    return res._retn();
}

void StorageSource::fillIntervals(const IPINT30::historyIntervalSet_t intervals, MMSS::StorageSource::IntervalSeq_out out)
{
    for (const auto& interval : intervals)
    {
        MMSS::StorageSource::Interval cInt;
        cInt.beginTime = IPINT30::ipintTimeToQword(interval.lower());
        cInt.endTime = IPINT30::ipintTimeToQword(interval.upper());
        NCorbaHelpers::PushBack(*out, cInt);
    }
}

void StorageSource::GetSize(const char*,const char *, 
    CORBA::LongLong_out,CORBA::LongLong_out)
{
}

void StorageSource::deactivateReader(const std::string& endpointName, bool needDecrease)
{
    ServantBaseSP servantBase;
    {
        boost::mutex::scoped_lock lock(m_servantsMapGuard);
        auto it = m_endpointsServantsMap.find(endpointName);
        if (it != m_endpointsServantsMap.end())
        {
            servantBase = it->second.m_servant;
            m_endpointsServantsMap.erase(it);

            if (m_embeddedStorage && !m_parentSource && needDecrease) // Decreasing/Increasing connections only for videoEndpoint: m_parentSoutce == nullptr is sign of videEndpoint
            {
                m_embeddedStorage->DecreaseEndpointConnection();
            }
        }
    }

    NCorbaHelpers::PContainerTrans container;
    {
        boost::mutex::scoped_lock lock(m_containerGuard);
        container = m_endpointsContainer;
    }

    if (!servantBase || !container)
    {
        return;
    }
    _dbg_ << ToString() << "deactivateReader-" << endpointName << ", readers count=" << m_endpointsServantsMap.size();
    container->DeactivateServant(servantBase.get());
}

void StorageSource::ClearCache()
{
    if (PStorageSource s = m_parentSource)
    {
        return s->ClearCache();
    }

    _inf_ << ToString() << __FUNCTION__;
    if (m_historyRequester)
        m_historyRequester->clearCache();

    if (m_historyRequester2)
        m_historyRequester2->clearCache();

    if (m_fairPresentationRangePolicy)
        m_fairPresentationRangePolicy->Clear();
}

void StorageSource::SetParent(PStorageSource source)
{
    m_parentSource = WPStorageSource(source);
}

std::string StorageSource::getReaderName()
{
    return (boost::format("%s-ArchiveReader%08d") % m_id % ++m_readerNameCount).str();
}

void StorageSource::terminate()
{
    endpointSevantsMap_t servantsMap;
    {
        boost::mutex::scoped_lock lock(m_servantsMapGuard);
        m_endpointsServantsMap.swap(servantsMap);
        m_terminated = true;
    }
    
    NCorbaHelpers::PContainerTrans container;
    {
        boost::mutex::scoped_lock lock(m_containerGuard);
        container = m_endpointsContainer;
    }
    if (!container)
    {
        return;
    }
    for (auto& record : servantsMap)
    {
        container->DeactivateServant(record.second.m_servant.get());
    }
}

void StorageSource::UT_tweakCachedHistoryRequester2(const CachedHistoryRequester2::STweaks& tweaks)
{
    if (m_historyRequester2)
        m_historyRequester2->UT_tweak(tweaks);
}

std::string StorageSource::checkAbilityToRegisterReader(const std::string& readreserName, MMSS::EEndpointReaderPriority priority)
{
    endpointSevantsMap_t readers;
    {
        boost::mutex::scoped_lock lock(m_servantsMapGuard);
        if (m_terminated)
        {
            throw std::runtime_error("StorageSource is stopped");
        }

        readers = m_endpointsServantsMap;
    }

    _dbg_ << ToString() << "checkAbilityToRegisterReader-" << readreserName << ", priority "
        << priority << ", readers count " << readers.size();
    // In case of working with limited connections count to the storage we have to deactivate
    // endpoint with lesser priority first and then try to increase endpoint connections counter
    // by embedded storage.
    auto reader = std::find_if(readers.begin(), readers.end(),
        [priority] (const endpointSevantsMap_t::value_type& item)
        {
            return item.second.m_prior < priority;
        });

    if (reader != readers.end())
    {
        return reader->first;
    }

    if (m_embeddedStorage && m_embeddedStorage->IncreaseEndpointConnection())
    {
        return std::string();
    }

    _wrn_ << ToString() << "There are not any available connections to the storage.";
    throw std::runtime_error("Maximal count of readers is reached!");
}

historyIntervalSet_t StorageSource::requestHistory(historyInterval_t searchRange)
{
    auto lock_parent = m_parent.lock();
    if (!lock_parent)
        return historyIntervalSet_t();

    std::string trackName;
    if (!getTrackId(m_recordingsInfo, ITV8::GDRV::Storage::EVideoTrack, trackName))
        throw std::runtime_error("Can't find video track");

    const auto time = std::chrono::steady_clock::now();
    RecordingSearch deviceRequester(GET_LOGGER_PTR, m_storageDevice, m_recordingsInfo->id, m_dynExec);
    auto ret = m_historyRequester->findRecordings(deviceRequester, trackName, searchRange, GET_LOGGER_PTR);

    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::steady_clock::now() - time));
    addStatistics(searchRange, diff.count(), deviceRequester.isFinished());
    return ret;
}

void StorageSource::performAsyncRecordingSearch(historyInterval_t searchRange, historyIntervalSetHandler_t handler,
    finishedHandler_t finishedHandler, boost::signals2::signal<void()>& stopSignal)
{
    std::string trackName;
    if (!getTrackId(m_recordingsInfo, ITV8::GDRV::Storage::EVideoTrack, trackName))
        throw std::runtime_error("Can't find video track");

    auto deviceRequester = std::make_shared<AsyncRecordingSearch>(GET_LOGGER_PTR, m_storageDevice, m_recordingsInfo->id);

    auto recordingHandler = [this, trackName, handler](RecordingRangeSP deviceResult)
        {
            auto h = makeHistoryIntervalSet(deviceResult, m_recordingsInfo->id, trackName, GET_LOGGER_PTR);
            handler(h);
        };

    auto finishedHandlerWrapper = [finishedHandler, deviceRequester](ITV8::hresult_t code)
        {
            finishedHandler(code);
        };
    
    deviceRequester->asyncFindRecordings(makeIpintTimeRange(searchRange), recordingHandler, finishedHandlerWrapper, stopSignal);
}

MMSS::HistoryResult StorageSource::requestHistory2(historyInterval_t searchRange,
                                                   unsigned int maxCount,
                                                   unsigned long long minGapMs,
                                                   historyIntervalSet_t& result)
{
    auto lock_parent = m_parent.lock();
    if (!lock_parent)
        throw std::runtime_error("Parent object does not exist");

    const auto time = std::chrono::steady_clock::now();
    MMSS::HistoryResult historyResult = MMSS::HR_TRY_LATER;

    bool hasFullRequestedHistory = false;
    auto scheduledGetHistory = m_historyRequester2->getRecordings(searchRange, maxCount, minGapMs, result, hasFullRequestedHistory);

    if (scheduledGetHistory)
        historyResult = hasFullRequestedHistory ? MMSS::HR_FULL : MMSS::HR_PARTIAL;

    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::steady_clock::now() - time));
    addStatistics(searchRange, diff.count(), true/*deviceRequester.isFinished()*/);
    return historyResult;
}

void StorageSource::handlePresentationRange(MMSS::StorageSource::IntervalSeq_out intervals)
{
    historyInterval_t presentation;
    auto supportsSingleRecordingInfo = false;
    auto deviceStrong = m_parent.lock();
    if (!deviceStrong)
        return;

    try
    {
        auto storageDevice = ITV8::contract_cast<ITV8::GDRV::IStorageDevice>(deviceStrong->getDevice());
        if (storageDevice && !m_fairPresentationRangePolicy->TryToGetPresentationRange(presentation))
        {
            NLogging::StopWatch sw;
            CSingleRecordingInfoHandler handler;
            storageDevice->RequestRecordingInfo(m_recordingsInfo->GetId(), &handler);
            handler.WaitForResult();

            const auto ec = handler.GetErrorCode();
            if (ec == ITV8::ENotError)
            {
                supportsSingleRecordingInfo = true;
                presentation = make_interval(handler.GetPresentationRange());
                m_fairPresentationRangePolicy->Update(presentation);
            }
            _inff_("{} RequestRecordingInfo id={} result={} ec={} elapsedMs={}",
                ToString(), m_recordingsInfo->id, rangeToString(presentation), ec, NLogging::PrintDuration(sw.ElapsedMs()));
        }
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << __FUNCTION__ << ": " << e.what();
    }

    if (supportsSingleRecordingInfo)
    {
        // Try to construct presentation range from cache if got empty range from
        // IStorageDevice::RequestRecordingInfo with ITV8::ENotError result code
        presentation = getPresentationRange(m_historyRequester->presentationRange(), presentation);
        if (!boost::icl::is_empty(presentation))
        {
            MMSS::StorageSource::Interval interval;
            interval.beginTime = ipintTimeToQword(presentation.lower());
            interval.endTime = ipintTimeToQword(presentation.upper());
            NCorbaHelpers::PushBack(*intervals, interval);
            return;
        }
    }

    presentation = getPresentationRange(m_historyRequester->presentationRange(),
        make_interval(m_recordingsInfo->m_presentationRange));
    if (boost::icl::is_empty(presentation))
    {
        if (!NCorbaHelpers::CEnvar::EnablePresentationRangeInterval())
            return;

        // Legacy behaviour: try to construct presentation range from IRecordingSearch::FindRecordings
        const historyInterval_t presentationInterval(toIpintTime(PRESENTATION_START), ipintNow());
        _wrnf_("{} Can't provide presentation range. Try to search for a long period interval={}", ToString(), rangeToString(presentationInterval));
        requestHistory(presentationInterval);
        presentation = m_historyRequester->presentationRange();
        if (boost::icl::is_empty(presentation))
            return;
    }

    MMSS::StorageSource::Interval interval;
    interval.beginTime = ipintTimeToQword(presentation.lower());
    interval.endTime = ipintTimeToQword(presentation.upper());
    NCorbaHelpers::PushBack(*intervals, interval);
}

void StorageSource::addStatistics(historyInterval_t searchRange, int64_t callDuration, bool fromDevice)
{
    static const auto TTL = std::chrono::seconds(120);

    if (!m_aggregator)
        return;

    boost::unique_lock<boost::mutex> lock(m_statMutex);
    static DefaultIntervalNormalizer n;
    m_statistics.push_back(std::make_tuple(length(n(searchRange)) / 1000, callDuration, fromDevice ? 1 : 0));

    if (std::chrono::system_clock::now() - m_lastHistoryRequestTime < TTL/2)
        return;
    m_lastHistoryRequestTime = std::chrono::system_clock::now();

    auto maxRequestInterval = std::max_element(m_statistics.begin(), m_statistics.end(),
        [](const statistics_format_t& lhs, const statistics_format_t& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
    auto minRequestInterval = std::min_element(m_statistics.begin(), m_statistics.end(),
        [](const statistics_format_t& lhs, const statistics_format_t& rhs) { return std::get<0>(lhs) < std::get<0>(rhs); });
    auto sumRequestInterval = std::accumulate(m_statistics.begin(), m_statistics.end(), int64_t(0),
        [](const int64_t& sum, const statistics_format_t & p2) { return sum + std::get<0>(p2); });

    auto maxRequestDuration = std::max_element(m_statistics.begin(), m_statistics.end(),
        [](const statistics_format_t& lhs, const statistics_format_t& rhs) { return std::get<1>(lhs) < std::get<1>(rhs); });
    auto minRequestDuration = std::min_element(m_statistics.begin(), m_statistics.end(),
        [](const statistics_format_t& lhs, const statistics_format_t& rhs) { return std::get<1>(lhs) < std::get<1>(rhs); });
    auto sumRequestDuration = std::accumulate(m_statistics.begin(), m_statistics.end(), int64_t(0),
        [](const int64_t& sum, const statistics_format_t& rhs) { return sum + std::get<1>(rhs); });

    auto deviceCallCount = std::accumulate(m_statistics.begin(), m_statistics.end(), int64_t(0),
        [](const int64_t& sum, const statistics_format_t& rhs) { return sum + std::get<2>(rhs); });

    auto statSize = m_statistics.size();
    m_statistics.clear();
    lock.unlock();

    using namespace NStatisticsAggregator;
    m_aggregator->Push(std::move(StatPoint("es_history_max_request_interval", m_objId, TTL).AddValue(std::get<0>(*maxRequestInterval))
                                                                                           .AddLabel("from_device", std::to_string(std::get<2>(*maxRequestInterval)))
                                                                                           .AddHint("s.")));
    m_aggregator->Push(std::move(StatPoint("es_history_min_request_interval", m_objId, TTL).AddValue(std::get<0>(*minRequestInterval))
                                                                                           .AddLabel("from_device", std::to_string(std::get<2>(*minRequestInterval)))
                                                                                           .AddHint("s.")));
    m_aggregator->Push(std::move(StatPoint("es_history_sum_request_interval", m_objId, TTL).AddValue(sumRequestInterval).AddHint("s.")));

    m_aggregator->Push(std::move(StatPoint("es_history_max_request_duration", m_objId, TTL).AddValue(std::get<1>(*maxRequestDuration))
                                                                                           .AddLabel("from_device", std::to_string(std::get<2>(*maxRequestDuration)))
                                                                                           .AddHint("ms.")));
    m_aggregator->Push(std::move(StatPoint("es_history_min_request_duration", m_objId, TTL).AddValue(std::get<1>(*minRequestDuration))
                                                                                           .AddLabel("from_device", std::to_string(std::get<2>(*minRequestDuration)))
                                                                                           .AddHint("ms.")));
    m_aggregator->Push(std::move(StatPoint("es_history_sum_request_duration", m_objId, TTL).AddValue(sumRequestDuration).AddHint("ms.")));
    m_aggregator->Push(std::move(StatPoint("es_history_call_count", m_objId, TTL).AddValue(statSize)));
    m_aggregator->Push(std::move(StatPoint("es_history_device_call_count", m_objId, TTL).AddValue(deviceCallCount)));
}

void StorageSource::waitHistoryRequester2BackgroundActions()
{
    if (m_historyRequester2)
        m_historyRequester2->waitBackgrounActions();
}

std::string StorageSource::ToString() const
{
    std::ostringstream oss;
    oss << "StorageSource." << this << "[" << m_id << "] ";
    return oss.str();
}

void StorageSource::fetchParams(uint32_t& cacheDepthMs, uint32_t& cacheUpdatePeriodSeconds)
{
    const auto& params = m_embeddedStorage->storageParams();

    auto getParam = [&](const NStructuredConfig::TCustomParameters& p, const std::string& name) -> uint32_t
    {
        auto it = std::find_if(p.begin(), p.end(),
            [&name](const NStructuredConfig::SCustomParameter& item) { return name == item.name; });

        if (it != p.end() && it->Type() == "int")
            return std::stoul(it->ValueUtf8());

        return 0;
    };

    cacheUpdatePeriodSeconds = getParam(params.metaParams, "cacheUpdatePeriod");

    auto cacheDepthDays = getParam(params.metaParams, "cacheDepth");
    if (0 != cacheDepthDays)
        cacheDepthMs = cacheDepthDays * 24 * 60 * 60 * 1000;
}

void StorageSource::initializeHistoryRequester(bool useCachedHistoryRequester)
{
    if (!useCachedHistoryRequester)
    {
        return;
    }

    const auto commonExpTime = getExpirationTimeByName("NGP_ES_COMMON_EXP_TIME_MS", DEFAULT_COMMON_EXP_TIME);
    const auto liveExpTime = getExpirationTimeByName("NGP_ES_LIVE_EXP_TIME_MS", DEFAULT_LIVE_EXP_TIME);
    DefaultExpirationPolicy expPolicy(commonExpTime, liveExpTime);
    DefaultIntervalNormalizer normalizer;
    IPINT30::RecordingsHistoryCache<> cache(expPolicy);
    _dbg_ << ToString() << "Cached history requester commonExpTimeMs=" << commonExpTime.total_milliseconds()
        << ", liveExpTimeMs=" << liveExpTime.total_milliseconds();
    m_historyRequester = std::make_shared<CachedHistoryRequester<>>(m_recordingsInfo->id, normalizer, cache);

    uint32_t cacheDepthMs = 0; 
    uint32_t cacheUpdatePeriodSeconds = 0;
    fetchParams(cacheDepthMs, cacheUpdatePeriodSeconds);
    m_historyRequester2 = std::make_shared<CachedHistoryRequester2>(GET_LOGGER_PTR, m_id, m_dynExec,
        cacheDepthMs, cacheUpdatePeriodSeconds,
        boost::bind(&StorageSource::performAsyncRecordingSearch, this, _1, _2, _3, _4));

    m_fairPresentationRangePolicy = std::make_unique<CFairPresentationRangePolicy>(historyInterval_t(), commonExpTime);
}

}

