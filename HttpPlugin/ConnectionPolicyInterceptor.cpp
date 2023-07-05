#ifndef CONNECTION_POLICY_INTERCEPTOR_H__
#define CONNECTION_POLICY_INTERCEPTOR_H__

#include <chrono>
#include <mutex>

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "HttpPlugin.h"
#include "Constants.h"

#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/ResolveServant.h>


#include <CommonNotificationCpp/Connector.h>

#include <InfraServer_IDL/ExecutionManagerC.h>
#include <InfraServer_IDL/EventTypeTraits.h>

namespace
{
    const uint32_t UPDATE_TIMEOUT = 15;
    const uint32_t LOAD_TIMEOUT = 5;
    const std::chrono::seconds TOKEN_TTL = std::chrono::seconds(120);

    const char* const COOKIE_FORMAT = "%s=%s; Path=/";

    const char* const LUCKY_TOKEN_COOKIE = "Lucky-Token";
    const char* const AGENT_NAME_COOKIE = "Agent-Name";

    const char* const EVENT_RECEIVER = "EventReceiver";
    const char* const STATUS_EVENT_RECEIVER = "StatusEventReceiver";

    const std::string SECURITY_MANAGER = "security/SecurityManager.0";

    boost::optional<std::string> Cookie(const NHttp::THttpHeaders& h)
    {
        return NHttp::GetHeader<std::string>(h, "Cookie");
    }

    NHttp::SHttpHeader SetCookie(const std::string& token)
    {
        std::ostringstream oss;
        oss << token;
        return NHttp::SHttpHeader("Set-Cookie", oss.str());
    }

    enum EClientType
    {
        EWEBCLIENT,
        EMOBILE
    };

    struct SConnectionInfo
    {
        EClientType connectionType;

        std::mutex mutex;
        std::uint32_t connectionCount;

        typedef std::map<std::string, std::chrono::system_clock::time_point> TCountTokens;
        TCountTokens tokens;

        DECLARE_LOGGER_HOLDER;

        SConnectionInfo(DECLARE_LOGGER_ARG, EClientType type, std::uint32_t count)
            : connectionType(type)
            , connectionCount(count)
        {
            INIT_LOGGER_HOLDER;
        }

        std::string AcquireConnection()
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (connectionCount > 0)
            {
                std::string token(NCorbaHelpers::GenerateUUIDString());
                tokens.insert(std::make_pair(token, std::chrono::system_clock::now()));
                --connectionCount;
                return token;
            }
            return std::string();
        }

        bool ValidateToken(const std::string& token)
        {
            std::unique_lock<std::mutex> lock(mutex);
            TCountTokens::iterator it = tokens.find(token);
            if (tokens.end() == it)
                return false;

            it->second = std::chrono::system_clock::now();

            ///////
            //std::time_t now_c = std::chrono::system_clock::to_time_t(it->second);
            //std::tm * ptm = std::localtime(&now_c);
            //char buffer[32];
            //// Format: Mo, 15.06.2009 20:20:00
            //std::strftime(buffer, 32, "%a, %d.%m.%Y %H:%M:%S", ptm);
            //_log_ << "Update token time to " << buffer;
            ///////

            return true;
        }

