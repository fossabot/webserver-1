#include <iostream>
#include <string>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/thread.hpp>
#include <Logging/log2.h>

#include "../CachedHistoryRequester.h"
#include "../TimeStampHelpers.h"
#include "../RecordingSearch.h"

using namespace IPINT30;
DECLARE_LOGGER_ARG;

namespace
{
struct AlwaysFresh
{
    bool operator()(const recordingsTimeline_t::value_type&) const
    {
        return false;
    }
};
}

namespace
{
boost::posix_time::ptime now()
{
    return boost::posix_time::second_clock::universal_time();
}
}

BOOST_AUTO_TEST_SUITE(TestRecordingsHistoryCache)

BOOST_AUTO_TEST_CASE(testAddNonOverlappingHistory)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    historyIntervalSet_t aSecondDayHistory;
    const ITV8::GDRV::DateTimeRange secondDay = {10, 20};
    {
        aSecondDayHistory.insert(historyInterval_t(12, 15));
        history.add(make_interval(secondDay), aSecondDayHistory);
    }

    optionalIntervalSet_t result = history.get(make_interval(secondDay));

    BOOST_CHECK_MESSAGE(result, "Should return non empty list");

    BOOST_TEST_INFO("Wrong result list size");
    BOOST_CHECK_EQUAL(aSecondDayHistory, *result);

    BOOST_TEST_INFO("Wrong list size");
    BOOST_CHECK_EQUAL(size_t(1u), result->iterative_size());
}

BOOST_AUTO_TEST_CASE(testAddOverlappingHistory)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(10, 20));
        ITV8::GDRV::DateTimeRange range = {13, 18};
        history.add(make_interval(range), aDayHistory);
    }
    historyIntervalSet_t aLastDayHistory;
    const ITV8::GDRV::DateTimeRange lastDay = {15, 25};
    {
        aLastDayHistory.insert(historyInterval_t(15, 20));
        aLastDayHistory.insert(historyInterval_t(21, 23));
        history.add(make_interval(lastDay), aLastDayHistory);
    }
    optionalIntervalSet_t result = history.get(make_interval(lastDay));

    BOOST_CHECK_MESSAGE(result, "Should return non empty list");

    BOOST_TEST_INFO("Wrong result list size");
    BOOST_CHECK_EQUAL(aLastDayHistory, *result);
}

BOOST_AUTO_TEST_CASE(testGetFailsOnUnknownRange)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(10, 20));
        ITV8::GDRV::DateTimeRange range = {13, 18};
        history.add(make_interval(range), aDayHistory);
    }
    optionalIntervalSet_t result = history.get(historyInterval_t(30, 40));

    BOOST_CHECK_MESSAGE(!result, "Should return empty list");
}

BOOST_AUTO_TEST_CASE(testGetPartsFromRanges)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        aDayHistory.insert(historyInterval_t(3, 10));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }

    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(13, 18));
        ITV8::GDRV::DateTimeRange range = {10, 20};
        history.add(make_interval(range), aDayHistory);
    }

    // Test little tiny edge range
    {
        historyIntervalSet_t expectedResult;
        expectedResult += historyInterval_t(0, 1);

        optionalIntervalSet_t result = history.get(historyInterval_t(0, 1));
        BOOST_CHECK_MESSAGE(result, "Should return non empty list");

        BOOST_TEST_INFO("Wrong result list size");
        BOOST_CHECK_EQUAL(expectedResult, *result);
    }

    // Test one range
    {
        historyIntervalSet_t expectedResult;
        expectedResult += historyInterval_t(15, 18);

        optionalIntervalSet_t result = history.get(historyInterval_t(15, 20));
        BOOST_CHECK_MESSAGE(result, "Should return non empty list");

        BOOST_TEST_INFO("Wrong result list size");
        BOOST_CHECK_EQUAL(expectedResult, *result);
    }

    // Test several ranges
    {
        historyIntervalSet_t expectedResult;
        expectedResult += historyInterval_t(5, 10);
        expectedResult += historyInterval_t(13, 15);

        optionalIntervalSet_t result = history.get(historyInterval_t(5, 15));
        BOOST_CHECK_MESSAGE(result, "Should return non empty list");

        BOOST_TEST_INFO("Wrong result list size");
        BOOST_CHECK_EQUAL(expectedResult, *result);
    }

    // Test whole history
    {
        historyIntervalSet_t expectedResult;
        expectedResult += historyInterval_t(0, 1);
        expectedResult += historyInterval_t(3, 10);
        expectedResult += historyInterval_t(13, 18);

        optionalIntervalSet_t result = history.get(historyInterval_t(0, 20));
        BOOST_CHECK_MESSAGE(result, "Should return non empty list");

        BOOST_TEST_INFO("Wrong result list size");
        BOOST_CHECK_EQUAL(expectedResult, *result);
    }
}

