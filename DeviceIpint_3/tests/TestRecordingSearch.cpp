#include <iostream>       // std::cout, std::cerr
#include <exception>      // std::exception, std::terminate

#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/test/unit_test.hpp>

#include <ItvSdk/include/IErrorService.h>
#include <IpUtil/include/TimeStampHelpers.h>

#include "../RecordingSearch.h"
#include "../StorageDataTypes.h"

using namespace IPINT30;
typedef boost::asio::io_service::work work_t;

namespace
{

class TestSandbox
{
public:
    TestSandbox()
        : m_dynExec(NExecutors::CreateDynamicThreadPool(NLogging::CreateLogger(), "DeviceIpint", NExecutors::IDynamicThreadPool::UNLIMITED_QUEUE_LENGTH, 2, 1024))
        , m_requestCount(0)
    {
    }

    ~TestSandbox()
    {
        m_dynExec->Shutdown();
    }

    RecordingSearch::rangeList_t runFindRecordings(ITV8::GDRV::IStorageDevice* storageDevice)
    {
        auto result = RecordingSearch::rangeList_t();
        try
        {
            RecordingSearch searcher(NLogging::CreateLogger(), storageDevice, "test", m_dynExec, boost::posix_time::millisec(500));
            updateRequestCount();
            result = searcher.findRecordings(ITV8::GDRV::DateTimeRange(), m_stopDeviceSearchSignal);
            updateRequestCount(false);
        }
        catch (const std::exception&)
        {
            updateRequestCount(false);

            throw;
        }

        return result;
    }

    ITV8::Utility::calendarList_t runGetCalendar(ITV8::GDRV::IStorageDevice* storageDevice)
    {
        auto result = ITV8::Utility::calendarList_t();
        try
        {
            RecordingSearch searcher(NLogging::CreateLogger(), storageDevice, "test", m_dynExec, boost::posix_time::millisec(500));
            updateRequestCount();
            result = searcher.getCalendar(ITV8::GDRV::DateTimeRange(), m_stopDeviceSearchSignal);
            updateRequestCount(false);
        }
        catch (const std::exception&)
        {
            updateRequestCount(false);

            throw;
        }

        return result;
    }

    void stop()
    {
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

private:
    void updateRequestCount(bool increaseOnly = true)
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

private:
    NExecutors::PDynamicThreadPool m_dynExec;
    IPINT30::stopSignal_t m_stopDeviceSearchSignal;
    std::mutex m_stopConditionGuard;
    std::condition_variable m_stopCondition;
    std::atomic<uint32_t> m_requestCount;
};

class MockStorageDeviceBase : protected ITV8::GDRV::IStorageDevice
{
public:
    MockStorageDeviceBase() :
        m_callsCount(0),
        m_sandbox(0)
    {}
    virtual void RequestRecordingsInfo(ITV8::GDRV::IRecordingsInfoHandler* handler)
    {}

    virtual void RequestRecordingInfo(const char* recordingToken,
        ITV8::GDRV::ISingleRecordingInfoHandler* handler) { }
    virtual ITV8::GDRV::IRecordingSearch* CreateRecordingSearch(const char* recordingId) { return 0;}
    virtual ITV8::GDRV::IRecordingSource* CreateRecordingSource(const char* recordingId,
        ITV8::MFF::IMultimediaFrameFactory* factory) { return 0; }
    virtual ITV8::GDRV::IRecordingPlayback* CreateRecordingPlayback(const char* recordingId, ITV8::MFF::IMultimediaFrameFactory* factory,
        ITV8::GDRV::Storage::ITrackIdEnumerator& tracks, ITV8::GDRV::IRecordingPlaybackHandler* handler)
    {
        return 0;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IStorageDevice)
    ITV8_END_CONTRACT_MAP()

