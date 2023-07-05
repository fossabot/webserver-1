
#include <ace/OS.h>
#include <string>
#include <initializer_list>

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/reverse_lock.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <CorbaHelpers/Container.h>
#include <CorbaHelpers/Refcounted.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/UnhandledException.h>
#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/CorbaStl.h>

#include "ConnectionInitiator.h"
#include "../MMSS.h"
#include "../ConnectionBroker.h"
#include "../Network/Network.h"
#include "MMTransport.h"
#include "QualityOfService.h"

// Диаграмма состояний sink endpoint-а находится в файле
// ngp/mmss/doc/SinkEndpointStatechart.png



namespace NMMSS
{
// Abstracts RPC calls used to initiate MM connection.
// Currently this interface uses SYNC methods and CORBA types.
// After dropping CORBA support we can move to async and corba free c++ types.
// While we can right now drop corba types and make RPC interface async
// I (anzor.apshev) feel no courage to do it right now,
// mainly because it is more risky and time consuming.
struct IConnectionRpcSteps
{
    virtual ~IConnectionRpcSteps() {};

    virtual bool IsReconnectPossible() const = 0;
    virtual boost::posix_time::seconds RemakeTimeout() const = 0;

    // throws, blocking call
    virtual void RequestConnection(std::uint32_t pid, const std::string& hostId,
        const ::MMSS::NetworkTransportSeq& sinkPrefs,
        const ::MMSS::QualityOfService& qos,
        ::MMSS::ConnectionInfo_var& connInfo,
        std::string& cookie) = 0;

    // throws, blocking call
    virtual void RequestQoS(const std::string& cookie, const ::MMSS::QualityOfService& qos) = 0;

    // Clean any cached state.
    virtual void CleanUp() = 0;


    using OnNetworkDisconnectHandler_t = std::function<void()>;
    // Returns empty source on errors.
    using OnInputSinkCreatedHandler_t = std::function<void(PPullStyleSource source)>;
    virtual void AsyncCreateMMTunnel(
        DECLARE_LOGGER_ARG,
        const ::MMSS::QualityOfService& qos,
        OnNetworkDisconnectHandler_t onDisconnect,
        OnInputSinkCreatedHandler_t onCreated) = 0;
};

using PConnectionRpcSteps = std::unique_ptr<IConnectionRpcSteps>;

}