BOOST_AUTO_TEST_CASE(testGetFailsOnPartlyKnownRange)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(13, 18));
        ITV8::GDRV::DateTimeRange range = {10, 20};
        history.add(make_interval(range), aDayHistory);
    }
    optionalIntervalSet_t result = history.get(historyInterval_t(15, 40));
    BOOST_CHECK_MESSAGE(!result, "Should return empty list");
}

BOOST_AUTO_TEST_CASE(testGetFailsOnRangeWithGap)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(11, 20));
        ITV8::GDRV::DateTimeRange range = {11, 20};
        history.add(make_interval(range), aDayHistory);
    }
    optionalIntervalSet_t result = history.get(historyInterval_t(0, 20));
    BOOST_CHECK_MESSAGE(!result, "Should return empty list");
}

BOOST_AUTO_TEST_CASE(testGetFailsWhenExpired)
{
    struct AlwaysExpired
    {
        bool operator()(const recordingsTimeline_t::value_type&) const
        {
            return true;
        }
    };

    RecordingsHistoryCache<AlwaysExpired> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(0, 1));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    historyIntervalSet_t aSecondDayHistory;
    const ITV8::GDRV::DateTimeRange secondDay = {10, 20};
    {
        aSecondDayHistory.insert(historyInterval_t(12, 15));
        history.add(make_interval(secondDay), aSecondDayHistory);
    }
    optionalIntervalSet_t result = history.get(make_interval(secondDay));
    BOOST_CHECK_MESSAGE(!result, "Should return empty list");
}

BOOST_AUTO_TEST_CASE(testAddEmptyresultListList)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        ITV8::GDRV::DateTimeRange range = {1386720000000, 1386806400000};
        history.add(make_interval(range), aDayHistory);
    }
    const ITV8::GDRV::DateTimeRange secondDay = {1386764594875, 1386771794875};
    optionalIntervalSet_t result = history.get(make_interval(secondDay));
    BOOST_CHECK_MESSAGE(result, "Should return non empty list");
}

BOOST_AUTO_TEST_CASE(testContainsFunction)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(5, 8));
        aDayHistory.insert(historyInterval_t(1, 2));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }

    {
        historyIntervalSet_t aDayHistory;
        ITV8::GDRV::DateTimeRange range = {20, 30};
        history.add(make_interval(range), aDayHistory);
    }

    BOOST_CHECK_MESSAGE(!history.contains(20), "Timestamp does not contain in interval");
    BOOST_CHECK_MESSAGE(history.contains(5), "Timestamp contains in interval");
    BOOST_CHECK_MESSAGE(history.contains(6), "Timestamp contains in interval");
    BOOST_CHECK_MESSAGE(history.contains(1), "Timestamp contains in interval");
    BOOST_CHECK_MESSAGE(history.contains(2), "Timestamp contains in interval");
    BOOST_CHECK_MESSAGE(history.contains(8), "Timestamp contains in interval");
    BOOST_CHECK_MESSAGE(!history.contains(9), "Timestamp does not contain in interval");
    BOOST_CHECK_MESSAGE(!history.contains(3), "Timestamp does not contain in interval");
    BOOST_CHECK_MESSAGE(!history.contains(25), "Timestamp does not contain in interval");
}

