#ifndef DEVICEIPINT_3_SINKENDPOINTIMPL_H
#define DEVICEIPINT_3_SINKENDPOINTIMPL_H

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "../MMTransport/MMTransport.h"

namespace IPINT30
{

typedef NCorbaHelpers::CAutoPtr<NMMSS::ISinkEndpoint> PISinkEndpoint;

const boost::posix_time::seconds KEEP_ALIVE_TIME(5);

namespace detail
{
    class TimedConnection;
    typedef boost::weak_ptr<TimedConnection> ConnectionWatchDogWP;
    typedef boost::shared_ptr<TimedConnection> ConnectionWatchDogSP;
}

class SinkEndpointImpl : boost::noncopyable
{
public:
    typedef ::CORBA::Long handle_t;

    explicit SinkEndpointImpl(boost::asio::io_service& service);
    
    typedef boost::function<NMMSS::ISinkEndpoint* ()> connectionfactory_t;

    handle_t create(connectionfactory_t connectionfactory, int priority,
        boost::posix_time::time_duration timeout = KEEP_ALIVE_TIME * 2);

    void destroy(handle_t handle);
    void keepAlive(handle_t handle);
    bool isBusy(int priority);

    // TODO: add strongly typed exceptions
    typedef std::runtime_error XInvalidOperation;

private:
    detail::ConnectionWatchDogSP getValidConnection(handle_t handle);

private:
    boost::mutex m_connectionGuard;
    handle_t m_handleGenerator;
    boost::asio::io_service& m_service;
    detail::ConnectionWatchDogWP m_connection;
};

}

#endif

