#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/optional.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/mpl/vector.hpp>

#include <ItvSdk/include/IErrorService.h>

#include "../RecordingsInfoRequester.h"

using namespace IPINT30;
typedef boost::asio::io_service::work work_t;

namespace
{
class Hell {};

class StartedStateHolder
{
public:
    typedef boost::function<void ()> onDestroy_t;
    explicit StartedStateHolder(onDestroy_t ondestroy, bool& raiseAhell) :
        m_raiseAhell(raiseAhell),
        m_onDestroy(ondestroy)
    {
    }

    ~StartedStateHolder() BOOST_NOEXCEPT_IF(false)
    {
      if (m_raiseAhell)
      {
          // It is kind a assert.
          throw Hell();
      }
      m_onDestroy();
    }


private:
    bool& m_raiseAhell;
    onDestroy_t m_onDestroy;
};

class TestSandbox
{
public:
    TestSandbox() :
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service)),
        m_dynExec(NExecutors::CreateDynamicThreadPool(NLogging::CreateLogger(), "DeviceIpint", NExecutors::IDynamicThreadPool::UNLIMITED_QUEUE_LENGTH, 2, 1024)),
        m_hanlderCalled(false),
        m_contextDestroyed(false),
        m_raiseAhell(true)
    {
    }

    ~TestSandbox()
    {
        {
            boost::mutex::scoped_lock lock(m_contextDestroyedGuard);
            m_contextDestroyedCondition.wait(lock, [this]() { return m_contextDestroyed; });
        }
        m_work.reset();
        m_thread.join();
        m_dynExec->Shutdown();
    }

    void waitHandlerCalled()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_handlerCalledCondition.wait(lock,
            boost::bind(std::equal_to<bool>(), true, boost::ref(m_hanlderCalled)));
    }

    void run(ITV8::GDRV::IStorageDevice* mockDevice)
    {
        StartedStateHolderSP stateHolder = boost::make_shared<StartedStateHolder>(
            boost::bind(&TestSandbox::onContextDestroyed, this), m_raiseAhell);
        m_stateHolderWeak = stateHolder;

        NLogging::ILogger* logger = nullptr;
        m_requester.reset(new RecordingsInfoRequester(logger, mockDevice, 0,
            boost::bind(&TestSandbox::handleRecordingsInfo, this, _1, _2),
            stateHolder, m_service, m_dynExec,
            boost::posix_time::millisec(100)));
    }

    void cancel()
    {
        if (StartedStateHolderSP holder = m_stateHolderWeak.lock())
        {
            m_raiseAhell = false;
        }
        m_requester->cancel();
    }

public:
    ITV8::Utility::recordingInfoList m_recordingsInfo;

private:
    void handleRecordingsInfo(const ITV8::Utility::recordingInfoList& recordingsInfo,
        StartedStateHolderSP startedState)
    {
        m_raiseAhell = false;
        m_recordingsInfo = recordingsInfo;
        boost::mutex::scoped_lock lock(m_mutex);
        m_hanlderCalled = true;
        m_handlerCalledCondition.notify_one();
    }

    void onContextDestroyed()
    {
        boost::mutex::scoped_lock lock(m_contextDestroyedGuard);
        m_contextDestroyed = true;
        m_contextDestroyedCondition.notify_one();
    }

private:
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;
    NExecutors::PDynamicThreadPool m_dynExec;

    bool m_hanlderCalled;
    boost::condition_variable m_handlerCalledCondition;
    boost::mutex m_mutex;
    boost::weak_ptr<void> m_stateHolderWeak;
    boost::scoped_ptr<RecordingsInfoRequester> m_requester;

    boost::condition_variable m_contextDestroyedCondition;
    bool m_contextDestroyed;
    boost::mutex m_contextDestroyedGuard;
    bool m_raiseAhell;
};

class MockStorageDevice : protected ITV8::GDRV::IStorageDevice
{
public:
    MockStorageDevice() :
        m_callsCount(0),
        m_sandbox(0)
    {}

    void joinThread()
    {}

    virtual void RequestRecordingsInfo(ITV8::GDRV::IRecordingsInfoHandler* handler)
    {
        m_callsCount++;
        doHandleRequest(handler);
    }

