#ifndef DEVICEIPINT3_CACHEDHISTORYREQUESTER_INL
#define DEVICEIPINT3_CACHEDHISTORYREQUESTER_INL

#include "CachedHistoryRequester.h"

namespace IPINT30
{
template<typename TExpirationdPolicy>
RecordingsHistoryCache<TExpirationdPolicy>::RecordingsHistoryCache(
        const TExpirationdPolicy& expiredPolicy /*= TExpirationdPolicy()*/) : 
    m_expired(expiredPolicy)
{
}

template<typename TExpirationdPolicy>
void RecordingsHistoryCache<TExpirationdPolicy>::add(const historyInterval_t& range,
                                const historyIntervalSet_t& records)
{
    m_history.erase(range);
    m_history.add(make_pair(range, TrackHistoryRange(records)));
}

template<typename TExpirationdPolicy>
void RecordingsHistoryCache<TExpirationdPolicy>::add(const ITV8::Utility::calendarList_t& dates,
    historyInterval_t interval)
{
    m_calendar.Added = boost::posix_time::second_clock::universal_time();
    m_calendar.Calendar = dates;
    m_calendar.Interval = interval;
}

template<typename TExpirationdPolicy>
bool RecordingsHistoryCache<TExpirationdPolicy>::contains(ITV8::timestamp_t timestamp) const
{
    // Handling the case when timestamp may point to very end of the interval.
    const historyInterval_t refinedInterval((timestamp ? timestamp - 1 : timestamp), 
        timestamp + 2);

    recordingsTimeline_t::const_iterator cit = 
        m_history.find(refinedInterval);

    if (cit != m_history.end())
    {
        using namespace boost::icl;
        return intersects(cit->second.intervalSet, refinedInterval);
    }
    return false;
}

template<typename TExpirationdPolicy>
void RecordingsHistoryCache<TExpirationdPolicy>::clear()
{
    m_history.clear();
    m_calendar.Clear();
}

template<typename TExpirationdPolicy /*= DefaultExpirationPolicy*/>
historyInterval_t RecordingsHistoryCache<TExpirationdPolicy>::presentationRange() const
{
    using namespace boost::icl;
    struct NonEmptyPayload
    {
        bool operator()(const recordingsTimeline_t::value_type& record)
        {
            return !is_empty(record.second.intervalSet);
        }
    };
    typedef recordingsTimeline_t::const_iterator it_t;
    typedef recordingsTimeline_t::const_reverse_iterator rit_t;
    it_t first = std::find_if(m_history.begin(), m_history.end(), NonEmptyPayload());
    rit_t last = std::find_if(m_history.rbegin(), m_history.rend(), NonEmptyPayload());
    if (first == m_history.end() || last == m_history.rend())
    {
        return historyInterval_t();
    }
    return historyInterval_t(lower(first->second.intervalSet), 
        upper(last->second.intervalSet));
}

template<typename TExpirationdPolicy>
ITV8::Utility::calendarList_t RecordingsHistoryCache<TExpirationdPolicy>::calendar(
    historyInterval_t requestedInterval) const
{
    if (!boost::icl::contains(m_calendar.Interval, requestedInterval))
    {
        return ITV8::Utility::calendarList_t();
    }

    if (m_expired(m_calendar.Added))
    {
        return ITV8::Utility::calendarList_t();
    }

    ITV8::Utility::calendarList_t result;
    for (const auto& c : m_calendar.Calendar)
    {
        if (boost::icl::contains(requestedInterval, c))
        {
            result.push_back(c);
        }
    }

    return result;
}

template<typename TExpirationdPolicy>
optionalIntervalSet_t RecordingsHistoryCache<TExpirationdPolicy>::get(const historyInterval_t& requestedInterval) const
{
    namespace icl = boost::icl;
    historyInterval_t foundInterval(requestedInterval.lower(), 
        requestedInterval.lower());
    historyIntervalSet_t foundRecords;

    recordingsTimeline_t::const_iterator cit = 
        m_history.find(requestedInterval.lower());

    // Collect contiguous unexpired records in history until
    // we construct sufficiently large range.
    while (!icl::contains(foundInterval, requestedInterval) &&
        cit != m_history.end() &&
        icl::contains(cit->first, foundInterval.upper()) &&
        !m_expired(*cit))
    {
        icl::add_intersection(foundRecords, cit->second.intervalSet, 
            requestedInterval);
        foundInterval = icl::hull(foundInterval, cit->first);
        ++cit;
    }

    if (icl::contains(foundInterval, requestedInterval))
    {
        return foundRecords;
    }
    return optionalIntervalSet_t();
}

template<typename TNormalizer, typename THistoryCache>
CachedHistoryRequester<TNormalizer, THistoryCache>::CachedHistoryRequester(const std::string& recordingId,
                        const TNormalizer& normalizer /*= TNormalizer()*/,
                        const THistoryCache& cache /*= THistoryCache()*/) :
    m_recordingId(recordingId),
    m_normalizer(normalizer),
    m_historyCache(cache),
    m_requestCount(0),
    m_stopped(false)
{
}

template<typename TNormalizer, typename THistoryCache>
bool CachedHistoryRequester<TNormalizer, THistoryCache>::containsRecordForTime(
    ITV8::timestamp_t timestamp) const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_historyGuard);
    return m_historyCache.contains(timestamp);
}

template<typename TNormalizer, typename THistoryCache>
historyInterval_t CachedHistoryRequester<TNormalizer, THistoryCache>::presentationRange() const
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_historyGuard);
    return m_historyCache.presentationRange();
}

