#include "CachedHistoryRequester.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

#include "TimeStampHelpers.h"
#include <Logging/Log3.h>
#include <Logging/Printers.h>
#include <CorbaHelpers/Envar.h>

#include "StorageSource.h"

namespace IPINT30
{

namespace
{

const uint64_t HOUR_DURATION_MS = 60 * 60 * 1000;
const uint16_t MAX_IPINT_SEARCH_COUNT = 3;

// GetHistory2 part
static constexpr uint64_t OVERHEAD_FROM_NOW_MS = 10ull * 1000;
static constexpr uint64_t ROUND_TO_TIME_MS = 60ull * 60 * 1000;

const std::chrono::seconds REQUESTS_DEPTH(30);
const std::chrono::milliseconds EMPTY_RESULT_TTL(30000);

}

bool operator==(const TrackHistoryRange& left, const TrackHistoryRange& right)
{ 
    return (left.timestamp == right.timestamp) && 
        (left.intervalSet == right.intervalSet);
}

std::ostream& operator<<(std::ostream& stream, const TrackHistoryRange& range)
{
    stream << /*" time=" <<range.timestamp << " list=" << */range.intervalSet;
    return stream;
}

std::ostream& operator<<(std::ostream& os, const CachedHistoryRequester2::STweaks& t)
{
    return os
        << "UpdateCacheTimeout=" << t.UpdateCacheTimeout
        << ", RecentRequestIntervalMs=" << t.RecentRequestInterval.count()
        << ", EmptyResultTrustIntervalMs=" << t.EmptyResultTrustInterval.count()
        << ", CacheDepthMs=" << t.CacheDepthMs;
}

historyInterval_t make_interval(const ITV8::GDRV::DateTimeRange& range)
{
    return historyInterval_t(range.rangeBegin,
        range.rangeEnd);
}

ITV8::GDRV::DateTimeRange makeIpintTimeRange(historyInterval_t interval)
{
    ITV8::GDRV::DateTimeRange result = {interval.lower(), interval.upper()};
    return result; 
}

TrackHistoryRange::TrackHistoryRange(const historyIntervalSet_t& intervalSet_) :
    timestamp(boost::posix_time::second_clock::universal_time()),
    intervalSet(intervalSet_)
{
}

TrackHistoryRange::TrackHistoryRange()
{
}

DefaultExpirationPolicy::DefaultExpirationPolicy(time_duration_t const& commonExpiration /*= posix_time::minutes(10)*/,
                                                 time_duration_t const& liveRecordingExpiration /*= posix_time::seconds(30)*/) :
    m_commonExpiration(commonExpiration),
    m_liveRecordingExpiration(liveRecordingExpiration)
{
}

bool DefaultExpirationPolicy::operator()(const recordingsTimeline_t::value_type& entry) const
{
    // If requested interval is close to real time,
    // we need to shorten expiration time duration to handle
    // possibly recording data.
    using namespace boost::posix_time;
    const ptime now = second_clock::universal_time();
    const ptime requestedIntervalBorder = ipintTimestampToPtime(entry.first.upper());
    const bool liveRecordingStateRequested = (now - requestedIntervalBorder) < hours(1);
    
    const time_duration_t expirationThreshold = (liveRecordingStateRequested ? 
        m_liveRecordingExpiration : m_commonExpiration);

    return (now - entry.second.timestamp) > expirationThreshold;
}

bool DefaultExpirationPolicy::operator()(const boost::posix_time::ptime& current) const
{
    const auto now = boost::posix_time::second_clock::universal_time();
    return current.is_not_a_date_time() || ((now - current) > m_commonExpiration);
}

historyInterval_t DefaultIntervalNormalizer::operator()(
    historyInterval_t requested, bool normalizeForCalendar) const
{
    using namespace boost::posix_time;
    auto begin = ipintTimestampToPtime(requested.lower());
    if (requested.upper() - requested.lower() < HOUR_DURATION_MS && !normalizeForCalendar)
    {
        auto hourBegin = ptime(begin.date(), hours(begin.time_of_day().hours()));

        auto end = ipintTimestampToPtime(requested.upper());
        auto hourEnd = ptime(end.date(), hours(end.time_of_day().hours() + 1));
        return historyInterval_t(toIpintTime(hourBegin), toIpintTime(hourEnd));
    }

    auto end = ipintTimestampToPtime(requested.upper()).date() + boost::gregorian::days(1);
    return historyInterval_t(toIpintTime(begin.date()), toIpintTime(end));
}

historyIntervalSet_t makeHistoryIntervalSet(rangeList_t results,
                const std::string& recordingId, const std::string& trackId, DECLARE_LOGGER_ARG)
{
    const auto origCount = results.size();
    _dbg_ << __FUNCTION__ << " Intervals size=" << origCount;
    // Debug print
    for (auto& r : results)
    {
        for (const auto& t : r->tracksRange)
        {
            _dbg_ << "Returned result from driver. recordingId=" << r->GetId() << ", trackId=" << t.id
                << ", range: (" << ipintTimestampToIsoString(t.range.rangeBegin)
                << ", " << ipintTimestampToIsoString(t.range.rangeEnd) << ")";
        }
    }

    // Create flat tracks range list
    typedef ITV8::Utility::TrackRange tracksRange_t;
    typedef ITV8::Utility::tracksRangeList_t tracksRangeList_t;
    tracksRangeList_t tracksList;
    BOOST_FOREACH(RecordingRangeSP range, results)
    {
        // Copy history from current video track
        std::remove_copy_if(range->tracksRange.begin(), range->tracksRange.end(),
            back_inserter(tracksList),
            boost::bind(&tracksRange_t::id, _1) != trackId);
    }

    if (origCount && tracksList.empty())
    {
        _wrn_ << "Can't locate video track in device search results! Driver returns result for wrong track? Looking for track id : " << trackId;
    }

    historyIntervalSet_t intevalSet;
    BOOST_FOREACH(const tracksRange_t& range, tracksList)
    {        
        intevalSet += make_interval(range.range);
    }
    return intevalSet;
}

historyIntervalSet_t makeHistoryIntervalSet(RecordingRangeSP result,
                const std::string& recordingId, const std::string& trackId, DECLARE_LOGGER_ARG)
{
    const auto origCount = result->tracksRange.size();
    for (const auto& t : result->tracksRange)
    {
        _dbg_ << "Returned result from driver. recordingId=" << result->GetId() << ", trackId=" << t.id
            << ", range: (" << ipintTimestampToIsoString(t.range.rangeBegin)
            << ", " << ipintTimestampToIsoString(t.range.rangeEnd) << ")";
    }
    
    // Create flat tracks range list
    typedef ITV8::Utility::TrackRange tracksRange_t;
    typedef ITV8::Utility::tracksRangeList_t tracksRangeList_t;
    tracksRangeList_t tracksList;

    // Copy history from current video track
    std::remove_copy_if(result->tracksRange.begin(), result->tracksRange.end(),
        back_inserter(tracksList),
        boost::bind(&tracksRange_t::id, _1) != trackId);
    
    if (origCount && tracksList.empty())
    {
        _wrn_ << "Can't locate video track in device search results! Driver returns result for wrong track? Looking for track id : " << trackId;
    }

    historyIntervalSet_t intevalSet;
    for (const auto& range: tracksList)  
        intevalSet += make_interval(range.range);
    
    return intevalSet;
}

void RequestStormGuard::beginRequest()
{
    if (m_lastTime.is_not_a_date_time())
        return;
    using namespace boost::posix_time;
    const ptime now = second_clock::universal_time();
    const time_duration deptTime = m_timeThreshold - (now - m_lastTime);
    if (!deptTime.is_negative())
    {
        boost::this_thread::sleep(deptTime);
    }
}

void RequestStormGuard::endRequest()
{
    using namespace boost::posix_time;
    m_lastTime = second_clock::universal_time();
}

RequestStormGuard::RequestStormGuard(posix_time::time_duration threshold 
                                     /*= posix_time::seconds(3)*/) :
    m_timeThreshold(threshold)
{

}

RequestStormGuard::ScopedGuard::ScopedGuard(RequestStormGuard& parent) :
    m_parent(parent)
{
    m_parent.beginRequest();
}

RequestStormGuard::ScopedGuard::~ScopedGuard()
{
    m_parent.endRequest();
}

IPINT30::historyInterval_t getPresentationRange(historyInterval_t cachePresentation,
                                              historyInterval_t devicePresentation)
{
    using namespace boost::icl;
    if (is_empty(devicePresentation))
    {
        return cachePresentation;
    }
    if (is_empty(cachePresentation))
    {
        return devicePresentation;
    }
    return hull(devicePresentation, cachePresentation);
}


std::vector<historyInterval_t> splitInterval(historyInterval_t requestedInterval, uint64_t splitBy)
{
    std::vector<historyInterval_t> result;
    if (requestedInterval.upper() == requestedInterval.lower())
        return result;

    if (requestedInterval.upper() - requestedInterval.lower() < splitBy)
    {
        result.emplace_back(requestedInterval);
        return result;
    }

    auto currentLower = requestedInterval.lower();
    do
    {
        result.emplace_back(historyInterval_t(currentLower, currentLower + splitBy));
        currentLower += splitBy;
    }
    while (currentLower < requestedInterval.upper());

    if (currentLower != requestedInterval.upper())
        result.emplace_back(historyInterval_t(currentLower, requestedInterval.upper()));   

    return result;
}

std::string rangeToString(const historyInterval_t& r)
{
    if (boost::icl::is_empty(r))
        return "empty";

    return fmt::format(FMT_STRING("[{}, {}]"), ipintTimestampToIsoString(r.lower()), ipintTimestampToIsoString(r.upper()));
}

std::string rangesToString(const historyIntervalSet_t& records)
{
    std::ostringstream oss;
    for (auto r : records)
        oss << rangeToString(r);

    auto str = oss.str();
    return str.empty() ? "empty" : str;
}

std::string requestTypeToString(CachedHistoryRequester2::RequestType requestType)
{
    switch (requestType)
    {
    case CachedHistoryRequester2::RT_USER_REQUEST:
        return "user_request";
    case CachedHistoryRequester2::RT_NORMALIZER_REQUEST:
        return "normalizer_request";
    case CachedHistoryRequester2::RT_UPDATE_CACHE_REQUEST:
        return "update_cache_request";
    default:
        break;
    }

    std::terminate();
}

uint64_t systemTimeMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// CachedHistoryRequester2 part
CachedHistoryRequester2::STweaks::STweaks():
    UpdateCacheTimeout(boost::posix_time::seconds(1ull * 60 * 60)),
    RecentRequestInterval(std::chrono::milliseconds(10ull * 1000)),
    EmptyResultTrustInterval(std::chrono::milliseconds(3ull * 10 * 1000)),
    CacheDepthMs(0)
{
}

CachedHistoryRequester2::STweaks& CachedHistoryRequester2::STweaks::operator=(const CachedHistoryRequester2::STweaks& copy)
{
    this->UpdateCacheTimeout = copy.UpdateCacheTimeout;
    this->RecentRequestInterval = copy.RecentRequestInterval;
    this->EmptyResultTrustInterval = copy.EmptyResultTrustInterval;
    this->CacheDepthMs = copy.CacheDepthMs;
    return *this;
}

void CachedHistoryRequester2::CPerformedRequests::Clear()
{
    m_performedRequests.clear();
    m_performedEmptyRequests.clear();
}

void CachedHistoryRequester2::CPerformedRequests::Add(historyInterval_t interval)
{
    m_performedRequests |= interval;
}

void CachedHistoryRequester2::CPerformedRequests::AddEmpty(historyInterval_t intervalWithEmptyResults,
    std::chrono::milliseconds expiration)
{
    m_performedEmptyRequests |= intervalWithEmptyResults;
    m_emptyResultsExpireTime = std::chrono::steady_clock::now() + expiration;
}

template <typename TInterval>
void CachedHistoryRequester2::CPerformedRequests::Subtract(TInterval interval)
{
    m_performedRequests ^= interval;
}

bool CachedHistoryRequester2::CPerformedRequests::AdjustToPerformedEmptyResults(historyInterval_t requestedInterval,
    historyIntervalSet_t& nonPerformedRequests)
{
    if (m_performedEmptyRequests.empty())
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (m_emptyResultsExpireTime <= now)
    {
        m_performedEmptyRequests.clear();
        return false;
    }

    // Strip right side from requested interval if it beyond of last performed empty
    requestedInterval = historyInterval_t(requestedInterval.lower(),
        std::min(requestedInterval.upper(), m_performedEmptyRequests.rbegin()->upper()));

    // Find intersection with performed empty results
    historyIntervalSet_t result;
    boost::icl::add_intersection(result, m_performedEmptyRequests, requestedInterval);

    // Check if we have unprocessed parts from left
    result ^= requestedInterval;
    if (result.empty())
    {
        return true;
    }

    // Check if result is a part of performed requests
    if (boost::icl::contains(m_performedRequests, result))
    {
        return true;
    }

    nonPerformedRequests |= result;
    return false;
}

historyIntervalSet_t CachedHistoryRequester2::CPerformedRequests::Get() const
{
    return m_performedRequests;
}

CachedHistoryRequester2::CachedHistoryRequester2(NLogging::ILogger* logger, const std::string& id, 
            NExecutors::PDynamicThreadPool dynExec, uint32_t cacheDepthMs, uint32_t cacheUpdatePeriodSeconds, recordingSearchHandler_t searcher) :
    NLogging::WithLogger(logger, fmt::format(FMT_STRING("CachedHistoryRequester2.{}[{}]"), static_cast<void*>(this), id)),
    m_dynExec(dynExec),
    m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
    m_updateCacheTimer(m_reactor->GetIO()),
    m_searcher(searcher),
    m_state(CS_STOPPED),
    m_searchInProgress(false),
    m_lastUpdateCacheTime(boost::posix_time::microsec_clock::universal_time()),
    m_unsuccessIpintSearchCount(0)
{
    m_tweaks.CacheDepthMs = cacheDepthMs;
    if (cacheUpdatePeriodSeconds != 0)
    {
        m_tweaks.UpdateCacheTimeout = boost::posix_time::seconds(cacheUpdatePeriodSeconds);
    }

    scheduleUpdateCacheJob();

    _dbg_ << "Created with parent id=" << id << ", " << m_tweaks;
}

CachedHistoryRequester2::~CachedHistoryRequester2()
{
    stop();
    _dbg_ << "Destroyed";
}

CachedHistoryRequester2::HistoryRequest CachedHistoryRequester2::pickRequestLocked()
{
    if (m_requests.empty())
        std::terminate();
  
    HistoryRequest req = *m_requests.begin();
    m_requests.erase(m_requests.begin());

    m_searchInProgress = true;
    return req;
}

historyIntervalSet_t mergeIntervals(const historyIntervalSet_t& result, uint64_t minGapMs)
{
    NGP_ASSERT(result.iterative_size() > 1);

    historyIntervalSet_t mergedResult;
    auto left = result.begin();
    auto right = left;
    ++right;

    auto isMergeable = [minGapMs](historyInterval_t left, historyInterval_t right)
    {
        return (right.lower() - left.upper()) < minGapMs;
    };

    historyInterval_t newRange;
    do
    {
        bool merged = false;
        if (!boost::icl::is_empty(newRange))
        {
            if (isMergeable(newRange, *right))
                newRange = historyInterval_t(newRange.lower(), right->upper());
            else
            {
                mergedResult.insert(newRange);
                newRange = historyInterval_t();
            }
        }
        else if (isMergeable(*left, *right))
        {
            merged = true;
            newRange = historyInterval_t(left->lower(), right->upper());
        }
        else
        {
            mergedResult.insert(*left);
        }

        ++left;
        ++right;

        // insert last result if non merged
        if (!merged && right == result.end())
            mergedResult.insert(*left);

    } while (right != result.end());

    if (!boost::icl::is_empty(newRange))
        mergedResult.insert(newRange);

    return mergedResult;
}

void roundTo(historyInterval_t& requestedInterval, uint64_t duration, uint64_t now, uint64_t overheadFromNow)
{
    uint64_t requestedIntervalDuration = requestedInterval.upper() - requestedInterval.lower();
    if (requestedIntervalDuration > duration)
        return;

    uint64_t adjust = (duration - requestedIntervalDuration) / 2;

    ITV8::timestamp_t upper = requestedInterval.upper();
    if (requestedInterval.upper() + adjust > now + overheadFromNow)
        upper = requestedInterval.upper() + overheadFromNow;

    ITV8::timestamp_t lower = requestedInterval.lower() > adjust ? (requestedInterval.lower() - adjust) : requestedInterval.lower();
    requestedInterval = historyInterval_t(lower, upper);
}

historyIntervalSet_t CachedHistoryRequester2::splitIntervalLocked(const historyInterval_t& interval)
{
    using namespace boost;
    historyIntervalSet_t result;

    if (!m_history.empty())
        icl::add_intersection(result, m_history, interval);

    if (!m_performedRequests.Get().empty())
        icl::add_intersection(result, m_performedRequests.Get(), interval);

    if (!result.empty())
        result ^= interval;
    else
        result |= interval;

    return result;
}

void CachedHistoryRequester2::addSearchJobLocked(historyInterval_t requestedInterval, RequestType type)
{
    if (performStopRequestLocked(__FUNCTION__))
        return;

    using namespace boost;
    if (!icl::is_empty(m_searchingRange) && icl::contains(m_searchingRange, requestedInterval))
        return;

    if (isReachedRequestsDepth(__FUNCTION__))
        return;

    historyIntervalSet_t splittedIntervals;

    // for update cache request not required split intervals with using m_history and m_performedRequsts
    if (RT_UPDATE_CACHE_REQUEST == type)
        splittedIntervals |= requestedInterval;
    else 
        splittedIntervals = splitIntervalLocked(requestedInterval);

    if (splittedIntervals.empty())
    {
        if (icl::is_empty(m_searchingRange) && m_requests.empty())
            changeStateToStoppedLocked(__FUNCTION__);

        return;
    }

    // skip the same or less interval requests 
    historyIntervalSet_t intersect = splittedIntervals;
    std::vector<RequestsSet::iterator> intersectList;

    for (auto interval = splittedIntervals.begin(); interval != splittedIntervals.end(); ++interval)
    {
        for (auto request = m_requests.begin(); request != m_requests.end(); ++request)
        {
            if (boost::icl::intersects(request->interval, *interval))
            {
                intersectList.push_back(request);
                intersect |= request->interval;
            }

            if (boost::icl::contains(request->interval, *interval))
            {
                if (request->type > type)
                {
                    HistoryRequest newRequest;
                    newRequest.interval = request->interval;
                    newRequest.type = type;

                    m_requests.erase(request);
                    m_requests.insert(newRequest);
                }

                _dbgf_("Request queue already contains requested interval {}, requestType {}", rangeToString(*interval), requestTypeToString(type));

                if (icl::is_empty(m_searchingRange) && m_requests.empty())
                    changeStateToStoppedLocked(__FUNCTION__);

                return;
            }
        }

        if (!intersectList.empty())
        {
            HistoryRequest r = *intersectList.front();
            r.interval = *intersect.begin();

            for (auto elem : intersectList)
                m_requests.erase(elem);

            if (r.type > type)
                r.type = type;

            m_requests.insert(r);
            _dbgf_("Added request merged {}, requestType {}, size {}", rangeToString(r.interval),
                requestTypeToString(type), m_requests.size());
            return;
        }

        m_requests.emplace(std::move(HistoryRequest(*interval, type)));
        _dbgf_("Added request {}, requestType {}, size {}", rangeToString(*interval),
            requestTypeToString(type), m_requests.size());
    }

    if (m_searchInProgress)
        return;

    auto historyRequest = pickRequestLocked();
    m_dynExec->PostTask(std::bind(&CachedHistoryRequester2::doRecordingSearch, this, historyRequest));
}

void CachedHistoryRequester2::doRecordingSearch(CachedHistoryRequester2::HistoryRequest historyRequest)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // stop requested
    if (performStopRequestLocked(__FUNCTION__))
        return;

