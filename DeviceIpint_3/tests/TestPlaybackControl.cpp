#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/function.hpp>
#include <boost/optional.hpp>

#include <ItvSdk/include/IErrorService.h>

#include "../PlaybackControl.h"
#include "../PlaybackControl.inl"

using namespace IPINT30;

namespace
{
class TestSandbox
{
public:
    TestSandbox(int underflowThreshold = 10,
        int overflowThreshold = 30) :
        m_error(0),
        m_callCount(0),
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service)),
        m_control(m_service, boost::bind(&TestSandbox::handleError, this, _1),
            underflowThreshold, overflowThreshold)
    {
    }

    ~TestSandbox()
    {
        m_work.reset();
        m_thread.join();
    }

    template<typename TRecordingPlayback>
    void run(TRecordingPlayback& recording, ITV8::timestamp_t time, const std::string& readerName,
        IObjectsGroupHolderSP stateholder)
    {
        m_control.start(time, readerName, &recording, *this, stateholder);
    }

    void handleError(ITV8::hresult_t error)
    {
        testThreadSafe();
        m_error = error;
        ++m_callCount;
    }

    void testThreadSafe()
    {
        BOOST_TEST_INFO("Thread safe guarantee violation! Ipint object or callback accessed from the wrong thread.");
        BOOST_CHECK_EQUAL(m_thread.get_id(), boost::this_thread::get_id());
    }

    void observeFrameQueue(int count)
    {
        BOOST_CHECK_MESSAGE(m_observer, "Observer should not be null");
        m_observer(count);
    }

public:
    typedef boost::function<void (int)> observer_t;
    void atachObserver(observer_t observer)
    {
        m_observer = observer;
    }

public:
    ITV8::hresult_t m_error;
    int m_callCount;


private:
    typedef boost::asio::io_service::work work_t;
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;

public:
    PlaybackControl m_control;
    observer_t m_observer;
};

struct MockPlayabackBase : boost::noncopyable
{
    explicit MockPlayabackBase(TestSandbox& sandbox) :
        m_playCallCount(0),
        m_pauseCallCount(0),
        m_seekCallCount(0),
        m_teardownCallCount(0),
        m_sandbox(sandbox)
    {}

    typedef boost::function<void (ITV8::hresult_t)> errorCallback_t;
    void play(errorCallback_t callback)
    {
        m_sandbox.testThreadSafe();
        ++m_playCallCount;
        doBasePlay(callback);
    }
    void pause(errorCallback_t callback)
    {
        m_sandbox.testThreadSafe();
        ++m_pauseCallCount;
        doBasePause(callback);
    }
    void seek(ITV8::timestamp_t timestamp, errorCallback_t callback)
    {
        m_sandbox.testThreadSafe();
        ++m_seekCallCount;
        doBaseSeek(timestamp, callback);
    }
    void teardown(errorCallback_t callback)
    {
        m_sandbox.testThreadSafe();
        ++m_teardownCallCount;
        doBaseTeardown(callback);
    }

    int m_playCallCount;
    int m_pauseCallCount;
    int m_seekCallCount;
    int m_teardownCallCount;

private:
     virtual void doBasePlay(errorCallback_t callback) = 0;
     virtual void doBasePause(errorCallback_t callback)  = 0;
     virtual void doBaseSeek(ITV8::timestamp_t timestamp,
         errorCallback_t callback)  = 0;
    virtual void doBaseTeardown(errorCallback_t callback)  = 0;
    TestSandbox& m_sandbox;
};

struct MockPlaybackSync : public MockPlayabackBase
{
    MockPlaybackSync(TestSandbox& sandbox) : MockPlayabackBase(sandbox)
    {}

private:
    virtual void doBasePlay(errorCallback_t callback) { doPlay(callback);}
    virtual void doBasePause(errorCallback_t callback){    doPause(callback);}
    virtual void doBaseSeek(ITV8::timestamp_t timestamp,
        errorCallback_t callback) { doSeek(timestamp, callback);}
    virtual void doBaseTeardown(errorCallback_t callback) { doTeardown(callback);}

