#include <vector>
#include <map>
#include <boost/asio/steady_timer.hpp>
#include <boost/smart_ptr/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Envar.h>
#include <CorbaHelpers/ListenEndpoints.h>
#include <CorbaHelpers/IpAddressChange.h>
#include "ConnectionAcceptor.h"

#include <MMIDL/MMEndpointC.h> // cookie length and greeting definition

using NMMSS::PTCPSocket;

class CTcpConnectionAcceptor: public NMMSS::ITcpConnectionAcceptor,
    public boost::enable_shared_from_this<CTcpConnectionAcceptor>
{
    typedef boost::shared_ptr<boost::asio::ip::tcp::acceptor> PAcceptor;

    struct SInterfaceContext
    {
        std::string IpAddress;
        PAcceptor Acceptor;
    };

    typedef std::vector<SInterfaceContext> TInterfaceContextList;
    typedef boost::asio::steady_timer TTimer;
    typedef boost::shared_ptr<TTimer> PTimer;
    typedef std::map<std::string, std::pair<FHandler, PTimer>> THandlers;
public:
    CTcpConnectionAcceptor(boost::mutex& mutex)
        :   m_reactor(NCorbaHelpers::GetReactorInstanceShared())
        ,   m_port(0)
        ,   m_accepting(false)
        ,   m_mutex(mutex)
    {
        unsigned short portSpanBase = NCorbaHelpers::CEnvar::NgpPortSpanBase();
        unsigned short portBase = portSpanBase ? portSpanBase : NCorbaHelpers::CEnvar::NgpPortBase() + 1;
        unsigned short portSpan = NCorbaHelpers::CEnvar::NgpPortSpan() - 1*(portSpanBase == 0);

        NCorbaHelpers::TInterfaceList up;
        NCorbaHelpers::TInterfaceList down;

        NCorbaHelpers::EnumInterfaces(NCorbaHelpers::CEnvar::NgpIfaceWhitelist(), up, down);
        if (up.empty())
            throw std::runtime_error("no available listen endpoints");
        
        bool has_loopback = std::any_of(up.begin(), up.end(), [](const NCorbaHelpers::TInterface& x) { return x.IsLoopback; })
                         || std::any_of(down.begin(), down.end(), [](const NCorbaHelpers::TInterface& x) { return x.IsLoopback; });
        if(!has_loopback)
            up.emplace_back("127.0.0.1", true, "");

        for(int k=0; k<portSpan; ++k)
        {
            TInterfaceContextList contexts;
            for (NCorbaHelpers::TInterfaceList::iterator it = up.begin(); it != up.end(); ++it)
            {
                PAcceptor acc(new boost::asio::ip::tcp::acceptor(m_reactor->GetIO()), closeAcceptorSafe);
                acc->open(boost::asio::ip::tcp::v4());
                boost::asio::ip::address_v4 a(boost::asio::ip::address_v4::from_string(it->IpAddress));
                boost::asio::ip::tcp::endpoint endpoint(a, portBase+k);
                boost::system::error_code ec;
                acc->bind(endpoint, ec);

                if (!ec)
                    contexts.push_back(SInterfaceContext{ it->IpAddress, acc });
            }

            if (contexts.size() == up.size())
            {
                m_port = portBase + k;
                m_contexts = contexts;
                break;
            }
        }
        if (m_contexts.empty())
            throw std::runtime_error("CTcpConnectionAcceptor ctor: could not listen on a port from specified range");
        for (TInterfaceContextList::iterator it = m_contexts.begin(); it != m_contexts.end(); ++it)
            if (it->Acceptor) it->Acceptor->listen();

        for (NCorbaHelpers::TInterfaceList::iterator it = down.begin(); it != down.end(); ++it)
            m_contexts.push_back(SInterfaceContext{ it->IpAddress, PAcceptor() });
    }

    ~CTcpConnectionAcceptor()
    {
        m_contexts.clear(); //closes them
    }

    virtual unsigned short GetPort()
    {
        return m_port;
    }

    void ListenIpAddressChanges()
    {
        typedef boost::shared_ptr<CTcpConnectionAcceptor> PTcpAcceptor;
        typedef boost::weak_ptr<CTcpConnectionAcceptor>   PWeakTcpAcceptor;

        PWeakTcpAcceptor weak(shared_from_this());

        m_ipAddressChangeListener.reset(new NCorbaHelpers::IpAddressChangeListener(
            nullptr,
            [weak](DECLARE_LOGGER_ARG, const NCorbaHelpers::TInterfaceList& interfaces)
            {
                try
                {
                    PTcpAcceptor solid(weak);
                    solid->update_contexts(interfaces);
                    auto postpone_unref_to_avoid_self_join_attempt = [solid](){};
                    auto reactor = solid->m_reactor;
                    solid.reset();
                    reactor->GetIO().post( std::move(postpone_unref_to_avoid_self_join_attempt) );
                }
                catch (const boost::bad_weak_ptr&) {}
                return false;
            },
            [](){}));
    }

private:

    void update_contexts(const NCorbaHelpers::TInterfaceList& interfaces)
    {
        boost::mutex::scoped_lock lock(m_mutex);

        // check if there is any dropped interface
        for (auto& ctx : m_contexts)
        {
            try
            {
                std::string address = ctx.IpAddress;
                auto iter = std::find_if(interfaces.begin(), interfaces.end(), [address](const NCorbaHelpers::TInterface& itf) { return itf.IpAddress == address; });

                if (interfaces.end() == iter && ctx.Acceptor)
                {
                    ctx.Acceptor->cancel();
                    ctx.Acceptor.reset();
                }
            }
            catch(...) { }
        }

        // check if there is any raised interface
        for (auto& itf : interfaces)
        {
            try
            {
                std::string address = itf.IpAddress;
                auto iter = std::find_if(m_contexts.begin(), m_contexts.end(), [address](const SInterfaceContext & ctx)
                {
                    return ctx.IpAddress == address;
                });

                if (m_contexts.end() != iter && !iter->Acceptor)
                {
                    PAcceptor acc(new boost::asio::ip::tcp::acceptor(m_reactor->GetIO()), closeAcceptorSafe);
                    acc->open(boost::asio::ip::tcp::v4());
                    boost::asio::ip::address_v4 a(boost::asio::ip::address_v4::from_string(iter->IpAddress));
                    boost::asio::ip::tcp::endpoint endpoint(a, m_port);
                    acc->bind(endpoint);
                    acc->listen();

                    iter->Acceptor = acc;
                }
            }
            catch(...) { }
        }
    }

    void async_accept(const boost::mutex::scoped_lock & lock)
    {
        if(!lock)
            throw std::logic_error("lock should be acquired");
        if(m_accepting)
            return;
        m_accepting=true;
        for (TInterfaceContextList::iterator it = m_contexts.begin(); it != m_contexts.end(); ++it)
            if (it->Acceptor) async_accept(lock, it->Acceptor);
    }
    void cancel_accept(const boost::mutex::scoped_lock & lock)
    {
        if(!lock)
            throw std::logic_error("lock should be acquired");
        if(!m_accepting)
            return;
        m_accepting=false;
        for (TInterfaceContextList::iterator it = m_contexts.begin(); it != m_contexts.end(); ++it)
            if (it->Acceptor) it->Acceptor->cancel();
    }
    void async_accept(const boost::mutex::scoped_lock & lock, PAcceptor acceptor)
    {
        if(!lock)
            throw std::logic_error("lock should be acquired");
        PTCPSocket peer(new NMMSS::TCPSocket_t{ NCorbaHelpers::GetReactorFromPool() });
        acceptor->async_accept(peer->sock(), 
            boost::bind(&CTcpConnectionAcceptor::handle_accept, shared_from_this(), _1, acceptor, peer));
    }
    void handle_accept(const boost::system::error_code& ec, PAcceptor acceptor, PTCPSocket peer)
    {
        if(ec) // cancel или abort
            return;
        boost::mutex::scoped_lock lock(m_mutex);
        if(!ec)
        {
            boost::shared_ptr<std::vector<char> > b(new std::vector<char>(MMSS::CONNECTION_COOKIE_LENGTH));
            b->resize(MMSS::CONNECTION_COOKIE_LENGTH);
            async_receive_cookie(b, 0, peer);
        }
        // продолжаем принимать соединения тем же акцептором, так чтобы все находились в одинаковом состоянии
        async_accept(lock, acceptor);
    }
    void async_receive_cookie(boost::shared_ptr<std::vector<char> > b, 
        size_t offset,
        PTCPSocket peer)
    {
        peer->sock().async_receive(boost::asio::buffer(&b->operator[](0), b->size()-offset),
            boost::bind(&CTcpConnectionAcceptor::handle_cookie_received, shared_from_this(), _1, _2, b, offset, peer));
    }
    void handle_cookie_received(const boost::system::error_code& ec, std::size_t bytes_transferred,
        boost::shared_ptr<std::vector<char> > b, size_t offset, PTCPSocket peer)
    {
        if(ec)
        {
            boost::system::error_code shut_close_ec;
            peer->sock().shutdown(boost::asio::ip::tcp::socket::shutdown_both, shut_close_ec);
            peer->sock().close(shut_close_ec);
            return;
        }
        if(offset + bytes_transferred < b->size())
        {
            async_receive_cookie(b, offset+bytes_transferred, peer);
            return;
        }
        std::string cookie(b->begin(), b->end());
        boost::mutex::scoped_lock lock(m_mutex);
        THandlers::iterator it(m_handlers.find(cookie));
        if(m_handlers.end()==it)
        {
            lock.unlock();
            boost::system::error_code shut_close_ec;
            peer->sock().shutdown(boost::asio::ip::tcp::socket::shutdown_both, shut_close_ec);
            peer->sock().close(shut_close_ec);
        }
        else
        {
            boost::system::error_code ignore_error;
            TTimer* timer = it->second.second.get();
            if(timer->cancel(ignore_error))
            {
                FHandler handler = std::move(it->second.first);
                m_handlers.erase(it);
                if(m_handlers.empty())
                    cancel_accept(lock);
                async_send_greeting(handler, peer);
            }
        }
    }
    void async_send_greeting(FHandler handler, PTCPSocket peer)
    {
        peer->sock().async_send(boost::asio::buffer(MMSS::CONNECTION_GREETING, strlen(MMSS::CONNECTION_GREETING)),
            boost::bind(&CTcpConnectionAcceptor::handle_greeting_sent,
                shared_from_this(), _1, _2, handler, peer));
    }
    void handle_greeting_sent(const boost::system::error_code& ec, 
        std::size_t bytes_transferred,
        FHandler handler, 
        PTCPSocket peer)
    {
        if(ec || (bytes_transferred!=strlen(MMSS::CONNECTION_GREETING)))
        {
            boost::system::error_code shut_close_ec;
            peer->sock().shutdown(boost::asio::ip::tcp::socket::shutdown_both, shut_close_ec);
            peer->sock().close(shut_close_ec);
            handler(PTCPSocket()); // так мы говорим что ничего не вышло.
        }
        else
            handler(peer);
    }
    void handle_timeout(const boost::system::error_code& ec, const std::string& cookie)
    {
        if(!ec)
        {
            boost::mutex::scoped_lock lock(m_mutex);
            auto it = m_handlers.find(cookie);
            if(m_handlers.end()!=it)
            {
                FHandler handler = std::move(it->second.first);
                m_handlers.erase(it);
                if(m_handlers.empty())
                    cancel_accept(lock);
                handler(nullptr);
            }
        }
    }

public:
    void Register(const std::string& cookie, FHandler onAccept, TDuration timeout) override
    {
        boost::mutex::scoped_lock lock(m_mutex);
        THandlers::iterator it(m_handlers.find(cookie));
        if(m_handlers.end()==it)
        {
            PTimer timer = boost::make_shared<TTimer>(m_reactor->GetIO(), timeout);
            timer->async_wait(boost::bind(
                &CTcpConnectionAcceptor::handle_timeout,
                shared_from_this(), _1, cookie));
            m_handlers.insert(std::make_pair(cookie, std::make_pair(onAccept, timer)));
            async_accept(lock);
        }
        else
            throw std::runtime_error("attempted to register a handler with the same cookie twice");
    }
    void Cancel(const std::string& cookie) override
    {
        boost::mutex::scoped_lock lock(m_mutex);
        THandlers::iterator it(m_handlers.find(cookie));
        if(m_handlers.end()!=it)
        {
            TTimer* timer = it->second.second.get();
            if(timer->cancel())
            {
                FHandler& handler = it->second.first;
                handler(nullptr);
                m_handlers.erase(it);
                if(m_handlers.empty())
                    cancel_accept(lock);
            }
        }
    }
    NCorbaHelpers::PReactor GetReactor()
    {
        return m_reactor;
    }
    
private:
    static void closeAcceptorSafe(boost::asio::ip::tcp::acceptor* acceptor)
    {
        if(!acceptor)
            return;
        boost::system::error_code code;
        acceptor->close(code);
        delete acceptor;
    }
private:
    NCorbaHelpers::PReactor m_reactor;
    unsigned short m_port;
    TInterfaceContextList m_contexts;
    bool m_accepting;
    std::unique_ptr<NCorbaHelpers::IpAddressChangeListener> m_ipAddressChangeListener;
    boost::mutex& m_mutex;
    THandlers m_handlers;
};

