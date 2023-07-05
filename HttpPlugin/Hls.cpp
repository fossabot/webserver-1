#include "Hls.h"

#include <boost/uuid/uuid.hpp>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <json/json.h>

#include <CorbaHelpers/Uuid.h>

#include "Constants.h"
#include "CommonUtility.h"

using namespace NHttp;

namespace
{
    const char* const PLAYLIST_NAME = "playlist.m3u8";
    const char* const PLAYLIST_PATH_PREFIX = "/hls/";

    const int DEFAULT_KEEP_ALIVE = 10 * 60; // 10 minutes
    const int DEFAULT_HLS_TIME = 5;
    const int DEFAULT_HLS_LIST_SIZE = 4;
    const int DEFAULT_HLS_WRAP = 10;

    const int MAX_HLS_LIST_SIZE = 20;

    const char* const SPEED_PARAM = "speed";
    const int DEFAULT_SPEED_VALUE = 1;

    typedef boost::filesystem::path path_t;
    path_t getRelativePath(const std::string& connectionId)
    {
        return  boost::filesystem::path(connectionId) / PLAYLIST_NAME;
    }

    struct SHlsRequestParams
    {
        const std::string ep;
        int keep_alive;
        int hls_time;
        int hls_list_size;
        int hls_wrap;

        bool operator <(const SHlsRequestParams& rhs) const
        {
            if (ep < rhs.ep)
                return true;

            else if (ep == rhs.ep)
            {
                if (hls_time < rhs.hls_time)
                    return true;
                else if (hls_time == rhs.hls_time)
                {
                    if (hls_list_size < rhs.hls_list_size)
                        return true;
                    else if (hls_list_size == rhs.hls_list_size)
                        return hls_wrap < rhs.hls_wrap;
                }
            }
            return false;
        }
        SHlsRequestParams(const std::string& e)
            : ep(e)
            , keep_alive(DEFAULT_KEEP_ALIVE)
            , hls_time(DEFAULT_HLS_TIME)
            , hls_list_size(DEFAULT_HLS_LIST_SIZE)
            , hls_wrap(DEFAULT_HLS_WRAP)
        {}
    };
    
    const char* const HLS_KEEP_COMMAND = "/hls/keep";
    const char* const HLS_STOP_COMMAND = "/hls/stop";

    using StopFunction_t = boost::function<IResponse::EStatus()>;

    struct SHlsContext : public std::enable_shared_from_this<SHlsContext>
    {
        DECLARE_LOGGER_HOLDER;

        SHlsContext(DECLARE_LOGGER_ARG)
            : keepAlive(false)
            , keepAliveTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
        {
            INIT_LOGGER_HOLDER;
        }

        ~SHlsContext()
        {
            Stop();
        }

        void Schedule(boost::posix_time::time_duration delay, StopFunction_t sf)
        {
            keepAliveTimer.expires_from_now(delay);
            keepAliveTimer.async_wait(std::bind(&SHlsContext::handle_timer, shared_from_this(), delay, sf, std::placeholders::_1));
        }

        void Stop()
        {
            keepAliveTimer.cancel();
        }

        void handle_timer(boost::posix_time::time_duration delay, StopFunction_t sf, const boost::system::error_code& error)
        {
            if (error)
                return;

            if (!keepAlive)
            {
                _dbg_ << "No keep-alive message. Stop HLS stream";
                sf();
            }
            else
            {
                _dbg_ << "We have keep-alive message. Prolongate HLS stream";
                keepAlive = false;
                Schedule(delay, sf);
            }
        }

        boost::uuids::uuid id;
        NHttp::PClientContext cc;
        NHttp::PCountedSynchronizer sync;
        bool keepAlive;

        boost::asio::deadline_timer keepAliveTimer;
    };
    using PHlsContext = std::shared_ptr<SHlsContext>;
}

namespace NPluginHelpers
{

class HlsSourceManagerImpl :  boost::noncopyable
{
    using CreateFunction_t = boost::function<NHttp::PClientContext(StopFunction_t)>;
public:
    HlsSourceManagerImpl(NCorbaHelpers::IContainer* c, const std::string& hlsContentPath, NHttp::PVideoSourceCache cache) 
        : m_container(c, NCorbaHelpers::ShareOwnership())
        , m_hlsContentPath(hlsContentPath)
        , m_videoSourceCache(cache)
    {
        INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
    }

    void ConnectToEnpoint(PRequest req, PResponse resp, const std::string& endpoint, const npu::TParams& params)
    {
        PHlsContext hlsCtx = std::make_shared<SHlsContext>(GET_LOGGER_PTR);
        CreateFunction_t cf = boost::bind(&NHttp::IVideoSourceCache::CreateHlsContext, m_videoSourceCache, resp, endpoint, getFilePath(hlsCtx), _1);
        connectToEndpoint(req, resp, hlsCtx, endpoint, params, cf);
    }