    virtual void doPlay(errorCallback_t callback) {};
    virtual void doPause(errorCallback_t callback){};
    virtual void doSeek(ITV8::timestamp_t timestamp,
        errorCallback_t callback) {};
    virtual void doTeardown(errorCallback_t callback)
    {
        callback(ITV8::ENotError);
    };
};

struct MockPlaybackAsync : public MockPlayabackBase
{
    MockPlaybackAsync(TestSandbox& sandbox) :
        MockPlayabackBase(sandbox),
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service))
    {}

    ~MockPlaybackAsync()
    {
        m_work.reset();
        m_thread.join();
    }

private:
    virtual void doBasePlay(errorCallback_t callback)
    {
        m_service.post(boost::bind(
            &MockPlaybackAsync::doPlay, this, callback));
    }

    virtual void doBasePause(errorCallback_t callback)
    {
        m_service.post(boost::bind(
            &MockPlaybackAsync::doPause, this, callback));
    }

    virtual void doBaseSeek(ITV8::timestamp_t timestamp,
        errorCallback_t callback)
    {
        m_service.post(boost::bind(
            &MockPlaybackAsync::doSeek, this, timestamp, callback));
    }

    virtual void doBaseTeardown(errorCallback_t callback)
    {
        m_service.post(boost::bind(
            &MockPlaybackAsync::doTeardown, this, callback));
    }

    virtual void doPlay(errorCallback_t callback) {};
    virtual void doPause(errorCallback_t callback){};
    virtual void doSeek(ITV8::timestamp_t timestamp,
        errorCallback_t callback) {};
    virtual void doTeardown(errorCallback_t callback)
    {
        callback(ITV8::ENotError);
    };

private:
    typedef boost::asio::io_service::work work_t;
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;
};

struct PlaybackStateHolder : public IObjectsGroupHolder
{
public:
    typedef boost::function<void()> callback_t;
    explicit PlaybackStateHolder(callback_t callback) :
        m_callback(callback)
    {
    }
    ~PlaybackStateHolder()
    {
        m_callback();
    }
    callback_t m_callback;
};

class OperationCompletition
{
public:
    OperationCompletition() :
        m_finished(false)
    {}

    typedef boost::shared_ptr<PlaybackStateHolder> PlaybackStateHolderSP;

    PlaybackStateHolderSP getHolder()
    {
        return boost::make_shared<PlaybackStateHolder>(
            boost::bind(&OperationCompletition::handleOperationsFinished, this));
    }

    void waitComplete()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        const bool completed = m_completeCondition.timed_wait(lock,
            boost::posix_time::seconds(205),
            boost::bind(&OperationCompletition::m_finished, this) == true);

        BOOST_TEST_INFO("Complete time is out");
        BOOST_CHECK_EQUAL(completed, true);
    }

    void timedWaitComplete()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        const bool completed = m_completeCondition.timed_wait(lock,
            boost::posix_time::millisec(300),
            boost::bind(&OperationCompletition::m_finished, this) == true);
        BOOST_TEST_INFO("Playback control should hold state object in this condition");
        BOOST_CHECK_EQUAL(completed, false);
    }

private:
    void handleOperationsFinished()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_finished = true;
        m_completeCondition.notify_all();
    }

private:
    boost::condition_variable_any m_completeCondition;
    boost::mutex m_mutex;
    bool m_finished;
};

}

