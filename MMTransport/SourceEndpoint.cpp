#include <ace/OS.h>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/date_time/posix_time/time_parsers.hpp>

#include <tao/TransportCurrent/TC_IIOPC.h>
#include <tao/TransportCurrent/TCC.h>

#include <Logging/log2.h>
#include <CorbaHelpers/Container.h>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/ResolveServant.h>
#include <CorbaHelpers/RefcountedServant.h>
#include <CorbaHelpers/ListenEndpoints.h>
#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/Unicode.h>

#include <MMIDL/MMEndpointS.h>
#include <MMIDL/MMStorageS.h>
#include "../MMSS.h"
#include "../Sample.h"
#include "../ConnectionBroker.h"
#include "../Network/Network.h"
#include "../ProxyPinImpl.h"
#include "MMTransport.h"
#include "SourceFactory.h"
#include "QualityOfService.h"
#include "ConnectionAcceptor.h"

#include <unordered_map>

namespace
{
    class CProxySource
        : public virtual NMMSS::IQoSAwareSource
        , public NMMSS::CProxySourceImpl<NMMSS::IQoSAwareSource>
        , public NCorbaHelpers::CWeakReferableImpl
    {
        using Base = NMMSS::CProxySourceImpl<NMMSS::IQoSAwareSource>;

    public:
        CProxySource(std::function<void()> onDestroy)
            : Base( nullptr, NCorbaHelpers::ShareOwnership() )
            , m_onDestroy( onDestroy )
        {}
        ~CProxySource()
        {
            m_onDestroy();
        }
        void SetSource( NMMSS::IQoSAwareSource * src )
        {
            Base::setPin( src, NCorbaHelpers::ShareOwnership() );
            if( auto pin = Base::getPin() )
                pin->ModifyQoS( GetQoS() );
        }
        MMSS::QualityOfService GetQoS() const
        {
            boost::unique_lock<boost::mutex> lock (m_mutex);
            return std::move(m_qos);
        }
    public:
        void ModifyQoS( MMSS::QualityOfService const& qos ) override
        {
            if( auto pin = Base::getPin() )
                pin->ModifyQoS( qos );
            else
            {
                boost::unique_lock<boost::mutex> lock (m_mutex);
                m_qos = qos;
            }
        }
        void ReprocessQoS() override
        {
            if (auto pin = Base::getPin())
                pin->ReprocessQoS();
        }
    private:
        mutable boost::mutex m_mutex;
        MMSS::QualityOfService m_qos;
        std::function<void()> m_onDestroy;
    };
    using PProxySource = NCorbaHelpers::CAutoPtr<CProxySource>;
    using PWeakProxySource = NCorbaHelpers::CWeakPtr<CProxySource>;

