#ifndef DEVICEIPINT3_CACHEDHISTORYREQUESTER_H
#define DEVICEIPINT3_CACHEDHISTORYREQUESTER_H

#include <ItvSdk/include/baseTypes.h>
#include <ItvDeviceSdk/include/deviceBaseTypes.h>

#include <chrono>
#include <mutex>
#include <set>

#include <CorbaHelpers/GccUtils.h>

GCC_SUPPRESS_WARNING_BEGIN((-Wunused-local-typedefs))
#include <boost/icl/interval_set.hpp>
#include <boost/icl/interval_map.hpp>
GCC_SUPPRESS_WARNING_END()

#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/signals2.hpp>

#include "StorageDataTypes.h"
#include <Logging/log2.h>
#include <Primitives/Executors/DynamicThreadPool.h>
#include <Primitives/CorbaHelpers/Reactor.h>

#include "DeviceIpint_Exports.h"

#ifdef _MSC_VER
#pragma warning(push)
// warning C4251: <data member>: <type> needs to have dll-interface to
#pragma warning(disable : 4251)
#endif

namespace IPINT30
{

typedef boost::icl::right_open_interval<ITV8::timestamp_t> historyInterval_t;
typedef boost::icl::interval_set<ITV8::timestamp_t, std::less, historyInterval_t> historyIntervalSet_t;
typedef boost::optional<historyIntervalSet_t> optionalIntervalSet_t;

struct SCalendar
{
    SCalendar() :
        Added(boost::posix_time::not_a_date_time),
        Interval(historyInterval_t())
    {
    }

    void Clear()
    {
        Calendar.clear();
        Added = boost::posix_time::not_a_date_time;
        Interval = historyInterval_t();
    }

    boost::posix_time::ptime Added;
    ITV8::Utility::calendarList_t Calendar;
    IPINT30::historyInterval_t Interval;
};

struct DEVICEIPINT_TESTABLE_DECLSPEC TrackHistoryRange
{
    explicit TrackHistoryRange();
    explicit TrackHistoryRange(const historyIntervalSet_t& intervalSet_);

    boost::posix_time::ptime timestamp;
    historyIntervalSet_t intervalSet;
};

DEVICEIPINT_TESTABLE_DECLSPEC bool operator==(const TrackHistoryRange&, const TrackHistoryRange&);
DEVICEIPINT_TESTABLE_DECLSPEC std::ostream& operator<<(std::ostream&, const TrackHistoryRange&);

typedef boost::icl::interval_map<ITV8::timestamp_t, 
    TrackHistoryRange,
    boost::icl::partial_absorber,
    std::less,
    boost::icl::inplace_identity,
    boost::icl::inter_section,
    historyInterval_t
> recordingsTimeline_t;

namespace posix_time = boost::posix_time;

class DEVICEIPINT_TESTABLE_DECLSPEC DefaultExpirationPolicy
{
public:
    typedef posix_time::time_duration time_duration_t;
    explicit DefaultExpirationPolicy( time_duration_t const& commonExpiration = posix_time::minutes(10),
        time_duration_t const& liveRecordingExpiration = posix_time::seconds(30));

    bool operator()(const recordingsTimeline_t::value_type& entry) const;

    bool operator()(const boost::posix_time::ptime& current) const;

private:
    time_duration_t m_commonExpiration;
    time_duration_t m_liveRecordingExpiration;
};

// Maps list of recordings information to history interval.
// For each known interval (beginDate, endDate) it will hold
// list of time intervals with recorded data.
template<typename TExpirationdPolicy = DefaultExpirationPolicy>
class RecordingsHistoryCache
{
public:
    explicit RecordingsHistoryCache(const TExpirationdPolicy& expiredPolicy 
            = TExpirationdPolicy());

    // Adds recording information for a history range
    void add(const historyInterval_t& range,
        const historyIntervalSet_t& records);

    void add(const ITV8::Utility::calendarList_t& dates, historyInterval_t interval);

    // Tests if we have recorded data for specified time point.
    bool contains(ITV8::timestamp_t timestamp) const;

    // Provides a range with earliest and oldest recording times.
    historyInterval_t presentationRange() const;

    ITV8::Utility::calendarList_t calendar(historyInterval_t requestedInterval) const;

    // Gets data recordings information for specified history interval. 
    // Returns empty optionalIntervalSet_t if this object
    // does not know passed history range.
    optionalIntervalSet_t get(const historyInterval_t& requestedInterval) const;

    void clear();

private:
    recordingsTimeline_t m_history;
    SCalendar m_calendar;
    TExpirationdPolicy m_expired;
};

DEVICEIPINT_TESTABLE_DECLSPEC historyInterval_t make_interval(const ITV8::GDRV::DateTimeRange& range);
DEVICEIPINT_TESTABLE_DECLSPEC ITV8::GDRV::DateTimeRange makeIpintTimeRange(historyInterval_t interval);

// Adjusts requested time interval to edges of whole day (24 hour).
// It improves efficiency of device cache, by absorbing different but
// similar (with only little time difference) interval requests.
// Short intervals are rounded to begin and end of hour.
struct DEVICEIPINT_TESTABLE_DECLSPEC DefaultIntervalNormalizer
{
    historyInterval_t operator()(historyInterval_t requestedInterval, bool normalizeForCalendar = false) const;
};

// Prevents excessively frequent requests to device
// blocking execution if it is necessary. 
class DEVICEIPINT_TESTABLE_DECLSPEC RequestStormGuard
{
public:
    explicit RequestStormGuard(posix_time::time_duration threshold =
        posix_time::seconds(1));