BOOST_AUTO_TEST_CASE(testPresentationRange)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    BOOST_TEST_INFO("presentationRange returns wrong result for ampty history");
    BOOST_CHECK_EQUAL(historyInterval_t(), history.presentationRange());

    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(5, 8));
        aDayHistory.insert(historyInterval_t(1, 2));
        ITV8::GDRV::DateTimeRange range = {0, 10};
        history.add(make_interval(range), aDayHistory);
    }
    BOOST_TEST_INFO("presentationRange returns wrong result for one period");
    BOOST_CHECK_EQUAL(historyInterval_t(1, 8), history.presentationRange());

    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(30, 40));
        aDayHistory.insert(historyInterval_t(60, 90));
        ITV8::GDRV::DateTimeRange range = {20, 100};
        history.add(make_interval(range), aDayHistory);
    }
    BOOST_TEST_INFO("presentationRange returns wrong result for two periods");
    BOOST_CHECK_EQUAL(historyInterval_t(1, 90), history.presentationRange());
}

BOOST_AUTO_TEST_CASE(testPresentationRangeWithEmptyGapes)
{
    RecordingsHistoryCache<AlwaysFresh> history;
    {
        historyIntervalSet_t aDayHistory;
        ITV8::GDRV::DateTimeRange range = {1, 6};
        history.add(make_interval(range), aDayHistory);
    }
    BOOST_TEST_INFO("presentationRange returns wrong result for history with one empty gap");
    BOOST_CHECK_EQUAL(historyInterval_t(), history.presentationRange());

    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(15, 18));
        aDayHistory.insert(historyInterval_t(11, 12));
        ITV8::GDRV::DateTimeRange range = {10, 20};
        history.add(make_interval(range), aDayHistory);
    }
    BOOST_TEST_INFO("presentationRange returns wrong result for two periods");
    BOOST_CHECK_EQUAL(historyInterval_t(11, 18), history.presentationRange());

    {
        historyIntervalSet_t aDayHistory;
        aDayHistory.insert(historyInterval_t(26, 29));
        ITV8::GDRV::DateTimeRange range = {25, 29};
        history.add(make_interval(range), aDayHistory);
    }
    BOOST_TEST_INFO("presentationRange returns wrong result for two periods");
    BOOST_CHECK_EQUAL(historyInterval_t(11, 29), history.presentationRange());

    {
        historyIntervalSet_t aDayHistory;
        ITV8::GDRV::DateTimeRange range = {31, 40};
        history.add(make_interval(range), aDayHistory);
    }
    BOOST_TEST_INFO("presentationRange returns wrong result for two periods");
    BOOST_CHECK_EQUAL(historyInterval_t(11, 29), history.presentationRange());
}

BOOST_AUTO_TEST_CASE(testGetCalendarExpired)
{
    struct AlwaysExpired
    {
        bool operator()(const boost::posix_time::ptime&) const
        {
            return true;
        }
    };

    RecordingsHistoryCache<AlwaysExpired> history;
    IPINT30::historyInterval_t interval(0, 1);
    {
        ITV8::Utility::calendarList_t dates;
        dates.push_back(ipintNow());
        history.add(dates, interval);
    }

    auto result = history.calendar(interval);
    BOOST_CHECK_MESSAGE(result.empty(), "Should return empty list");
}

BOOST_AUTO_TEST_CASE(testGetCalendarDifferentIntarvals)
{
    struct AlwaysFresh
    {
        bool operator()(const boost::posix_time::ptime&) const
        {
            return false;
        }
    };

    RecordingsHistoryCache<AlwaysFresh> history;
    {
        ITV8::Utility::calendarList_t dates;
        dates.push_back(5);
        dates.push_back(6);
        dates.push_back(7);
        dates.push_back(8);
        dates.push_back(9);
        IPINT30::historyInterval_t interval(5, 10);
        history.add(dates, interval);
    }

    auto result = history.calendar(IPINT30::historyInterval_t(1, 2));
    BOOST_CHECK_MESSAGE(result.empty(), "Should return empty list");

    result = history.calendar(IPINT30::historyInterval_t(5, 6));
    BOOST_CHECK_MESSAGE(result.size() == 1ull, "Should return 1 element");

    result = history.calendar(IPINT30::historyInterval_t(5, 10));
    BOOST_CHECK_MESSAGE(result.size() == 5ull, "Should return 5 elements");

    result = history.calendar(IPINT30::historyInterval_t(5, 20));
    BOOST_CHECK_MESSAGE(result.empty(), "Should return empty list");

    result = history.calendar(IPINT30::historyInterval_t(10, 20));
    BOOST_CHECK_MESSAGE(result.empty(), "Should return empty list");
}