namespace
{

typedef enum {      
    ES_Closed, 
    ES_Open_Disconnected, ES_Open_Connecting, ES_Open_Connected, ES_Open_Disconnecting, 
    ES_Closing, ES_Closing_Disconnecting, 
    ES_Destroyed 
} ESinkEndpointState;

std::ostream& operator<<(std::ostream& os, ESinkEndpointState state)
{
    switch(state)
    {
    case ES_Closed:                 os << "ES_Closed"; break;
    case ES_Open_Disconnected:      os << "ES_Open_Disconnected"; break;
    case ES_Open_Connecting:        os << "ES_Open_Connecting"; break;
    case ES_Open_Connected:         os << "ES_Open_Connected"; break;
    case ES_Open_Disconnecting:     os << "ES_Open_Disconnecting"; break;
    case ES_Closing:                os << "ES_Closing"; break;
    case ES_Closing_Disconnecting:  os << "ES_Closing_Disconnecting"; break;
    case ES_Destroyed:              os << "ES_Destroyed"; break;
    default: throw std::logic_error("invalid enum value");
    }
    return os;
}

template < class ToDuration, class FromRep, class FromPeriod > inline
ToDuration cross_library_duration_cast( boost::chrono::duration<FromRep, FromPeriod> const& orig )
{
    typedef typename ToDuration::period ToPeriod;
    return ToDuration( boost::chrono::duration_cast< boost::chrono::duration< typename ToDuration::rep, boost::ratio<ToPeriod::num, ToPeriod::den> >, FromRep, FromPeriod >( orig ).count() );
}

std::string getLoggerPrefix(const void* ptr, const std::string& endpointName)
{
    std::ostringstream oss;
    oss << "CSinkEndpoint[";

    if (!endpointName.empty())
        oss << endpointName << "]/";
    else
        oss << std::hex << ptr << "]/";

    return oss.str();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CSinkEndpoint 
    : public NLogging::WithLogger
    , public virtual NMMSS::ISinkEndpoint
    , public virtual NCorbaHelpers::CWeakReferableImpl
{
private:
    typedef boost::shared_ptr<NMMSS::IConnectionBase> PConnectionBase;
    typedef NCorbaHelpers::CAutoPtr<NMMSS::IPullStyleSink> PSink;

private:
    bool IsOpen(const boost::recursive_mutex::scoped_lock& lock) const
    {
        if(!lock)
            throw std::logic_error("SinkEndpoint::IsOpen(): lock should be acquired");
        switch(m_state)
        {
        case ES_Open_Disconnected:
        case ES_Open_Connecting:
        case ES_Open_Connected:
        case ES_Open_Disconnecting:
            return true;
        default:
            return false;
        }
    }

    void ReconnectThreadProcedure()
    {
        // НЕ СТАВЬТЕ ЗДЕСЬ НИКАКИХ ЛОГГЕРОВ И Т.Д.!!
        {
            TRACE_BLOCK;

            const unsigned int DEFAULT_TIME_TO_SLEEP = 8; // seconds
            boost::posix_time::seconds timeToSleep = m_rpcSteps->RemakeTimeout();

            boost::recursive_mutex::scoped_lock lock(m_mutex);
            while (ES_Open_Disconnected == m_state && m_rpcSteps->IsReconnectPossible())
            {
                _trc_ << "Connecting...";

                try
                {
                    Connect(lock);
                    m_connectionAttempts = 0;
                    m_error = "";
                }   
                catch(const CORBA::Exception& e)
                {
                    if (m_error != e._name())
                    {
                        m_error = e._name();
                        _err_ << __FUNCTION__ << ". " << m_error;
                    }
                }
                catch(const std::exception& e)
                {
                    if (m_error != e.what())
                    {
                        m_error = e.what();
                        _err_ << __FUNCTION__ << ". " << m_error;
                    }
                }

                if (ES_Open_Disconnected == m_state && m_rpcSteps->IsReconnectPossible())
                {
                    m_rpcSteps->CleanUp();
                    if (timeToSleep < boost::posix_time::seconds(DEFAULT_TIME_TO_SLEEP))
                        timeToSleep *= 2;
                    this->m_conditionStateChanged.timed_wait(lock,
                        boost::get_system_time() + boost::posix_time::seconds(timeToSleep));
                }
            }
            this->m_threadReconnect.reset();
        }
    }

    void InitReconnect(boost::recursive_mutex::scoped_lock &lock)
    {
        if(!lock)
            throw std::logic_error("CSinkEndpoint::InitReconnect(): lock should be acquired");
        if(ES_Open_Disconnected!=m_state)
            return;
        if (m_rpcSteps->IsReconnectPossible())
        {
            m_threadReconnect.reset(new boost::thread(
                boost::bind(ExecuteCatchUnhandled<boost::function0<void> >,
                    boost::function0<void>(boost::bind(&CSinkEndpoint::ReconnectThreadProcedure, NCorbaHelpers::ShareRefcounted(this))),
                        NLogging::PLogger(GET_LOGGER_PTR, NCorbaHelpers::ShareOwnership()), 
                        "CSinkEndpoint::ReconnectThreadProcedure")));
        }
    }

    static void DestroyConnection(NMMSS::IConnectionBase *conn)
    {
        if(!conn)
            return;

        try
        {
            NMMSS::GetConnectionBroker()->DestroyConnection(conn);
        }
        catch(...) {}
    }

    // without disconnection
    void Connect(boost::recursive_mutex::scoped_lock &lock)
    {
        TRACE_BLOCK;
        // хватаем аргумент, поскольку мы им владеем пока не отдали владение им в транспортный канал.
        if(!lock)
            throw std::logic_error("CSinkEndpoint::Connect(): lock should be acquired");

        CheckState("Invalid state for Connect(). ", m_state, {ES_Open_Disconnected});
        ChangeState(ES_Open_Connecting, lock);

        MMSS::QualityOfService qos = m_qos;

        try
        {
            MMSS::ConnectionInfo_var connInfo;
            std::string cookie;

            MMSS::NetworkTransportSeq_var nts = generateTransport();

            {
                boost::reverse_lock<boost::recursive_mutex::scoped_lock> unlock(lock);
                m_rpcSteps->RequestConnection(ACE_OS::getpid(), NCorbaHelpers::ToUtf8(NCorbaHelpers::GetLocalHostW()),
                    nts, qos, connInfo, cookie);
            }
            // пока мы не держали lock, кто-то изменил состояние
            CheckState("Endpoint state hase changed since connect was requested. ", m_state, { ES_Open_Connecting, ES_Open_Connected });

            m_cookie = cookie;

            _trc_block_(log)
            {
                log << "ConnectionInfo: transport=" << connInfo->_d() << " cookie=" << m_cookie;
                switch (connInfo->_d())
                {
                case MMSS::ELOCAL:
                {
                    log << " port=" << connInfo->local_().port;
                    break;
                }
                case MMSS::ETCP:
                {
                    log << " port=" << connInfo->tcp().port << " address=";
                    for (const auto& addr : NCorbaHelpers::make_range(connInfo->tcp().addresses))
                        log << addr << " ";
                    break;
                }
                case MMSS::EUDP:
                {
                    log << " port=" << connInfo->udp().controlPort << " address=" << connInfo->udp().address;
                    break;
                }
                case MMSS::EMULTICAST:
                {
                    log << " port=" << connInfo->mcast().controlPort << " address=" << connInfo->mcast().controlIface;
                    break;
                }
                default:
                    break;
                }
            }

            PSink sink(m_sink);
            asyncCreateInputChannelAndEstablishConnection(sink, connInfo, cookie, lock);

            if (m_state != ES_Open_Connected)
            {
                WaitStateChange(lock);
            }
            // пока мы не держали lock, кто-то изменил состояние
            CheckState("Endpoint state hase changed since connect was requested. ", m_state, { ES_Open_Connecting, ES_Open_Connected, ES_Open_Disconnected });
        }
        catch(...)
        {
            if(ES_Destroyed!=m_state)
                ChangeState(ES_Open_Disconnected, lock);
            throw;
        }

        if (!m_sourceConnection)
        {
            ChangeState(ES_Open_Disconnected, lock);
            throw std::runtime_error("Cannot create channel");
        }
        else
        {
            if( qos != m_qos )
            {
                qos = m_qos;
                auto cookie = m_cookie;
                boost::reverse_lock<boost::recursive_mutex::scoped_lock> unlock(lock);
                _inf_ << "Call RequestQoS for SindkEndpoint! cookie = " << cookie;
                m_rpcSteps->RequestQoS(cookie, qos);
            }
        }
    }

    void asyncCreateInputChannelAndEstablishConnection(PSink sink, MMSS::ConnectionInfo_var connInfo, const std::string& cookie, boost::recursive_mutex::scoped_lock& lock)
    {
        unsigned short port=0;
        std::string address;
        switch(connInfo->_d())
        {
        case MMSS::EINPROC:
        {
            NMMSS::PPullStyleSource pSource((NMMSS::IPullStyleSource*)connInfo->inproc().pullSourcePtr);
            NMMSS::PPullStyleSink pWrapper(CreatePullInprocInputChannel(GET_LOGGER_PTR, sink.Get(), makeDisconnectHandler()));
            m_sourceConnection.reset(NMMSS::GetConnectionBroker()->SetConnection(pSource.Get(), pWrapper.Get(), GET_LOGGER_PTR), DestroyConnection);
            ChangeState(ES_Open_Connected, lock);
            return;
        }
        case MMSS::ELOCAL:
        {
            port = connInfo->local_().port;
            std::vector<std::string> addresses;
            addresses.push_back("127.0.0.1");

            m_connectionInitiator->InitiateConnection(cookie, addresses, port,
                boost::bind(&CSinkEndpoint::handleTcpSocketConnected,
                    NCorbaHelpers::ShareRefcounted(this), _1, connInfo, sink));

            break;
        }
        case MMSS::ETCP:
        {
            port = connInfo->tcp().port;
            std::vector<std::string> addresses;
            for (CORBA::ULong k = 0; k < connInfo->tcp().addresses.length(); ++k)
                addresses.push_back(connInfo->tcp().addresses[k].in());

            m_connectionInitiator->InitiateConnection(cookie, addresses, port,
                boost::bind(&CSinkEndpoint::handleTcpSocketConnected,
                NCorbaHelpers::CAutoPtr<CSinkEndpoint>(this, NCorbaHelpers::ShareOwnership()),
                _1, connInfo, sink));

            break;
        }
        case MMSS::EUDP:
        {
            port = connInfo->udp().controlPort;
            address = connInfo->udp().address;

            initiateUdpConnection(address, port,
                boost::bind(&CSinkEndpoint::handleUdpControlSocketConnected,
                NCorbaHelpers::CAutoPtr<CSinkEndpoint>(this, NCorbaHelpers::ShareOwnership()),
                _1, connInfo, sink, cookie));

            break;
        }
        case MMSS::EMULTICAST:
        {
            port = connInfo->mcast().controlPort;
            address = connInfo->mcast().controlIface;

            initiateUdpConnection(address, port,
                boost::bind(&CSinkEndpoint::handleUdpMulticastSocketConnected,
                NCorbaHelpers::CAutoPtr<CSinkEndpoint>(this, NCorbaHelpers::ShareOwnership()),
                _1, connInfo, sink, cookie));

            break;
        }
        case MMSS::ERPC_TUNNEL:
        {
            NCorbaHelpers::CAutoPtr<CSinkEndpoint> pthis(this, NCorbaHelpers::ShareOwnership());
            m_rpcSteps->AsyncCreateMMTunnel(GET_LOGGER_PTR, m_qos, makeDisconnectHandler(), [pthis, sink](NMMSS::PPullStyleSource source)
            {
                pthis->handleRpcTunnelCreated(source, sink);
            });
            break;
        }
        default:
        {
            _err_ << "UNEXPEXTED ENUM VALUE! CSinkEndpoint.asyncCreateInputChannelAndEstablishConnection: Can't initiate connection for transport: "
                << connInfo->_d();
        }
        }
    }

    void initiateUdpConnection(const std::string& address, unsigned short port, boost::function1<void, NMMSS::PUDPSocket> handler)
    {
        boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(),
            address, std::to_string(port),
            boost::asio::ip::udp::resolver::query::address_configured);
        NCorbaHelpers::PReactor reactor(NCorbaHelpers::GetReactorInstanceShared());
        boost::asio::ip::udp::resolver resolver(reactor->GetIO());
        try
        {
            boost::asio::ip::udp::resolver::iterator it = resolver.resolve(query);
            if (it==boost::asio::ip::udp::resolver::iterator())
                throw std::runtime_error("could not resolve udp endpoint to a non empty list");
            NMMSS::PUDPSocket socket(new NMMSS::UDPSocket_t(NCorbaHelpers::GetReactorFromPool()));
            socket->sock().open(boost::asio::ip::udp::v4());
            socket->sock().connect(it->endpoint());
            handler(socket);
        }
        catch (const std::runtime_error& e)
        {
            _err_ << __FUNCTION__ << ": " << e.what() << ", address was " << address;
            handler(NMMSS::PUDPSocket());
        }
    }

    NMMSS::FOnNetworkDisconnect makeDisconnectHandler()
    {
        return boost::bind(&CSinkEndpoint::DisconnectAndReconnect, 
            NCorbaHelpers::ShareRefcounted(this));
    }

    void handleTcpSocketConnected(NMMSS::PTCPSocket sock, 
        MMSS::ConnectionInfo_var connInfo, PSink sink)
    {
        NMMSS::FOnNetworkDisconnect disconnectHandler(makeDisconnectHandler());
        if(!sock)
        {
            disconnectHandler();
            return;
        }

        boost::recursive_mutex::scoped_lock lock(m_mutex);
        if(ES_Open_Connecting==m_state)
            ChangeState(ES_Open_Connected, lock);
        else
        {
            _wrn_ << "Close tcp socket because sink endpoint has incorrect state";
            if (sock->sock().is_open())
            {
                boost::system::error_code code;
                sock->sock().shutdown(boost::asio::ip::tcp::socket::shutdown_both, code);
                sock->sock().close(code);
            }
            return;
        }

        NMMSS::PPullStyleSource channel;
        switch(connInfo->_d())
        {
        case MMSS::ELOCAL:
        {
            const MMSS::AllocatorParams &allocatorParams = connInfo->local_().allocator;
            NMMSS::SAllocatorId allocID(allocatorParams.allocatorId);
            NMMSS::PAllocatorFactory factory(NMMSS::SAllocatorId() != allocID ? NMMSS::GetSharedMemorySlaveFactory( allocID ) : nullptr);
            channel = NMMSS::CreateLocalInputChannel(GET_LOGGER_PTR, sock, factory.Get(), m_bufferingPolicy, disconnectHandler);
            break;
        }
        case MMSS::ETCP:
        {
            channel = NMMSS::CreatePullTcpInputChannel(GET_LOGGER_PTR, sock, m_bufferingPolicy, disconnectHandler);
            break;
        }
        default:
            break;
        }
        m_sourceConnection.reset(NMMSS::GetConnectionBroker()->SetConnection(channel.Get(), sink.Get(), GET_LOGGER_PTR), DestroyConnection);
    }

    void handleRpcTunnelCreated(NMMSS::PPullStyleSource source, PSink sink)
    {
        NMMSS::FOnNetworkDisconnect disconnectHandler(makeDisconnectHandler());
        if (!source)
        {
            disconnectHandler();
            return;
        }

        boost::recursive_mutex::scoped_lock lock(m_mutex);
        if (ES_Open_Connecting == m_state)
            ChangeState(ES_Open_Connected, lock);
        else
        {
            _wrn_ << "Ignore created RPC tunneled source because endpoint has incorrect state.";
            return;
        }
        m_sourceConnection.reset(NMMSS::GetConnectionBroker()->SetConnection(source.Get(), sink.Get(), GET_LOGGER_PTR), DestroyConnection);
    }

    void handleUdpControlSocketConnected(NMMSS::PUDPSocket sock,
        MMSS::ConnectionInfo_var connInfo, PSink sink, const std::string& cookie)
    {
        unsigned short port = connInfo->udp().dataPort;
        std::string address = connInfo->udp().address.in();

        initiateUdpConnection(address, port,
            boost::bind(&CSinkEndpoint::handleUdpDataSocketConnected,
            NCorbaHelpers::CAutoPtr<CSinkEndpoint>(this, NCorbaHelpers::ShareOwnership()),
            _1, sock, connInfo, sink, cookie));
    }

    void handleUdpDataSocketConnected(NMMSS::PUDPSocket dataSock,
        NMMSS::PUDPSocket controlSock,
        MMSS::ConnectionInfo_var connInfo, PSink sink, const std::string& cookie)
    {
        NMMSS::FOnNetworkDisconnect disconnectHandler(makeDisconnectHandler());
        if (!dataSock)
        {
            disconnectHandler();
            return;
        }

        boost::recursive_mutex::scoped_lock lock(m_mutex);
        if (ES_Open_Connecting == m_state)
            ChangeState(ES_Open_Connected, lock);
        else
        {
            _wrn_ << "Close udp socket because sink endpoint has incorrect state";
            if (dataSock->sock().is_open())
            {
                boost::system::error_code code;
                dataSock->sock().shutdown(boost::asio::ip::udp::socket::shutdown_both, code);
                dataSock->sock().close(code);
            }
            return;
        }

        NMMSS::PPullStyleSource channel;
        switch (connInfo->_d())
        {
        case MMSS::EUDP:
        {
            channel = NMMSS::CreateUdpInputChannel(GET_LOGGER_PTR, 
                controlSock, dataSock, cookie, m_bufferingPolicy, disconnectHandler);
            break;
        }
        default:
            break;
        }
        m_sourceConnection.reset(NMMSS::GetConnectionBroker()->SetConnection(channel.Get(), sink.Get(), GET_LOGGER_PTR), DestroyConnection);
    }

    void handleUdpMulticastSocketConnected(NMMSS::PUDPSocket controlSock,
        MMSS::ConnectionInfo_var connInfo, PSink sink, const std::string& cookie)
    {
        NMMSS::FOnNetworkDisconnect disconnectHandler(makeDisconnectHandler());
        if (!controlSock)
        {
            disconnectHandler();
            return;
        }

        boost::recursive_mutex::scoped_lock lock(m_mutex);
        if (ES_Open_Connecting == m_state)
            ChangeState(ES_Open_Connected, lock);
        else
            return;

        NMMSS::PPullStyleSource channel;
        switch (connInfo->_d())
        {
        case MMSS::EMULTICAST:
        {
            channel = NMMSS::CreateUdpMulticastInputChannel(GET_LOGGER_PTR, controlSock, 
                connInfo->mcast().dataIface.in(), connInfo->mcast().dataPort, cookie, m_bufferingPolicy, disconnectHandler);
            break;
        }
        default:
            break;
        }
        m_sourceConnection.reset(NMMSS::GetConnectionBroker()->SetConnection(channel.Get(), sink.Get(), GET_LOGGER_PTR), DestroyConnection);
    }


    void DisconnectAndReconnect()
    {
        boost::recursive_mutex::scoped_lock lock(m_mutex);
        if(IsOpen(lock))
        {
            Disconnect(lock);
            SheduleReconnect(lock);
        }
    }

private:
    void SheduleReconnect(boost::recursive_mutex::scoped_lock &lock)
    {
            const int seconds=(1<<(std::max<int>(++m_connectionAttempts, 3)));
            boost::posix_time::time_duration timeout=boost::posix_time::seconds(seconds);
            m_connectTimer.expires_from_now(timeout);
            m_connectTimer.async_wait(boost::bind(&CSinkEndpoint::NextReconnectTimeout, 
                NCorbaHelpers::CAutoPtr<CSinkEndpoint>(this, NCorbaHelpers::ShareOwnership()),
                boost::asio::placeholders::error));
    }

    void NextReconnectTimeout(const boost::system::error_code& error)
    {
        if (!error)
        {
            boost::recursive_mutex::scoped_lock lock(m_mutex);
            if(IsOpen(lock))
            {
                InitReconnect(lock);
            }
        }
        else
        {
            _log_ << "Reconnect timer error: " << error.message();
        }
    }

    MMSS::NetworkTransportSeq* generateTransport()
    {
        MMSS::NetworkTransportSeq_var nts = new MMSS::NetworkTransportSeq;
        if (MMSS::EAUTO == m_transport)
        {
            nts->length(3);
            nts[0] = MMSS::EINPROC;
            nts[1] = MMSS::ELOCAL;
            nts[2] = MMSS::ETCP;
        }
        else
        {
            nts->length(1);
            nts[0] = m_transport;
        }
        return nts._retn();
    }

public:
    CSinkEndpoint(DECLARE_LOGGER_ARG, 
        NMMSS::PConnectionRpcSteps&& rpcSteps,
        NMMSS::IPullStyleSink *sink,
        MMSS::ENetworkTransport transport,
        MMSS::QualityOfService const* qos,
        NMMSS::EFrameBufferingPolicy bufferingPolicy,
        const std::string& endpointName = "")
        :   NLogging::WithLogger(GET_LOGGER_PTR, getLoggerPrefix(this, endpointName))
        ,   m_sink(sink, NCorbaHelpers::ShareOwnership())
        ,   m_rpcSteps(std::move(rpcSteps))
        ,   m_reactor(NCorbaHelpers::GetReactorFromPool())
        ,   m_state(ES_Closed)
        ,   m_connectionAttempts(0)
        ,   m_connectTimer(m_reactor->GetIO())
        ,   m_transport(transport)
        ,   m_qos( qos ? *qos : MMSS::QualityOfService() )
        ,   m_bufferingPolicy(bufferingPolicy)
    {
        m_connectionInitiator = NMMSS::GetConnectionInitiatorInstance();

        Open();
    }

    virtual ~CSinkEndpoint()
    {
        Destroy();

        std::unique_ptr<boost::thread> threadReconnect( std::move(m_threadReconnect) );
        if(threadReconnect.get() && (threadReconnect->get_id()!=boost::this_thread::get_id()))
            threadReconnect->join();
        _log_ << "destroyed";
    }

    void RequestQoS( MMSS::QualityOfService const& qos ) override
    {
        boost::recursive_mutex::scoped_lock lock(m_mutex);
        std::string cookie = m_cookie;
        m_qos = qos;
        lock.unlock();
        _inf_ << "Direct Call RequestQoS for SindkEndpoint! cookie = " << cookie;
        m_rpcSteps->RequestQoS( cookie, qos );
    }

public:
    void Open()
    {
        TRACE_BLOCK;
        boost::recursive_mutex::scoped_lock lock(m_mutex);
        CheckState("invalid sink endpoint state for Open(). ", m_state, {ES_Closed});
        ChangeState(ES_Open_Disconnected, lock);
        InitReconnect(lock);
    }

    void Close(boost::recursive_mutex::scoped_lock& lock)
    {
        TRACE_BLOCK;

        if(!lock)
            throw std::logic_error("lock should be acquired before calling to SinkEndpoint::Close()");

        if(ES_Closed==m_state)
            return;
        if(ES_Closing==m_state || ES_Closing_Disconnecting==m_state)
            throw std::logic_error("recursive SinkEndpoint::Close()?? wtf?");

        ChangeState(ES_Closing, lock);

        m_connectTimer.cancel();
        Disconnect(lock);

        ChangeState(ES_Closed, lock);
    }

    void DestroyUnSafely()
    {
        boost::recursive_mutex::scoped_lock lock(m_mutex);
        if (ES_Destroyed == m_state)
            return;
        Close(lock);
        ChangeState(ES_Destroyed, lock);
    }

    void Destroy()
    {
        try
        {
            DestroyUnSafely();
            m_connectTimer.cancel();
        }
        catch (const std::exception &e)
        {
            _err_ << "Couldn't destroy the SinkEndpoint properly, an error has occured: " << e.what();
        }
    }

    void Disconnect(boost::recursive_mutex::scoped_lock& lock)
    {
        TRACE_BLOCK;
        if(!lock)
            throw std::logic_error("lock should be acquired before calling to disconnect impl");
        if(ES_Open_Disconnecting==m_state || ES_Open_Disconnected==m_state || ES_Closed==m_state || ES_Closing_Disconnecting==m_state)
            return;

        if(ES_Closing==m_state)
            ChangeState(ES_Closing_Disconnecting, lock);
        else
            ChangeState(ES_Open_Disconnecting, lock);

        if(m_sourceConnection)
        {
            PConnectionBase connection;
            connection.swap(m_sourceConnection);

            boost::reverse_lock<boost::recursive_mutex::scoped_lock> unlock(lock);
            connection.reset();
        }

        m_rpcSteps->CleanUp();

        if(ES_Closing_Disconnecting==m_state)
            ChangeState(ES_Closing, lock);
        else
            ChangeState(ES_Open_Disconnected, lock);
    }

private:
    static void CheckState(const char* prefix, ESinkEndpointState current, std::initializer_list<ESinkEndpointState> expected)
    {
        for (auto e : expected)
        {
            if (current == e)
                return;
        }
        std::ostringstream msg;
        msg << prefix << "current state: " << current << ", expected:";
        for (auto e : expected)
            msg << ' ' << e;
        throw std::logic_error(msg.str());
    }
    void ChangeState(ESinkEndpointState state, boost::recursive_mutex::scoped_lock& lock)
    {
        if(!lock)
            throw std::logic_error("lock was not acquired before changing SinkEndpoint state");
        if(state!=m_state)
        {
            _trc_ << "CSinkEndpoint::ChangeState(): Old state: " << m_state << ", requested state: " << state;
            if(!ValidateTransition(m_state, state))
            {
                _wrn_ << "invalid transition requested";
                return;
            }

            m_state=state;
            m_conditionStateChanged.notify_all();
        }
    }

    /// В коде присутствует гонка для состояний. В связи с тем что брошенное исключение
    /// из asio потока приведет к завершению процесса, по результатам работы функции 
    /// ValidateTransition исключений кидаться не должно. В функции ChangeState исключение
    /// заменено на логирование
    static bool ValidateTransition(ESinkEndpointState stateOld, ESinkEndpointState stateNew)
    {
        return ES_Destroyed != stateOld;
    }
    void WaitStateChange(boost::recursive_mutex::scoped_lock& lock)
    {
        if(!lock)
            throw std::logic_error("lock was not acquired before changing SinkEndpoint state");
        ESinkEndpointState oldState=m_state;
        while(oldState==m_state)
            m_conditionStateChanged.wait(lock);
    }


private:
    NMMSS::PConnectionInitiator m_connectionInitiator;
    PSink m_sink;
    NMMSS::PConnectionRpcSteps m_rpcSteps;
    NExecutors::PReactor m_reactor;
    ESinkEndpointState m_state;
    int m_connectionAttempts;
    boost::asio::deadline_timer m_connectTimer;
    MMSS::ENetworkTransport m_transport;
    std::string m_cookie;
    MMSS::QualityOfService m_qos;
    NMMSS::EFrameBufferingPolicy m_bufferingPolicy;
    PConnectionBase m_sourceConnection;
    boost::condition m_conditionStateChanged;
    std::unique_ptr<boost::thread> m_threadReconnect;
    std::string m_error;
    boost::recursive_mutex m_mutex;
};


class CDummyEndpointFactory : public NMMSS::IEndpointFactory, public NCorbaHelpers::CRefcountedImpl
{
    MMSS::Endpoint_var m_endpoint;

public:

