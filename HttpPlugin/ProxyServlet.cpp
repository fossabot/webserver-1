#include <boost/asio.hpp>

#include <HttpServer/BasicServletImpl.h>
#include <CorbaHelpers/Reactor.h>

#include <Crypto/Crypto.h>

#include "HttpPlugin.h"
#include "RegexUtility.h"
#include "Constants.h"

using namespace NHttp;
namespace npu = NPluginUtility;
using tcp = boost::asio::ip::tcp;
namespace asio = boost::asio;
using error_code = boost::system::error_code;

namespace
{
    const char* const SESSION_COOKIE_PARAM = "sessioncookie";
    const char* const SESSION_COOKIE_HEADER = "x-sessioncookie";

    const uint32_t LIVE_MEDIA_TIMEOUT = 60;

    class RTSPProxySession;

    typedef std::function<void(std::shared_ptr<RTSPProxySession>)> create_session_callback_t;
    typedef std::function<void(std::shared_ptr<RTSPProxySession>)> release_session_callback_t;

    class RTSPProxySession : public std::enable_shared_from_this<RTSPProxySession>
    {
    public:
        RTSPProxySession(DECLARE_LOGGER_ARG, boost::asio::io_service& service, const int port, const std::string& sessionId,
            create_session_callback_t createCb, release_session_callback_t releaseCb)
            : m_getSocket(service)
            , m_postSocket(service)
            , m_endpoint(asio::ip::address::from_string("127.0.0.1"), port)
            , m_sessionId(sessionId)
            , m_create(createCb)
            , m_release(releaseCb)
            , m_timer(service)
            , m_postConnectCompleted(false)
        {
            INIT_LOGGER_HOLDER;
            m_getSocket.open(boost::asio::ip::tcp::v4());
            m_postSocket.open(boost::asio::ip::tcp::v4());

            m_postConnected.clear();

            _this_dbg_ << "RTSPProxySession " << this << " created";
        }

        ~RTSPProxySession()
        {
            _this_dbg_ << "RTSPProxySession " << this << " destroyed";
            closeSockets();
        }

        std::string getSessionId() const
        {
            return m_sessionId;
        }

        void sendGetRequest(PResponse resp, std::string request)
        {
            auto self(shared_from_this());
            std::call_once(m_getMode, [this, self, resp, request]()
            {
                m_getSocket.async_connect(m_endpoint,
                    [this, self, resp, request](const error_code& err)
                {
                    if (err)
                    {
                        _this_err_ << "Async connect to GET socket failed";
                    }
                    else
                    {
                        asio::async_write(m_getSocket, boost::asio::buffer(request),
                            std::bind(&RTSPProxySession::handleGetRequest, self, resp, request, std::placeholders::_1));
                    }
                });
            });
        }

        void sendPostRequest(PRequest req, PResponse resp, std::string request)
        {
            auto self(shared_from_this());

            if (!m_postConnected.test_and_set())
            {
                _this_dbg_ << "Try to set POST connection...";
                std::call_once(m_postMode, [this, self, req, resp, request]()
                {
                    m_postSocket.async_connect(m_endpoint,
                        [this, self, req, resp, request](const error_code& err)
                    {
                        if (err)
                        {
                            cancelAll("Async connect to POST socket failed");
                            return;
                        }
                        else
                        {
                            asio::async_write(m_postSocket, boost::asio::buffer(request),
                                std::bind(&RTSPProxySession::handlePostRequest, self, req, resp, request, std::placeholders::_1));

                            {
                                std::lock_guard<std::mutex> lk(m_postMutex);
                                m_postConnectCompleted = true;
                            }
                            m_postCV.notify_one();
                        }
                    });
                });
            }
            else
            {
                {
                    std::unique_lock<std::mutex> lk(m_postMutex);
                    m_postCV.wait(lk, [this] { return m_postConnectCompleted; });
                }

                sendRTSPCommand(req, resp);
            }
        }

    private:
        /// GET request processing ////////////////////////////////////////////////////////////////
        void handleGetRequest(PResponse resp, std::string request, const error_code& err)
        {
            if (err)
            {
                cancelAll("Get request failed");
                return;
            }

            m_create(shared_from_this());
            setTimer();
            readRTSPData(resp);
        }

        void readRTSPData(PResponse resp)
        {
            auto self(shared_from_this());
            m_getSocket.async_read_some(asio::buffer(m_readBuffer),
                [this, self, resp](error_code ec, std::size_t bytes_transferred)
            {
                if (ec)
                {
                    cancelAll("RTSP data read error");
                    return;
                }
                try
                {
                    std::string data(m_readBuffer.data(), bytes_transferred);
                    std::weak_ptr<RTSPProxySession> wp = self;

                    resp->AsyncWrite(m_readBuffer.data(), bytes_transferred,
                        [this, wp, resp](error_code ec)
                        {
                            auto sp = wp.lock();
                            if (!sp)
                                return;

                            if (ec)
                                return cancelAll("AsyncWrite error");

                            readRTSPData(resp);
                        });
                }
                catch (const std::exception& )
                {
                    cancelAll("Write response error");
                }
            });
        }

