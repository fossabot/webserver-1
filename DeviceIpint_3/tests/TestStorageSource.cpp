#include <memory>
#include <utility>

#include <stdlib.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/test/unit_test.hpp>

#include <Logging/Log3.h>
#include <ItvSdk/include/baseTypes.h>

#include "../StorageSource.h"
#include "../EmbeddedStorage.h"
#include "../TimeStampHelpers.h"
#include "TestUtils.h"
#include "MockRecordingSearch.h"

#include <CorbaHelpers/CorbaStl.h>


using namespace DeviceIpint_3::UnitTesting;
using namespace IPINT30;

namespace
{
const ITV8::timestamp_t ONE_DAY_MS = 1ull * 24 * 60 * 60 * 1000;

void adjustEpoch(ITV8::timestamp_t& diff)
{
    {
        // test time
        auto pt = NMMSS::PtimeFromQword(diff);
        auto ipintTime = IPINT30::toIpintTime(pt);
        if (ipintTime != 0)
            return;
    }

    using namespace boost::posix_time;
    const static ptime anEpoch(boost::gregorian::date(1900, 1, 1));
    ptime t(IPINT30::startOfEpoch(), milliseconds(diff));

    diff = (t - anEpoch).total_milliseconds();
}

ITV8::timestamp_t convTime(ITV8::timestamp_t diff)
{
    adjustEpoch(diff);
    return diff;
}

ITV8::timestamp_t stripEpoch(ITV8::timestamp_t diff)
{
    static const auto EPOCH = convTime(0);
    if (diff >= EPOCH)
        return diff - EPOCH;

    return diff;
}

class Fixture: public BasicFixture
{
    boost::shared_ptr<IPINT30::IIpintDevice> m_parent;
    ITV8::Utility::RecordingInfoSP m_recordingsInfo;
    std::shared_ptr<ITV8::GDRV::IDevice> m_ipintDevice;
    std::shared_ptr<MockRecordingSearch> m_mockRecordingSearch;
    PStorageSource m_storageSource;

    SEmbeddedStoragesParam m_storageParam;
    EmbeddedStoragePtr m_es;

public: 
    Fixture()
    {
        m_mockRecordingSearch = std::make_shared<MockRecordingSearch>(GetLogger());
        m_recordingsInfo = boost::make_shared<ITV8::Utility::RecordingInfo>();
        m_recordingsInfo->id = "test";
        m_recordingsInfo->tracks.emplace_back(ITV8::Utility::TrackInfo("video", "video track", ITV8::GDRV::Storage::EVideoTrack));
        m_ipintDevice = CreateTestIpintDevice(m_mockRecordingSearch, m_recordingsInfo);
        m_parent = CreateFakeIpintDevice(m_ipintDevice);
    }

    ITV8::GDRV::IStorageDevice* getStorageDevice()
    {
        try
        {
            return ITV8::contract_cast<ITV8::GDRV::IStorageDevice>(m_ipintDevice.get());
        }
        catch (const std::runtime_error&)
        {
        }
        return nullptr;
    }

    // StorageSource initialization part
    void init()
    {
        m_es = EmbeddedStoragePtr(new EmbeddedStorage(GetLogger(), GetDynamicThreadPool(), nullptr,
            m_storageParam, false, nullptr, "EmbeddedStorage", nullptr, m_contNamed, 1, "test"));

        int playbackSpeed = 0;
        bool requestAudio = false;
        const std::string objectId = "test_StorageSource";
        const std::string eventChannel = "test_channel";



        m_storageSource = new StorageSource(GetContainerNamed(), "video_id", m_recordingsInfo, m_parent,
            m_es.Get(), getStorageDevice(), GetDynamicThreadPool(), playbackSpeed, requestAudio, objectId, eventChannel);
    }

    std::shared_ptr<MockRecordingSearch>& mockRecordingSearch() { return m_mockRecordingSearch; }

    ITV8::Utility::RecordingInfoSP& recordingsInfo() { return m_recordingsInfo; }

    PStorageSource storageSource() { return m_storageSource; }