    if (!m_searcher)
        std::terminate();

    const auto requestedInterval = historyRequest.interval;

    auto updateCacheRecords = std::make_shared<historyIntervalSet_t>();
    auto foundRecords = std::make_shared<historyIntervalSet_t>();

    m_stopDeviceSearchSignal.disconnect_all_slots();
    m_searchingRange = requestedInterval;
    _dbgf_("Start Ipint search: requested interval {}", rangeToString(requestedInterval));
    NLogging::StopWatch sw;

    std::function<void(const historyIntervalSet_t&)> historyHandler =
        [this, requestedInterval, historyRequest, updateCacheRecords, foundRecords](const historyIntervalSet_t& records)
        {
            if (records.empty())
                return;

            if (historyRequest.type == RT_UPDATE_CACHE_REQUEST)
            {
                (*updateCacheRecords) |= records;
                return;
            }

            auto recentRequestsLambda = [this]()
            {
                if (m_recentRequests.empty())
                    return;

                if (m_recentRequests.rbegin()->upper() <= m_history.rbegin()->upper())
                {
                    m_recentRequests.clear();
                    return;
                }

                historyInterval_t recentRequestsRange(m_history.rbegin()->upper(), m_recentRequests.rbegin()->upper());

                historyIntervalSet_t recentRequests;
                boost::icl::add_intersection(recentRequests, m_recentRequests, recentRequestsRange);
                m_recentRequests = recentRequests;
            };

            (*foundRecords) |= records;

            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_history.empty())
            {
                m_history |= records;
                m_lastUpdateCacheTime = boost::posix_time::microsec_clock::universal_time();
                moveUpdateCacheRange();
                chargeUpdateTimer();
                recentRequestsLambda();
                return;
            }