    ITV8::GDRV::IStorageDevice* getStorageDevice(TestSandbox* sandbox)
    {
        m_sandbox = sandbox;
        return this;
    }

public:
    int m_callsCount;
    TestSandbox* m_sandbox;
};

template <typename TRecordingSearch>
class MockStorageDevice : public MockStorageDeviceBase
{
public:
    virtual ITV8::GDRV::IRecordingSearch* CreateRecordingSearch(const char* recordingId)
    {
        ++m_callsCount;
        return new TRecordingSearch;
    }
};

class MockRecordingsSource : public ITV8::GDRV::IRecordingSearch
{
public:
    MockRecordingsSource():
        m_callsCount(0),
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service))
     {}

    ~MockRecordingsSource()
    {
        m_work.reset();
        m_thread.join();
    }

    virtual void FindRecordings(const ITV8::DateTimeRange& timeRange,
        ITV8::GDRV::IRecordingSearchHandler* handler)
    {
        ++m_callsCount;
        m_service.post(boost::bind(&MockRecordingsSource::doFindRecordings,
            this, handler));
    }

    virtual void CancelFindRecordings()
    {
        std::cerr << "CancelFindRecordings should not be called";
        std::terminate();
    }

    virtual void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler) = 0;

    void Destroy()
    {
        BOOST_CHECK_EQUAL(1, m_callsCount);
        delete this;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingSearch)
    ITV8_END_CONTRACT_MAP()

public:
    int m_callsCount;

protected:
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;
};

class MockCalendarSource : public MockRecordingsSource
                         , public ITV8::GDRV::ICalendarSearch
{
public:
    MockCalendarSource() :
        MockRecordingsSource()
    {
    }

    ~MockCalendarSource()
    {
    }

    virtual void GetCalendar(const ITV8::DateTimeRange& timeRange,
        ITV8::GDRV::ICalendarHandler* handler)
    {
        ++m_callsCount;
        m_service.post(boost::bind(&MockCalendarSource::doGetCalendar, this, handler));
    }

    virtual void CancelCalendar()
    {
        std::cerr << "CancelCalendar should not be called";
        std::terminate();
    }

    virtual void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler*)
    {
        std::cerr << "doFindRecordings should not be called";
        std::terminate();
    }

    virtual void doGetCalendar(ITV8::GDRV::ICalendarHandler* handler) = 0;

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::ICalendarSearch)
    ITV8_END_CONTRACT_MAP()
};

}

BOOST_AUTO_TEST_SUITE(TestRecordingSearch)