BOOST_AUTO_TEST_SUITE_END() // TestRecordingsHistoryCache

BOOST_AUTO_TEST_SUITE(TestPolicyClasses)

BOOST_AUTO_TEST_CASE(testCommonExpiration)
{
    using namespace boost::posix_time;
    DefaultExpirationPolicy expired(minutes(5));

    // For sure this time period in the past
    const historyInterval_t freshHistoryRange(toIpintTime(now() - hours(7*24)),
        toIpintTime(now() - hours(6*24)));

    recordingsTimeline_t::value_type entry(freshHistoryRange, TrackHistoryRange());

    entry.second.timestamp = now() - hours(2);
    BOOST_CHECK_MESSAGE(expired(entry), "this cache entry should be expired!");

    entry.second.timestamp = now() - minutes(4);
    BOOST_CHECK_MESSAGE(!expired(entry), "This cache entry should not be expired!");

    auto pTime = now() - minutes(10);
    BOOST_CHECK_MESSAGE(expired(pTime), "This cache entry should be expired!");

    pTime = now() - minutes(4);
    BOOST_CHECK_MESSAGE(!expired(pTime), "This cache entry should not be expired!");
}

BOOST_AUTO_TEST_CASE(testLiveRecordingExpiration)
{
    using namespace boost::posix_time;
    DefaultExpirationPolicy expired(minutes(5), seconds(30));

    const historyInterval_t freshHistoryRange(toIpintTime(now() - hours(7*24)),
        toIpintTime(now()));
    recordingsTimeline_t::value_type entry(freshHistoryRange, TrackHistoryRange());
    entry.second.timestamp = now() - seconds(40);
    BOOST_CHECK_MESSAGE(expired(entry), "this cache entry should be expired!");

    entry.second.timestamp = now() - seconds(20);
    BOOST_CHECK_MESSAGE(!expired(entry), "this cache entry should not be expired!");
}

BOOST_AUTO_TEST_CASE(testDefaultRequestedIntervalNormalizerOneDay)
{
    DefaultIntervalNormalizer normalizer;
    const historyInterval_t requested(toIpintTime("20131211T053522"),
        toIpintTime("20131211T063522"));

    const historyInterval_t expected(toIpintTime("20131211T000000"), toIpintTime("20131212T00000"));

    BOOST_TEST_INFO("Wrong normalizer result!");
    BOOST_CHECK_EQUAL(expected, normalizer(requested));
}

BOOST_AUTO_TEST_CASE(testDefaultRequestedIntervalNormalizerSeveralDays)
{
    DefaultIntervalNormalizer normalizer;
    const historyInterval_t requested(0, toIpintTime("20131211T063522"));
    const historyInterval_t expected(0, toIpintTime("20131212T0000"));

    BOOST_TEST_INFO("Wrong normalizer result!");
    BOOST_CHECK_EQUAL(expected, normalizer(requested));
}

BOOST_AUTO_TEST_CASE(testDefaultRequestedIntervalNormalizerOneHour)
{
    DefaultIntervalNormalizer normalizer;
    const historyInterval_t requested(toIpintTime("20210721T141147"), toIpintTime("20210721T141250"));
    const historyInterval_t expected(toIpintTime("20210721T140000"), toIpintTime("20210721T150000"));

    BOOST_TEST_INFO("Wrong normalizer result!");
    BOOST_CHECK_EQUAL(expected, normalizer(requested));
}

BOOST_AUTO_TEST_CASE(testDefaultRequestedIntervalNormalizerTwoHours)
{
    DefaultIntervalNormalizer normalizer;
    const historyInterval_t requested(toIpintTime("20210721T144547"), toIpintTime("20210721T154450"));
    const historyInterval_t expected(toIpintTime("20210721T140000"), toIpintTime("20210721T160000"));

    BOOST_TEST_INFO("Wrong normalizer result!");
    BOOST_CHECK_EQUAL(expected, normalizer(requested));
}