class CUdpConnectionAcceptor : public NMMSS::IUdpConnectionAcceptor,
    public boost::enable_shared_from_this<CUdpConnectionAcceptor>
{
public:
    CUdpConnectionAcceptor()
        : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
    {
        auto portSpanBase = NCorbaHelpers::CEnvar::NgpPortSpanBase();
        m_portBase = portSpanBase ? portSpanBase : NCorbaHelpers::CEnvar::NgpPortBase() + 1;
        m_portMax = m_portBase + NCorbaHelpers::CEnvar::NgpPortSpan() - 1 - 1*(portSpanBase == 0);
    }

    virtual NMMSS::PUDPSocket CreateUdpSocket(const std::string& iface)
    {
        NMMSS::PUDPSocket peer;
        unsigned short port = getPort(iface);
        while (0 != port)
        {
            peer.reset(new NMMSS::UDPSocket_t(NCorbaHelpers::GetReactorFromPool()));

            boost::asio::ip::udp::resolver resolver(m_reactor->GetIO());
            boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), iface,
                boost::lexical_cast<std::string>(port),
                boost::asio::ip::udp::resolver::query::address_configured);
            boost::asio::ip::udp::endpoint endpoint = *resolver.resolve(query);

            peer->sock().open(endpoint.protocol());
            boost::system::error_code ec;
            peer->sock().bind(endpoint, ec);
            if (!ec)
                break;

            port = getPort(iface);
        }
        return peer;
    }