    explicit CDummyEndpointFactory(MMSS::Endpoint* endpoint)
        : m_endpoint(MMSS::Endpoint::_duplicate(endpoint))
    { }

    MMSS::Endpoint* MakeEndpoint()
    {
        return m_endpoint._retn();
    }

    bool IsRemakePossible() const
    {
        return !CORBA::is_nil(m_endpoint);
    };

    boost::posix_time::seconds RemakeTimeout() const
    {
        return boost::posix_time::seconds(1);
    }
};


class CNamingEndpointFactory : public NMMSS::IEndpointFactory, public NCorbaHelpers::CRefcountedImpl
{
    DECLARE_LOGGER_HOLDER;

    CosNaming::NamingContextExt_var m_naming;
    std::string m_epName;
    std::string m_error;

public:

    CNamingEndpointFactory(DECLARE_LOGGER_ARG, CosNaming::NamingContextExt* naming, const char* epName)
        : m_naming(CosNaming::NamingContextExt::_duplicate(naming))
        , m_epName(epName)
    {
        INIT_LOGGER_HOLDER;
    }

    MMSS::Endpoint* MakeEndpoint()
    {
        try
        {
            CORBA::Object_var obj = m_naming->resolve_str(m_epName.c_str());
            obj = NCorbaHelpers::SetTimeoutPolicyStubwise(obj, 10 * 1000);
            MMSS::Endpoint_var endpoint = MMSS::Endpoint::_narrow(obj);

            if (CORBA::is_nil(endpoint))
                throw std::runtime_error("nil reference");

            _wrn_if_(!m_error.empty()) << "Resolved endpoint with name " << m_epName;

            m_error = "";
            return endpoint._retn();
        }
        catch (const CORBA::Exception& ex)
        {
            std::string error = ex._name();
            _wrn_if_(m_error != error) << "Exception (" << error << ") on resolving endpoint with name " << m_epName;
            m_error = error;
        }
        catch (const std::exception& ex)
        {
            std::string error = ex.what();
            _wrn_if_(m_error != error) << "Exception (" << error << ") on resolving endpoint with name " << m_epName;
            m_error = error;
        }

        return ::MMSS::Endpoint::_nil();
    }