    ::MMSS::HistoryResult GetHistory2(::MMSS::HistoryScanMode mode,
                                      ::CORBA::LongLong timeFrom,
                                      ::CORBA::LongLong timeTo,
                                      MMSS::StorageSource::IntervalSeq_var& intervals,
                                      unsigned long minGapMs = 0, unsigned int maxCount = 0)
    {
        return m_storageSource->GetHistory2(mode, convTime(timeFrom), convTime(timeTo), maxCount, minGapMs, intervals.out());
    }

    void GetHistory(const char* beginTime, const char* endTime, MMSS::StorageSource::IntervalSeq_var intervals,
        uint32_t maxCount = 0)
    {
        return m_storageSource->GetHistory(beginTime, endTime, maxCount, 1000, intervals.out());
    }

    void WaitStorageSourceBackgroundActions()
    {
        m_storageSource->waitHistoryRequester2BackgroundActions();
    }  

    void Tweak(const CachedHistoryRequester2::STweaks& tweaks)
    {
        m_storageSource->UT_tweakCachedHistoryRequester2(tweaks);
    }

    void PrintRange(DECLARE_LOGGER_ARG, int logLevel, const char* prefix, const std::vector<ITV8::GDRV::DateTimeRange>& range)
    {
        std::ostringstream oss; 
        oss << prefix << ": ";
        for (auto b = range.begin(); b != range.end(); ++b)
            oss << '[' << b->rangeBegin << ", " << b->rangeEnd << "] ";

        _lognf_(logLevel, "{}", oss.str());
    }

    std::vector<ITV8::GDRV::DateTimeRange> fromOrbIntervals(const MMSS::StorageSource::IntervalSeq_var& actualOrb)
    {
        auto actualRange = NCorbaHelpers::make_range(actualOrb.in());

        // convert to normal vector
        std::vector<ITV8::GDRV::DateTimeRange> actual;
        std::transform(actualRange.begin(), actualRange.end(), std::inserter(actual, actual.end()), [](auto& p)
        {
            ITV8::GDRV::DateTimeRange r;
            r.rangeBegin = stripEpoch(p.beginTime);
            r.rangeEnd = stripEpoch(p.endTime);
            return r;
        });

        return actual;
    }

    void check(const MMSS::StorageSource::IntervalSeq_var& actualOrb, std::vector<ITV8::GDRV::DateTimeRange> expected)
    {
        auto actual = fromOrbIntervals(actualOrb);
        BOOST_CHECK(actual.size() == expected.size());

        auto expectedInterval = expected.begin();
        for (const auto& actualInterval : actual)
        {
            BOOST_TEST(actualInterval.rangeBegin == expectedInterval->rangeBegin);
            BOOST_TEST(actualInterval.rangeEnd == expectedInterval->rangeEnd);
            ++expectedInterval;
        }

        if (IsCurrentTestFailed())
        {
            _errf_("=== FAILED CHECK RANGE BEGIN ===");
            PrintRange(GetLogger(), NLogging::LEVEL_ERROR, "expected", expected);
            PrintRange(GetLogger(), NLogging::LEVEL_ERROR, "actual  ", actual);
            _errf_("=== FAILED CHECK RANGE END ===");
            BOOST_REQUIRE(false);
        }
    }

    void checkIfEmpty(const MMSS::StorageSource::IntervalSeq_var& actualOrb)
    {
        auto actualRange = NCorbaHelpers::make_range(actualOrb.in());
        auto actualRangeSize = std::distance(actualRange.begin(), actualRange.end());
        BOOST_TEST(actualRangeSize == 0);
    }

	void checkedStopStorageSource()
	{
		auto fut = std::async([this]() 
        {
            _dbg_ << "Stopping StorageSource ...";
            storageSource()->Stop(); 
            _dbg_ << "Stopped StorageSource.";
        });
        
        BOOST_CHECK(fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
		fut.get();
	}
};

class TracksGen
{
public:
    uint32_t delay = 0;
    ITV8::Utility::tracksRangeList_t list;
    const std::string name;

    TracksGen(uint32_t delay_, const std::string& name_ = "video"):
        delay(delay_),
        name(name_)
    {}