BOOST_AUTO_TEST_CASE(testSearchFails)
{
    class RecordingsSource : public MockRecordingsSource
    {
    public:
        virtual void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler)
        {
            handler->Finished(this, ITV8::EGeneralError);
        }
    };

    MockStorageDevice<RecordingsSource> storageDevice;
    TestSandbox sandbox;
    try
    {
        sandbox.runFindRecordings(storageDevice.getStorageDevice(&sandbox));
        BOOST_FAIL("should throw an exception");
    }
    catch (const std::runtime_error&)
    {
    }
    BOOST_CHECK_EQUAL(6, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testSearchSuccess)
{
    class RecordingsSource : public MockRecordingsSource
    {
    public:
        virtual void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler)
        {
            ITV8::Utility::RecordingRange range("test", ITV8::GDRV::Storage::ERecordStatusRecording);
            ITV8::GDRV::DateTimeRange dateTimeRange = { ITV8::timestamp_t(), ITV8::Utility::timeStamp() };
            range.tracksRange.emplace_back("video", dateTimeRange);

            handler->RangeFound(this, range);
            handler->Finished(this, ITV8::ENotError);
        }
    };

    MockStorageDevice<RecordingsSource> storageDevice;
    TestSandbox sandbox;
    RecordingSearch::rangeList_t result =
        sandbox.runFindRecordings(storageDevice.getStorageDevice(&sandbox));
    BOOST_CHECK_EQUAL(size_t(1u), result.size());
    BOOST_CHECK_EQUAL(std::string("test"), result.front()->id);
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testSearchDeviceReturnsNullPointer)
{
    MockStorageDeviceBase storageDevice;
    TestSandbox sandbox;
    BOOST_TEST_INFO("This test should throw exception");
    BOOST_CHECK_THROW(sandbox.runFindRecordings(storageDevice.getStorageDevice(&sandbox)), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(testSearchDeviceReturnsTimeOut)
{
    class RecordingsSource : public MockRecordingsSource
    {
    public:
        RecordingsSource() :
            m_cancelCalled(false),
            m_handler(0)
        {}

        ~RecordingsSource()
        {
            BOOST_CHECK_MESSAGE(m_cancelCalled, "CancelFindRecordings was not called");
        }

        void CancelFindRecordings() final override
        {
            m_cancelCalled = true;
            m_handler->Finished(this, ITV8::EOperationCancelled);
        }

        virtual void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler)
        {
            m_handler = handler;
        }

    private:
        bool m_cancelCalled;
        ITV8::GDRV::IRecordingSearchHandler* m_handler;
    };

    MockStorageDevice<RecordingsSource> storageDevice;
    TestSandbox sandbox;
    BOOST_TEST_INFO("This test should throw exception");
    BOOST_CHECK_THROW(sandbox.runFindRecordings(storageDevice.getStorageDevice(&sandbox)), std::runtime_error);
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testRangeFoundWithIncorrectId)
{
    class RecordingsSource : public MockRecordingsSource
    {
    public:
        void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler) final override
        {
            ITV8::Utility::RecordingRange range("incorrect", ITV8::GDRV::Storage::ERecordStatusRecording);
            ITV8::GDRV::DateTimeRange dateTimeRange = { ITV8::timestamp_t(), ITV8::Utility::timeStamp() };
            range.tracksRange.emplace_back("video", dateTimeRange);

            handler->RangeFound(this, range);
            handler->Finished(this, ITV8::ENotError);
        }
    };

    MockStorageDevice<RecordingsSource> storageDevice;
    TestSandbox sandbox;
    RecordingSearch::rangeList_t result =
        sandbox.runFindRecordings(storageDevice.getStorageDevice(&sandbox));
    BOOST_CHECK_EQUAL(size_t(0u), result.size());
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testCalendarSearchSuccess)
{
    class CalendarSource : public MockCalendarSource
    {
    public:
        void doGetCalendar(ITV8::GDRV::ICalendarHandler* handler) final override
        {
            const auto date = ITV8::Utility::timeStamp();
            handler->DateFound(this, date);
            handler->Finished(this, ITV8::ENotError);
        }
    };

    MockStorageDevice<CalendarSource> storageDevice;
    TestSandbox sandbox;
    auto result = sandbox.runGetCalendar(storageDevice.getStorageDevice(&sandbox));
    BOOST_CHECK_EQUAL(size_t(1u), result.size());
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testCalendarSearchFails)
{
    class CalendarSource : public MockCalendarSource
    {
    public:
        void doGetCalendar(ITV8::GDRV::ICalendarHandler* handler) final override
        {
            handler->Finished(this, ITV8::EGeneralError);
        }
    };

    MockStorageDevice<CalendarSource> storageDevice;
    TestSandbox sandbox;
    try
    {
        sandbox.runGetCalendar(storageDevice.getStorageDevice(&sandbox));
        BOOST_FAIL("should throw an exception");
    }
    catch (const std::runtime_error&)
    {
    }
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testCalendarSearchTimeout)
{
    class CalendarSource : public MockCalendarSource
    {
    public:
        CalendarSource() :
            m_cancelCalled(false),
            m_handler(0)
        {
        }

        ~CalendarSource()
        {
            BOOST_CHECK_MESSAGE(m_cancelCalled, "CancelFindRecordings was not called");
        }

        void CancelCalendar() final override
        {
            m_cancelCalled = true;
            m_handler->Finished(this, ITV8::EOperationCancelled);
        }

        void doGetCalendar(ITV8::GDRV::ICalendarHandler* handler) final override
        {
            m_handler = handler;
        }

    private:
        bool m_cancelCalled;
        ITV8::GDRV::ICalendarHandler* m_handler;
    };

    MockStorageDevice<CalendarSource> storageDevice;
    TestSandbox sandbox;
    BOOST_TEST_INFO("This test should throw exception");
    BOOST_CHECK_THROW(sandbox.runGetCalendar(storageDevice.getStorageDevice(&sandbox)), std::runtime_error);
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testCalendarSearchMixed)
{
    class CalendarSource : public MockRecordingsSource
    {
    public:
        void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler) final override
        {
            ITV8::Utility::RecordingRange range("test", ITV8::GDRV::Storage::ERecordStatusRecording);
            ITV8::GDRV::DateTimeRange dateTimeRange = { ITV8::timestamp_t(), ITV8::Utility::timeStamp() };
            range.tracksRange.emplace_back("video", dateTimeRange);

            handler->RangeFound(this, range);
            handler->Finished(this, ITV8::ENotError);
        }
    };

    MockStorageDevice<CalendarSource> storageDevice;
    TestSandbox sandbox;
    auto result = sandbox.runGetCalendar(storageDevice.getStorageDevice(&sandbox));
    BOOST_CHECK_EQUAL(size_t(2u), result.size());
    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testCalendarSearchInternalStop)
{
    class CalendarSource : public MockCalendarSource
    {
    public:
        CalendarSource() :
            m_cancelCalled(false),
            m_handler(0)
        {
        }

        ~CalendarSource()
        {
            BOOST_CHECK_MESSAGE(m_cancelCalled, "CancelFindRecordings was not called");
        }

        void CancelCalendar() final override
        {
            m_cancelCalled = true;
            m_handler->Finished(this, ITV8::EOperationCancelled);
        }

        void doGetCalendar(ITV8::GDRV::ICalendarHandler* handler) final override
        {
            m_handler = handler;
        }

    private:
        bool m_cancelCalled;
        ITV8::GDRV::ICalendarHandler* m_handler;
    };

    MockStorageDevice<CalendarSource> storageDevice;
    TestSandbox sandbox;
    std::thread searchThread(
        [&]()
        {
            BOOST_CHECK_THROW(sandbox.runGetCalendar(storageDevice.getStorageDevice(&sandbox)),
                std::runtime_error);
        });

    std::thread stopThread([&]() { sandbox.stop(); });

    stopThread.join();
    searchThread.join();

    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_CASE(testRecordingSearchInternalStop)
{
    class RecordingsSource : public MockRecordingsSource
    {
    public:
        RecordingsSource() :
            m_cancelCalled(false),
            m_handler(0)
        {}

        ~RecordingsSource()
        {
            BOOST_CHECK_MESSAGE(m_cancelCalled, "CancelFindRecordings was not called");
        }

        void CancelFindRecordings() final override
        {
            m_cancelCalled = true;
            m_handler->Finished(this, ITV8::EOperationCancelled);
        }

        virtual void doFindRecordings(ITV8::GDRV::IRecordingSearchHandler* handler)
        {
            m_handler = handler;
        }

    private:
        bool m_cancelCalled;
        ITV8::GDRV::IRecordingSearchHandler* m_handler;
    };

    MockStorageDevice<RecordingsSource> storageDevice;
    TestSandbox sandbox;
    std::thread searchThread(
        [&]()
        {
            BOOST_CHECK_THROW(sandbox.runFindRecordings(storageDevice.getStorageDevice(&sandbox)),
                std::runtime_error);
        });

    std::thread stopThread([&]() { sandbox.stop(); });

    stopThread.join();
    searchThread.join();

    BOOST_CHECK_EQUAL(1, storageDevice.m_callsCount);
}

BOOST_AUTO_TEST_SUITE_END()