    void beginRequest();
    void endRequest();

    class ScopedGuard
    {
    public:
        ScopedGuard(RequestStormGuard& parent);
        ~ScopedGuard();

    private:
        RequestStormGuard& m_parent;
    };

private:
    const posix_time::time_duration m_timeThreshold;
    posix_time::ptime m_lastTime;
};

// Requests device for recordings history. Minimizes 
// actual requests to device by maintaining cache with requests result.
// Class is fully thread safe.
template<typename TNormalizer = DefaultIntervalNormalizer,
    typename THistoryCache = RecordingsHistoryCache<> >
class CachedHistoryRequester
{
public:
    explicit CachedHistoryRequester(const std::string& recordingId, 
        const TNormalizer& normalizer = TNormalizer(),
        const THistoryCache& cache = THistoryCache());

    // Tests whether cache contains recording data for specified time point.
    bool containsRecordForTime(ITV8::timestamp_t timestamp) const;

    // Provides a range with earliest and oldest recording times in cache.
    historyInterval_t presentationRange() const;

    // Do actual request for recordings for specified time interval in history.
    // If internal cache contains valid record for this time slot result will return
    // immediately without asking device.
    template<typename TRequester>
    historyIntervalSet_t findRecordings(TRequester& deviceRequester, const std::string& trackId,
        historyInterval_t requestedInterval, DECLARE_LOGGER_ARG);

    template<typename TRequester>
    ITV8::Utility::calendarList_t getCalendar(TRequester& deviceRequester,
        historyInterval_t requestedInterval);

    void clearCache();

    void stop();

private:
    optionalIntervalSet_t requestCache(historyInterval_t requestedInterval);

    void updateRequestCount(bool increaseOnly = true);

private:
    const std::string m_recordingId;
    TNormalizer m_normalizer;
    THistoryCache m_historyCache;
    mutable boost::shared_mutex m_historyGuard;
    RequestStormGuard m_requestStormGuard;
    boost::signals2::signal<void()> m_stopDeviceSearchSignal;
    std::mutex m_stopConditionGuard;
    std::condition_variable m_stopCondition;
    std::atomic<uint32_t> m_requestCount;
    std::atomic_bool m_stopped;
};

typedef std::function<void(const historyIntervalSet_t&)> historyIntervalSetHandler_t;
typedef std::function<void(ITV8::hresult_t)> finishedHandler_t;

class StorageSource;
class CachedHistoryRequester2 : public NLogging::WithLogger
{
private:
    enum CacheState
    {
        CS_STOPPED = 0,
        CS_STOP_REQUESTED,
        CS_WORKING
    };

public:
    enum RequestType
    {
        RT_USER_REQUEST = 0,
        RT_NORMALIZER_REQUEST,
        RT_UPDATE_CACHE_REQUEST
    };

    struct HistoryRequest
    {
        historyInterval_t interval;
        RequestType type = RT_USER_REQUEST;
		std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();

        HistoryRequest() {}
        HistoryRequest(const historyInterval_t& interval_, RequestType type_ = RT_USER_REQUEST) :
            interval(interval_),
            type(type_)
        { }

        bool operator< (const HistoryRequest& r) const
        {
			return std::tie(type, timestamp) < std::tie(r.type, r.timestamp);
        }

    };

    class RequestsSet : public std::multiset<HistoryRequest>
    {
    public:
        typedef std::multiset<HistoryRequest> base_t;

    public:
        iterator erase(base_t::const_iterator e);
        void insert(const HistoryRequest& r);
        void emplace(HistoryRequest&& r);
        void clear();

        const historyIntervalSet_t& historyIntervals() const;

    private:
        historyIntervalSet_t m_requestsSet;
    };

    struct DEVICEIPINT_TESTABLE_DECLSPEC STweaks
    {
        // Update cache interval.
        boost::posix_time::time_duration UpdateCacheTimeout;

        // This timeout required to prevent storm of requests after m_history end time.
        std::chrono::milliseconds RecentRequestInterval;

        // This timeout charges if Ipint search ends with empty result
        // (ITV8::ENotError/0 recordings) and requested interval is in
        // neighborhood of now. GetHistory2 result will be HR_FULL
        // until this timeout expires.
        std::chrono::milliseconds EmptyResultTrustInterval;

        uint32_t CacheDepthMs;

        STweaks();

        STweaks& operator=(const STweaks& copy);
    };

    class CPerformedRequests
    {
    public:
        CPerformedRequests() = default;

        void Clear();

        void Add(historyInterval_t interval);

        void AddEmpty(historyInterval_t intervalWithEmptyResults, std::chrono::milliseconds expiration);