BOOST_AUTO_TEST_CASE(test_getPresentationRange_function)
{
    {
        historyInterval_t cacheInterval;
        historyInterval_t deviceInterval;
        BOOST_TEST_INFO("Wrong result for empty ranges");
        BOOST_CHECK_EQUAL(historyInterval_t(), getPresentationRange(cacheInterval, deviceInterval));
    }

    {
        historyInterval_t cacheInterval(0 , 10);
        historyInterval_t deviceInterval;
        BOOST_TEST_INFO("Wrong result for empty deviceInterval");
        BOOST_CHECK_EQUAL(historyInterval_t(0, 10), getPresentationRange(cacheInterval, deviceInterval));
    }

    {
        historyInterval_t cacheInterval;
        historyInterval_t deviceInterval(2, 5);
        BOOST_TEST_INFO("Wrong result for empty cacheInterval");
        BOOST_CHECK_EQUAL(historyInterval_t(2, 5), getPresentationRange(cacheInterval, deviceInterval));
    }

    {
        historyInterval_t cacheInterval(1, 3);
        historyInterval_t deviceInterval(2, 5);
        BOOST_TEST_INFO("Wrong result for valid intervals");
        BOOST_CHECK_EQUAL(historyInterval_t(1, 5), getPresentationRange(cacheInterval, deviceInterval));
    }
}

BOOST_AUTO_TEST_SUITE_END() // TestPolicyClasses

namespace
{
struct MockCacheBase
{
    virtual void add(const historyInterval_t& range,
        const historyIntervalSet_t& records)
    {
        BOOST_FAIL("Should not be called");
    }

    virtual optionalIntervalSet_t get(const historyInterval_t& requestedInterval) const
    {
        BOOST_FAIL("Should not be called");
        return optionalIntervalSet_t();
    }
};

struct MockDeviceRequester
{
    typedef historyIntervalSet_t rangeList_t;
    virtual rangeList_t findRecordings(const ITV8::GDRV::DateTimeRange&, IPINT30::stopSignal_t&)
    {
        BOOST_FAIL("Should not be called");
        return rangeList_t();
    }
};

}

namespace IPINT30
{
historyIntervalSet_t makeHistoryIntervalSet(MockDeviceRequester::rangeList_t r,
                        std::string recordingId, std::string trackID, DECLARE_LOGGER_ARG)
{
    BOOST_TEST_INFO("Wrong recording id");
    BOOST_CHECK_EQUAL(std::string("recordingId"), recordingId);

    BOOST_TEST_INFO("Wrong trackID id");
    BOOST_CHECK_EQUAL(std::string("trackID"), trackID);
    return r;
}
}

#include "../CachedHistoryRequester.inl"

BOOST_AUTO_TEST_SUITE(TestCachedHistoryRequester)

BOOST_AUTO_TEST_CASE(testFoundInCache)
{
    struct MockCache : public MockCacheBase
    {
        virtual optionalIntervalSet_t get(const historyInterval_t& requestedInterval) const
        {
            BOOST_TEST_INFO("Wrong requested interval");
            BOOST_CHECK_EQUAL(historyInterval_t(0, 10), requestedInterval);

            historyIntervalSet_t result;
            result += historyInterval_t(0, 5);
            return result;
        }
    };
    MockDeviceRequester deviceRequester;

    CachedHistoryRequester<DefaultIntervalNormalizer, MockCache> cahedRequester("recordingId");
    historyIntervalSet_t intervalSet  = cahedRequester.findRecordings(
        deviceRequester, "trackID", historyInterval_t(0, 10), GET_LOGGER_PTR);

    BOOST_TEST_INFO("Wrong result interval returned");
    BOOST_CHECK_EQUAL(historyInterval_t(0, 5), boost::icl::hull(intervalSet));
}