    class CSourceEndpoint
        :   public NLogging::WithLogger
        ,   public virtual POA_MMSS::Endpoint
        ,   public virtual NCorbaHelpers::CWeakReferableServant
    {
    private:
        typedef NMMSS::ISourceFactory TSourceFactory;
        typedef NCorbaHelpers::CAutoPtr<TSourceFactory> PSourceFactory;

    public:
        CSourceEndpoint(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* container,
            TSourceFactory* sourceFactory,
            NMMSS::FSourceEndpointDtor onDestroy)
        :   NLogging::WithLogger(GET_LOGGER_PTR)
        ,   m_container(container)
        ,   m_orb(CORBA::ORB::_duplicate(container->GetORB()))
        ,   m_sourceFactory(sourceFactory, NCorbaHelpers::ShareOwnership())
        ,   m_onDestroy(onDestroy)
        ,   m_tcpConnectionAcceptor(NMMSS::GetTcpConnectionAcceptorInstance(NCorbaHelpers::GetReactorInstanceShared()))
        ,   m_udpConnectionAcceptor(NMMSS::GetUdpConnectionAcceptorInstance())
        ,   m_multicastDataPort(0)
        {
            TRACE_BLOCK;
        }

        virtual ~CSourceEndpoint()
        {
            TRACE_BLOCK;
            Destroy();
        }

    private:
        static std::string newCookie()
        {
            static const char *hexdigits="0123456789abcdef";
            std::string res;
            res.reserve(64);
            boost::uuids::uuid u(NCorbaHelpers::GenerateUUID());
            for(boost::uuids::uuid::iterator it=u.begin(); it!=u.end(); ++it)
            {
                res+=hexdigits[*it/16];
                res+=hexdigits[*it%16];
            }
            if(res.size() != size_t(MMSS::CONNECTION_COOKIE_LENGTH))
                throw std::logic_error("generated cookie size mismatches expected (declared) size");
            return res;
        }
        void fillAllocatorParams(MMSS::AllocatorParams& ap)
        {
            NMMSS::SAllocatorRequirements req = m_sourceFactory->GetFactoryAllocatorRequirements();
            NMMSS::IAllocatorFactory* f = req.allocatorFactory;
            ap.allocatorId = nullptr != f ? f->GetAllocatorID() : NMMSS::SAllocatorId();
            ap.bufferCount = req.nBufferCount;
            ap.bufferSize = req.nBufferSize;
            ap.alignment = req.nAlignment;
        }
        NMMSS::ITcpConnectionAcceptor::FHandler makeTcpConnectionHandler(
            PProxySource const& proxy, MMSS::QualityOfService const& qos, const MMSS::ConnectionInfo& ci )
        {
            return boost::bind(&CSourceEndpoint::connectTcpOutputChannel, 
                NCorbaHelpers::ShareRefcounted(this), proxy, qos, ci._d(), _1);
        }
        static NMMSS::FOnNetworkDisconnect makeDisconnectHandler(PProxySource proxy)
        {
            PWeakProxySource weak(proxy);
            return [weak] () { if (PProxySource proxy = weak) proxy->Disconnect(); };
        }

    private:

        void connectTcpOutputChannel(
            PProxySource const& proxy,
            MMSS::QualityOfService const& qos,
            MMSS::ENetworkTransport transportType,
            NMMSS::PTCPSocket sock)
        {
            if(!sock)
                return;

            NMMSS::PQoSAwareSource src( m_sourceFactory->CreateSource(qos) );

            if(!src)
                return; //??? at least close the socket

            NMMSS::PPullStyleSink sink;
            if(MMSS::ELOCAL==transportType)
            {
                sink = NMMSS::CreateLocalOutputChannel(GET_LOGGER_PTR, sock, m_sourceFactory->GetFactoryAllocatorRequirements().allocatorFactory, makeDisconnectHandler(proxy));
            }
            else if(MMSS::ETCP==transportType)
            {
                sink = NMMSS::CreatePullTcpOutputChannel(GET_LOGGER_PTR, sock, makeDisconnectHandler(proxy));
            }
            else
                throw std::logic_error("CSourceEndpoint::connectTcpOutputChannel(): unsupported network transport type");

            if(!sink)
                return; //??? at least close the socket

            proxy->SetSource( src.Get() );
            NMMSS::GetConnectionBroker()->SetConnection(sink.Get(), proxy.Get(), GET_LOGGER_PTR);
        }
        void connectUdpOutputChannel( PProxySource const& proxy
            , MMSS::QualityOfService const& qos
            , MMSS::ENetworkTransport transportType
            , NMMSS::PUDPSocket controlPeer
            , NMMSS::PUDPSocket dataPeer)
        {
            NMMSS::PQoSAwareSource src( m_sourceFactory->CreateSource(qos) );
            if(!src)
                return; //??? at least close the socket

            NMMSS::PPullStyleSink sink;
            if (MMSS::EUDP == transportType)
            {
                sink = NMMSS::CreateUdpOutputChannel(GET_LOGGER_PTR, controlPeer, dataPeer, makeDisconnectHandler(proxy));
            }
            else
                throw std::logic_error("CSourceEndpoint::connectUdpOutputChannel(): unsupported network transport type");

            if (!sink)
                return; //??? at least close the socket

            proxy->SetSource( src.Get() );
            NMMSS::GetConnectionBroker()->SetConnection(sink.Get(), proxy.Get(), GET_LOGGER_PTR);
        }
        void connectUdpMulticastOutputChannel( PProxySource const& proxy
            , MMSS::QualityOfService const& qos
            , const MMSS::ConnectionInfo& ci
            , NMMSS::PUDPSocket peer)
        {
            NMMSS::PQoSAwareSource src( m_sourceFactory->CreateSource(qos) );
            if(!src)
                return; //??? at least close the socket

            NMMSS::PPullStyleSink sink;
            if (MMSS::EMULTICAST == ci._d())
            {
                sink = NMMSS::CreateUdpMulticastOutputChannel(GET_LOGGER_PTR, peer, ci.mcast().dataIface.in(), ci.mcast().dataPort, makeDisconnectHandler(proxy));
            }
            else
                throw std::logic_error("CSourceEndpoint::connectUdpMulticastOutputChannel(): unsupported network transport type");

            if (!sink)
                return; //??? at least close the socket

            proxy->SetSource( src.Get() );
            NMMSS::GetConnectionBroker()->SetConnection(sink.Get(), proxy.Get(), GET_LOGGER_PTR);
        }
    public:
        void RequestConnection(CORBA::ULong processId, 
            const char* hostId, const MMSS::NetworkTransportSeq& sinkPrefs,
            ::CORBA::Boolean useAllAdresses,
            MMSS::QualityOfService const& qos,
            MMSS::ConnectionInfo_out connInfo, CORBA::String_out cookieOut)
        {
            std::set<MMSS::ENetworkTransport> allowed;
            allowed.insert(MMSS::ETCP);
            allowed.insert(MMSS::EUDP);
            allowed.insert(MMSS::EMULTICAST);

            bool isLocalRequest = (NCorbaHelpers::ToUtf8(NCorbaHelpers::GetLocalHostW()) == hostId);
            if (isLocalRequest)
            {
                NMMSS::PAllocatorFactory allocatorFactory(m_sourceFactory->GetFactoryAllocatorRequirements().allocatorFactory, NCorbaHelpers::ShareOwnership());
                if (! (allocatorFactory && allocatorFactory->SupportedSharedMemory() == NMMSS::ESM_NONE) )
                    allowed.insert(MMSS::ELOCAL);
                if(CORBA::ULong(ACE_OS::getpid()) == processId)
                    allowed.insert(MMSS::EINPROC);
            }
            CORBA::ULong chosen;
            for (chosen = 0; chosen < sinkPrefs.length(); ++chosen)
                if (allowed.find(sinkPrefs[chosen]) != allowed.end())
                    break;
            connInfo = new MMSS::ConnectionInfo;
            if (chosen >= sinkPrefs.length())
            {
                cookieOut = CORBA::string_dup("");
                connInfo->_d(MMSS::EINPROC);
                connInfo->inproc().pullSourcePtr = 0;
                return;
            }
            std::string cookie(newCookie());
            cookieOut = CORBA::string_dup(cookie.c_str());

            auto proxy = NCorbaHelpers::MakeRefcounted<CProxySource>(
                boost::bind(&CSourceEndpoint::onProxyDestroy, NCorbaHelpers::CWeakPtr<CSourceEndpoint>(this), cookie));
            proxy->ModifyQoS(qos);
            {
                boost::unique_lock<boost::mutex> cookieLock(m_cookieMutex);
                m_cookieMap[cookie] = PWeakProxySource(proxy);
            }
            if (MMSS::EINPROC == sinkPrefs[chosen])
            {
                auto src = NCorbaHelpers::TakeRefcounted(m_sourceFactory->CreateSource(qos));
                proxy->SetSource( src.Get() );
                MMSS::InprocConnectionInfo inproc;
                inproc.pullSourcePtr=(CORBA::ULongLong)(NMMSS::IPullStyleSource*)(proxy.Dup());
                connInfo->inproc(inproc);
                return;
            }
            else if (MMSS::EUDP == sinkPrefs[chosen])
            {
                CORBA::String_var iiopIface(getIiopLocalAddress(NCorbaHelpers::GetLocalHost().c_str()));

                NMMSS::PUDPSocket controlPeer = m_udpConnectionAcceptor->CreateUdpSocket(iiopIface.in());
                NMMSS::PUDPSocket dataPeer = m_udpConnectionAcceptor->CreateUdpSocket(iiopIface.in());

                if ((0 == controlPeer.get()) || (0 == dataPeer.get()))
                {
                    _err_ << "Failed to create udp socket";
                    throw MMSS::Endpoint::InvalidState();
                }

                MMSS::UdpConnectionInfo udp;
                udp.address = iiopIface;
                udp.controlPort = controlPeer->sock().local_endpoint().port();
                udp.dataPort = dataPeer->sock().local_endpoint().port();
                connInfo->udp(udp);

                _log_ << "Create UDP connection " << udp.address.in() << ":" << udp.controlPort << "&" << udp.dataPort;

                connectUdpOutputChannel(proxy, qos, connInfo->_d(), controlPeer, dataPeer);
                return;
            }
            else if (MMSS::EMULTICAST == sinkPrefs[chosen])
            {
                if (m_multicastDataIface.empty())
                {
                    std::pair<std::string, uint16_t> mcastAddress = generateMulticastAddress();
                    if (0 == mcastAddress.second)
                    {
                        _err_ << "Multicast address generation failed";
                        throw MMSS::Endpoint::InvalidState();
                    }

                    _log_ << "Generated multicast address: " << mcastAddress.first << ":" << mcastAddress.second;

                    boost::mutex::scoped_lock lock(m_mcastMutex);
                    if (m_multicastDataIface.empty())
                    {         
                        m_multicastControlIface = getIiopLocalAddress(NCorbaHelpers::GetLocalHost().c_str());
                        NMMSS::PUDPSocket controlPeer = m_udpConnectionAcceptor->CreateUdpSocket(m_multicastControlIface.in());

                        if (0 == controlPeer.get())
                        {
                            _err_ << "Failed to create udp socket";
                            throw MMSS::Endpoint::InvalidState();
                        }

                        m_multicastDataPort = mcastAddress.second;
                        m_multicastDataIface = mcastAddress.first;

                        m_multicastData = new MMSS::MulticastConnectionInfo;
                        m_multicastData->controlIface = m_multicastControlIface;
                        m_multicastData->controlPort = controlPeer->sock().local_endpoint().port();
                        m_multicastData->dataIface = CORBA::string_dup(m_multicastDataIface.c_str());
                        m_multicastData->dataPort = m_multicastDataPort;

                        connInfo->mcast(m_multicastData);

                        connectUdpMulticastOutputChannel(proxy, qos, *connInfo, controlPeer);
                    }
                }
                else
                    connInfo->mcast(m_multicastData);
                
                return;
            }
            else if(MMSS::ELOCAL==sinkPrefs[chosen])
            {
                MMSS::LocalConnectionInfo local;
                local.port=m_tcpConnectionAcceptor->GetPort();
                fillAllocatorParams(local.allocator);
                connInfo->local_(local);
            }
            else if(MMSS::ETCP==sinkPrefs[chosen])
            {
                const std::string LOOPBACK_IP("127.0.0.1");

                MMSS::TcpConnectionInfo tcp;
                tcp.port = m_tcpConnectionAcceptor->GetPort();

                tcp.addresses.length(1);
                tcp.addresses[0] = getIiopLocalAddress(NCorbaHelpers::GetLocalHost().c_str());
                if (isLocalRequest)
                {
                    tcp.addresses.length(2); 
                    tcp.addresses[1] = CORBA::string_dup(LOOPBACK_IP.c_str());
                }
                else
                {
                    NCorbaHelpers::TInterfaceList upInterfaces;
                    NCorbaHelpers::EnumInterfaces(NCorbaHelpers::CEnvar::NgpIfaceWhitelist(), upInterfaces);

                    std::string local(tcp.addresses[0].in());
                    auto localInterfaceIsNotAlive = std::none_of(upInterfaces.begin(), upInterfaces.end(),
                        [local, LOOPBACK_IP](const NCorbaHelpers::TInterface& i) { return i.IpAddress == local || i.IpAddress == LOOPBACK_IP; });
                    if (useAllAdresses || localInterfaceIsNotAlive)
                    {
                        std::size_t i = 0;
                        tcp.addresses.length(upInterfaces.size());
                        for (const auto& iface : upInterfaces)
                        {
                            _dbg_ << "Replace tcp address from: " << local << " to " << iface.IpAddress;
                            tcp.addresses[i++] = CORBA::string_dup(iface.IpAddress.c_str());
                        }
                    }
                }

                std::vector<std::string> altAddresses(NCorbaHelpers::CEnvar::NgpAltAddr());
                std::size_t i = tcp.addresses.length();
                tcp.addresses.length(altAddresses.size() + tcp.addresses.length());
                for (const auto& iface : altAddresses)
                    tcp.addresses[i++] = CORBA::string_dup(iface.c_str());

                connInfo->tcp(tcp);	
            }
            else
            {
                _err_ << "Unknown connection type";
                throw MMSS::Endpoint::InvalidState();
            }

            m_tcpConnectionAcceptor->Register(cookie,
                makeTcpConnectionHandler(proxy, qos, *connInfo),
                std::chrono::seconds(60));
        }
        void RequestQoS(const char* cookie, MMSS::QualityOfService const& qos)
        {
            boost::unique_lock<boost::mutex> cookieLock(m_cookieMutex);
            auto it = m_cookieMap.find(cookie);
            if (it != m_cookieMap.end())
            {
                NCorbaHelpers::CAutoPtr<CProxySource> proxy = it->second;
                cookieLock.unlock(); // Unlock to avoid deadlock on cookie mutex in OnProxyDestroy.
                if (proxy)
                    proxy->ModifyQoS(qos);
                return;
            }
            _err_ << "Unknown cookie value: " << cookie;
            throw MMSS::Endpoint::InvalidState();
        }

        MMSS::EndpointStatistics GetStatistics() /*throw (CORBA::SystemException)*/
        {
            MMSS::EndpointStatistics stats = { 0 };

            const NMMSS::IStatisticsProvider* statisticsProvider = m_sourceFactory->GetStatisticsProvider();
            if (!statisticsProvider)
            {
                _inf_ << "IStatisticsProvider is not supported";
                return stats;
            }
            
            const NMMSS::IStatisticsCollector* statisticsCollector = statisticsProvider->GetStatisticsCollector();
            if (!statisticsCollector)
            {
                _inf_ << "IStatisticsCollector doesn't exist";
                return stats;
            }

            const NMMSS::StatisticsInfo info = statisticsCollector->GetStatistics();

            stats.bitrate = info.bitrate;
            stats.fps = info.fps;
            stats.width = info.width;
            stats.height = info.height;
            stats.mediaType = info.mediaType;
            stats.streamType = info.streamType;

            return stats;
        }

    protected:
        NCorbaHelpers::WPContainer m_container;
        CORBA::ORB_var m_orb;
        PSourceFactory m_sourceFactory;
        NMMSS::FSourceEndpointDtor m_onDestroy;
        boost::mutex m_cookieMutex;
        using CookieMap = std::unordered_map<std::string, PWeakProxySource>;
        CookieMap m_cookieMap;

        NMMSS::PTCPConnectionAcceptor m_tcpConnectionAcceptor;
        NMMSS::PUDPConnectionAcceptor m_udpConnectionAcceptor;

        boost::mutex m_mcastMutex;
        uint16_t m_multicastDataPort;
        std::string m_multicastDataIface;
        MMSS::MulticastConnectionInfo_var m_multicastData;
        CORBA::String_var m_multicastControlIface;

    private:
        CORBA::String_var getIiopLocalAddress(const char* defaultAddress)
        {
            try
            {
                CORBA::Object_var o = m_orb->resolve_initial_references("TAO::Transport::IIOP::Current");
                if(CORBA::is_nil(o))
                {
                    _err_ << "CSourceEndpoint::getIiopLocalAddress(): TAO::Transport::IIOP::Current not available; are we within a CORBA call?";
                    return defaultAddress;
                }
                TAO::Transport::IIOP::Current_var tc = TAO::Transport::IIOP::Current::_narrow(o);
                if(CORBA::is_nil(tc))
                {
                    _err_ << "CSourceEndpoint::getIiopLocalAddress(): narrow to TAO::Transport::IIOP failed";
                    return defaultAddress;
                }
                CORBA::String_var local(tc->local_host());
                _log_ << "CSourceEndpoint::getIiopLocalAddress(): local address is " << local.in();
                return local._retn();
            }
            catch(const CORBA::Exception& e)
            {
                _err_ << "CSourceEndpoint::getIiopLocalAddress(): could not determine peer address: " << e._info().c_str();
            }
            return CORBA::string_dup(defaultAddress);
        }

        std::pair<std::string, uint16_t> generateMulticastAddress()
        {
            const uint8_t MULTICAST_BYTE = 235;

#if defined(__linux__)
            // Most default Linux configurations use the following range.
            // The proper way to check this is to parse /proc/sys/net/ipv4/ip_local_port_range
            const uint16_t EPHEMERAL_PORT_RANGE_BEGIN = 32768;
            const uint16_t EPHEMERAL_PORT_RANGE_END = 60999;
#else
            // Use IANA recommended range otherwise
            const uint16_t EPHEMERAL_PORT_RANGE_BEGIN = 42152;
            const uint16_t EPHEMERAL_PORT_RANGE_END = 65535;
#endif
            static_assert(EPHEMERAL_PORT_RANGE_END >= EPHEMERAL_PORT_RANGE_BEGIN, "ephemeral port range is incorrect");

            // range is inclusive, i.e. [begin; end]
            const uint16_t EPHEMERAL_PORT_RANGE_SIZE = EPHEMERAL_PORT_RANGE_END - EPHEMERAL_PORT_RANGE_BEGIN + 1;

            boost::uuids::uuid randbytes = NCorbaHelpers::GenerateUUID();
            static_assert(sizeof(randbytes.data) >= 4 + 2, "data types size mismatch");

            // use bytes 0-3 of randbytes for ipv4 address, 4-5 for port
            boost::asio::ip::address_v4::bytes_type addr = {{
                MULTICAST_BYTE, randbytes.data[1], randbytes.data[2], randbytes.data[3]
            }};

            uint16_t port = (static_cast<uint16_t>(randbytes.data[4]) << 8) + randbytes.data[5];
            port %= EPHEMERAL_PORT_RANGE_SIZE;
            port += EPHEMERAL_PORT_RANGE_BEGIN;

            return std::make_pair(boost::asio::ip::address_v4(addr).to_string(), port);
        }

        static void onProxyDestroy(NCorbaHelpers::CWeakPtr<CSourceEndpoint> endpoint, const std::string& cookie)
        {
            NCorbaHelpers::CAutoPtr<CSourceEndpoint> ep (endpoint.Dup());
            if( ep == nullptr )
                return;

            ep->OnProxyDestroy(cookie);
        }

    protected:

        virtual void OnProxyDestroy(const std::string& cookie)
        {
            boost::unique_lock<boost::mutex> cookieLock(m_cookieMutex);
            m_cookieMap.erase(cookie);
            if (m_cookieMap.empty())
            {
                cookieLock.unlock();
                Destroy();
            }
        }

        void Destroy()
        {
            CookieMap cookies;
            {
                boost::unique_lock<boost::mutex> cookieLock(m_cookieMutex);
                std::swap(m_cookieMap, cookies);
            }

            for (auto& c : cookies)
            {
                if (PProxySource proxy = c.second)
                    proxy->Disconnect();
            }

            try
            {
                NMMSS::FSourceEndpointDtor onDestroy = m_onDestroy;
                m_onDestroy.clear();
                if (!onDestroy.empty())
                    onDestroy(this);
            }
            catch (const CORBA::Exception& ex)
            {
                _err_ << __FUNCTION__ << ". " << ex._name();
            }
        }

        bool IsUsing()
        {
            boost::unique_lock<boost::mutex> cookieLock(m_cookieMutex);
            return !m_cookieMap.empty();
        }
    };

