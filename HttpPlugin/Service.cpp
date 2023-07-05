#include <ace/OS.h>
#include <set>
#include <mutex>
#include <memory>
#include <thread>
#include <condition_variable>

#include <boost/filesystem.hpp>

#include <Lifecycle/ImplementModule.h>
#include <Lifecycle/ImplementApp.h>
#include <CorbaHelpers/Resource.h>
#include <CorbaHelpers/Container.h>
#include <CorbaHelpers/ObjectName.h>
#include <CorbaHelpers/Envar.h>
#include <CorbaHelpers/ListenEndpoints.h>
#include <ConsulUtils/NgpDiscovery.h>
#include <HttpServer/HttpServer.h>
#include <CorbaHelpers/Reactor.h>
#include <Vendor/ngpVersion.h>
#include "HttpPlugin.h"
#include "TrustedIpList.h"
#include "RightsChecker.h"
#include <CorbaHelpers/Timer.h>
#include "WebServerConfig.h"

#include <SecurityManager/client/SecurityManagerClient.h>

#include "Constants.h"
#include "Gstreamer.h"
#include "GrpcHelpers.h"
#include "MMCache.h"
//#include "GrpcWebProxyGoManager.h"
#include "StatisticsCache.h"
#include "ONVIFServer.h"

#include "HttpGrpcPlugin.h"

namespace
{

const char *HOST = "0.0.0.0";
const char *VAR_STATIC_CONTENT = "WEB_STATIC_CONTENT_PATH";
const char *VAR_HLS_CONTENT = "WEB_HLS_CONTENT_PATH";
const char *VAR_EXPORT_CONTENT = "WEB_EXPORT_CONTENT_PATH";

const uint8_t ALLOWED_LOGIN_TRY_COUNT = 5;
const uint8_t BLACK_LIST_TIME = 10;
const bool INSECURE_TOKEN = true;

struct SessionInfo
{
    typedef NHttp::IRequest::PAuthSessionData PAuthSessionData;
    
    SessionInfo() = default;
    
    explicit SessionInfo(PAuthSessionData session_) :
        session(session_),
        lastUpdateTime(boost::posix_time::second_clock::local_time())
    {
    }

    PAuthSessionData session;
    boost::posix_time::ptime lastUpdateTime;

    bool isExpired() const
    {
        const int TTL_SECONDS = 10;
        return (boost::posix_time::second_clock::local_time() - lastUpdateTime).total_seconds() > TTL_SECONDS;
    }
};

struct SUserWithIp
{
    SUserWithIp(const std::wstring& ip, const std::wstring& user) : m_ip(ip), m_user(user) {}

    std::wstring m_ip;
    std::wstring m_user;
};

inline bool operator<(const SUserWithIp& lhs, const SUserWithIp& rhs)
{
    return std::tie(lhs.m_ip, lhs.m_user) < std::tie(rhs.m_ip, rhs.m_user);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CHttpServer : public NCorbaHelpers::IResource
    {
        typedef std::function<void()> TBlackEntryCallback;

        struct SBlackEntry : public std::enable_shared_from_this<SBlackEntry>
        {
            DECLARE_LOGGER_HOLDER;
            SBlackEntry(DECLARE_LOGGER_ARG, boost::asio::io_service& io, TBlackEntryCallback cb)
                : m_loginCount(0)
                , m_cb(cb)
                , m_timer(io)
                , m_timerIsSet(false)
            {
                INIT_LOGGER_HOLDER;
            }

            ~SBlackEntry()
            {
                {
                    std::unique_lock<std::mutex> lock(m_cbMutex);
                    m_cb = TBlackEntryCallback();
                }
                m_timer.cancel();
            }

            bool isBlocked()
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                bool blocked = ALLOWED_LOGIN_TRY_COUNT <= m_loginCount;
                if (blocked)
                {
                    if (!m_timerIsSet)
                    {
                        m_timer.expires_from_now(boost::posix_time::minutes(BLACK_LIST_TIME));
                        m_timer.async_wait(std::bind(&SBlackEntry::handle_timeout,
                            std::weak_ptr<SBlackEntry>(shared_from_this()), std::placeholders::_1));
                        m_timerIsSet = true;
                    }
                }
                return blocked;
            }

            void IncrementTryCount()
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                ++m_loginCount;
            }

        private:
            static void handle_timeout(std::weak_ptr<SBlackEntry> wbe, const boost::system::error_code& error)
            {
                if (!error)
                {
                    PBlackEntry be = wbe.lock();
                    if (be)
                        be->releaseUser();
                }
            }

            void releaseUser()
            {
                std::unique_lock<std::mutex> lock(m_cbMutex);
                if (m_cb)
                    m_cb();
            }

            std::mutex m_mutex;
            std::uint32_t m_loginCount;

            std::mutex m_cbMutex;
            TBlackEntryCallback m_cb;

            boost::asio::deadline_timer m_timer;

            bool m_timerIsSet;
        };
        typedef std::shared_ptr<SBlackEntry> PBlackEntry;