BOOST_AUTO_TEST_CASE(testRequestDevice)
{
    struct MockNormalizer
    {
        historyInterval_t operator()(historyInterval_t interval)
        {
            BOOST_TEST_INFO("Wrong requested interval");
            BOOST_CHECK_EQUAL(historyInterval_t(0, 10), interval);

            return historyInterval_t(0, 20);
        }
    };

    struct MockCache
    {
        MockCache() : m_firstTime(true)
        {}

        virtual void add(const historyInterval_t& range,
            const historyIntervalSet_t& records)
        {
            BOOST_TEST_INFO("Wrong interval added");
            BOOST_CHECK_EQUAL(historyInterval_t(0, 20), range);

            BOOST_TEST_INFO("Wrong records added");
            BOOST_CHECK_EQUAL(historyInterval_t(5, 15), boost::icl::hull(records));
        }

        virtual optionalIntervalSet_t get(const historyInterval_t& requestedInterval) const
        {
            BOOST_TEST_INFO("Wrong requested interval");
            BOOST_CHECK_EQUAL(historyInterval_t(0, 10), requestedInterval);

            if (m_firstTime)
            {
                m_firstTime = false;
                return optionalIntervalSet_t();
            }
            historyIntervalSet_t result;
            result += historyInterval_t(5, 10);
            return result;
        }

    private:
        mutable bool m_firstTime;
    };

    struct MockDevice
    {
        typedef historyIntervalSet_t rangeList_t;
        virtual rangeList_t findRecordings(const ITV8::GDRV::DateTimeRange& timeBounds, IPINT30::stopSignal_t&)
        {
            BOOST_TEST_INFO("Wrong interval requested");
            BOOST_CHECK_EQUAL(historyInterval_t(0, 20), make_interval(timeBounds));

            historyIntervalSet_t result;
            result += historyInterval_t(5, 15);
            return result;
        }
    };

    MockDevice deviceRequester;

    CachedHistoryRequester<MockNormalizer, MockCache> cahedRequester("recordingId");
    historyIntervalSet_t intervalSet  = cahedRequester.findRecordings(
        deviceRequester, "trackID", historyInterval_t(0, 10), GET_LOGGER_PTR);

    BOOST_TEST_INFO("Wrong result interval returned");
    BOOST_CHECK_EQUAL(historyInterval_t(5, 10), boost::icl::hull(intervalSet));
}

BOOST_AUTO_TEST_CASE(testRequestDevice2)
{
    // test case from ACR-60107
    struct MockDevice
    {
        typedef historyIntervalSet_t rangeList_t;
        virtual rangeList_t findRecordings(const ITV8::GDRV::DateTimeRange& timeBounds, IPINT30::stopSignal_t&)
        {
            historyIntervalSet_t result;
            
            if (timeBounds.rangeBegin < toIpintTime("20211120T080000"))
                result.insert(historyInterval_t{ timeBounds.rangeBegin + 100, timeBounds.rangeBegin + 200 });

            result.insert(historyInterval_t{ toIpintTime("20211120T085300"), toIpintTime("20211120T085500") });
            result.insert(historyInterval_t{ toIpintTime("20211120T085000"), toIpintTime("20211120T085200") });
            return result;
        }
    };

    MockDevice deviceRequester;
    CachedHistoryRequester< DefaultIntervalNormalizer, RecordingsHistoryCache<AlwaysFresh> > cahedRequester("recordingId");

    historyIntervalSet_t intervalSet = cahedRequester.findRecordings(
        deviceRequester, "trackID", historyInterval_t(toIpintTime("20211120T084000"), toIpintTime("20211120T091000")), GET_LOGGER_PTR);
    BOOST_TEST_INFO("Wrong first results from device");
    BOOST_CHECK_EQUAL(intervalSet.iterative_size(), 2ull);

    intervalSet = cahedRequester.findRecordings(
        deviceRequester, "trackID", historyInterval_t(toIpintTime("20211120T060000"), toIpintTime("20211120T085400")), GET_LOGGER_PTR);
    BOOST_TEST_INFO("Wrong second results from device");
    BOOST_CHECK_EQUAL(intervalSet.iterative_size(), 2ull);
}

BOOST_AUTO_TEST_SUITE_END() // TestCachedHistoryRequester