    void ConnectToArchiveEnpoint(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, 
        PRequest req, PResponse resp, const std::string& endpoint, const std::string& archiveName, const std::string& startTime, const npu::TParams& params)
    {
        PHlsContext hlsCtx = std::make_shared<SHlsContext>(GET_LOGGER_PTR);
        NHttp::PCountedSynchronizer sync = boost::make_shared<NHttp::SCountedSynchronizer>(GET_LOGGER_PTR, m_container.Get());
        hlsCtx->sync = sync;

        NHttp::PArchiveContext aCtx = boost::make_shared<NHttp::SHlsArchiveContext>(GET_LOGGER_PTR, getFilePath(hlsCtx));
        aCtx->videoEndpoint = endpoint;
        aCtx->archiveName = archiveName;
        aCtx->startTime = startTime;

        CreateFunction_t cf = boost::bind(&NHttp::IVideoSourceCache::CreateArchiveMp4Context, m_videoSourceCache,
            grpcManager, credentials, resp, sync, aCtx, _1);
        connectToEndpoint(req, resp, hlsCtx, endpoint, params, cf);
    }

    bool handleCommand(PRequest req, const npu::TParams& params, PResponse resp)
    {
        try
        {
            const std::string streamId = npu::GetParam(params, STREAM_ID_PARAMETER, std::string());
            if (streamId.empty())
            {
                return false;
            }
            const std::string path = req->GetPathInfo();
            const auto id = boost::lexical_cast<boost::uuids::uuid>(streamId);
            boost::optional<IResponse::EStatus> returnCode;
            if (path == HLS_KEEP_COMMAND)
            {
                returnCode = handleKeepCommand(id);
            }
            else if (path == HLS_STOP_COMMAND)
            {
                returnCode = handleStopCommand(id);
            }

            if (returnCode)
            {
                Error(resp, *returnCode);
                // command handled!
                return true;
            }
        }
        catch (const std::exception&)
        {
        }
        return false;
    }

    void erase(const boost::uuids::uuid& connectionId)
    {
        try
        {
            handleStopCommand(connectionId);
        }
        catch (std::exception&)
        {
        }
    }

    void erase_all()
    {
        boost::mutex::scoped_lock lock(m_connectionGuard);
        connectionsMap_t::iterator it1 = m_connections.begin(), it2 = m_connections.end();
        for (; it1 != it2; ++it1)
            it1->second->cc->Stop();
        m_connections.clear();
    }

private:
    void connectToEndpoint(PRequest req, PResponse resp, PHlsContext hlsCtx, const std::string& endpoint, const npu::TParams& params, CreateFunction_t cf)
    {
        SHlsRequestParams p = parseRequest(endpoint.c_str(), params);
     
        NHttp::PClientContext cc;

        StopFunction_t sf = boost::bind(&HlsSourceManagerImpl::handleStopCommand, this, hlsCtx->id);
        try {
            
            cc = cf(sf);
            if (cc)
            {
                cc->Init();
                hlsCtx->cc = cc;

                if (hlsCtx->sync)
                {
                    int speed = 1;
                    npu::GetParam(params, SPEED_PARAM, speed, DEFAULT_SPEED_VALUE);
                    hlsCtx->sync->Start(speed);
                }
            }
        }
        catch (const std::exception& e) {
            _err_ << e.what();
            Error(resp, IResponse::InternalServerError);
            return;
        }

        hlsCtx->Schedule(boost::posix_time::seconds(p.keep_alive), sf);

        {
            boost::mutex::scoped_lock lock(m_connectionGuard);
            m_connections[hlsCtx->id] = hlsCtx;
        }

        sendResponse(req, resp, boost::lexical_cast<std::string>(hlsCtx->id), boost::posix_time::seconds(p.keep_alive));
    }
    void sendResponse(NHttp::PRequest req, NHttp::PResponse resp, const std::string& connectionId, const boost::posix_time::seconds& keepAlive)
    {
        // Create response uri
        const std::string baseUrl = req->GetPrefix() + req->GetContextPath();
        const std::string streamUrl = req->GetPrefix() + PLAYLIST_PATH_PREFIX + NLogging::ws2s(getRelativePath(connectionId).generic_string());
        const std::string keepAliveUrl = baseUrl + HLS_KEEP_COMMAND + "?stream_id=" + connectionId;
        const std::string stopUrl = baseUrl + HLS_STOP_COMMAND + "?stream_id=" + connectionId;
        Json::Value json;
        json["stream_url"] = streamUrl;
        json["keep_alive_seconds"] = keepAlive.total_seconds();
        json["keep_alive_url"] = keepAliveUrl;
        json["stop_url"] = stopUrl;
        
        Json::StreamWriterBuilder writer;
        NPluginUtility::SendText(resp, Json::writeString(writer, json));
    }