    class CTransientSourceEndpoint : public CSourceEndpoint, public virtual POA_MMSS::Endpoint
    {
        NCorbaHelpers::PReactor     m_reactor;
        boost::asio::deadline_timer m_timer;

        typedef NCorbaHelpers::CAutoPtr<CTransientSourceEndpoint> PTransientSourceEndpoint;
        typedef NCorbaHelpers::CWeakPtr<CTransientSourceEndpoint> PWeakTransientSourceEndpoint;

    public:

        CTransientSourceEndpoint(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* container,
            NMMSS::ISourceFactory* sourceFactory,
            NMMSS::FSourceEndpointDtor onDestroy)
            : CSourceEndpoint(GET_LOGGER_PTR, container, sourceFactory, onDestroy)
            , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_timer(m_reactor->GetIO())
        {
            long WAINING_REQUEST_CONNECTION = 60;
            m_timer.expires_from_now(boost::posix_time::seconds(WAINING_REQUEST_CONNECTION));
            m_timer.async_wait(boost::bind(&CTransientSourceEndpoint::OnTimeout, PWeakTransientSourceEndpoint(this), boost::asio::placeholders::error));
        }

        virtual ~CTransientSourceEndpoint()
        {
            boost::system::error_code error;
            m_timer.cancel(error);
        }