template<typename TNormalizer, typename THistoryCache>
template<typename TRequester>
historyIntervalSet_t CachedHistoryRequester<TNormalizer, THistoryCache>::findRecordings(
    TRequester& deviceRequester, const std::string& trackId, historyInterval_t requestedInterval, DECLARE_LOGGER_ARG)
{
    // Try get from cache
    if (optionalIntervalSet_t result = requestCache(requestedInterval))
    {
        return *result;
    }

    // Request from device
    boost::upgrade_lock<boost::shared_mutex> upgradableLock(m_historyGuard);
    // Double check pattern: check cache after acquiring the upgradeable lock.
    // Another thread may already upgrade the cache, while we were waiting for the lock.
    if (optionalIntervalSet_t result = requestCache(requestedInterval))
    {
        return *result;
    }

    if (m_stopped.load())
    {
        return historyIntervalSet_t();
    }

    // Actual device requesting
    RequestStormGuard::ScopedGuard protectScope(m_requestStormGuard);
    const historyInterval_t normalizedInterval = m_normalizer(requestedInterval);
    updateRequestCount();
    typename TRequester::rangeList_t deviceResults;
    try
    {
        deviceResults = deviceRequester.findRecordings(makeIpintTimeRange(normalizedInterval), m_stopDeviceSearchSignal);
    }
    catch (const std::runtime_error&)
    {
        updateRequestCount(false);

        throw;
    }

    updateRequestCount(false);
    // Store results to cache
    {
        boost::upgrade_to_unique_lock<boost::shared_mutex> writerLock(upgradableLock);
        m_historyCache.add(normalizedInterval, 
            makeHistoryIntervalSet(deviceResults, m_recordingId, trackId, GET_LOGGER_PTR));
    }

    // Return adapted results.
    if (optionalIntervalSet_t result = m_historyCache.get(requestedInterval))
    {
        return *result;
    }
    return historyIntervalSet_t();
}

template<typename TNormalizer, typename THistoryCache>
template<typename TRequester>
ITV8::Utility::calendarList_t CachedHistoryRequester<TNormalizer, THistoryCache>::getCalendar(
    TRequester& deviceRequester, historyInterval_t requestedInterval)
{
    ITV8::Utility::calendarList_t result;
    const auto normalizedInterval = m_normalizer(requestedInterval, true);
    boost::upgrade_lock<boost::shared_mutex> upgrade(m_historyGuard);
    result = m_historyCache.calendar(normalizedInterval);
    if (!result.empty())
    {
        return result;
    }

    if (m_stopped.load())
    {
        return ITV8::Utility::calendarList_t();
    }

    updateRequestCount();
    try
    {
        result = deviceRequester.getCalendar(makeIpintTimeRange(normalizedInterval), m_stopDeviceSearchSignal);
    }
    catch (const std::runtime_error&)
    {
        updateRequestCount(false);

        throw;
    }

    updateRequestCount(false);
    {
        boost::upgrade_to_unique_lock<boost::shared_mutex> unique(upgrade);
        m_historyCache.add(result, normalizedInterval);
    }

    return result;
}

template<typename TNormalizer, typename THistoryCache>
optionalIntervalSet_t CachedHistoryRequester<TNormalizer, THistoryCache>::requestCache(
    historyInterval_t requestedInterval)
{
    boost::shared_lock<boost::shared_mutex> readerLock(m_historyGuard);
    return m_historyCache.get(requestedInterval);
}

template<typename TNormalizer /*= DefaultIntervalNormalizer*/,
    typename THistoryCache /*= RecordingsHistoryCache<> */>
    void IPINT30::CachedHistoryRequester<TNormalizer, THistoryCache>::clearCache()
{
    boost::unique_lock<boost::shared_mutex> writerLock(m_historyGuard);
    return m_historyCache.clear();
}

template<typename TNormalizer /*= DefaultIntervalNormalizer*/,
    typename THistoryCache /*= RecordingsHistoryCache<> */>
    void IPINT30::CachedHistoryRequester<TNormalizer, THistoryCache>::stop()
{
    m_stopped.exchange(true);
    std::unique_lock<std::mutex> lock(m_stopConditionGuard);
    bool stopDeviceSearchSignalInvoked = false;
    auto stopDeviceSearchSignalLambda = [this, &stopDeviceSearchSignalInvoked]()
    {
        if (m_stopDeviceSearchSignal.num_slots() == 0)
            return;

        stopDeviceSearchSignalInvoked = true;
        m_stopDeviceSearchSignal();
        m_stopDeviceSearchSignal.disconnect_all_slots();
    };

    stopDeviceSearchSignalLambda();

    while (m_requestCount != 0ul)
    {
        if (!stopDeviceSearchSignalInvoked)
            stopDeviceSearchSignalLambda();

        m_stopCondition.wait_for(lock, std::chrono::milliseconds(100),
            [&]() { return m_requestCount == 0ul; });
    }
}

template<typename TNormalizer /*= DefaultIntervalNormalizer*/,
    typename THistoryCache /*= RecordingsHistoryCache<> */>
    void IPINT30::CachedHistoryRequester<TNormalizer, THistoryCache>::updateRequestCount(bool increaseOnly)
{
    std::unique_lock<std::mutex> lock(m_stopConditionGuard);
    if (increaseOnly)
    {
        m_requestCount += 1;
        return;
    }

    m_requestCount -= 1;
    m_stopCondition.notify_one();
}

}

#endif