    bool IsRemakePossible() const
    {
        return true;
    }

    boost::posix_time::seconds RemakeTimeout() const
    {
        return boost::posix_time::seconds(1);
    }
};

class CorbaConnectionRpcSteps : public NMMSS::IConnectionRpcSteps, public NLogging::WithLogger

{
public:
    CorbaConnectionRpcSteps(DECLARE_LOGGER_ARG, NMMSS::PEndpointFactory epFactory) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_epFactory(epFactory)
    {

    }

    virtual bool IsReconnectPossible() const override
    {
        return m_epFactory->IsRemakePossible();
    }

    virtual void RequestConnection(std::uint32_t pid, const std::string& hostId,
        const ::MMSS::NetworkTransportSeq& sinkPrefs,
        const ::MMSS::QualityOfService& qos,
        ::MMSS::ConnectionInfo_var& connInfo,
        std::string& cookie) override
    {
        MMSS::Endpoint_var source;
        {
            boost::mutex::scoped_lock lock(m_mutex);
            source = m_endpoint;
        }
        if (CORBA::is_nil(source))
        {
            source = m_epFactory->MakeEndpoint();
        }
        if (CORBA::is_nil(source))
            throw std::runtime_error("Can't resolve endpoint!");

        CORBA::String_var cc;
        source->RequestConnection(pid, hostId.c_str(), sinkPrefs, false, qos, connInfo, cc);
        cookie = cc.in();
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_endpoint = source;
        }
    }