        /// POST request processing ///////////////////////////////////////////////////////////////

        void handlePostRequest(PRequest req, PResponse resp, std::string request, const error_code& err)
        {
            if (err)
            {
                cancelAll("Post request failed");
                return;
            }
            sendRTSPCommand(req, resp);
        }

        void sendRTSPCommand(PRequest req, PResponse resp)
        {
            _this_dbg_ << "Sending RTSP command...";
            std::vector<uint8_t> body(req->GetBody());
            std::string rtspCommand(eraseRtspProxyPrefix(NCrypto::FromBase64((const char* const)&body[0], body.size()).value_or("")));
            std::string adornmentRTSPCommand(prepareForGstreamerAutorization(rtspCommand));
            
            asio::async_write(m_postSocket, asio::buffer(&adornmentRTSPCommand[0], adornmentRTSPCommand.size()),
                std::bind(&RTSPProxySession::handleRTSPCommand, shared_from_this(), resp, std::placeholders::_1));
        }

        std::string eraseRtspProxyPrefix(std::string&& rtspCommand)
        {
            static const std::string rtspProxyPrefix("/rtspproxy");

            size_t pos = rtspCommand.find(rtspProxyPrefix);

            if (pos != std::string::npos)
                rtspCommand.erase(pos, rtspProxyPrefix.length());

            return NCrypto::ToBase64Pem(&rtspCommand[0], rtspCommand.size());
        }


        std::string prepareForGstreamerAutorization(const std::string& rtspCommand)
        {
            static const std::string authHeader("Authorization: Basic ");
            static const std::uint8_t authHeaderLength = authHeader.size();

            std::string decodedRTSPCommand =
                NCrypto::FromBase64(rtspCommand.c_str(), rtspCommand.size()).value_or("");

            std::size_t pos = decodedRTSPCommand.find(authHeader);
            if (std::string::npos == pos)
            {
                _log_ << "RTSP command doesn't have Authorization header";
                return rtspCommand;
            }

            std::size_t pos2 = decodedRTSPCommand.find('\r', pos);
            std::size_t credsPos = pos + authHeaderLength;
            std::string base64creds = decodedRTSPCommand.substr(credsPos, pos2 - credsPos);

            std::string decodedCreds(NCrypto::FromBase64(base64creds.c_str(), base64creds.size()).value_or(""));
            std::size_t sepPos = decodedCreds.find(':');
            if (std::string::npos == sepPos)
            {
                _wrn_ << "Incorrect credentials format";
                return rtspCommand;
            }

            std::string user(decodedCreds.substr(0, sepPos));

            if (std::string::npos != user.find('@'))
            {
                std::string forChange(user);
                std::replace(forChange.begin(), forChange.end(), '@', '_');
                forChange.append(decodedCreds.substr(sepPos));
                std::string nextAuth((boost::format("%1%: Basic %2%\r\n") % AXXON_AUTHORIZATION % base64creds).str());
                decodedRTSPCommand.insert(pos2 + 2, nextAuth);
                decodedRTSPCommand.replace(credsPos, pos2 - credsPos + 2, NCrypto::ToBase64Pem(forChange.c_str(), forChange.size()));

                return NCrypto::ToBase64Pem(decodedRTSPCommand.c_str(), decodedRTSPCommand.size());
            }

            return rtspCommand;
        }

        void handleRTSPCommand(PResponse resp, error_code err)
        {
            if (err)
            {
                cancelAll("RTSP command error");
            }
            m_timer.cancel();
        }      

        ///////////////////////////////////////////////////////////////////////////////////////////

        void closeSockets()
        {
            boost::system::error_code code;

            m_getSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, code);
            m_getSocket.close(code);