            historyInterval_t foundRange(records.begin()->lower(), records.rbegin()->upper());

            m_history |= records;
            m_performedRequests.Add(foundRange);

            m_searchingRange = boost::icl::left_subtract(m_searchingRange, foundRange & m_searchingRange);

            recentRequestsLambda();
        };

    std::function<void(ITV8::hresult_t)> finishedHandler = [this, requestedInterval, sw, foundRecords, updateCacheRecords, historyRequest](ITV8::hresult_t code)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            bool ipintSearchSuccess = (code == ITV8::ENotError);

            m_searchingRange = historyInterval_t();

            if (!ipintSearchSuccess)
            {
                _wrnf_("Ipint search {}/{} failed with code {}", ++m_unsuccessIpintSearchCount,
                    MAX_IPINT_SEARCH_COUNT, code);
            }
            else
            {
                m_unsuccessIpintSearchCount = 0;
            }

            const auto recordingsFound = !foundRecords->empty()
                || (!updateCacheRecords->empty() && historyRequest.type == RT_UPDATE_CACHE_REQUEST);
            if (recordingsFound)
            {
                _dbgf_("Successfully performed Ipint search: requested interval {}, elapsed time {}, found records {}, history {}",
                    rangeToString(requestedInterval),
                    NLogging::PrintDuration(sw.ElapsedMs()),
                    foundRecords->iterative_size(),
                    rangesToString(m_history));
            }
            else
            {
                _dbgf_("Performed Ipint search with empty result: requested interval {}, elapsed time {}",
                    rangeToString(requestedInterval),
                    NLogging::PrintDuration(sw.ElapsedMs()));
            }

            // stop requested
            if (performStopRequestLocked(__FUNCTION__))
                return;

            if (historyRequest.type == RT_UPDATE_CACHE_REQUEST)
                updateCacheLocked(requestedInterval, *updateCacheRecords);

            if (ipintSearchSuccess)
            {
                // This is special heuristic for processing requests near of now() time. We expect that
                // after EMPTY_RESULT_TTL time device will return new data. Adding requested interval
                // to performed empty requests means, that next client request intervals which
                // intersects requestedInterval will be completed with HR_FULL result until
                // EmptyResultTrustInterval expires
                const auto now = systemTimeMs();
                const historyInterval_t nowNeighbourhood(now - EMPTY_RESULT_TTL.count() * 2, now + EMPTY_RESULT_TTL.count());
                // No need to process intervals from future, strip right side of requested interval according of nowEnvironment
                auto croppedFromRight = historyInterval_t(requestedInterval.lower(), std::min(requestedInterval.upper(), nowNeighbourhood.upper()));

                // No intersection: add croppedFromRight to performed requests
                if (boost::icl::disjoint(croppedFromRight, nowNeighbourhood))
                {
                    // Full
                    if (m_history.empty())
                    {
                        m_performedRequests.Add(croppedFromRight);
                    }
                    // We can add to performed requests only left side of m_history end time
                    else if (m_history.rbegin()->lower() > croppedFromRight.lower())
                    {
                        m_performedRequests.Add(historyInterval_t(croppedFromRight.lower(), std::min(m_history.rbegin()->upper(), requestedInterval.upper())));
                    }
                }
                else
                {
                    // Subtraction result between croppedFromRight and nowEnvironment will be placed to performed requests
                    if (nowNeighbourhood.lower() > croppedFromRight.lower())
                    {
                        m_performedRequests.Add(historyInterval_t(croppedFromRight.lower(), nowNeighbourhood.lower()));
                        historyInterval_t rightSide(nowNeighbourhood.lower(), croppedFromRight.upper());
                        m_performedRequests.AddEmpty(rightSide, m_tweaks.EmptyResultTrustInterval);
                    }
                    else
                    {
                        m_performedRequests.AddEmpty(croppedFromRight, m_tweaks.EmptyResultTrustInterval);
                    }
                }
            }

            HistoryRequest newHistoryRequest;

            do
            {
                if (!ipintSearchSuccess && m_unsuccessIpintSearchCount < MAX_IPINT_SEARCH_COUNT)
                    newHistoryRequest = historyRequest;
                else
                {
                    // finished background action
                    if (m_requests.empty())
                    {
                        // stop
                        changeStateToStoppedLocked(__FUNCTION__);
                        return;
                    }

                    newHistoryRequest = pickRequestLocked();
                    m_unsuccessIpintSearchCount = 0;
                }

                historyIntervalSet_t result;
                historyIntervalSet_t nonPerformedRequests;
                auto hasFullRequestedHistory = getCachedHistoryLocked(newHistoryRequest.interval, result, nonPerformedRequests);

                // history already consist interval from current request, so just skip request and pick next one
                if (hasFullRequestedHistory && ipintSearchSuccess)
                    continue;

                if (!nonPerformedRequests.empty())
                {
                    const uint64_t nowTs = systemTimeMs();

                    auto req = nonPerformedRequests.begin();
                    newHistoryRequest.interval = *req;
                    ++req; // skip first range, because it search will made at current iteration

                    roundTo(newHistoryRequest.interval, ROUND_TO_TIME_MS, nowTs, OVERHEAD_FROM_NOW_MS);

                    for (; req != nonPerformedRequests.end(); ++req)
                    {
                        auto newInterval = *req;
                        roundTo(newInterval, ROUND_TO_TIME_MS, nowTs, OVERHEAD_FROM_NOW_MS);
                        addSearchJobLocked(newInterval, newHistoryRequest.type);
                    }
                }
            } while (false);

           m_dynExec->PostTask(std::bind(&CachedHistoryRequester2::doRecordingSearch, this, std::move(newHistoryRequest)));
        };

    lock.unlock();

    // UNLOCKED PART
    // do recordings search on device
    m_searcher(requestedInterval, historyHandler, finishedHandler, m_stopDeviceSearchSignal);
}

