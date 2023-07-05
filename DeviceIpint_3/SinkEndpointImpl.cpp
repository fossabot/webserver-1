#include "SinkEndpointImpl.h"

#include <boost/make_shared.hpp>
#include <boost/nondet_random.hpp>
#include <boost/enable_shared_from_this.hpp>

namespace IPINT30
{

typedef SinkEndpointImpl::handle_t handle_t;
typedef SinkEndpointImpl::XInvalidOperation XInvalidOperation;

namespace detail
{

// Life management notes:
// An instance of this class should be hold only by weak_ptr.
// When clients wish to signal keep-alive event, they should try to obtain
// shared_ptr from weak one and call keepAlive() method.
// The obtained shared_ptr should be destroyed immediately after the call.
// An instance will be self-destroyed immediately after firing an event.

class TimedConnection : 
    public boost::enable_shared_from_this<TimedConnection>
{
public:

    typedef boost::posix_time::time_duration time_duration_t;
    
    static ConnectionWatchDogWP create(time_duration_t timePeriod,
        boost::asio::io_service& service,
        handle_t handle,
        int priority,
        PISinkEndpoint sink);

    ~TimedConnection();

    void cancel();
    void keepAlive();
    handle_t getHandle() const;
    int priority() const;

private:
    TimedConnection(time_duration_t timePeriod,
        boost::asio::io_service& service,
        handle_t handle,
        int priority,
        PISinkEndpoint sink);

    void start();
    void handleTimeout(const boost::system::error_code& error);

private:
    time_duration_t m_deadlineTime;
    boost::asio::deadline_timer m_timer;
    const handle_t m_handle;
    const int m_priority;
    PISinkEndpoint m_sink;
};

TimedConnection::TimedConnection(boost::posix_time::time_duration timePeriod,
        boost::asio::io_service& service,
        handle_t handle,
        int priority,
        PISinkEndpoint sink) : 
    m_deadlineTime(timePeriod),
    m_timer(service),
    m_handle(handle),
    m_priority(priority),
    m_sink(sink)
{
    if (!m_sink)
    {
        throw XInvalidOperation("Invalid sink object");
    }
}


void TimedConnection::start()
{
    m_timer.expires_from_now(m_deadlineTime);
    m_timer.async_wait(boost::bind(&TimedConnection::handleTimeout,
        shared_from_this(), _1));
}

void TimedConnection::cancel()
{
    m_timer.cancel();
}

void TimedConnection::keepAlive()
{
    cancel();
    start();
}

void TimedConnection::handleTimeout(
    const boost::system::error_code& error)
{
    // Do nothing. This method is only used to track the life-time of the object
}

ConnectionWatchDogWP TimedConnection::create(time_duration_t timePeriod,
        boost::asio::io_service& service,
        handle_t handle,
        int priority,
        PISinkEndpoint sink)
{
    ConnectionWatchDogSP instance(new TimedConnection(
        timePeriod,  boost::ref(service), handle, priority, sink));
    instance->start();
    return instance;
}

handle_t TimedConnection::getHandle() const
{
    return m_handle;
}

TimedConnection::~TimedConnection()
{
    m_sink->Destroy();
}

int TimedConnection::priority() const
{
    return m_priority;
}

} // of namespace detail


SinkEndpointImpl::SinkEndpointImpl(boost::asio::io_service& service) :
    m_handleGenerator(boost::random::random_device()()),
    m_service(service)
{}

handle_t SinkEndpointImpl::create(connectionfactory_t connectionfactory, int priority,
        boost::posix_time::time_duration timeout/*= KEEP_ALIVE_TIME * 2*/)
{
    boost::mutex::scoped_lock lock(m_connectionGuard);
    if (detail::ConnectionWatchDogSP connection = m_connection.lock())
    {
        if (connection->priority() >= priority)
        {
            throw XInvalidOperation("Invalid handle");
        }
        connection->cancel();
    }
    
    const handle_t newHandle = ++m_handleGenerator;

    m_connection = detail::TimedConnection::create(timeout, 
        m_service, newHandle, priority, PISinkEndpoint(connectionfactory()));

    return newHandle;
}

void SinkEndpointImpl::destroy(handle_t handle)
{
    boost::mutex::scoped_lock lock(m_connectionGuard);
    getValidConnection(handle)->cancel();
    m_connection.reset();
}

void SinkEndpointImpl::keepAlive(handle_t handle)
{
    boost::mutex::scoped_lock lock(m_connectionGuard);
    getValidConnection(handle)->keepAlive();
}

detail::ConnectionWatchDogSP SinkEndpointImpl::getValidConnection(handle_t handle)
{
    detail::ConnectionWatchDogSP connection = m_connection.lock();
    if (!connection || connection->getHandle() != handle)
    {
        throw XInvalidOperation("Invalid handle");
    }
    return connection;
}

bool SinkEndpointImpl::isBusy(int priority)
{
    boost::mutex::scoped_lock lock(m_connectionGuard);
    detail::ConnectionWatchDogSP connection = m_connection.lock();
    return connection && (connection->priority() >= priority);
}

}