            m_postSocket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, code);
            m_postSocket.close(code);
        }

        void cancelAll(std::string msg)
        {
            _this_dbg_ << this << ": " << msg;
            release_session_callback_t cb;
            {
                boost::mutex::scoped_lock lock(m_mutex);
                cb = m_release;
                m_release = release_session_callback_t();
            }
            if (cb)
                cb(shared_from_this());
            m_timer.cancel();
            m_postSocket.cancel();
            m_getSocket.cancel();         
        }

        void setTimer()
        {
            m_timer.expires_from_now(boost::posix_time::seconds(LIVE_MEDIA_TIMEOUT));
            m_timer.async_wait(std::bind(&RTSPProxySession::handleTimeout,
                shared_from_this(),
                std::placeholders::_1));
        }

        void handleTimeout(const error_code& err)
        {
            if (err == boost::system::errc::operation_canceled)
            {
                _this_dbg_ << "RTSP command has been received. Timer reinitialization...";
                boost::mutex::scoped_lock lock(m_mutex);
                if (m_release)
                    setTimer();
            }
            else if (err)
                cancelAll("Timer error");
            else
                cancelAll("RTSP timeout");
        }

        DECLARE_LOGGER_HOLDER;
        boost::asio::ip::tcp::socket m_getSocket;
        boost::asio::ip::tcp::socket m_postSocket;
        boost::asio::ip::tcp::endpoint m_endpoint;

        std::string m_sessionId;

        create_session_callback_t m_create;
        release_session_callback_t m_release;

        std::once_flag m_getMode;
        std::once_flag m_postMode;
        std::atomic_flag m_postConnected;

        std::array<char, 1024 * 64> m_readBuffer;

        boost::mutex m_mutex;

        boost::asio::deadline_timer m_timer;

        std::mutex m_postMutex;
        std::condition_variable m_postCV;
        bool m_postConnectCompleted;
    };
    typedef std::shared_ptr<RTSPProxySession> PRTSPProxySession;

    class CProxyServlet : public IProxyServlet, public NHttpImpl::CBasicServletImpl
    {
    public:
        explicit CProxyServlet(DECLARE_LOGGER_ARG, int port, const std::string& target) :
            m_port(port),
            m_target(target)
        {
            INIT_LOGGER_HOLDER;
        }

    private:
        virtual void Head(const PRequest req, PResponse resp) { Redirect(req, resp); }
        virtual void Get(const PRequest req, PResponse resp)  { Redirect(req, resp); }
        virtual void Post(const PRequest req, PResponse resp) { Redirect(req, resp); }
        virtual void Put(const PRequest req, PResponse resp)  { Redirect(req, resp); }

        virtual PResponse Process(const PRequest req, PResponse resp)
        {
            std::string context = req->GetContextPath();
            if (context != m_target)
            {            
                return resp;
            }

            Redirect(req, resp);
            return PResponse();
        }

    private:
        void Redirect(const PRequest req, PResponse resp)
        {
            // TODO: work it out
            const auto destination = "rtspproxy" + req->GetPathInfo();

            static const std::vector<std::string> IGNORE_LIST = 
            {
                "Host",                
            };

            std::string currentSessionId;
            for (auto header : req->GetHeaders())
            {
                if (SESSION_COOKIE_HEADER == header.second.Name)
                    currentSessionId.assign(header.second.Value);
            }

            bool addHeaders = false;
            if (currentSessionId.empty())
            {
                npu::TParams params;
                if (!npu::ParseParams(req->GetQuery(), params))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                currentSessionId.assign(npu::GetParam(params, SESSION_COOKIE_PARAM, std::string()));

                if (currentSessionId.empty())
                {
                    _err_ << "Query doesn't contain session id";
                    Error(resp, IResponse::BadRequest);
                    return;
                }
                addHeaders = true;
            }

            std::string method(req->GetMethod());

            std::ostringstream stream;
            stream << boost::format("%1% %2% HTTP/%3%\r\n") % method % destination % req->GetVersion();
            stream << "Host: 127.0.0.1\r\n";
            for (auto header : req->GetHeaders())
            {
                if (std::find(IGNORE_LIST.begin(), IGNORE_LIST.end(), header.second.Name) != IGNORE_LIST.end())
                    continue;
                stream << boost::format("%1%: %2%\r\n") % header.second.Name % header.second.Value;
            }

            if (addHeaders)
            {
                stream << boost::format("%1%: %2%\r\n") % "x-sessioncookie" % currentSessionId;
                stream << boost::format("%1%: %2%\r\n") % "Range" % "0-";
            }
            stream << "\r\n";

            PRTSPProxySession session;        
            if (method == "GET")
            {
                session.reset(new RTSPProxySession(GET_LOGGER_PTR, NCorbaHelpers::GetReactorInstanceShared()->GetIO(), m_port,
                    currentSessionId, std::bind(&CProxyServlet::createSession, this, shared_from_this(), std::placeholders::_1),
                    std::bind(&CProxyServlet::releaseSession, this, shared_from_this(), std::placeholders::_1)));

                if (session)
                    session->sendGetRequest(resp, stream.str());
            }
            else
            {
                {
                    boost::mutex::scoped_lock lock(m_mutex);
                    std::map<std::string, PRTSPProxySession>::iterator it = m_sessions.find(currentSessionId);
                    if (it != m_sessions.end())
                        session = it->second;
                }

                if (session)
                    session->sendPostRequest(req, resp, stream.str());
            }
        }

    private:
        void createSession(PServlet, PRTSPProxySession s)
        {
            _dbg_ << "Add session with id " << s->getSessionId();
            boost::mutex::scoped_lock lock(m_mutex);
            m_sessions.insert(std::make_pair(s->getSessionId(), s));
        }

        void releaseSession(PServlet, PRTSPProxySession s)
        {
            _dbg_ << "Remove session with id " << s->getSessionId();
            boost::mutex::scoped_lock lock(m_mutex);
            m_sessions.erase(s->getSessionId());
        }

        DECLARE_LOGGER_HOLDER;
        const int m_port;
        const std::string m_target;

        boost::mutex m_mutex;
        std::map<std::string, PRTSPProxySession> m_sessions;
    };
}

namespace NHttp
{
    IProxyServlet* CreateProxyServlet(DECLARE_LOGGER_ARG, int port, const std::string& target)
    {
        return new CProxyServlet(GET_LOGGER_PTR, port, target);
    }
}