bool CachedHistoryRequester2::getRecordings(historyInterval_t requestedInterval,
                                            unsigned int maxCount,
                                            unsigned long long minGapMs,
                                            historyIntervalSet_t& result,
                                            bool& hasFullRequestedHistory)
{
    historyIntervalSet_t nonPerformedRequests;

    std::lock_guard<std::mutex> lock(m_mutex);

    _dbgf_("User requested interval {}", rangeToString(requestedInterval));

    // request history from cache
    hasFullRequestedHistory = getCachedHistoryLocked(requestedInterval, result, nonPerformedRequests);

    // adjust incoming request taking into account recent trusted empty results
    if (m_performedRequests.AdjustToPerformedEmptyResults(requestedInterval, nonPerformedRequests))
    {
        hasFullRequestedHistory = true;
    }

    // merge intervals with using min gap
    if (minGapMs != 0 && result.iterative_size() > 1)
        result = mergeIntervals(result, minGapMs);

    // strip result by maxCount 
    if ((maxCount != 0 && result.iterative_size() > maxCount))
    {
        hasFullRequestedHistory = false;
        
        auto diff = result.iterative_size() - maxCount;
        while (diff-- != 0)
            result.erase(*result.rbegin());

        return true;
    }

    // here we need just return result without any background action if found result for whole requested range
    if (hasFullRequestedHistory)
    {
        return true;
    }

    // round short user requests
    const uint64_t nowTs = systemTimeMs();
    const auto requestedIntervalOriginal = requestedInterval;
    roundTo(requestedInterval, ROUND_TO_TIME_MS, nowTs, OVERHEAD_FROM_NOW_MS);

    if (!boost::icl::is_empty(m_searchingRange) && boost::icl::contains(m_searchingRange, requestedInterval))
    {
        _dbg_ << "Requested interval now is searching: " << rangeToString(requestedInterval);
        return true;
    }    

    bool trustToRightSideOfHistory = false;
    // special case when history empty or request history from right side of current history
    if (m_history.empty() || requestedInterval.upper() > m_history.rbegin()->upper())
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_recentRequestsExpiration <= now)
        {
            m_recentRequestsExpiration = now + m_tweaks.RecentRequestInterval;
            m_recentRequests.clear();
        }

        do
        {
            historyInterval_t intervalOfRightSideHistory = requestedInterval;

            if (!m_history.empty())
            {
                intervalOfRightSideHistory = historyInterval_t(std::max(m_history.rbegin()->upper(), requestedInterval.lower()),
                    requestedInterval.upper());
            }

            // check if we beside of current time
            if (intervalOfRightSideHistory.upper() + OVERHEAD_FROM_NOW_MS >= nowTs)
            {
                intervalOfRightSideHistory = historyInterval_t(intervalOfRightSideHistory.lower(), intervalOfRightSideHistory.upper() + OVERHEAD_FROM_NOW_MS);
                trustToRightSideOfHistory = true;
            }

            nonPerformedRequests = (nonPerformedRequests | intervalOfRightSideHistory) ^ m_recentRequests;

            // range for nonPerformedRequests should not be greater than requestedIntervalOriginal
            if (!nonPerformedRequests.empty())
            {
                historyIntervalSet_t nonPerformedRequestsForRequestedInterval;
                boost::icl::add_intersection(nonPerformedRequestsForRequestedInterval, nonPerformedRequests, requestedIntervalOriginal);
                nonPerformedRequests = nonPerformedRequestsForRequestedInterval;
            }

            m_recentRequests |= intervalOfRightSideHistory;
        } while (false);
    }

    if (nonPerformedRequests.empty())
    {
        _dbg_ << "NonPerformedRequests is empty " << rangeToString(requestedInterval);
        if (trustToRightSideOfHistory)
        {
            hasFullRequestedHistory = true;
        }

        return true;
    }

    if (isReachedRequestsDepth(__FUNCTION__))
        return false;    

    {
        if (m_state == CS_STOP_REQUESTED)
        {
            _inf_ << "Stop requested, skip search range "<< rangeToString(requestedInterval);     
            return false;
        }
        else if (m_state != CS_WORKING)
        {
            _dbg_ << "Started background work...";
            m_state = CS_WORKING;
        }
    }

    scheduleSearchJob(requestedInterval, nowTs, nonPerformedRequests);

    return true;
}