    TracksGen& operator ()(ITV8::GDRV::DateTimeRange r)
    {
        list.emplace_back(ITV8::Utility::TrackRange(name, r));
        return *this;
    }

    std::pair<uint32_t, ITV8::Utility::tracksRangeList_t> get()
    {
        return std::make_pair(delay, list);
    }
};
}

BOOST_FIXTURE_TEST_SUITE(TestGetHistory, Fixture)

BOOST_AUTO_TEST_CASE(ZeroRange)
{
    init();
    MMSS::StorageSource::IntervalSeq_var intervals;
    storageSource()->GetHistory(0, 0, 0, 1000, intervals.out());
    BOOST_CHECK_MESSAGE(intervals->length() == 0, "Intervals length should be null");
    storageSource()->GetHistory(0, "20211018T092343.999", 0, 1000, intervals.out());
    BOOST_CHECK_MESSAGE(intervals->length() == 0, "Intervals length should be null");
}

BOOST_AUTO_TEST_CASE(PresentationRangeByRecordingInfo)
{
    init();
    recordingsInfo()->m_presentationRange = makeIpintTimeRange(historyInterval_t(0, 100500));
    using namespace boost::posix_time;

    MMSS::StorageSource::IntervalSeq_var intervals;
    storageSource()->GetHistory(to_iso_string(ptime(min_date_time)).c_str(), to_iso_string(ptime(max_date_time)).c_str(), 1, 1000, intervals.out());
    check(intervals, { {
                {0, 100500}
            } });
}

BOOST_AUTO_TEST_CASE(PresentationRangeFromCache)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(10)({100, 140})({250, 260})({1000, 1100}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    using namespace boost::posix_time;
    // Fill cache first
    storageSource()->GetHistory(ipintTimestampToIsoString(50).c_str(), ipintTimestampToIsoString(1500).c_str(), 1000, 1000, intervals.out());
    storageSource()->GetHistory(to_iso_string(ptime(min_date_time)).c_str(), to_iso_string(ptime(max_date_time)).c_str(), 1, 1000, intervals.out());
    check(intervals, { {
                {100, 1100}
            } });
}

BOOST_AUTO_TEST_CASE(PresentationRangeMerged)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(10)({100, 140})({250, 260})({1000, 1100}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);
    recordingsInfo()->m_presentationRange = makeIpintTimeRange(historyInterval_t(4, 999));

    MMSS::StorageSource::IntervalSeq_var intervals;
    using namespace boost::posix_time;
    // Fill cache first
    storageSource()->GetHistory(ipintTimestampToIsoString(50).c_str(), ipintTimestampToIsoString(1500).c_str(), 1000, 1000, intervals.out());
    storageSource()->GetHistory(to_iso_string(ptime(min_date_time)).c_str(), to_iso_string(ptime(max_date_time)).c_str(), 1, 1000, intervals.out());
    check(intervals, { {
                {4, 1100}
            } });
}