        void CheckConnections(std::chrono::system_clock::time_point tp)
        {
            std::unique_lock<std::mutex> lock(mutex);
            TCountTokens::iterator it1 = tokens.begin(), it2 = tokens.end();
            for (; it1 != it2;)
            {
                std::chrono::seconds delta = std::chrono::duration_cast<std::chrono::seconds>(tp - it1->second);
                if (delta > TOKEN_TTL)
                {
                    _dbg_ << "Revoke connection";
                    ++connectionCount;
                    it1 = tokens.erase(it1);
                }
                else
                    ++it1;
            }
        }
    };

    struct IUserConnectionInfo
    {
        virtual ~IUserConnectionInfo() {}

        virtual std::string AcquireConnection(EClientType) const = 0;
        virtual bool ValidateToken(EClientType, const std::string&) const = 0;
        virtual void CheckConnections() = 0;
    };
    typedef std::shared_ptr<IUserConnectionInfo> PUserConnectionInfo;

    struct SDefaultConnectionInfo : public IUserConnectionInfo
    {
        virtual std::string AcquireConnection(EClientType) const
        {
            return "00000000-0000-0000-0000-000000000000";
        }

        virtual bool ValidateToken(EClientType, const std::string&) const
        {
            return true;
        }

        virtual void CheckConnections() {}
    };

    struct SUserConnectionInfo : public IUserConnectionInfo
    {
        DECLARE_LOGGER_HOLDER;

        SUserConnectionInfo(DECLARE_LOGGER_ARG, int webCount, int mobileCount)
            : webInfo(new SConnectionInfo(GET_LOGGER_PTR, EWEBCLIENT, webCount))
            , mobileInfo(new SConnectionInfo(GET_LOGGER_PTR, EMOBILE, mobileCount))
        {
            INIT_LOGGER_HOLDER;
        }

        std::string AcquireConnection(EClientType type) const
        {
            switch (type)
            {
            case EWEBCLIENT:
            {
                _dbg_ << "Acquire web connection";
                return webInfo->AcquireConnection();
            }
            case EMOBILE:
            {
                _dbg_ << "Acquire mobile connection";
                return mobileInfo->AcquireConnection();
            }
            }
            return "";
        }

        bool ValidateToken(EClientType type, const std::string& token) const
        {
            switch (type)
            {
            case EWEBCLIENT:
            {
                _dbg_ << "Validate web connection";
                return webInfo->ValidateToken(token);
            }
            case EMOBILE:
            {
                _dbg_ << "Validate mobile connection";
                return mobileInfo->ValidateToken(token);
            }
            }
            return false;
        }

        void CheckConnections()
        {
            std::chrono::system_clock::time_point tp = std::chrono::system_clock::now();
            webInfo->CheckConnections(tp);
            mobileInfo->CheckConnections(tp);
        }

        std::shared_ptr<SConnectionInfo> webInfo;
        std::shared_ptr<SConnectionInfo> mobileInfo;
    };

    class CConnectionManager : public std::enable_shared_from_this<CConnectionManager>
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CConnectionManager(NCorbaHelpers::IContainer* c)
            : m_sm(NCorbaHelpers::ResolveServant<InfraServer::SecurityManager::ISecurityManager>(c, "SecurityManager/Server"))
            , m_timer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_connectionMode(false)
            , m_loadTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);

            LoadUserInfo();
        }

        void Start()
        {
            if (m_connectionMode)
                SetUpdateTimer();
        }

        void Stop()
        {
            m_timer.cancel();
            m_loadTimer.cancel();
        }

        bool IsConnectionLimitDisabled() const
        {
            return !m_connectionMode;
        }

        std::string AcquireConnection(const std::wstring& user, EClientType type)
        {
            PUserConnectionInfo uci;
            {
                boost::shared_lock<boost::shared_mutex> lock(m_mutex);
                TUserConnectionPolicy::iterator it = m_userConnectionPolicy.find(NCorbaHelpers::UtfToLower(user));
                if (m_userConnectionPolicy.end() == it)
                    return "";

                uci = it->second;
            }
            return uci->AcquireConnection(type);
        }

        bool ValidateToken(const std::wstring& user, EClientType type, const std::string& token)
        {
            PUserConnectionInfo uci;
            {
                boost::shared_lock<boost::shared_mutex> lock(m_mutex);
                TUserConnectionPolicy::iterator it = m_userConnectionPolicy.find(NCorbaHelpers::UtfToLower(user));
                if (m_userConnectionPolicy.end() == it)
                    return false;

                uci = it->second;
            }
            return uci->ValidateToken(type, token);
        }

        void LoadUserInfo()
        {
            boost::upgrade_lock<boost::shared_mutex> lock(m_mutex);
            boost::upgrade_to_unique_lock<boost::shared_mutex> uniqueLock(lock);

            if (!CORBA::is_nil(m_sm))
            {
                InfraServer::SecurityManager::SConfig_var cfg = m_sm->GetConfig();

                m_userConnectionPolicy.clear();

                InfraServer::SecurityManager::SConfig::_users_seq users = cfg->users;
                CORBA::ULong userCount = users.length();
                _dbg_ << "Load info about " << userCount << " users";
                for (CORBA::ULong i = 0; i < userCount; ++i)
                {
                    std::wstring u(NCorbaHelpers::UtfToLower(users[i].login.in()));

                    int webCount = users[i].connectionPolicy.webCount;
                    int mobileCount = users[i].connectionPolicy.mobileCount;

                    bool isDefault = (webCount == std::numeric_limits<std::int32_t>::max() && mobileCount == std::numeric_limits<std::int32_t>::max());

                    IUserConnectionInfo* uci = nullptr;
                    if (isDefault)
                        uci = new SDefaultConnectionInfo;
                    else
                    {
                        m_connectionMode = true;
                        uci = new SUserConnectionInfo(GET_LOGGER_PTR, webCount, mobileCount);
                    }

                    m_userConnectionPolicy.insert(std::make_pair(u, PUserConnectionInfo(uci)));
                }
            }
        }

        void UpdateUserInfo()
        {
            try
            {
                LoadUserInfo();
            }
            catch (const CORBA::Exception&)
            {
                _wrn_ << "LoadUserInfo failed. Set timer for next try";
                SetLoadTimer();
            }
        }

    private:
        void SetUpdateTimer()
        {
            m_timer.expires_from_now(boost::posix_time::seconds(UPDATE_TIMEOUT));
            m_timer.async_wait(std::bind(&CConnectionManager::handle_timeout,
                shared_from_this(), std::placeholders::_1));
        }

        void SetLoadTimer()
        {
            m_loadTimer.expires_from_now(boost::posix_time::seconds(LOAD_TIMEOUT));
            m_loadTimer.async_wait(std::bind(&CConnectionManager::handle_load_timeout,
                shared_from_this(), std::placeholders::_1));
        }

        void handle_timeout(const boost::system::error_code& error)
        {
            if (!error)
            {
                {
                    boost::shared_lock<boost::shared_mutex> lock(m_mutex);
                    TUserConnectionPolicy::iterator it1 = m_userConnectionPolicy.begin(),
                        it2 = m_userConnectionPolicy.end();
                    for (; it1 != it2; ++it1)
                    {
                        it1->second->CheckConnections();
                    }
                }
                SetUpdateTimer();
            }
        }

        void handle_load_timeout(const boost::system::error_code& error)
        {
            if (!error)
                UpdateUserInfo();
        }

        InfraServer::SecurityManager::ISecurityManager_var m_sm;

        typedef std::map<std::wstring, PUserConnectionInfo> TUserConnectionPolicy;
        TUserConnectionPolicy m_userConnectionPolicy;

        boost::asio::deadline_timer m_timer;
        bool m_connectionMode;

        boost::shared_mutex m_mutex;

        boost::asio::deadline_timer m_loadTimer;
    };

    typedef std::weak_ptr<CConnectionManager> WConnectionManager;
    typedef std::shared_ptr<CConnectionManager> PConnectionManager;

    class CEventConsumerSink : public NCommonNotification::IEventConsumerSink
        , public NCorbaHelpers::CRefcountedImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CEventConsumerSink(NCorbaHelpers::IContainer *c, WConnectionManager cm)
            : m_cm(cm)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void ProcessEvents(const Notification::Events& events)
        {
            const CORBA::ULong length = events.length();
            for (CORBA::ULong i = 0; i < length; ++i)
            {
                Notification::Event ev = events[i];
                {
                    const InfraServer::SItemStatus* stat = nullptr;
                    ev.Body >>= stat;

                    if (stat && stat->Status == InfraServer::S_Changed)
                    {
                        if (SECURITY_MANAGER == stat->ItemName.in())
                        {
                            _dbg_ << "SecurityManager updated. Refreshing user info...";
                            PConnectionManager cm = m_cm.lock();
                            if (cm)
                                cm->UpdateUserInfo();
                        }
                    }
                }
            }
        }

    private:
        WConnectionManager m_cm;
    };
    typedef NCorbaHelpers::CAutoPtr<CEventConsumerSink> PEventConsumerSink;

    class CConnectionPolicyInterceptor : public NHttp::IInterceptor
    {
        DECLARE_LOGGER_HOLDER;
        typedef std::map<std::string, std::string> TCookies;
    public:
        CConnectionPolicyInterceptor(NCorbaHelpers::IContainer* c, const char* globalObjectName)
            : m_cm(new CConnectionManager(c))
            , m_sink(new CEventConsumerSink(c, std::weak_ptr<CConnectionManager>(m_cm)))
            , m_eventReceiverContainer(c->CreateContainer(EVENT_RECEIVER, NCorbaHelpers::IContainer::EA_DontAdvertise))
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);

            InitEventChannel(globalObjectName);

            if (m_cm)
                m_cm->Start();

        }

        ~CConnectionPolicyInterceptor()
        {
            m_eventReceiverContainer.Reset();
            m_sink.Reset();

            if (m_cm)
                m_cm->Stop();
        }

        virtual NHttp::PResponse Process(const NHttp::PRequest req, NHttp::PResponse resp)
        {
            std::string ctx = req->GetContextPath();
            if (("/" == ctx) || m_cm->IsConnectionLimitDisabled() || (req->GetAuthSession().id == TOKEN_AUTH_SESSION_ID) || (0 == ctx.find("/v1/authentication/authenticate")))
                return resp;

            std::wstring user(NCorbaHelpers::FromUtf8(req->GetAuthSession().user));

            NHttp::THttpHeaders h = req->GetHeaders();
            boost::optional<std::string> countToken = Cookie(h);
            
            TCookies cookies;
            std::string token;
            if (countToken)
            {
                ParseCookies(countToken.get(), cookies);
                token.assign(GetCookieValue(cookies, LUCKY_TOKEN_COOKIE));

            }

            EClientType ct = GetClientType(cookies);

            if (!countToken || token.empty())
            {
                return AcquireConnection(resp, user, ct);
            }
            else
            {
                if (!m_cm->ValidateToken(user, ct, token))
                {
                    _wrn_ << "Count token expired";
                    return AcquireConnection(resp, user, ct);
                }
            }
            return resp;
        }

    private:
        NHttp::PResponse AcquireConnection(NHttp::PResponse resp, const std::wstring& user, EClientType ct)
        {
            std::string token(m_cm->AcquireConnection(user, ct));
            if (token.empty())
            {
                _wrn_ << "Cannot acquire connection";
                return NHttp::PResponse();
            }
            resp << SetCookie(boost::str(boost::format(COOKIE_FORMAT) % LUCKY_TOKEN_COOKIE % token));
            return resp;
        }

        EClientType GetClientType(const TCookies& cookies)
        {
            std::string agentName(GetCookieValue(cookies, AGENT_NAME_COOKIE));
            if (!agentName.empty())
                return EWEBCLIENT;
            return EMOBILE;
        }

        void ParseCookies(const std::string& cookie, TCookies& cookies)
        {
            static const char COOKIE_DELIMITER = ';';
            static const char VALUE_DELIMITER = '=';

            std::vector<std::string> cookieParts;
            size_t pos = 0, lastPos = 0;
            std::string token;
            while ((pos = cookie.find(COOKIE_DELIMITER, pos)) != std::string::npos) {
                token = cookie.substr(lastPos, pos - lastPos);
                boost::trim(token);
                cookieParts.push_back(token);
                lastPos = ++pos;
            }

            token.assign(cookie.substr(lastPos));
            boost::trim(token);
            cookieParts.push_back(token);

            std::vector<std::string>::iterator it1 = cookieParts.begin(), it2 = cookieParts.end();
            for (; it1 != it2; ++it1)
            {
                pos = (*it1).find(VALUE_DELIMITER);
                if (std::string::npos != pos)
                {
                    cookies.insert(std::make_pair((*it1).substr(0, pos), (*it1).substr(pos + 1)));
                }
            }
        }

        std::string GetCookieValue(const TCookies& cookies, const char* const mask)
        {
            TCookies::const_iterator it = cookies.find(mask);
            if (cookies.end() != it)
                return it->second;
            return "";
        }

        void InitEventChannel(const char* globalObjectName)
        {
            try
            {
                auto id = NCommonNotification::MakeEventConsumerAuxUid(globalObjectName, STATUS_EVENT_RECEIVER);
                std::unique_ptr<NCommonNotification::CEventConsumer> statusConsumer(
                    NCommonNotification::CreateEventConsumerServantNamed(m_eventReceiverContainer.Get(), id.c_str(), m_sink.Get()));
                m_eventReceiverContainer->ActivateServant(statusConsumer.get(), STATUS_EVENT_RECEIVER);
                NCommonNotification::FilterBuilder builder;
                builder.Include<InfraServer::SItemStatus>(SECURITY_MANAGER); // Only security manager config
                statusConsumer->SetFilter(builder.Get());
                statusConsumer->SetSubscriptionAddress("ExecutionManager/EventSupplier");
                statusConsumer.release();
            }
            catch (const CORBA::Exception&)
            {
                _err_ << "Unable to activate EventChannel";
            }
        }

        std::shared_ptr<CConnectionManager> m_cm;

        PEventConsumerSink m_sink;
        NCorbaHelpers::PContainerNamed m_eventReceiverContainer;
    };
}

namespace NHttp
{
IInterceptor* CreateConnectionPolicyInterceptor(NCorbaHelpers::IContainer* c, const char* globalObjectName)
{
    return new CConnectionPolicyInterceptor(c, globalObjectName);
    }
}

#endif // CONNECTION_POLICY_INTERCEPTOR_H__