        template <typename TInterval>
        void Subtract(TInterval interval);

        bool AdjustToPerformedEmptyResults(historyInterval_t requestedInterval, historyIntervalSet_t& nonPerformedRequests);

        historyIntervalSet_t Get() const;

    private:
        std::chrono::steady_clock::time_point m_emptyResultsExpireTime;
        historyIntervalSet_t m_performedRequests;
        historyIntervalSet_t m_performedEmptyRequests;
    };

    typedef std::function<void(const historyInterval_t&, historyIntervalSetHandler_t, finishedHandler_t, boost::signals2::signal<void()>&)> recordingSearchHandler_t;

public:
    CachedHistoryRequester2(NLogging::ILogger* logger, const std::string& id, NExecutors::PDynamicThreadPool dynExec,
        uint32_t cacheDepthMs, uint32_t cacheUpdatePeriodSeconds, recordingSearchHandler_t searcher);
    ~CachedHistoryRequester2();

    // Non-blocking request actual recordings.
    // If internal cache contains valid record for this time slot result will return immediately without asking device.
    // If internal cache does not contain valid records then do in background(without blocking caller thread)
    // request for recordings for specified time interval in history.
    // Returns false if cannot schedule fetch a history intervals for requested range, otherwise returns true.
    bool getRecordings(historyInterval_t requestedInterval, unsigned int maxCount, unsigned long long minGapMs, historyIntervalSet_t& result, bool& hasFullRequestedHistory);

    // Stop all internal operation, blocking method.
    // This call also will send IRecordingSearch::CancelFindRecordings and will wait until it operation finished.
    void stop();

    // Blocking wait until all currently requested history operations are finished.
    // This method now used only in UT.
    void waitBackgrounActions();

    // Clear internal cache.
    // Performs stop and after clear cache.
    void clearCache();

    void UT_tweak(const STweaks& tweaks);

private:
    bool getCachedHistoryLocked(const historyInterval_t& requestedInterval, historyIntervalSet_t& outResult, historyIntervalSet_t& nonPerformedRequests) const;
    void addSearchJobLocked(historyInterval_t requestedInterval, RequestType type = RT_USER_REQUEST);
    void doRecordingSearch(HistoryRequest historyRequest);
    HistoryRequest pickRequestLocked();
    void moveUpdateCacheRange();
    bool performStopRequestLocked(const std::string& requester);
    void chargeUpdateTimer();
    void updateTimerHandler(const boost::system::error_code& error);
    void updateCacheLocked(historyInterval_t requestedInterval, const historyIntervalSet_t& records);
	void internalStop();
    void changeStateToStoppedLocked(const std::string& requester);
    bool isReachedRequestsDepth(const std::string& requester);
    historyIntervalSet_t splitIntervalLocked(const historyInterval_t& interval);

    void scheduleSearchJob(const historyInterval_t& requestedInterval,
        uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
        const historyIntervalSet_t& nonPerformedRequests = historyIntervalSet_t());
    void scheduleUpdateCacheJob();

private:
    NExecutors::PDynamicThreadPool m_dynExec;
    NCorbaHelpers::PReactor        m_reactor;
    boost::asio::deadline_timer    m_updateCacheTimer;
    STweaks                        m_tweaks;

    DefaultIntervalNormalizer m_normalizer;
    recordingSearchHandler_t m_searcher;

    // State for background actions
    CacheState                 m_state;
    std::condition_variable    m_stopCondition;

    RequestsSet                m_requests;
    std::mutex                 m_mutex;
    bool                       m_searchInProgress;
    boost::posix_time::ptime   m_lastUpdateCacheTime;
    historyInterval_t          m_updateCacheRange;

    historyIntervalSet_t m_history;
    CPerformedRequests m_performedRequests;

    historyIntervalSet_t                  m_recentRequests;
    std::chrono::steady_clock::time_point m_recentRequestsExpiration;

    historyInterval_t                m_searchingRange;

    boost::signals2::signal<void()> m_stopDeviceSearchSignal;

    uint16_t    m_unsuccessIpintSearchCount;
};

typedef boost::shared_ptr<ITV8::Utility::RecordingRange> RecordingRangeSP;
typedef std::vector<RecordingRangeSP> rangeList_t;
DEVICEIPINT_TESTABLE_DECLSPEC historyIntervalSet_t makeHistoryIntervalSet(rangeList_t results,
                        const std::string& recordingId, const std::string& trackId, DECLARE_LOGGER_ARG);

DEVICEIPINT_TESTABLE_DECLSPEC historyIntervalSet_t makeHistoryIntervalSet(RecordingRangeSP result,
                        const std::string& recordingId, const std::string& trackId, DECLARE_LOGGER_ARG);

// Tries to get timestamps of earliest and oldest data records in storage
// using information provided by device and local cache.
DEVICEIPINT_TESTABLE_DECLSPEC historyInterval_t getPresentationRange(historyInterval_t cachePresentation,
                                     historyInterval_t devicePresentation);

std::string rangeToString(const historyInterval_t& r);

std::string rangesToString(const historyIntervalSet_t& records);

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif

