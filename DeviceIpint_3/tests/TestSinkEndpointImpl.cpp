#include "../SinkEndpointImpl.h"
#include <CorbaHelpers/RefcountedImpl.h>

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/optional.hpp>
#include <boost/scope_exit.hpp>

#include <boost/test/unit_test.hpp>

using namespace IPINT30;
typedef boost::asio::io_service::work work_t;

namespace
{
const int PRIORITY_DEFAULT = 0;
const int PRIORITY_LOW = 1;
const int PRIORITY_HIGH = 2;

struct MockEndpoint : public NMMSS::ISinkEndpoint,
    public NCorbaHelpers::CRefcountedImpl
{
    MockEndpoint() : 
        m_destroyCallCount(0)
    {}

    virtual void RequestQoS( MMSS::QualityOfService const& qos ) override {}

    virtual void Destroy() override
    {
        ++m_destroyCallCount;
    }

int m_destroyCallCount;

private:
    virtual NCorbaHelpers::IWeakReference* MakeWeakReference()
    {
        return 0;
    }
};

class ConnectionHolderSandbox
{
public:
    ConnectionHolderSandbox() : 
        expected_destroy_count(1),
        m_work(work_t(m_service)),
        m_thread(boost::bind(&boost::asio::io_service::run, &m_service)),
        m_endpoint(new MockEndpoint())
    {
    }

    ~ConnectionHolderSandbox()
    {
        m_work.reset();
        m_thread.join();

        BOOST_CHECK_MESSAGE(1==m_endpoint->GetCounter(), "Enpoint reference leak detected");

        BOOST_CHECK_MESSAGE(expected_destroy_count==m_endpoint->m_destroyCallCount, "Enpoint connection destroy error detected");
    }

    void run()
    {
        m_connectionHolder.reset(new SinkEndpointImpl(m_service));
    }

    SinkEndpointImpl::handle_t create(boost::posix_time::time_duration timeout,
        int priority = PRIORITY_DEFAULT)
    {
        return m_connectionHolder->create(getFactory(), priority, timeout);
    }

    bool isConnected() const
    {
        return m_endpoint->m_destroyCallCount == 0;
    }

    int expected_destroy_count;

public:
    boost::scoped_ptr<SinkEndpointImpl> m_connectionHolder;

private:
    SinkEndpointImpl::connectionfactory_t getFactory()
    {
        return boost::bind(&ISinkEndpointSP::Dup, m_endpoint);
    }

private:
    typedef NCorbaHelpers::CAutoPtr<MockEndpoint> ISinkEndpointSP;
    boost::asio::io_service m_service;
    boost::optional<work_t> m_work;
    boost::thread m_thread;
    ISinkEndpointSP m_endpoint;
};

}

#ifndef BOOST_CHECK_NO_THROW_MESSAGE
#define BOOST_CHECK_NO_THROW_MESSAGE(expr, msg) BOOST_CHECK_NO_THROW( do { expr; (void) msg; } while (false) )
#endif //BOOST_CHECK_NO_THROW_MESSAGE
#ifndef BOOST_CHECK_THROW_MESSAGE
#define BOOST_CHECK_THROW_MESSAGE(expr, msg, ex) BOOST_CHECK_THROW( do { expr; (void) msg; } while (false), ex )
#endif //BOOST_CHECK_NO_THROW_MESSAGE


BOOST_AUTO_TEST_SUITE(ConnectionHolder)

BOOST_AUTO_TEST_CASE(CreateThenDestroy)
{
    ConnectionHolderSandbox sandbox;
    sandbox.run();
    
    const SinkEndpointImpl::handle_t handle = sandbox.create(
        boost::posix_time::seconds(1));
    
    BOOST_CHECK_NO_THROW_MESSAGE( sandbox.m_connectionHolder->destroy(handle), "Destroy should not throw on valid object" );
}

