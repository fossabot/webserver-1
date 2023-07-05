#include <boost/thread/condition_variable.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>

#include <boost/test/unit_test.hpp>

#include "../PullToPushAdapter.h"

using namespace IPINT30;

namespace
{

DECLARE_LOGGER_ARG;
class TestSandbox
{
public:
    explicit TestSandbox(DECLARE_LOGGER_ARG, int maxQueueSize = 1000) :
        m_queueSize(-1),
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service))
    {
        m_operationHolder = m_completion.getHolder();
        m_adapter = boost::make_shared<PushToPullAdapter<int>>
            ([](int frame){ return true; },
             boost::bind(&TestSandbox::handleFrame, this, _1),
             boost::ref(m_service),
             m_operationHolder,
             GET_LOGGER_PTR,
             maxQueueSize);
        m_adapter->atachObserver(boost::bind(&TestSandbox::stateObserver, this, _1));
    }

    ~TestSandbox()
    {
        m_operationHolder.reset();
        m_adapter.reset();
        m_completion.waitComplete();
        m_work.reset();
        m_thread.join();
    }

    void push(int frame)
    {
        m_adapter->receiveFrame(frame);
        sync();
    }

    void request(int count)
    {
        m_adapter->request(count);
        sync();
    }

    void detach()
    {
        m_adapter->detach();
    }

private:
    void sync()
    {
        struct NullHandler
        {
            void operator() () {}
        };
        boost::packaged_task<void> syncronizationTask((NullHandler()));
        boost::unique_future<void> future = syncronizationTask.get_future();
        m_service.post(boost::bind(&boost::packaged_task<void>::operator(),
            &syncronizationTask));
        future.wait();
    }

public:
    std::vector<int> m_frames;
    int m_queueSize;

private:
    void handleFrame(int frame)
    {
        m_frames.push_back(frame);
    }

    void stateObserver(int size)
    {
        m_queueSize = size;
    }

private:
    typedef boost::asio::io_service::work work_t;
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;

    OperationCompletion m_completion;
    IObjectsGroupHolderSP m_operationHolder;
    boost::shared_ptr<PushToPullAdapter<int>> m_adapter;
};
}

BOOST_AUTO_TEST_SUITE(TestPullToPushStyleAdapter)

BOOST_AUTO_TEST_CASE(testPushThenRequest)
{
    TestSandbox sandbox(GET_LOGGER_PTR);
    sandbox.push(1);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 1);

    sandbox.push(2);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 2);

    sandbox.push(3);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 3);

    sandbox.request(1);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(1u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 1);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 2);

    sandbox.request(2);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(3u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 3);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);
}

BOOST_AUTO_TEST_CASE(testRequestThenPush)
{
    TestSandbox sandbox(GET_LOGGER_PTR);

    sandbox.request(1);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);

    sandbox.push(1);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(1u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 1);

    sandbox.request(2);
    sandbox.push(2);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(2u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 2);

    sandbox.push(3);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(3u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 3);
}

BOOST_AUTO_TEST_CASE(testDetachThenPush)
{
    TestSandbox sandbox(GET_LOGGER_PTR);

    sandbox.request(1);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);

    sandbox.push(1);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(1u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 1);

    sandbox.request(2);
    sandbox.push(2);
    BOOST_CHECK_EQUAL(sandbox.m_queueSize, 0);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(2u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 2);

    // After detach adapter should not call the handler.
    sandbox.detach();
    sandbox.push(3);
    BOOST_CHECK_EQUAL(sandbox.m_frames.size(), size_t(2u));
    BOOST_CHECK_EQUAL(sandbox.m_frames.back(), 2);
}

BOOST_AUTO_TEST_CASE(testExceedQueueSizeLimit)
{
    // Set queue limit to 2.
    TestSandbox sandbox(GET_LOGGER_PTR, 2);

    sandbox.push(1);
    sandbox.push(2);
    BOOST_CHECK_EQUAL(2, sandbox.m_queueSize);

    sandbox.push(3);
    sandbox.push(4);
    BOOST_CHECK_EQUAL(2, sandbox.m_queueSize);
}

BOOST_AUTO_TEST_SUITE_END() // TestPullToPushStyleAdapter