BOOST_AUTO_TEST_CASE(PresentationEmpty)
{
    init();
    MMSS::StorageSource::IntervalSeq_var intervals;
    using namespace boost::posix_time;
    storageSource()->GetHistory(to_iso_string(ptime(min_date_time)).c_str(), to_iso_string(ptime(max_date_time)).c_str(), 1, 1000, intervals.out());
    checkIfEmpty(intervals);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(TestGetHistory2, Fixture)

BOOST_AUTO_TEST_CASE(ZeroRanges)
{
    init();
    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(storageSource()->GetHistory2(MMSS::HSM_EXACT, 0, 0, 0, 1000, intervals.out()) == MMSS::HR_FULL);
    BOOST_TEST(storageSource()->GetHistory2(MMSS::HSM_EXACT, 0, 1, 0, 1000, intervals.out()) == MMSS::HR_FULL);
}

BOOST_AUTO_TEST_CASE(SimpleRange)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10})({0, 10}).get(),
            TracksGen(10)({1, 10})({20, 40}).get(),
            TracksGen(11)({1, 10})({1000, 2000}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 1000, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {1, 10},
                {20, 40}
            } });
    }

    // get from cache immediately
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 11, 2000, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {20, 40},
                {1000, 2000}
            } });
    }

    // get from cache immediately
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1100, 2000, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {1100, 2000}
            } });
    }

    // get from cache immediately
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 15, 100, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {20, 40}
            } });
    }

    // get from cache immediately
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1500, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {1000, 1500}
            } });
    }

    // get from cache immediately partial result
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 1000, intervals, 0, 1) == MMSS::HR_PARTIAL);

        check(intervals, { {
                {1, 10}
            } });
    }

    // try get from non existing range
    {
        MMSS::StorageSource::IntervalSeq_var intervals;

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 2000, 2500, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();
        checkIfEmpty(intervals);

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 2000, 2500, intervals) == MMSS::HR_PARTIAL);
        checkIfEmpty(intervals);
    }

	checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(MergeByMinGap_OneInterval)
{
    init();
    reportingTracks_t t =
    { {
        TracksGen(1)({100, 140}).get()
     } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        // min gap equal 30
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
        check(intervals, { {{100, 140}} });
    }

    checkedStopStorageSource();
} 


BOOST_AUTO_TEST_CASE(MergeByMinGap_ZeroInterval)
{
    init();
    reportingTracks_t t =
    { {
        TracksGen(1).get()
     } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
    WaitStorageSourceBackgroundActions();

    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_FULL);
    checkIfEmpty(intervals);
    
    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(MergeByMinGap_OneMergableInterval)
{
    init();
    reportingTracks_t t =
        {{
            TracksGen(1)({100, 140})({160, 170}).get()
         }};
    mockRecordingSearch()->setReportingTracks(t);

    {
        // min gap equal 30
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
        check(intervals, {{{100, 170}}});
    }

    checkedStopStorageSource();
}  

BOOST_AUTO_TEST_CASE(MergeByMinGap_OneNonMergableInterval)
{
    init();
    reportingTracks_t t =
        {{
            TracksGen(1)({100, 140})({180, 190}).get()
         }};
    mockRecordingSearch()->setReportingTracks(t);

    {
        // min gap equal 30
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals, 30) == MMSS::HR_PARTIAL);
        check(intervals, {{{100, 140}, {180, 190}}});
    }

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(MergeByMinGap)
{
    init();
    reportingTracks_t t =
        {{
            TracksGen(1)({100, 140})({160, 170})({210, 220})({240, 250})({260, 270})({320, 330}).get()
         }};
    mockRecordingSearch()->setReportingTracks(t);

    {
        // min gap equal 30
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 330, intervals, 30) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 330, intervals, 30) == MMSS::HR_FULL);
        check(intervals, {{{100, 170}, {210, 270}, {320, 330}}});

        // check for partial result on maxcount = 2 
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 330, intervals, 30, 2) == MMSS::HR_PARTIAL);
        check(intervals, {{{100, 170}, {210, 270}}});      

        // check for full result on maxcount = 1 
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 330, intervals, 51, 1) == MMSS::HR_FULL);
        check(intervals, {{{100, 330}}});
    }

    checkedStopStorageSource();
}   

BOOST_AUTO_TEST_CASE(MergeByMinGap2)
{
    init();
    reportingTracks_t t =
        {{
            TracksGen(1)({160, 170})({210, 220})({240, 250}).get()
         }};
    mockRecordingSearch()->setReportingTracks(t);

    {
        // min gap equal 30
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 250, intervals, 30) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 250, intervals, 30) == MMSS::HR_FULL);
        check(intervals, {{{160, 170}, {210, 250}}});
    }

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(PartialResult)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get(),
            TracksGen(11)({ONE_DAY_MS + 1000, ONE_DAY_MS + 2000}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {1, 10},
                {20, 40},
                {400, 500}
            } });

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 450, ONE_DAY_MS + 2000, intervals) == MMSS::HR_PARTIAL);

        check(intervals, { {
                {450, 500}
            } });

        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 450, ONE_DAY_MS + 2000, intervals) == MMSS::HR_FULL);
        check(intervals, { {
                {450, 500},
                {ONE_DAY_MS + 1000, ONE_DAY_MS + 2000}
            } });
    }

	checkedStopStorageSource();

}