    virtual void RequestQoS(const std::string& cookie, const ::MMSS::QualityOfService& qos) override
    {
        MMSS::Endpoint_var source;
        {
            boost::mutex::scoped_lock lock(m_mutex);
            source = m_endpoint;
        }
        if (!source)
        {
            _wrn_ << "Can't request QOS, endpoint is null!";
            // Legacy behavior: no throw here
            return;
        }
        source->RequestQoS(cookie.c_str(), qos);
    }

    virtual boost::posix_time::seconds RemakeTimeout() const override
    {
        return m_epFactory->RemakeTimeout();
    }

    virtual void CleanUp() override
    {
        boost::mutex::scoped_lock lock(m_mutex);
        m_endpoint = MMSS::Endpoint::_nil();
    }


    virtual void AsyncCreateMMTunnel(
        DECLARE_LOGGER_ARG,
        const ::MMSS::QualityOfService& qos,
        OnNetworkDisconnectHandler_t onDisconnect,
        OnInputSinkCreatedHandler_t onCreated) override
    {
        // Unsupported for corba.
        onCreated(NMMSS::PPullStyleSource());
    }

private:
    boost::mutex m_mutex;
    MMSS::Endpoint_var m_endpoint;
    NMMSS::PEndpointFactory m_epFactory;
};