    protected:

        void Wrn(const std::string& msg)
        {
            _wrn_ << msg;
        }

        static void OnTimeout(PWeakTransientSourceEndpoint weak, const boost::system::error_code& error)
        {
            if (error != boost::asio::error::operation_aborted)
            {
                if (PTransientSourceEndpoint ptr = weak)
                {
                    if (!ptr->IsUsing())
                    {
                        ptr->Destroy();
                        if (!error)
                            ptr->Wrn("Source endpoint was destroyed on timeout because unused");
                        else
                            ptr->Wrn("Source endpoint was destroyed on timer error: " + error.message());
                    }
                }
            }
        }
    };

    class CSeekableSourceEndpoint : public CTransientSourceEndpoint, public virtual POA_MMSS::StorageEndpoint
    {
        NMMSS::WSeekableSource m_sourceImpl;

    public:

        CSeekableSourceEndpoint(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* container,
            NMMSS::ISourceFactory* sourceFactory,
            NMMSS::ISeekableSource* pSourceImpl,
            NMMSS::FSourceEndpointDtor onDestroy)
            : CTransientSourceEndpoint(GET_LOGGER_PTR, container, sourceFactory, onDestroy)
            , m_sourceImpl(pSourceImpl)
        {}

        void Seek(const char* time, MMSS::EStartPosition startPos, CORBA::Long mode, CORBA::ULong sessionId)
        {
            NMMSS::PSeekableSource sourceImpl = m_sourceImpl;
            if (!sourceImpl)
                throw MMSS::Endpoint::InvalidState();

            boost::posix_time::ptime ptime;
            try
            {
                ptime = boost::posix_time::from_iso_string(time);
            }
            catch(...)
            {
                throw CORBA::BAD_PARAM();
            }

            sourceImpl->Seek(ptime, Convert(startPos), (NMMSS::EPlayModeFlags)mode, sessionId);
        }