BOOST_AUTO_TEST_CASE(PartialIfError)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    CachedHistoryRequester2::STweaks tweaks;
    tweaks.RecentRequestInterval = std::chrono::milliseconds(1000);
    Tweak(tweaks);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        mockRecordingSearch()->setSearchWillFail(true);
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 40, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        // means that all 3 unsuccessfully tries will take more time than setRecentRequestInterval()
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        mockRecordingSearch()->setSearchWillFail(false);
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 40, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 40, intervals) == MMSS::HR_FULL);
        check(intervals, { {
                {1, 10},
                {20, 40}
            } });
    }

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(PartialResultWithGap)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        // full because there already readed history and it has gap at the requested interval
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 10, 20, intervals) == MMSS::HR_FULL);

        checkIfEmpty(intervals);
    }

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 10, 30, intervals) == MMSS::HR_FULL);

        check(intervals, { {
            {20, 30},
            } });
    }

    {
        MMSS::StorageSource::IntervalSeq_var intervals;

        //request non-existing range
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1000, intervals) == MMSS::HR_PARTIAL);

        checkIfEmpty(intervals);

    }

	checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(AddRangeAtSearching)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    CachedHistoryRequester2::STweaks tweaks;
    tweaks.RecentRequestInterval = std::chrono::milliseconds(1000);
    Tweak(tweaks);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        //request non-existing range
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();
        checkIfEmpty(intervals);

        // ensure after all background actions there is not exist requested range
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        // add range to device
        t.insert(TracksGen(11)({ 500, 1000 }).get());
        mockRecordingSearch()->setReportingTracks(t);

        // CachedHistoryRequester2 remember recent requests with empty result at right side from history end time.
        // Therefore here need wait >10 seconds until it will clean it recent requests.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        //search again
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 500, 1000, intervals) == MMSS::HR_FULL);

        check(intervals, { {
                {500, 1000}
            } });

    }
    {
        // add range to device
        t.insert(TracksGen(12)({ 1000, 1200 }).get());
        mockRecordingSearch()->setReportingTracks(t);

        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1000, 1200, intervals) == MMSS::HR_PARTIAL); 
        checkIfEmpty(intervals);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1000, 1200, intervals) == MMSS::HR_FULL);
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1000, 1300, intervals) == MMSS::HR_PARTIAL);

        check(intervals, { {
                {1000, 1200}
            } });
    }

	checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(EmptyHistoryWithDownIntervals)
{
    init();

    reportingTracks_t t =
    { {
        TracksGen(1).get()
     } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 100, 110, intervals) == MMSS::HR_PARTIAL);
    WaitStorageSourceBackgroundActions();

    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 100, intervals) == MMSS::HR_FULL);
    WaitStorageSourceBackgroundActions();

    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 100, intervals) == MMSS::HR_FULL);

    checkIfEmpty(intervals);

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(FullResultAtRightSideOfHistory)
{
    init();
    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    historyInterval_t requestInterval(nowTs - 90000, nowTs);
    historyInterval_t deviceHistory(nowTs - 200000, nowTs);
    reportingTracks_t t =
    { {
        TracksGen(1)({deviceHistory.lower(), deviceHistory.upper()}).get(),
    } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper(), intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper(), intervals) == MMSS::HR_FULL);
        check(intervals, { {
                {requestInterval.lower(), deviceHistory.upper()}
            } });

        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper() + 15000, intervals) == MMSS::HR_FULL);
        check(intervals, { {
                {requestInterval.lower(), deviceHistory.upper()}
            } });
    }

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(StopAtSearching)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(200)({20, 40})({400, 500}).get(),
            TracksGen(10000)({2000, 4000})({4000, 5000}).get()
    } };

    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 5000, intervals) == MMSS::HR_PARTIAL);

	checkedStopStorageSource();

}