class AbstractRpcConnectionSteps : public NMMSS::IConnectionRpcSteps, public NLogging::WithLogger
{
public:
    AbstractRpcConnectionSteps(DECLARE_LOGGER_ARG, NMMSS::PEndpointClient endpoint) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_endpoint(endpoint)
    {

    }

    virtual bool IsReconnectPossible() const override
    {
        return m_endpoint->IsReconnectPossible();
    }

    virtual void RequestConnection(std::uint32_t pid, const std::string& hostId,
        const ::MMSS::NetworkTransportSeq& sinkPrefs,
        const ::MMSS::QualityOfService& qos,
        ::MMSS::ConnectionInfo_var& connInfo,
        std::string& cookie) override
    {
        m_endpoint->RequestConnection(pid, hostId, sinkPrefs, qos, connInfo, cookie);
    }

    virtual void RequestQoS(const std::string& cookie, const ::MMSS::QualityOfService& qos) override
    {
        m_endpoint->RequestQoS(cookie, qos);
    }

    virtual boost::posix_time::seconds RemakeTimeout() const override
    {
        return boost::posix_time::seconds(1);
    }

    virtual void CleanUp() override
    {
    }

    virtual void AsyncCreateMMTunnel(DECLARE_LOGGER_ARG,
        const ::MMSS::QualityOfService& qos,
        OnNetworkDisconnectHandler_t onDisconnect,
        OnInputSinkCreatedHandler_t onCreated) override
    {
        m_endpoint->AsyncCreateMMTunnel(GET_LOGGER_PTR, qos, onDisconnect, onCreated);
    }

private:
    NMMSS::PEndpointClient m_endpoint;
};

} // anonymous namespace