    protected:

        NMMSS::EEndpointStartPosition Convert(MMSS::EStartPosition sp)
        {
            switch (sp)
            {
            case MMSS::spAtKeyFrame:        return NMMSS::espAtKeyFrame;
            case MMSS::spExactly:           return NMMSS::espExactly;
            case MMSS::spOneFrameBack:      return NMMSS::espOneFrameBack;
            case MMSS::spNearestKeyFrame:   return NMMSS::espNearestKeyFrame;
            case MMSS::spAtKeyFrameOrAtEos: return NMMSS::espAtKeyFrameOrAtEos;
            case MMSS::spStrict:            return NMMSS::espStrict;
            default:
                assert(false);
                return NMMSS::espAtKeyFrame;
            }
        }
    };
}

namespace NMMSS
{
    PortableServer::Servant CreatePullSourceEndpoint(DECLARE_LOGGER_ARG,
        NCorbaHelpers::IContainer* container,
        ISourceFactory* sourceFactory,
        FSourceEndpointDtor onDestroy)
    {
        return onDestroy.empty()
            ? new CSourceEndpoint(GET_LOGGER_PTR, container, sourceFactory, onDestroy)
            : new CTransientSourceEndpoint(GET_LOGGER_PTR, container, sourceFactory, onDestroy);
    }

    PortableServer::Servant CreateSeekableSourceEndpoint(DECLARE_LOGGER_ARG,
        NCorbaHelpers::IContainer* container,
        ISeekableSource* pSourceImpl,
        FSourceEndpointDtor onDestroy)
    {
        NMMSS::PSourceFactory srcFactory(NMMSS::CreateDisposablePullStyleSourceFactory(GET_LOGGER_PTR, pSourceImpl));
        return new CSeekableSourceEndpoint(GET_LOGGER_PTR, container, srcFactory.Get(), pSourceImpl, onDestroy);
    }
}