    IResponse::EStatus handleKeepCommand(const boost::uuids::uuid& id)
    {
        {
            boost::mutex::scoped_lock lock(m_connectionGuard);
            connectionsMap_t::iterator it = m_connections.find(id);
            if (m_connections.end() != it)
                it->second->keepAlive = true;
        }
        return IResponse::OK;
    }

    IResponse::EStatus handleStopCommand(const boost::uuids::uuid& id)
    {
        PHlsContext hlsCtx;
        boost::mutex::scoped_lock lock(m_connectionGuard);
        connectionsMap_t::iterator it = m_connections.find(id);
        if (m_connections.end() == it)
            return NHttp::IResponse::NotFound;

        hlsCtx = it->second;
        m_connections.erase(it);
        lock.unlock();

        hlsCtx->cc->Stop();
        hlsCtx->Stop();

        const std::string streamId = boost::lexical_cast<std::string>(id);
        boost::filesystem::path streamPath = m_hlsContentPath / streamId;
        boost::system::error_code ignore;
        remove_all(streamPath.parent_path(), ignore);

        return IResponse::OK;
    }

    std::string getFilePath(PHlsContext hlsCtx) 
    {
        boost::uuids::uuid reqId = NCorbaHelpers::GenerateUUID();
        boost::filesystem::path streamPath = m_hlsContentPath / boost::lexical_cast<std::string>(reqId);
        boost::filesystem::create_directories(streamPath);
        hlsCtx->id = reqId;
        return NLogging::ws2s(streamPath.string());
    }

private:
    NCorbaHelpers::PContainer m_container;
    const path_t m_hlsContentPath;
    NHttp::PVideoSourceCache m_videoSourceCache;
    typedef std::map<boost::uuids::uuid, PHlsContext> connectionsMap_t;
    boost::mutex m_connectionGuard;
    connectionsMap_t m_connections;
    DECLARE_LOGGER_HOLDER;

    SHlsRequestParams parseRequest(const char* const ep, const npu::TParams& params)
    {
        using namespace npu;
        SHlsRequestParams p(ep);

        npu::GetParam(params, PARAM_KEEP_ALIVE, p.keep_alive, DEFAULT_KEEP_ALIVE);
        npu::GetParam(params, PARAM_HLS_TIME, p.hls_time, DEFAULT_HLS_TIME);
        npu::GetParam(params, PARAM_HLS_LIST_SIZE, p.hls_list_size, DEFAULT_HLS_LIST_SIZE);
        npu::GetParam(params, PARAM_HLS_WRAP, p.hls_wrap, DEFAULT_HLS_WRAP);

        validateParams(p);

        return p;
    }

    void validateParams(SHlsRequestParams& p)
    {
        if (p.hls_list_size <= 0)
            p.hls_list_size = DEFAULT_HLS_LIST_SIZE;
        if (p.hls_list_size > MAX_HLS_LIST_SIZE)
            p.hls_list_size = MAX_HLS_LIST_SIZE;

        if (p.keep_alive <= 0)
            p.keep_alive = DEFAULT_KEEP_ALIVE;

        if (p.hls_time <= 0)
            p.hls_time = DEFAULT_HLS_TIME;

        if (p.hls_wrap <= 0)
            p.hls_wrap = DEFAULT_HLS_WRAP;
    }
};

HlsSourceManager::HlsSourceManager(NCorbaHelpers::IContainer* c, const std::string& hlsContentPath, NHttp::PVideoSourceCache cache) :
    m_impl(new HlsSourceManagerImpl(c, hlsContentPath, cache))
{
}

void HlsSourceManager::ConnectToEnpoint(NHttp::PRequest req, NHttp::PResponse resp, 
    const std::string& endpoint, const npu::TParams& params)
{
    m_impl->ConnectToEnpoint(req, resp, endpoint, params);
}

void HlsSourceManager::ConnectToArchiveEnpoint(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, 
    NHttp::PRequest req, NHttp::PResponse resp, const std::string& endpoint,
    const std::string& archiveName, const std::string& startTime, const npu::TParams& params)
{
    m_impl->ConnectToArchiveEnpoint(grpcManager, credentials, req, resp, endpoint, archiveName, startTime, params);
}

HlsSourceManager::~HlsSourceManager()
{
    m_impl->erase_all();
    m_impl.reset();
}

bool HlsSourceManager::HandleCommand(PRequest req, const npu::TParams& params, PResponse resp)
{
    return m_impl->handleCommand(req, params, resp);
}

void HlsSourceManager::Erase(const std::string& connectionId)
{
    m_impl->erase(boost::lexical_cast<boost::uuids::uuid>(connectionId));
}

}