namespace NMMSS
{

ISinkEndpoint* CreatePullConnectionByObjref(DECLARE_LOGGER_ARG, MMSS::Endpoint* endpoint,
    NMMSS::IPullStyleSink* _pSink,
    MMSS::ENetworkTransport transport,
    MMSS::QualityOfService const* qos,
    EFrameBufferingPolicy bufferingPolicy)
{
    return new CSinkEndpoint(GET_LOGGER_PTR,
        std::make_unique<CorbaConnectionRpcSteps>(GET_LOGGER_PTR, PEndpointFactory(new CDummyEndpointFactory(endpoint))),
        _pSink, transport, qos, bufferingPolicy);
}

ISinkEndpoint* CreatePullConnectionByNsref(DECLARE_LOGGER_ARG, 
    const char* endpointAddress, 
    CosNaming::NamingContextExt* naming,
    NMMSS::IPullStyleSink* _pSink, 
    MMSS::ENetworkTransport transport,
    MMSS::QualityOfService const* qos,
    EFrameBufferingPolicy bufferingPolicy)
{
    return new CSinkEndpoint(GET_LOGGER_PTR, 
        std::make_unique<CorbaConnectionRpcSteps>(GET_LOGGER_PTR, PEndpointFactory{ new CNamingEndpointFactory{ GET_LOGGER_PTR, naming, endpointAddress } }),
        _pSink, transport, qos, bufferingPolicy, endpointAddress);
}

ISinkEndpoint* CreatePullConnectionByEpFactory(DECLARE_LOGGER_ARG,
    IEndpointFactory* epFactory,
    NMMSS::IPullStyleSink* sink,
    MMSS::ENetworkTransport transport,
    MMSS::QualityOfService const* qos,
    EFrameBufferingPolicy bufferingPolicy)
{
    return new CSinkEndpoint(GET_LOGGER_PTR,
        std::make_unique<CorbaConnectionRpcSteps>(GET_LOGGER_PTR, PEndpointFactory(epFactory)),
        sink, transport, qos, bufferingPolicy);
}

ISinkEndpoint* CreatePullConnectionByEndpointClient(DECLARE_LOGGER_ARG,
    PEndpointClient endpoint, NMMSS::IPullStyleSink* sink,
    MMSS::ENetworkTransport transport, MMSS::QualityOfService const* qos,
    EFrameBufferingPolicy bufferingPolicy)
{
    return new CSinkEndpoint(GET_LOGGER_PTR,
        std::make_unique<AbstractRpcConnectionSteps>(GET_LOGGER_PTR, endpoint),
        sink, transport, qos, bufferingPolicy);
}

}