bool CachedHistoryRequester2::getCachedHistoryLocked(const historyInterval_t& requestedInterval, historyIntervalSet_t& outResult, historyIntervalSet_t& nonPerformedRequests) const
{
    namespace icl = boost::icl;

    // select performed requests for current range
    historyIntervalSet_t performedRequests;
    icl::add_intersection(performedRequests, m_performedRequests.Get(), requestedInterval);

    if (m_history.empty() && performedRequests.empty())
    {
        nonPerformedRequests.add(requestedInterval);
        return false;
    }

    icl::add_intersection(outResult, m_history, requestedInterval);

    historyIntervalSet_t invertedOutResult = outResult;
    invertedOutResult ^= requestedInterval;

    // full match
    if (invertedOutResult.empty())
        return true;

    nonPerformedRequests = (performedRequests | outResult) ^ requestedInterval;

    auto historyMax = m_history.empty() ? 0 : m_history.rbegin()->upper();

    // history last range greater than requested interval and we already tried perform search for all requested interval,
    // thats mean that we already have fresh history
    if (nonPerformedRequests.empty() && historyMax >= requestedInterval.upper())
        return true;

    if (historyMax < requestedInterval.upper())
    {
        if (!outResult.empty())
        {
            nonPerformedRequests.add(historyInterval_t(outResult.rbegin()->upper(), requestedInterval.upper()));
        }
        // In this case we have no history but request was performed with empty result
        else if (outResult.empty() && !performedRequests.empty())
        {
            return true;
        }
        else
        {
            nonPerformedRequests.add(requestedInterval);
        }
    }

    if (outResult.empty())
    {
        return false;
    }

    // here we check if already searching requested interval
    {
        historyIntervalSet_t currentRequestsForRequestedInterval;
        if (!m_requests.historyIntervals().empty())
            boost::icl::add_intersection(currentRequestsForRequestedInterval, m_requests.historyIntervals(), requestedInterval);

        if (!boost::icl::is_empty(m_searchingRange))
            currentRequestsForRequestedInterval |= (m_searchingRange & requestedInterval);

        if (!currentRequestsForRequestedInterval.empty() && outResult.rbegin()->upper() > currentRequestsForRequestedInterval.begin()->lower())
        {
            outResult.erase(historyInterval_t(
                std::max(currentRequestsForRequestedInterval.begin()->lower(), outResult.begin()->lower()), outResult.rbegin()->upper()));
            return false;
        }
    }

    if (!nonPerformedRequests.empty())
    {
        // requested left side of current history, so need clean output result
        for (auto& out : outResult)
        {
            if (nonPerformedRequests.begin()->lower() < out.lower())
            {
                outResult.clear();
                return false;
            }
        }
    }
    else
    {
        historyIntervalSet_t nonCurrentRequests = m_requests.historyIntervals();
        nonCurrentRequests |= m_searchingRange;
        nonCurrentRequests ^= requestedInterval;

        // no one request intersects with requestedInterval
        return nonCurrentRequests.empty();
    }

    return nonPerformedRequests.empty();
}