BOOST_AUTO_TEST_CASE(StopAtSearchingAfterWaitStorageSourceBackgroundActions)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(100)({20, 40})({400, 500}).get(),
            TracksGen(210)({2000, 4000})({4000, 5000}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 5000, intervals) == MMSS::HR_PARTIAL);
    WaitStorageSourceBackgroundActions();

	checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(StormRequestsAndGetResult)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get(),
            TracksGen(100)({ONE_DAY_MS + 1000, ONE_DAY_MS + 2000}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        for (size_t i = 0; i < 100; i++)
        {
            
            MMSS::StorageSource::IntervalSeq_var intervals;
            GetHistory2(MMSS::HSM_EXACT, 1, i * 10, intervals);
            _dbg_ << "TEST ITERATION " << i;
        }

        WaitStorageSourceBackgroundActions();

        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 500, intervals) == MMSS::HR_FULL);
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 1000, intervals) == MMSS::HR_PARTIAL);

        check(intervals, { {
                {1, 10},
                {20, 40},
                {400, 500}
            } });
    }

	checkedStopStorageSource();

}

BOOST_AUTO_TEST_CASE(StormRequestsAndStop)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get(),
            TracksGen(100)({ONE_DAY_MS + 1000, ONE_DAY_MS + 2000}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    for (size_t i = 0; i < 100; i++)
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        GetHistory2(MMSS::HSM_EXACT, 1, i * 10, intervals);
    }

	checkedStopStorageSource();
} 

BOOST_AUTO_TEST_CASE(LastSearchSecondDay)
{
    init();
    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    const auto WEEK = ONE_DAY_MS * 7;
    historyInterval_t firstDay(nowTs - WEEK * 3, nowTs - WEEK * 3 + ONE_DAY_MS);
    historyInterval_t secondDay(nowTs - WEEK * 2, nowTs - WEEK * 2 + ONE_DAY_MS);
    historyInterval_t thirdDay(nowTs - WEEK, nowTs - WEEK + ONE_DAY_MS);
    historyInterval_t whole(firstDay.lower(), thirdDay.upper());
    reportingTracks_t t =
        { {
            TracksGen(1)({firstDay.lower(), firstDay.upper()}).get(),
            TracksGen(20)({secondDay.lower(), secondDay.upper()}).get(),
            TracksGen(21)({thirdDay.lower(), thirdDay.upper()}).get(),
        } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        _dbg_ << "FIRST TEST: third day";
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, thirdDay.lower(), thirdDay.lower() + 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, thirdDay.lower(), thirdDay.lower() + 1000, intervals) == MMSS::HR_FULL);
        check(intervals, { {
            {thirdDay.lower(), thirdDay.lower() + 1000}
        } });
    } 

    {
        _dbg_ << "SECOND TEST: first day";
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, firstDay.lower(), firstDay.lower() + 1000, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, firstDay.lower(), firstDay.lower() + 1000, intervals) == MMSS::HR_FULL);
        check(intervals, { {
            {firstDay.lower(), firstDay.lower() + 1000}
        } });
    }  

    {
        _dbg_ << "THIRD TEST: all days, include second";

        MMSS::StorageSource::IntervalSeq_var intervals;
        for (int i = 0; i < 4; i++)
        {
            BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, whole.lower(), whole.upper(), intervals) == MMSS::HR_PARTIAL);

            auto actualRange = NCorbaHelpers::make_range(intervals.in());
            size_t actualRangeSize = std::distance(actualRange.begin(), actualRange.end());

            if (i == 3)
            {
                BOOST_TEST(actualRangeSize == 1u);
                break;
            }

            if (actualRangeSize == 1u)
                break;
        }

        check(intervals, { {
            {firstDay.lower(), firstDay.upper()}
        } });

        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, whole.lower(), whole.upper(), intervals) == MMSS::HR_FULL);
        check(intervals, { {
           {firstDay.lower(), firstDay.upper()},
           {secondDay.lower(), secondDay.upper()},
           {thirdDay.lower(), thirdDay.upper()}
        } });
    }

    checkedStopStorageSource();
}  