template <typename TPlaybackBase>
void seekReturnsError()
{
    TestSandbox sandbox;
    class MockPlayback : public TPlaybackBase
    {
    public:
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::EInternalError);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());



    synch.waitComplete();

    BOOST_TEST_INFO("Handler should be called with error");
    BOOST_CHECK_EQUAL(static_cast<ITV8::hresult_t>(ITV8::EInternalError), sandbox.m_error);

    BOOST_TEST_INFO("Handler should be called single time");
    BOOST_CHECK_EQUAL(1, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);
    BOOST_CHECK_EQUAL(0, playback.m_playCallCount);
    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void playReturnsError()
{
    TestSandbox sandbox;
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}
        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::EUnknownEncoding);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.waitComplete();

    BOOST_TEST_INFO("Handler should be called with error");
    BOOST_CHECK_EQUAL(static_cast<ITV8::hresult_t>(ITV8::EUnknownEncoding), sandbox.m_error);

    BOOST_TEST_INFO("Handler should be called single time");
    BOOST_CHECK_EQUAL(1, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_TEST_INFO("Play should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_playCallCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);
    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void stateMachineHoldsStateObject()
{
    TestSandbox sandbox;
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.timedWaitComplete();

    BOOST_TEST_INFO("Handler should not be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_TEST_INFO("Play should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_playCallCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);
    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(0, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void testPauseWhenQueyeOtherflow()
{
    TestSandbox sandbox(10, 20);
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPause(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.timedWaitComplete();

    // Test for small queue sizes
    sandbox.observeFrameQueue(0);
    sandbox.observeFrameQueue(9);
    sandbox.observeFrameQueue(15);
    sandbox.observeFrameQueue(19);
    synch.timedWaitComplete();
    BOOST_TEST_INFO("Pause should not be called");
    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);

    // Test for overflow
    sandbox.observeFrameQueue(21);
    synch.timedWaitComplete();
    BOOST_TEST_INFO("Pause should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_pauseCallCount);

    // Test several overflow
    sandbox.observeFrameQueue(40);
    sandbox.observeFrameQueue(50);
    sandbox.m_control.terminate();
    synch.waitComplete();
    BOOST_TEST_INFO("Handler should not be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_TEST_INFO("Play should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_playCallCount);

    BOOST_CHECK_EQUAL(1, playback.m_pauseCallCount);
    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void testPlayWhenQueyeUnderflow()
{
    TestSandbox sandbox(10, 20);
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPause(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.timedWaitComplete();

    // Make overflow
    sandbox.observeFrameQueue(21);
    synch.timedWaitComplete();

    BOOST_TEST_INFO("Pause should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Play should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_playCallCount);

    // Make underflow
    sandbox.observeFrameQueue(9);
    synch.timedWaitComplete();
    BOOST_TEST_INFO("Pause should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Play should be called twice");
    BOOST_CHECK_EQUAL(2, playback.m_playCallCount);


    // Generate several underflow
    sandbox.observeFrameQueue(5);
    sandbox.observeFrameQueue(0);
    sandbox.m_control.terminate();
    synch.waitComplete();

    BOOST_TEST_INFO("Handler should not be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_TEST_INFO("Play should be called single time");
    BOOST_CHECK_EQUAL(2, playback.m_playCallCount);

    BOOST_CHECK_EQUAL(1, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void testTerminatedNoWaitForHandler()
{
    TestSandbox sandbox(10, 20);
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.timedWaitComplete();
    sandbox.m_control.terminate();
    synch.waitComplete();

    BOOST_TEST_INFO("Handler shouldn't be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_TEST_INFO("Play should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_playCallCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void startThenterminatedImmediately()
{
    TestSandbox sandbox(10, 20);
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    sandbox.m_control.terminate();
    synch.waitComplete();

    BOOST_TEST_INFO("Handler shouldn't be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlayback>
void testTerminatedWaitForHandler(int seekCallCount, int playCallCount = 0, int pauseCallCaunt = 0,
                                  int queueCount = 0)
{
    TestSandbox sandbox(10, 20);
    TPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.timedWaitComplete();
    if (queueCount)
    {
        sandbox.observeFrameQueue(queueCount);
        synch.timedWaitComplete();
    }
    sandbox.m_control.terminate();
    synch.timedWaitComplete();
    BOOST_TEST_INFO("Seek should be called");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    playback.m_callback(ITV8::ENotError);
    synch.waitComplete();

    BOOST_TEST_INFO("Handler shouldn't be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(seekCallCount, playback.m_seekCallCount);

    BOOST_TEST_INFO("Play should not be called");
    BOOST_CHECK_EQUAL(playCallCount, playback.m_playCallCount);

    BOOST_CHECK_EQUAL(pauseCallCaunt, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void testSeekSentThenTerminated()
{
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            m_callback = callback;
        }

        errorCallback_t m_callback;
    };
    testTerminatedWaitForHandler<MockPlayback>(1);
}

template <typename TPlaybackBase>
void testPlaySentThenTerminated()
{
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            m_callback = callback;
        }

        errorCallback_t m_callback;
    };
    testTerminatedWaitForHandler<MockPlayback>(1, 1);
}

template <typename TPlaybackBase>
void testPauseSentThenTerminated()
{
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPause(errorCallback_t callback)
        {
            m_callback = callback;
        }

        errorCallback_t m_callback;
    };
    const int QUEUE_OVERFLOW_TRIGGER = 1000;
    testTerminatedWaitForHandler<MockPlayback>(1, 1, 1, QUEUE_OVERFLOW_TRIGGER);
}

namespace TestPlaybackControl
{

void seekReturnsErrorSync()
{
    seekReturnsError<MockPlaybackSync>();
}

void playReturnsErrorSync()
{
    playReturnsError<MockPlaybackSync>();
}

void stateMachineHoldsStateObjectSync()
{
    stateMachineHoldsStateObject<MockPlaybackSync>();
}

void testPauseWhenQueueOtherflowSync()
{
    testPauseWhenQueyeOtherflow<MockPlaybackSync>();
}

void testPlayWhenQueueUnderflowSync()
{
    testPlayWhenQueyeUnderflow<MockPlaybackSync>();
}

void testTerminatedNoWaitForHandlerSync()
{
    testTerminatedNoWaitForHandler<MockPlaybackSync>();
}

void testSeekSentThenTerminatedSync()
{
    testSeekSentThenTerminated<MockPlaybackSync>();
}

void testPlaySentThenTerminatedSync()
{
    testPlaySentThenTerminated<MockPlaybackSync>();
}

void testPauseSentThenTerminatedSync()
{
    testPauseSentThenTerminated<MockPlaybackSync>();
}

void seekReturnsErrorAync()
{
    seekReturnsError<MockPlaybackAsync>();
}

void playReturnsErrorAync()
{
    playReturnsError<MockPlaybackAsync>();
}

void stateMachineHoldsStateObjectAsync()
{
    stateMachineHoldsStateObject<MockPlaybackAsync>();
}

void testPauseWhenQueueOtherflowAsync()
{
    testPauseWhenQueyeOtherflow<MockPlaybackAsync>();
}

void testPlayWhenQueueUnderflowAsync()
{
    testPlayWhenQueyeUnderflow<MockPlaybackAsync>();
}

void testTerminatedNoWaitForHandlerAsync()
{
    testTerminatedNoWaitForHandler<MockPlaybackAsync>();
}

void testSeekSentThenTerminatedAsync()
{
    testSeekSentThenTerminated<MockPlaybackAsync>();
}

void testPlaySentThenTerminatedAsync()
{
    testPlaySentThenTerminated<MockPlaybackAsync>();
}

void testPauseSentThenTerminatedAsync()
{
    testPauseSentThenTerminated<MockPlaybackAsync>();
}

void startThenterminatedImmediatelySync()
{
    startThenterminatedImmediately<MockPlaybackSync>();
}

void startThenterminatedImmediatelyAsync()
{
    startThenterminatedImmediately<MockPlaybackAsync>();
}

template <typename TPlaybackBase>
void testWaitsTerminatedAfterError()
{
    TestSandbox sandbox;
    class MockPlayback : public TPlaybackBase
    {
    public:
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::EInternalError);
        }

        virtual void doTeardown(errorCallback_t callback)
        {
            m_callback = callback;
        };

        errorCallback_t m_callback;
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    synch.timedWaitComplete();

    playback.m_callback(ITV8::ENotError);
    synch.waitComplete();

    BOOST_TEST_INFO("Handler should be called with error");
    BOOST_CHECK_EQUAL(static_cast<ITV8::hresult_t>(ITV8::EInternalError), sandbox.m_error);

    BOOST_TEST_INFO("Handler should be called single time");
    BOOST_CHECK_EQUAL(1, sandbox.m_callCount);

    BOOST_TEST_INFO("Seek should be called single time");
    BOOST_CHECK_EQUAL(1, playback.m_seekCallCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);
    BOOST_CHECK_EQUAL(0, playback.m_playCallCount);

    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}

template <typename TPlaybackBase>
void testWaitsTerminatedAfterUserCancel()
{
    TestSandbox sandbox(10, 20);
    struct MockPlayback : public TPlaybackBase
    {
        MockPlayback(TestSandbox& sandbox) : TPlaybackBase(sandbox) {}

        void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }
        void doPlay(errorCallback_t callback)
        {
            callback(ITV8::ENotError);
        }

        virtual void doTeardown(errorCallback_t callback)
        {
            m_callback = callback;
        };

        errorCallback_t m_callback;
    };

    MockPlayback playback(sandbox);
    OperationCompletition synch;
    sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
    sandbox.m_control.terminate();
    synch.timedWaitComplete();

    playback.m_callback(ITV8::ENotError);
    synch.waitComplete();

    BOOST_TEST_INFO("Handler shouldn't be called");
    BOOST_CHECK_EQUAL(0, sandbox.m_callCount);

    BOOST_CHECK_EQUAL(0, playback.m_pauseCallCount);

    BOOST_TEST_INFO("Terminate should be colled single time");
    BOOST_CHECK_EQUAL(1, playback.m_teardownCallCount);
}


void testWaitsTerminatedAfterErrorSync()
{
    testWaitsTerminatedAfterError<MockPlaybackSync>();
}

void testWaitsTerminatedAfterErrorAsync()
{
    testWaitsTerminatedAfterError<MockPlaybackAsync>();
}

void testWaitsTerminatedAfterUserCancelSync()
{
    testWaitsTerminatedAfterUserCancel<MockPlaybackSync>();
}

void testWaitsTerminatedAfterUserCancelAsync()
{
    testWaitsTerminatedAfterUserCancel<MockPlaybackAsync>();
}

void testCallingObserverAfterPlaybackControlDestroyIsSafe()
{
    TestSandbox::observer_t observer;
    {
        TestSandbox sandbox;
        class MockPlayback : public MockPlaybackSync
        {
        public:
            MockPlayback(TestSandbox& sandbox) : MockPlaybackSync(sandbox) {}

            void doSeek(ITV8::timestamp_t timestamp, errorCallback_t callback)
            {
                callback(ITV8::EInternalError);
            }
        };
        MockPlayback playback(sandbox);
        OperationCompletition synch;
        sandbox.run(playback, 0, "MockArchiveReader-1", synch.getHolder());
        synch.waitComplete();
        observer = sandbox.m_observer;
    }
    // Test multiple time to cause problems
    for (int i = 0; i < 1000; ++i)
    {
        observer(i);
    }
}

} // TestPlaybackControl

BOOST_AUTO_TEST_SUITE(TestPlaybackControl)

BOOST_AUTO_TEST_CASE(seekReturnsErrorSync)
{
    TestPlaybackControl::seekReturnsErrorSync();
}

BOOST_AUTO_TEST_CASE(playReturnsErrorSync)
{
    TestPlaybackControl::playReturnsErrorSync();
}

BOOST_AUTO_TEST_CASE(stateMachineHoldsStateObjectSync)
{
    TestPlaybackControl::stateMachineHoldsStateObjectSync();
}

BOOST_AUTO_TEST_CASE(testPauseWhenQueueOtherflowSync)
{
    TestPlaybackControl::testPauseWhenQueueOtherflowSync();
}

BOOST_AUTO_TEST_CASE(testPlayWhenQueueUnderflowSync)
{
    TestPlaybackControl::testPlayWhenQueueUnderflowSync();
}

BOOST_AUTO_TEST_CASE(testTerminatedNoWaitForHandlerSync)
{
    TestPlaybackControl::testTerminatedNoWaitForHandlerSync();
}

BOOST_AUTO_TEST_CASE(testSeekSentThenTerminatedSync)
{
    TestPlaybackControl::testSeekSentThenTerminatedSync();
}

BOOST_AUTO_TEST_CASE(testPlaySentThenTerminatedSync)
{
    TestPlaybackControl::testPlaySentThenTerminatedSync();
}

BOOST_AUTO_TEST_CASE(testPauseSentThenTerminatedSync)
{
    TestPlaybackControl::testPauseSentThenTerminatedSync();
}

BOOST_AUTO_TEST_CASE(startThenterminatedImmediatelySync)
{
    TestPlaybackControl::startThenterminatedImmediatelySync();
}

BOOST_AUTO_TEST_CASE(testWaitsTerminatedAfterErrorSync)
{
    TestPlaybackControl::testWaitsTerminatedAfterErrorSync();
}

BOOST_AUTO_TEST_CASE(testWaitsTerminatedAfterUserCancelSync)
{
    TestPlaybackControl::testWaitsTerminatedAfterUserCancelSync();
}

BOOST_AUTO_TEST_CASE(seekReturnsErrorAync)
{
    TestPlaybackControl::seekReturnsErrorAync();
}

BOOST_AUTO_TEST_CASE(playReturnsErrorAync)
{
    TestPlaybackControl::playReturnsErrorAync();
}

BOOST_AUTO_TEST_CASE(stateMachineHoldsStateObjectAsync)
{
    TestPlaybackControl::stateMachineHoldsStateObjectAsync();
}

BOOST_AUTO_TEST_CASE(testPauseWhenQueueOtherflowAsync)
{
    TestPlaybackControl::testPauseWhenQueueOtherflowAsync();
}

BOOST_AUTO_TEST_CASE(testPlayWhenQueueUnderflowAsync)
{
    TestPlaybackControl::testPlayWhenQueueUnderflowAsync();
}

BOOST_AUTO_TEST_CASE(testTerminatedNoWaitForHandlerAsync)
{
    TestPlaybackControl::testTerminatedNoWaitForHandlerAsync();
}

BOOST_AUTO_TEST_CASE(testSeekSentThenTerminatedAsync)
{
    TestPlaybackControl::testSeekSentThenTerminatedAsync();
}

BOOST_AUTO_TEST_CASE(testPlaySentThenTerminatedAsync)
{
    TestPlaybackControl::testPlaySentThenTerminatedAsync();
}

BOOST_AUTO_TEST_CASE(testPauseSentThenTerminatedAsync)
{
    TestPlaybackControl::testPauseSentThenTerminatedAsync();
}

BOOST_AUTO_TEST_CASE(startThenterminatedImmediatelyAsync)
{
    TestPlaybackControl::startThenterminatedImmediatelyAsync();
}

BOOST_AUTO_TEST_CASE(testWaitsTerminatedAfterErrorAsync)
{
    TestPlaybackControl::testWaitsTerminatedAfterErrorAsync();
}

BOOST_AUTO_TEST_CASE(testWaitsTerminatedAfterUserCancelAsync)
{
    TestPlaybackControl::testWaitsTerminatedAfterUserCancelAsync();
}

BOOST_AUTO_TEST_CASE(testCallingObserverAfterPlaybackControlDestroyIsSafe)
{
    TestPlaybackControl::testCallingObserverAfterPlaybackControlDestroyIsSafe();
}

BOOST_AUTO_TEST_SUITE_END() // TestPlaybackControl