void CachedHistoryRequester2::scheduleSearchJob(const historyInterval_t& requestedInterval, 
     uint64_t nowTs, const historyIntervalSet_t& nonPerformedRequests)
{
    m_dynExec->PostTask([this, nonPerformedRequests, requestedInterval, nowTs]()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // stop requested
        if (performStopRequestLocked(__FUNCTION__))
            return;

        // first add request for user range
        for (auto p : nonPerformedRequests)
            addSearchJobLocked(p, RT_USER_REQUEST);

        addSearchJobLocked(m_normalizer(requestedInterval), RT_NORMALIZER_REQUEST);
    });
}

void CachedHistoryRequester2::scheduleUpdateCacheJob()
{
    if (m_tweaks.CacheDepthMs != 0)
    {
        m_state = CS_WORKING;

        const uint64_t nowTs = systemTimeMs();
        if (nowTs <= m_tweaks.CacheDepthMs)
            std::terminate();

        const historyInterval_t requestedInterval(nowTs - m_tweaks.CacheDepthMs, nowTs);
        scheduleSearchJob(requestedInterval, nowTs);
    }
}

bool CachedHistoryRequester2::isReachedRequestsDepth(const std::string& requester)
{
    auto steadyTimestamp = std::chrono::steady_clock::now();
    if (!m_requests.empty() && (steadyTimestamp - m_requests.begin()->timestamp) > REQUESTS_DEPTH)
    {
        _wrn_ << "Cannot add request, reached max requests depth from " << requester;
        return true;
    }

    return false;
}