BOOST_AUTO_TEST_CASE(PartialWithEmptyResult)
{
    init();
    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    historyInterval_t whole(nowTs - ONE_DAY_MS * 2, nowTs - 10000);
    historyInterval_t rightSide(nowTs - ONE_DAY_MS / 2, nowTs - 10000);
    reportingTracks_t t =
        { {
            TracksGen(10)({rightSide.lower(), rightSide.upper()}).get(),
        } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, rightSide.lower(), rightSide.upper(), intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, rightSide.lower(), rightSide.upper(), intervals) == MMSS::HR_FULL);
        check(intervals, { {
            {rightSide.lower(), rightSide.upper()}
        } });
    }

    {   
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, whole.lower(), whole.upper(), intervals) == MMSS::HR_PARTIAL);

        // left side of requested interval was not searched but right side already has non-empty, in this case here shold be empty result
        checkIfEmpty(intervals);

        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, whole.lower(), whole.upper(), intervals) == MMSS::HR_FULL);
        check(intervals, { {
            {rightSide.lower(), rightSide.upper()}
        } });
    }

    checkedStopStorageSource();
}   

BOOST_AUTO_TEST_CASE(ShortRequestAtRightSideOfHistory)
{
    init();
    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    historyInterval_t requestInterval(nowTs - 90000, nowTs);
    historyInterval_t deviceHistory(nowTs - 200000, nowTs - 120000);
    reportingTracks_t t =
        { {
            TracksGen(1)({deviceHistory.lower(), deviceHistory.upper()}).get(),
        } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper(), intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper(), intervals) == MMSS::HR_FULL);
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper(), intervals) == MMSS::HR_FULL);

        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, requestInterval.lower(), requestInterval.upper(), intervals) == MMSS::HR_FULL);
        checkIfEmpty(intervals);
    }

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(RequestsAtRightSideOfHistory)
{
    init();
    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10}).get(),
            TracksGen(10)({20, 40})({400, 500}).get(),
            TracksGen(300)({1000, 2000}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 2010, 3000, intervals) == MMSS::HR_PARTIAL);
    WaitStorageSourceBackgroundActions();

    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 2010, 3000, intervals) == MMSS::HR_FULL); 
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1000, 2000, intervals) == MMSS::HR_FULL);


    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(RequestsBesideOfNow_BigInterval)
{
    init();
    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    const auto yesterdayMs = nowTs - ONE_DAY_MS - 1;
    reportingTracks_t t =
        { {
            TracksGen(10)({nowTs - ONE_DAY_MS * 2, yesterdayMs}).get(),
        } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, yesterdayMs, nowTs - 5000, intervals) == MMSS::HR_PARTIAL);
    WaitStorageSourceBackgroundActions();

    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, yesterdayMs, nowTs - 5000, intervals) == MMSS::HR_FULL);

    checkIfEmpty(intervals);

	checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(RequestsBesideOfNow_ShortInterval)
{
    init();

    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto minuteAgoMs = nowTs - 60 * 1000;
    reportingTracks_t t =
    { {
        TracksGen(10)({nowTs - ONE_DAY_MS * 2, nowTs - ONE_DAY_MS}).get(),
    } };
    mockRecordingSearch()->setReportingTracks(t);

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, minuteAgoMs, nowTs - 5000, intervals) == MMSS::HR_PARTIAL);
    WaitStorageSourceBackgroundActions();

    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, minuteAgoMs, nowTs - 5000, intervals) == MMSS::HR_FULL);

    checkIfEmpty(intervals);

    checkedStopStorageSource();

}