BOOST_AUTO_TEST_CASE(KeepAliveAndDestroyOnInvalidObjects)
{
    ConnectionHolderSandbox sandbox;
    sandbox.run();

    BOOST_CHECK_THROW_MESSAGE(sandbox.m_connectionHolder->keepAlive(0), "Keep alive should throw on invalid object", SinkEndpointImpl::XInvalidOperation);

    BOOST_CHECK_THROW_MESSAGE(sandbox.m_connectionHolder->destroy(0), "Destroy should throw on invalid object", SinkEndpointImpl::XInvalidOperation);    

    const SinkEndpointImpl::handle_t handle = sandbox.create(boost::posix_time::millisec(10));
    
    boost::this_thread::sleep(boost::posix_time::millisec(200));
    
    BOOST_CHECK_THROW_MESSAGE(sandbox.m_connectionHolder->keepAlive(handle), "Keep alive should throw on invalid object", SinkEndpointImpl::XInvalidOperation);

    BOOST_CHECK_THROW_MESSAGE(sandbox.m_connectionHolder->destroy(handle), "Destroy should throw on invalid object", SinkEndpointImpl::XInvalidOperation);
}

BOOST_AUTO_TEST_CASE(KeepingObjectAlive)
{
     ConnectionHolderSandbox sandbox;
     sandbox.run();
     const SinkEndpointImpl::handle_t handle = 
         sandbox.create(boost::posix_time::millisec(300));
     
     for (int i= 1; i < 10; ++i)
     {
         boost::this_thread::sleep(boost::posix_time::millisec(150));
         BOOST_CHECK_NO_THROW_MESSAGE(sandbox.m_connectionHolder->keepAlive(handle), "Keep-alive should not throw exception on valid connection");
         BOOST_CHECK_MESSAGE(sandbox.isConnected(), "Connection should not be destroyed!");
     }

     BOOST_CHECK_NO_THROW_MESSAGE(sandbox.m_connectionHolder->destroy(handle), "Destroy should not throw on valid object");
}

BOOST_AUTO_TEST_CASE(PriorityConnection)
{
    ConnectionHolderSandbox sandbox;
    sandbox.run();
    SinkEndpointImpl::handle_t handle = sandbox.create(boost::posix_time::minutes(100), PRIORITY_LOW);
    BOOST_SCOPE_EXIT(&handle, &sandbox) 
    {
        BOOST_CHECK_NO_THROW_MESSAGE(sandbox.m_connectionHolder->destroy(handle), "Destroy should not throw on valid object");
    } BOOST_SCOPE_EXIT_END

    BOOST_CHECK_THROW_MESSAGE(sandbox.create(boost::posix_time::minutes(100), PRIORITY_LOW), "Create should throw on active connection with the same priority", SinkEndpointImpl::XInvalidOperation);

    BOOST_CHECK_THROW_MESSAGE(sandbox.create(boost::posix_time::minutes(100), PRIORITY_DEFAULT), "Create should throw on active connection with higher priority", SinkEndpointImpl::XInvalidOperation);

    BOOST_CHECK_NO_THROW_MESSAGE(handle = sandbox.create(boost::posix_time::minutes(100), PRIORITY_HIGH), "Create should not throw exception on active connection with lower priority");

    sandbox.expected_destroy_count = 2;
}

BOOST_AUTO_TEST_CASE(IsBusy)
{
    ConnectionHolderSandbox sandbox;
    sandbox.run();
    const SinkEndpointImpl::handle_t handle = 
        sandbox.create(boost::posix_time::seconds(1), PRIORITY_LOW);
    BOOST_CHECK_MESSAGE(sandbox.m_connectionHolder->isBusy(PRIORITY_DEFAULT), "Should be busy");
    BOOST_CHECK_MESSAGE(sandbox.m_connectionHolder->isBusy(PRIORITY_LOW), "Should be busy");

    BOOST_CHECK_MESSAGE(!sandbox.m_connectionHolder->isBusy(PRIORITY_HIGH), "Should not be busy");
    
    BOOST_CHECK_NO_THROW_MESSAGE(sandbox.m_connectionHolder->destroy(handle), "Destroy should not throw on valid object");
    
    BOOST_CHECK_MESSAGE(!sandbox.m_connectionHolder->isBusy(PRIORITY_DEFAULT), "Should not be busy");
}

BOOST_AUTO_TEST_SUITE_END()