void CachedHistoryRequester2::changeStateToStoppedLocked(const std::string& requester)
{
    _dbg_ << "Requests completed from " << requester << ", standby...";
    m_searchInProgress = false;
    m_state = CS_STOPPED;
    m_stopCondition.notify_one();
}

bool CachedHistoryRequester2::performStopRequestLocked(const std::string& requester)
{
    if (m_state == CS_STOPPED)
        return true;

    bool performStopRequest = m_state == CS_STOP_REQUESTED;
    if (!performStopRequest)
        return false;

    _dbg_ << "Performed stop request from " << requester;
    m_state = CS_STOPPED;
    m_searchInProgress = false;
    m_requests.clear();
    m_stopCondition.notify_one();
    return performStopRequest;
}

void CachedHistoryRequester2::stop()
{
	_dbg_ << "Stop requested";
	internalStop();
}

void CachedHistoryRequester2::internalStop()
{   
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_state == CS_STOPPED)
    {
        _dbg_ << "Already stopped.";
        boost::system::error_code ec;
        m_updateCacheTimer.cancel(ec);
        return;
    }

    m_state = CS_STOP_REQUESTED;

    boost::system::error_code ec;
    m_updateCacheTimer.cancel(ec);

    bool stopDeviceSearchSignalInvoked = false;
    auto stopDeviceSearchSignalLambda = [this, &stopDeviceSearchSignalInvoked]()
        {
            if (m_stopDeviceSearchSignal.num_slots() == 0)
                return;

            _dbg_ << "Requested stop IPINT searcher.";
            stopDeviceSearchSignalInvoked = true;
            m_stopDeviceSearchSignal();
            m_stopDeviceSearchSignal.disconnect_all_slots();
        };

    // try to stop ipint search at first time
    stopDeviceSearchSignalLambda();

    while(m_state != CS_STOPPED)
    {
        if (!stopDeviceSearchSignalInvoked)
            stopDeviceSearchSignalLambda();

        m_stopCondition.wait_for(lock, std::chrono::milliseconds(100), 
                [&] () { return m_state == CS_STOPPED; });
    }

    _dbg_ << "Stop completed!";
}

void CachedHistoryRequester2::waitBackgrounActions()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_state == CS_STOPPED)
        return;

    m_stopCondition.wait(lock, [this]() { return m_state == CS_STOPPED; });
}

void CachedHistoryRequester2::clearCache()
{
    internalStop();

    std::lock_guard<std::mutex> lock(m_mutex);

    m_history.clear();
    m_performedRequests.Clear();
}