    virtual void doHandleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler) = 0;

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

protected:
    TestSandbox* m_sandbox;
};

class MockStorageDeviceSync :  public MockStorageDevice
{
public:
      virtual void doHandleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler)
      {
        handleRequest(handler);
      }
      virtual void handleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler) = 0;
};

class MockStorageDeviceASync : public MockStorageDevice
{
public:
    MockStorageDeviceASync():
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service)),
        m_timer(m_service)
     {}

    ~MockStorageDeviceASync()
    {
    }

    void joinThread()
    {
        m_work.reset();
        m_thread.join();
    }

      virtual void doHandleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler)
      {
          m_timer.expires_from_now(boost::posix_time::milliseconds(100));
          m_timer.async_wait(boost::bind(&MockStorageDeviceASync::doHandle, this,
              _1, handler));
      }

      void doHandle(const boost::system::error_code& error,
          ITV8::GDRV::IRecordingsInfoHandler* handler)
      {
            if (error)
            {
                return;
            }
            handleRequest(handler);
      }

      virtual void handleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler) = 0;

private:
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;
    boost::asio::deadline_timer m_timer;
};

}

BOOST_AUTO_TEST_SUITE(TestRecordingsInfoRequester)

typedef boost::mpl::vector<MockStorageDeviceSync, MockStorageDeviceASync> TMockDeviceType;

BOOST_AUTO_TEST_CASE_TEMPLATE(testSearchFailsPermanently,
                              TMockDeviceBase,
                              TMockDeviceType)
{
    class StorageDevice : public TMockDeviceBase
    {
    public:
        virtual void handleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler)
        {
            handler->Failed(this, ITV8::EInvalidOperation);
        }
    };

    StorageDevice mockDevice;
    {
        TestSandbox sandbox;

        // Run sandbox. With this handler, it is expected work infinitely,
        // (re)trying to obtain recordings info.
        sandbox.run(mockDevice.getStorageDevice(&sandbox));
        // wait while several requests take place
        boost::this_thread::sleep(boost::posix_time::millisec(500));
        // cancel operation
        sandbox.cancel();
        // here sandbox will be destroyed (after closing the scope)
        // it will take some time, while internal threads stop.
    }
    mockDevice.joinThread();
    BOOST_CHECK_MESSAGE(mockDevice.m_callsCount > 1, "Handler should be called several times");
    BOOST_CHECK_MESSAGE(mockDevice.m_callsCount < 20, "Handler should not be called too many times (seems that retry timeout does not work)");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(testSearchSucceed,
                              TMockDeviceBase,
                              TMockDeviceType)
{
    class StorageDevice : public TMockDeviceBase
    {
    public:

        virtual void handleRequest(ITV8::GDRV::IRecordingsInfoHandler* handler)
        {
            using namespace ITV8::Utility;
            RecordingInfoSP info = boost::make_shared<RecordingInfo>();
            info->id = "test";
            recordingInfoList recordings;
            recordings.push_back(info);
            RecordingInfoEnumerator enumerator(recordings);
            handler->RequestRecordingsDone(this, enumerator);
        }
    };

    StorageDevice mockDevice;
    TestSandbox sandbox;

    {
        // Run sandbox. With this handler, it is expected work infinitely,
        // (re)trying to obtain recordings info.
        sandbox.run(mockDevice.getStorageDevice(&sandbox));
        // wait while request take place
        sandbox.waitHandlerCalled();
        // here sandbox will be destroyed (after closing the scope)
        // it will take some time, while internal threads stop.
        BOOST_CHECK_MESSAGE(sandbox.m_recordingsInfo.size() == 1, "Wrong handler result");
        BOOST_CHECK_MESSAGE(sandbox.m_recordingsInfo.front()->GetId() == std::string("test"), "Wrong handler result");
    }
    mockDevice.joinThread();
    BOOST_CHECK_MESSAGE(mockDevice.m_callsCount == 1, "Handler should not be called many times");
}

BOOST_AUTO_TEST_SUITE_END() // TestRecordingsInfoRequester