BOOST_AUTO_TEST_CASE(CacheDepth)
{
    init();

    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    reportingTracks_t t =
    { {
        TracksGen(1)({nowTs - ONE_DAY_MS , nowTs}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    // cache update time 10 seconds and cache depth 1 day
    CachedHistoryRequester2::STweaks tweaks;
    tweaks.CacheDepthMs = 24 * 1000 * 60 * 60;
    tweaks.UpdateCacheTimeout = boost::posix_time::seconds(10);
    Tweak(tweaks);

    WaitStorageSourceBackgroundActions();

    MMSS::StorageSource::IntervalSeq_var intervals;
    BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, nowTs - ONE_DAY_MS, nowTs - 5000, intervals) == MMSS::HR_FULL);

    check(intervals, { {
        {nowTs - ONE_DAY_MS, nowTs - 5000}
    } });

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(CacheDepthFeatureAfterUpdate)
{
    init();

    const uint64_t nowTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    reportingTracks_t t =
    { {
        TracksGen(1)({nowTs - ONE_DAY_MS, nowTs - ONE_DAY_MS + 1000}).get(),
        TracksGen(2)({nowTs - ONE_DAY_MS + 1000, nowTs}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    // cache update time 1 second and cache depth 1 day
    CachedHistoryRequester2::STweaks tweaks;
    tweaks.CacheDepthMs = 24 * 1000 * 60 * 60;
    tweaks.UpdateCacheTimeout = boost::posix_time::seconds(1);
    Tweak(tweaks);
    WaitStorageSourceBackgroundActions();

    std::vector<ITV8::GDRV::DateTimeRange> expected = { {nowTs - ONE_DAY_MS, nowTs - 5000} };
    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, nowTs - ONE_DAY_MS, nowTs - 5000, intervals) == MMSS::HR_FULL);

        check(intervals, expected);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    WaitStorageSourceBackgroundActions();

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, nowTs - ONE_DAY_MS + 1000, nowTs - 5000, intervals) == MMSS::HR_FULL);

        const auto actual = fromOrbIntervals(intervals);

        BOOST_TEST(actual.front().rangeBegin > expected.front().rangeBegin);
        BOOST_TEST(actual.front().rangeEnd == expected.front().rangeEnd);

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, nowTs - ONE_DAY_MS, nowTs - 5000, intervals) == MMSS::HR_PARTIAL);
    }

    checkedStopStorageSource();
}

BOOST_AUTO_TEST_CASE(EmulateWriteByRing)
{
    init();

    // cache update time 1 second and cache depth 0 day
    CachedHistoryRequester2::STweaks tweaks;
    tweaks.UpdateCacheTimeout = boost::posix_time::seconds(1);
    tweaks.EmptyResultTrustInterval = std::chrono::milliseconds(1000);
    Tweak(tweaks);

    reportingTracks_t t =
    { {
            TracksGen(0)({1, 10})({20, 40}).get(),
            TracksGen(1)({40, 50})({50, 60}).get()
    } };
    mockRecordingSearch()->setReportingTracks(t);

    {
        MMSS::StorageSource::IntervalSeq_var intervals;
        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 60, intervals) == MMSS::HR_PARTIAL);
        WaitStorageSourceBackgroundActions();

        BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 60, intervals) == MMSS::HR_FULL);
        WaitStorageSourceBackgroundActions();

        check(intervals, { {
                {1, 10},
                {20, 60}
            } });
    }

    // Emulate write by ring
    reportingTracks_t newTracks =
    { {
            TracksGen(0)({20, 40}).get(),
            TracksGen(1)({40, 50})({50, 60})({60, 70}).get()
    } };

    mockRecordingSearch()->setReportingTracks(newTracks);

    bool firstTest = true;
    for (int i = 0; i < 2; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500 * (i + 1)));
        WaitStorageSourceBackgroundActions();

        {
            _dbg_ << "first test " << firstTest;
            MMSS::StorageSource::IntervalSeq_var intervals;
            if (firstTest)
            {
                BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 70, intervals) == MMSS::HR_PARTIAL);
                WaitStorageSourceBackgroundActions();
            }

            BOOST_TEST(GetHistory2(MMSS::HSM_EXACT, 1, 70, intervals) == MMSS::HR_FULL);

            if (firstTest)
            {
                auto actualRange = NCorbaHelpers::make_range(intervals.in());
                size_t actualRangeSize = std::distance(actualRange.begin(), actualRange.end());

                if (actualRangeSize != 1)
                {
                    firstTest = false;
                    continue;
                }
            }

            check(intervals, { {
                    {20, 70}
                } });

            break;
        }
    }
	checkedStopStorageSource();
}

BOOST_AUTO_TEST_SUITE_END()