private:
    unsigned short getPort(const std::string& iface)
    {
        unsigned short port = 0;
        {
            boost::mutex::scoped_lock lock(m_mutex);
            TIfacePorts::iterator it = m_ifacePorts.find(iface);
            if (m_ifacePorts.end() == it)
                it = m_ifacePorts.insert(m_ifacePorts.end(), std::make_pair(iface, m_portBase));

            if (it->second <= m_portMax)
            {
                port = it->second;
                ++m_ifacePorts[iface];
            }

        }
        return port;
    }

    NCorbaHelpers::PReactor m_reactor;

    unsigned short m_portBase;
    unsigned short m_portMax;

    boost::mutex m_mutex;

    typedef std::map<std::string, unsigned short> TIfacePorts;
    TIfacePorts m_ifacePorts;
};

namespace NMMSS
{

PTCPConnectionAcceptor GetTcpConnectionAcceptorInstance(NCorbaHelpers::PReactor reactor)
{
    static boost::mutex mutex;
    static boost::weak_ptr<CTcpConnectionAcceptor> weak;

    boost::mutex::scoped_lock lock(mutex);
    boost::shared_ptr<CTcpConnectionAcceptor> res = weak.lock();
    if(!res || (res->GetReactor() != reactor))
    {
        res.reset(new CTcpConnectionAcceptor(mutex));
        weak = res;
        res->ListenIpAddressChanges();
    }
    return res;
}

PUDPConnectionAcceptor GetUdpConnectionAcceptorInstance()
{
    static boost::mutex mutex;
    static boost::weak_ptr<CUdpConnectionAcceptor> weak;

    boost::mutex::scoped_lock lock(mutex);
    boost::shared_ptr<CUdpConnectionAcceptor> res = weak.lock();
    if (!res)
    {
        res.reset(new CUdpConnectionAcceptor);
        weak = res;
    }
    return res;
}

}