void CachedHistoryRequester2::UT_tweak(const STweaks& tweaks)
{
    m_tweaks = tweaks;
    _dbg_ << m_tweaks;
    scheduleUpdateCacheJob();
}

void CachedHistoryRequester2::chargeUpdateTimer()
{
    const auto universalTime = boost::posix_time::microsec_clock::universal_time();
    const auto updateTime = std::max(m_lastUpdateCacheTime + m_tweaks.UpdateCacheTimeout, universalTime);

    _dbg_ << "Charge update cache timer to " << updateTime << ", now time is " << universalTime;

    m_updateCacheTimer.expires_at(updateTime);
    m_updateCacheTimer.async_wait(boost::bind(&CachedHistoryRequester2::updateTimerHandler, this, _1));
}

void CachedHistoryRequester2::moveUpdateCacheRange()
{
    // Here we assume that device write recording by ring, so enough check first hour of our history.
    static const ITV8::timestamp_t ONE_HOUR_MS = 60ull * 60 * 1000;

    if (m_updateCacheRange.upper() == m_updateCacheRange.lower())
        m_updateCacheRange = *m_history.begin();
    else
    {
        historyInterval_t requestedInterval(m_updateCacheRange.upper(), m_history.rbegin()->upper());

        historyIntervalSet_t historyIntersection;
        boost::icl::add_intersection(historyIntersection, m_history, requestedInterval);
        if (!historyIntersection.empty())
            m_updateCacheRange = *historyIntersection.begin();
        else
            m_updateCacheRange = historyInterval_t(m_updateCacheRange.lower(), m_updateCacheRange.lower());
    }
    
    if (m_updateCacheRange.upper() - m_updateCacheRange.lower() > ONE_HOUR_MS)
        m_updateCacheRange = historyInterval_t(m_updateCacheRange.lower(), m_updateCacheRange.lower() + ONE_HOUR_MS);
}

void CachedHistoryRequester2::updateTimerHandler(const boost::system::error_code& error)
{
    if (error)  
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if stop requested.
    // At momemt when invoked this handler all background actions may be completed, 
    // so need check firstly for CS_STOP_REQUESTED.
    if (m_state == CS_STOP_REQUESTED && performStopRequestLocked(__FUNCTION__))
        return;

    if (m_state != CS_WORKING)
    {
        _dbg_ << "Started background work for update cache...";
        m_state = CS_WORKING;
    }

    if (0 != m_tweaks.CacheDepthMs)
    {
        const uint64_t nowTs = systemTimeMs();
        const uint64_t expireTime = nowTs - m_tweaks.CacheDepthMs;

        historyInterval_t clearInterval(0, expireTime);
        if (!m_history.empty())
            clearInterval = historyInterval_t(std::min(expireTime, m_history.begin()->lower()), std::max(expireTime, m_history.begin()->lower()));

        m_history ^= clearInterval;
        m_performedRequests.Subtract(clearInterval);

        uint64_t startTime = expireTime;
        if (!m_history.empty())
        {
            startTime = m_history.rbegin()->upper();
            _dbg_ << "Cleared history interval " << rangeToString(clearInterval) << ", history now started from " << rangeToString(*m_history.begin());
        }
        else
        {
            _dbg_ << "Cleared history interval " << rangeToString(clearInterval) << ", history is empty.";
        }

        const historyInterval_t requestedInterval(startTime, nowTs);

        _dbg_ << "Update cache requested interval " << rangeToString(requestedInterval);
        scheduleSearchJob(requestedInterval, nowTs);
        return;
    }
    
    addSearchJobLocked(m_updateCacheRange, RT_UPDATE_CACHE_REQUEST);
}

void CachedHistoryRequester2::updateCacheLocked(historyInterval_t requestedInterval, const historyIntervalSet_t& records)
{
    historyIntervalSet_t historyIntersection;
    boost::icl::add_intersection(historyIntersection, m_history, requestedInterval);

    if (records == historyIntersection)
    {
        _dbg_ << "Update cache, no changes found for range " << rangeToString(requestedInterval) << ", found records: " << rangesToString(records);
        m_updateCacheRange = historyInterval_t();
        moveUpdateCacheRange();

        m_lastUpdateCacheTime = boost::posix_time::microsec_clock::universal_time();
        chargeUpdateTimer();
        return;
    }

    // remove oldest result for selected interval
    m_history ^= requestedInterval;

    // add new results
    m_history |= records;

    _dbg_ << "Updated cache for range " << rangeToString(requestedInterval) << ", found records: " 
          << rangesToString(records) << ", history for that range: " << rangesToString(historyIntersection);

    // check if next range exist
    moveUpdateCacheRange();

    // charge timer right now
    chargeUpdateTimer();
}

const historyIntervalSet_t& CachedHistoryRequester2::RequestsSet::historyIntervals() const
{
    return m_requestsSet;
}

CachedHistoryRequester2::RequestsSet::iterator CachedHistoryRequester2::RequestsSet::erase(CachedHistoryRequester2::RequestsSet::const_iterator e)
{
    m_requestsSet.erase(e->interval);
    return base_t::erase(e);
}    

void CachedHistoryRequester2::RequestsSet::insert(const CachedHistoryRequester2::HistoryRequest& r)
{
    m_requestsSet |= (r.interval);
    base_t::insert(r);
}

void CachedHistoryRequester2::RequestsSet::emplace(HistoryRequest&& r)
{
    m_requestsSet |= (r.interval);
    base_t::emplace(r);
}

void CachedHistoryRequester2::RequestsSet::clear()
{
    m_requestsSet.clear();
    base_t::clear();
}



}