    public:
        CHttpServer(const char *objId, NCorbaHelpers::IContainerNamed *cont, std::istream &config)
        : m_container(cont)
        , m_mmCache(NHttp::CreateMMCache(cont))
        , m_videoCache(NHttp::GetVideoSourceCache(cont, m_mmCache))
        , m_statisticsCache(NHttp::CreateStatisticsCache(cont))
        , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(cont);

            try
            {
                m_staticContent = GetPath(objId, VAR_STATIC_CONTENT);

                m_hlsContent = GetPath(objId, VAR_HLS_CONTENT);
                if (m_hlsContent.empty())
                {
                    boost::filesystem::path content(m_staticContent);
                    content /= "hls";
                    m_hlsContent = content.string();
                }

                std::string rootPath;
                if (!NCorbaHelpers::CEnvar::Lookup(VAR_EXPORT_CONTENT, rootPath))
                    throw std::runtime_error(std::string("Couldn't find the environment variable: ") + VAR_EXPORT_CONTENT);
                boost::filesystem::path exportContentPath(rootPath);
                if (!exists(exportContentPath))
                    throw std::runtime_error(std::string("Output directory for export functionality is absent: ") + rootPath);

                // Create subdirectories
                const auto nodeName = NCorbaHelpers::CEnvar::NgpNodeName();
                if (!boost::contains(rootPath, nodeName))
                    exportContentPath /= nodeName;
                exportContentPath /= "WebServer";
                boost::filesystem::create_directories(exportContentPath);

                // Clear forgotten exports
                for (boost::filesystem::directory_iterator end_dir_it, it(exportContentPath); it != end_dir_it; ++it) {
                    boost::filesystem::remove_all(it->path());
                }

                m_exportContent = exportContentPath.string();

                m_config.Parse(config, GET_LOGGER_PTR);
                m_grpcManager = NWebGrpc::CreateGrpcManager(GET_LOGGER_PTR);
                m_grpcRegistrator = NWebGrpc::CreateGrpcRegistrator(GET_LOGGER_PTR);
                m_rightsChecker = NPluginUtility::CreateRightsChecker(GET_LOGGER_PTR, m_grpcManager);
                //m_grpcwebproxyManager = NPluginHelpers::CreateGrpcWebManager(GET_LOGGER_PTR, NCorbaHelpers::GetReactorInstanceShared()->GetIO(), cont);

                auto ONVIFEndpont = NCorbaHelpers::CEnvar::ONVIFServerEndpoint();

                if (!ONVIFEndpont.empty())
                    m_ONVIFServer = std::make_unique<NONVIFServer::CONVIFServerImpl>(ONVIFEndpont, m_config.RtspPort, cont, m_reactor->GetIO(), 
                        boost::bind(&CHttpServer::Authorized, this, _1, _2, _3));

                Create(objId, cont);

                Start();
                m_worker = std::thread(&NHttp::IHttpServer::Run, m_server.get());
            }
            catch(const std::exception &e)
            {
                _err_ << "An initialization error: " << e.what();
                throw;
            }
        }

        virtual ~CHttpServer()
        {
            Stop();
        }

    private:
        void Create(const std::string &objectId, NCorbaHelpers::IContainerNamed *cont)
            /*throw(std::exception)*/
        {

            using namespace NHttp;
            m_server.reset(CreateHttpServer(GET_LOGGER_PTR, HOST, m_config.Port, m_config.SslPort, m_config.CertificateFile, m_config.PrivateKeyFile, m_config.EnableCORS));
            auto tokenAuthentificator = std::make_shared<TokenAuthenticator>();
            auto rtspUrlBuilder = std::make_shared<UrlBuilder>(m_config.RtspPort, std::stoi(m_config.Port), tokenAuthentificator);

            TDirectoryIndex indeces;
            indeces.push_back("index.html");
            indeces.push_back("index.htm");

            m_server->Install("",                      CreateRedirectionServlet("/"));
            m_server->Install("/",                     CreateStaticContentServlet(m_staticContent, SM_Read, indeces));
            m_server->Install("/hls",                  CreateStaticContentServlet(m_hlsContent, SM_Read, indeces));
            m_server->Install("/product",              CreateCommonServlet(cont, m_grpcManager));
            m_server->Install("/hosts",                CreateHostServlet(cont));
            m_server->Install("/live/media",           CreateVideoServlet(cont, m_grpcManager, m_rightsChecker, m_hlsContent, rtspUrlBuilder, m_videoCache));
            m_server->Install("/live/media/snapshot",  CreateLiveSnapshotServlet(cont, m_rightsChecker));
            m_server->Install("/video-origins",        CreateVideoAccessPointServlet(GET_LOGGER_PTR, m_grpcManager));
            m_server->Install("/video-sources",        CreateVideoAccessPointServlet(GET_LOGGER_PTR, m_grpcManager));
            m_server->Install("/archive",              CreateArchiveServlet(cont, m_grpcManager, m_rightsChecker, m_hlsContent, rtspUrlBuilder, m_videoCache));
            m_server->Install("/uuid",                 CreateCommonServlet(cont, m_grpcManager));
            m_server->Install("/languages",            CreateCommonServlet(cont, m_grpcManager));
            m_server->Install("/logout",               CreateCommonServlet(cont, m_grpcManager));
            m_server->Install("/control/telemetry",    CreateTelemetryServlet(cont, m_grpcManager, m_rightsChecker));
            m_server->Install("/archive/events",       CreateEventServlet(GET_LOGGER_PTR, m_grpcManager, m_rightsChecker));
            m_server->Install("/export",               CreateExportServlet(cont, m_exportContent, m_grpcManager, m_rightsChecker));
            m_server->Install("/search/auto",          CreateAutoSearchServlet(cont, m_grpcManager));
            m_server->Install("/search/face",          CreateFaceSearchServlet(cont));
            m_server->Install("/search/vmda",          CreateVmdaSearchServlet(cont, m_grpcManager));
            m_server->Install("/search/stranger",      CreateStrangerSearchServlet(cont));
            m_server->Install("/faceAppearanceRate",   CreateFarStatusServlet(cont));
            m_server->Install("/group",                CreateGroupServlet(GET_LOGGER_PTR, m_grpcManager));
            m_server->Install("/macro",                CreateMacroServlet(cont, m_grpcManager));
            m_server->Install("/archive/list",         CreateArchiveListServlet(cont, m_grpcManager));
            m_server->Install("/archive/health",       CreateArchiveHealthServlet(cont, m_grpcManager));
            m_server->Install("/archive/calendar",     CreateArchiveCalendarServlet(GET_LOGGER_PTR, m_grpcManager));
            m_server->Install("/notifications", CreateCloudServlet(cont, objectId.c_str(), m_grpcManager));
            m_server->Install("/audit",                CreateAuditServlet(cont, m_rightsChecker));
            m_server->Install("/detectors",            CreateDetectorServlet(cont, m_grpcManager));
            m_server->Install("/ws",                   CreateWebSocketServlet(cont, rtspUrlBuilder, m_grpcManager, m_rightsChecker,
                                                                     m_videoCache, m_statisticsCache, m_config));
            m_server->Install("/layouts",              CreateLayoutServlet(cont, m_rightsChecker));
            m_server->Install("/camera",               CreateCameraListServlet(GET_LOGGER_PTR, m_grpcManager));
            m_server->Install("/camera/discovery",     CreateDiscoverCamerasServlet(cont, m_grpcManager));
            m_server->Install("/currentuser",          CreateCommonServlet(cont, m_grpcManager));
            m_server->Install("/ports",                CreateCommonServlet(cont, m_grpcManager));
            m_server->Install("/events",               CreateCameraEventServlet(GET_LOGGER_PTR, m_grpcManager, rtspUrlBuilder));
            m_server->Install("/search/heatmap",       CreateHeatMapSearchServlet(cont, m_grpcManager));
            m_server->Install("/grpc",                 CreateGrpcServlet(GET_LOGGER_PTR, m_grpcManager, m_grpcRegistrator));

            InstallArchiveService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallArchiveVolumeService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallAuditEventInjector(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallAuthenticationService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallBackupSourceService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallCloudService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallConfigurationService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallConfigurationManager(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallDevicesCatalog(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallDynamicParametersService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallDiscoveryService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallDomainService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallNgpNodeService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallGroupManager(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallHeatMapService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallLayoutManager(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallLicenseService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallLogicService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallMapService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallNodeNotifier(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallDomainNotifier(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallSecurityService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallStateControlService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallStatisticService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallEventHistoryService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallTagAndTrackService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallTelemetryService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallTimeZoneManager(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallVMDAService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallVideowallService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallExportService(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallFileSystemBrowser(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallEMailNotifier(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);
            InstallGSMNotifier(GET_LOGGER_PTR, m_server.get(), m_grpcManager, m_grpcRegistrator);

            bool prefixUsed = !m_config.Prefix.empty();
            std::string root_path = "/" + m_config.Prefix;

            boost::shared_ptr<NHttp::IStatisticsHandler>
                sh(CreateStatisticsHandler(cont, m_grpcManager, m_statisticsCache));
            m_server->Install("/statistics", sh); // as a servlet

            // Install proxy servlet as interceptor and servant to handle request with out authentication.
            {
                boost::shared_ptr<NHttp::IProxyServlet> ps(CreateProxyServlet(GET_LOGGER_PTR, m_config.RtspPort, RTSP_PROXY_PATH));
                m_server->Install(RTSP_PROXY_PATH, ps, prefixUsed);
                m_server->Install(ps, prefixUsed);
            }

            m_server->SetPrefix(m_config.Prefix);

            PAccessChecker rightsInterceptor(NHttp::CreateRightsInterceptor(GET_LOGGER_PTR, m_grpcManager));
            m_server->Install(rightsInterceptor);
            if (prefixUsed)
            {
                root_path = std::string(root_path.begin(), std::unique(root_path.begin(), root_path.end(), [](char l, char r)
                                                            {
                                                                return ((l == r) && (l == '/'));
                                                            }));
            }

            PInterceptor basicAuthInterceptor(NHttp::CreateBasicAuthInterceptor(GET_LOGGER_PTR, NGPVersionInfo.ProductName, root_path,
                                            boost::bind(&CHttpServer::Authorized, this, _1, _2, _3)));

            PInterceptor bearerAuthInterceptor(NHttp::CreateBearerAuthInterceptor(GET_LOGGER_PTR, basicAuthInterceptor));

            PInterceptor tokenAuthInterceptor(CreateTokenAuthInterceptor(GET_LOGGER_PTR, tokenAuthentificator, bearerAuthInterceptor));
            PInterceptor ipTrustedListInterseptor(CreateTrustedIpListInterceptor(GET_LOGGER_PTR,  tokenAuthInterceptor, cont));

            m_server->Install(ipTrustedListInterseptor);

            m_server->Install(NHttp::PInterceptor(NHttp::CreateConnectionPolicyInterceptor(cont, objectId.c_str())));

            m_server->Install(sh); // as an interceptor

            m_gstManager = NPluginHelpers::CreateGstManager(GET_LOGGER_PTR);
            _log_ << "Run RTSP server on port " << m_config.RtspPort;
            _log_ << "Hardware_concurrency: " << NCorbaHelpers::CEnvar::NgpHardwareConcurrency();

            m_rtspServer = NPluginHelpers::CreateRTSPServer(GET_LOGGER_PTR, cont, m_config.RtspPort,
                m_grpcManager, tokenAuthentificator, boost::bind(&CHttpServer::Authorized, this, _1, _2, _3), m_mmCache); 

            m_server->Install("/rtsp/stat", CreateRtspStatServlet(GET_LOGGER_PTR, m_rtspServer));

            registerServiceDiscovery();
        }

        void Start()
            /*throw()*/
        {
            _log_ << "Listening: " << HOST << ":" << m_config.Port;
            _log_ << "Static content: [" << m_staticContent << "]";
            m_gstManager->Start();
            m_grpcManager->Start();
            m_rtspServer->Start();
            //m_grpcwebproxyManager->Start();
        }

        void Stop()
            /*throw()*/
        {
            try
            {
                {
                    std::unique_lock<std::mutex> listLock(m_blackListMutex);
                    m_blackList.clear();
                }

                //m_grpcwebproxyManager->Stop();

                m_rtspServer->Stop();

                m_server->Stop();
                m_worker.join();

                m_grpcManager->Stop();
                m_gstManager->Stop();
            }
            catch(const std::exception &) {}
        }

        static std::string GetPath(const std::string &objectName, const std::string& varName)
            /*throw(std::exception)*/
        {   
            std::string var;
            if (!NCorbaHelpers::CEnvar::Lookup(varName, var))
                throw std::runtime_error(std::string("Couldn't find the environment variable: ") + varName);

            const NCorbaHelpers::CObjectName name = NCorbaHelpers::CObjectName::FromString(objectName);

            boost::filesystem::path content(var);
            content /= name.GetObjectId();
            boost::filesystem::create_directories(content);
            return content.string();
        }

        NHttp::IRequest::PChangedAuthSessionData Authorized(const std::wstring& ip, const std::wstring& user, const std::wstring& pass)
        {
            typedef NHttp::IRequest::PAuthSessionData PAuthSessionData;
            typedef NHttp::IRequest::PChangedAuthSessionData PChangedAuthSessionData;

            if (user.empty())
            {
                _err_ << "Empty user is not allowed";
                return PChangedAuthSessionData();
            }

            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                return PChangedAuthSessionData();
            }

            PAuthSessionData sData;
            const std::wstring token = ip + L":" + user + L":" + pass;
            {
                std::lock_guard<std::mutex> l(m_sessionsGuard);
                auto it = m_sessionsCache.find(token);
                if (it != m_sessionsCache.end())
                {
                    sData = it->second.session;
                    if (!it->second.isExpired())
                    {
                        return PChangedAuthSessionData(sData, PAuthSessionData());
                    }
                }
            }

            SUserWithIp uip(ip, user);

            std::unique_lock<std::mutex> listLock(m_blackListMutex);
            TBlackList::iterator blit = m_blackList.find(uip);
            if (m_blackList.end() != blit && blit->second->isBlocked())
            {
                _wrn_ << "User " << NCorbaHelpers::ToUtf8(uip.m_user) << " from ip " << NCorbaHelpers::ToUtf8(uip.m_ip) << " is in the black list";
                return PChangedAuthSessionData();
            }
            listLock.unlock();

            int64_t passwordTimeLeft;
            auto session = std::make_shared<PAuthSessionData::element_type>();
            auto code = NSecurityManager::TryAuthenticate(cont.Get(),
                NLogging::s2ws(user), NLogging::s2ws(pass), false, *session, passwordTimeLeft);
            if (code != NSecurityManager::AuthenticateCode::Ok)
            {
                if (code == NSecurityManager::AuthenticateCode::WrongCredentials)
                {
                    listLock.lock();
                    if (m_blackList.end() == blit)
                    {
                        std::pair<TBlackList::iterator, bool> res = m_blackList.insert(std::make_pair(uip,
                            std::make_shared<SBlackEntry>(GET_LOGGER_PTR, m_reactor->GetIO(),
                            std::bind(&CHttpServer::processBlackListEntry, this, uip))));
                        blit = res.first;
                    }
                    blit->second->IncrementTryCount();
                    listLock.unlock();
                }

                return PChangedAuthSessionData();
            }
                
            std::lock_guard<std::mutex> l(m_sessionsGuard);
            m_sessionsCache[token] = SessionInfo(session);
            return PChangedAuthSessionData(session, sData);
        }

    private:
        void processBlackListEntry(SUserWithIp uip)
        {
            std::unique_lock<std::mutex> listLock(m_blackListMutex);
            m_blackList.erase(uip);
        }

        void registerServiceDiscovery()
        {
            const std::string SD_CONSUL = "consul";

            if (NCorbaHelpers::CEnvar::ServiceDiscoveryMode().find(SD_CONSUL) == std::string::npos)
                return;
            
            // Hardcoded tags:
            const std::string HTTP_TAG  = "HTTP";
            const std::string HTTPS_TAG = "HTTPS";
            const std::string NODE_TAG  = "node";
            const std::string RTSP_TAG  = "RTSP";
            const std::string RTSP_OVER_HTTP_TAG = "RTSP/HTTP";

            const std::string TAG_KV_SEPARATOR = "=";

            // Protocols:
            const std::string HTTP  = "http";
            const std::string HTTPS = "https";
            const std::string RTSP  = "rtsp";

            const std::string WEB_SERVICE = "ngp-http-api";

            using namespace NConsulUtils;
            auto emptyRegistrationHandler = [](const std::string&, const std::vector<std::string>&, bool, 
                 const std::unordered_map<std::string,std::string>&){};
            auto emptyConflictDetector = [](const std::set<ServiceRecord>& input)->std::set<ServiceRecord> { return input; };

            m_sdHelper = CreateNgpDiscoveryHelper(GET_LOGGER_PTR, WEB_SERVICE, NCorbaHelpers::CEnvar::ConsulUrl(),
                                                  emptyRegistrationHandler, emptyConflictDetector);

            // Resolve advertise address
            std::string baseAddress;
            NCorbaHelpers::TGiopEndpointsList endpoints;
            {
                NCorbaHelpers::BuildGiopEndpointsList(NCorbaHelpers::CEnvar::NgpPortBase(),
                                                      NCorbaHelpers::CEnvar::NgpIfaceWhitelist(),
                                                      endpoints);

                // Remove all loopback endpoints
                endpoints.erase(std::remove_if(endpoints.begin(), endpoints.end(),
                    [](const NCorbaHelpers::TIiop12Endpoint& item)
                {
                    return item.Interface.IsLoopback;
                }), endpoints.end());

                if (endpoints.empty())
                {
                    _err_ << "No network interfaces to register";
                    return;
                }

                baseAddress = endpoints[0].Interface.AdvertiseAddress;
            }

            auto getPortAsNumber = [](const std::string& port)->int
            {
                try         { return std::stoi(port); }
                catch (...) { return 0; }
            };
            int httpPort  = getPortAsNumber(m_config.Port);
            int httpsPort = getPortAsNumber(m_config.SslPort);

            std::string nodeTag = NODE_TAG + TAG_KV_SEPARATOR + NCorbaHelpers::CEnvar::NgpNodeName();
            std::vector<std::string> tags{ nodeTag };

            auto appendUrlTag = [&](const std::string& tag, const std::string& protocol, int port, const std::string& path)
            {
                if (port > 0)
                {
                    std::ostringstream stream;
                    bool empty = true;
                    for (const auto&e : endpoints)
                    {
                        stream
                            << (empty ? "" : ",")
                            << protocol + "://" + e.Interface.IpAddress + ":" + std::to_string(port) + path;
                        empty = false;
                    }

                    tags.push_back(tag + TAG_KV_SEPARATOR + stream.str());
                }
            };

            appendUrlTag(HTTP_TAG, HTTP, httpPort, m_config.Prefix);
            appendUrlTag(HTTPS_TAG, HTTPS, httpsPort, m_config.Prefix);
            appendUrlTag(RTSP_TAG, RTSP, m_config.RtspPort, std::string());
            appendUrlTag(RTSP_OVER_HTTP_TAG, HTTP, m_config.RtspOverHttpPort, std::string());

            m_sdHelper->RegisterService(baseAddress, httpPort, tags, nodeTag, std::unordered_map<std::string,std::string>{});
        }

        DECLARE_LOGGER_HOLDER;
        NCorbaHelpers::WPContainer m_container;
        NHttp::PMMCache m_mmCache;
        NHttp::PVideoSourceCache m_videoCache;
        NHttp::PStatisticsCache m_statisticsCache;
        NHttp::SConfig m_config;
        NPluginUtility::PRigthsChecker m_rightsChecker;

        std::string m_staticContent;
        std::string m_hlsContent;
        std::string m_exportContent;
        std::auto_ptr<NHttp::IHttpServer> m_server;
        NPluginHelpers::PGstManager m_gstManager;
        std::thread m_worker;
        NCorbaHelpers::PReactor m_reactor;

        typedef std::unordered_map<std::wstring, SessionInfo> sessionMapt_t;
        sessionMapt_t m_sessionsCache;
        std::mutex m_sessionsGuard;

        NPluginHelpers::PGstRTSPServer m_rtspServer;

        std::mutex m_blackListMutex;
        typedef std::map<SUserWithIp, PBlackEntry> TBlackList;
        TBlackList m_blackList;

        NWebGrpc::PGrpcManager m_grpcManager;
        NWebGrpc::PGrpcRegistrator m_grpcRegistrator;

        //NPluginHelpers::PGrpcWebProxyManager m_grpcwebproxyManager;

        NConsulUtils::PNGPServiceDiscoveryHelper m_sdHelper;
        std::unique_ptr<NONVIFServer::CONVIFServerImpl> m_ONVIFServer;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

REGISTER_APP_IMPL(HttpServer, (new CAppFactory0<CHttpServer, std::istream>()));
