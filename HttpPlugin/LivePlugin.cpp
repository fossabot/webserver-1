#include <HttpServer/BasicServletImpl.h>
#include <CorbaHelpers/Uuid.h>

#include "HttpPlugin.h"
#include "DataSink.h"
#include "Constants.h"
#include "Tokens.h"
#include "RegexUtility.h"
#include "Hls.h"
#include "UrlBuilder.h"
#include "VideoSourceCache.h"
#include "CommonUtility.h"
#include "BLQueryHelper.h"

using namespace NHttp;
namespace npu = NPluginUtility;

namespace
{
    const char* const VIDEO_COMPRESSION_PARAMETER = "vc";
    uint32_t VIDEO_COMPRESSION_DEFAULT_VALUE = 1;
    uint32_t MAX_COMPRESSION_LEVEL = 0;
    uint32_t MIN_COMPRESSION_LEVEL = 6;

    const char* const HLS_COMMAND_PREFIX = "/hls";

    class CVideoContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;

        enum ESendMode { SM_Headers, SM_Full };

        NHttp::PVideoSourceCache m_videoSourceCache;

        typedef boost::uuids::uuid TRequestId;
        typedef std::map<TRequestId, NHttp::PClientContext> TContexts;

    public:
        CVideoContentImpl(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager, const npu::PRigthsChecker rc,
            const std::string& hlsContent, UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache)
            : m_videoSourceCache(cache)
            , m_container(c, NCorbaHelpers::ShareOwnership())
            , m_grpcManager(grpcManager)
            , m_rightsChecker(rc)
            , m_hlsManager(c, hlsContent, cache)
            , m_rtspManager(rtspUrls)        
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        ~CVideoContentImpl()
        {
            DeleteContexts();
        }

        virtual void Head(const PRequest req, PResponse resp)
        {
            Send(req, resp, SM_Headers);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Send(req, resp, SM_Full);
        }

    private:
        void Send(const PRequest req, PResponse resp, ESendMode sm)
        {
            if (0 == req->GetPathInfo().find(HLS_COMMAND_PREFIX))
            {
                npu::TParams params;
                if (!npu::ParseParams(req->GetQuery(), params))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                m_hlsManager.HandleCommand(req, params, resp);
                return;
            }

            npu::PObjName endpoint = npu::ObjName(3, "hosts/");
            if(!npu::Match(req->GetPathInfo(), endpoint))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            std::string epName;
            try
            {
                epName.assign(endpoint->Get().ToString());
            }
            catch (...)
            {
                _err_ << "Incorrect endpoint in query " << req->GetPathInfo();
                return Error(resp, IResponse::BadRequest);
            }

            m_rightsChecker->IsCameraAllowed(epName, req->GetAuthSession(),
                boost::bind(&CVideoContentImpl::isCameraAllowed,
                    boost::weak_ptr<CVideoContentImpl>(shared_from_base<CVideoContentImpl>()), req, resp, epName, boost::placeholders::_1));
        }

        TRequestId CreateId() const
        {
            return NCorbaHelpers::GenerateUUID();
        }

        void AddContext(TRequestId id, NHttp::PClientContext cc)
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_contexts.insert(std::make_pair(id, cc));
        }

        void DeleteContext(TRequestId id)
        {
            NHttp::PClientContext cc; // so that dtor will be called with mutex released
            boost::mutex::scoped_lock lock(m_mutex);
            const TContexts::iterator it = m_contexts.find(id);
            if(it != m_contexts.end())
            {
                cc=it->second;
                m_contexts.erase(it);
                lock.unlock();

                cc->Stop();
            }
        }

        void DeleteContexts()
        {
            TContexts stale;
            {
                boost::mutex::scoped_lock lock(m_mutex);
                stale.swap(m_contexts);
            }

            TContexts::iterator it1 = stale.begin(), it2 = stale.end();
            for (; it1 != it2; ++it1)
                it1->second->Stop();
        }

        void CheckCompression(uint32_t& vc)
        {
            if (vc < MAX_COMPRESSION_LEVEL || vc > MIN_COMPRESSION_LEVEL)
            {
                _wrn_ << "Adjust compression level to 1";
                vc = VIDEO_COMPRESSION_DEFAULT_VALUE;
            }
        }

        static void isCameraAllowed(boost::weak_ptr<CVideoContentImpl> owner, const PRequest req, PResponse resp, std::string epName, int cameraAccessLevel)
        {
            if (cameraAccessLevel < axxonsoft::bl::security::CAMERA_ACCESS_MONITORING_ON_PROTECTION)
            {
                Error(resp, IResponse::Forbidden);
                return;
            }

            boost::shared_ptr<CVideoContentImpl> o = owner.lock();
            if (o)
                o->processRequest(req, resp, epName);
        }

        void processRequest(const PRequest req, PResponse resp, const std::string& epName)
        {
            npu::TParams params;
            if (!npu::ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            const TRequestId reqId = CreateId();
            try
            {
                const std::string format = npu::GetParam(params, FORMAT_PARAMETER, std::string(FORMAT_VALUE_MJPEG));
                if (format == FORMAT_VALUE_HLS)
                {
                    m_hlsManager.ConnectToEnpoint(req, resp, epName, params);
                    return;
                }
                int enableTokenAuth = 0;
                int validForHours = 0;
                npu::GetParam(params, PARAM_ENABLE_TOKEN_AUTH, enableTokenAuth, 0);
                npu::GetParam(params, PARAM_TOKEN_VALID_HOURS, validForHours, 12);
                auto expiresAt = boost::posix_time::second_clock::local_time() +
                    boost::posix_time::hours(std::min(abs(validForHours), 24 * 7)); // Lease a week at max.

                if (format == FORMAT_VALUE_RTSP)
                {
                    m_rtspManager->handleLiveRtsp(resp, epName, enableTokenAuth, expiresAt);
                    return;
                }

                if (enableTokenAuth)
                {
                    m_rtspManager->handleHttpToken(resp, req, params, expiresAt);
                    return;
                }

                std::uint32_t keyFrames = 0;
                npu::GetParam<std::uint32_t>(params, KEY_FRAMES_PARAMETER, keyFrames, 0);

                PClientContext cc;
                if (format == FORMAT_VALUE_MP4)
                {
                    NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, req->GetAuthSession());

                    NWebBL::AuxillaryCallback_t ac = boost::bind(&CVideoContentImpl::runMP4Stream,
                        shared_from_base<CVideoContentImpl>(), resp, reqId, epName, keyFrames, _1);
                    NWebBL::ResolveCameraAudioStream(GET_LOGGER_PTR, m_grpcManager, metaCredentials, epName, ac);
                    return;
                }
                else
                {
                    uint32_t width = npu::GetParam<int>(params, WIDTH_PARAMETER, 0);
                    uint32_t height = npu::GetParam<int>(params, HEIGHT_PARAMETER, 0);

                    float fps = FRAMERATE_DEFAULT_VALUE;
                    npu::GetParam<float>(params, FRAMERATE_PARAMETER, fps, FRAMERATE_DEFAULT_VALUE);

                    uint32_t vc = VIDEO_COMPRESSION_DEFAULT_VALUE;
                    npu::GetParam<uint32_t>(params, VIDEO_COMPRESSION_PARAMETER, vc, VIDEO_COMPRESSION_DEFAULT_VALUE);
                    CheckCompression(vc);

                    resp->SetStatus(IResponse::OK);
                    std::string content = str(boost::format("multipart/x-mixed-replace; boundary=%s") % BOUNDARY_HEADER);
                    resp << ContentType(content) << CacheControlNoCache();
                    resp->FlushHeaders();

                    NMMSS::PPullStyleSink sink(NPluginHelpers::CreateFlowControlSink(
                        resp,
                        boost::bind(&CVideoContentImpl::DeleteContext, this, reqId),
                        0));

                    cc = m_videoSourceCache->CreateClientContext(epName,
                        width, height, vc, sink, keyFrames != 0, fps);
                }

                if (!cc)
                {
                    _err_ << "Cannot create client context";
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                cc->Init();
                AddContext(reqId, cc);
                return;
            }
            catch (...) {}
            Error(resp, IResponse::InternalServerError);
            DeleteContext(reqId);
        }

    private:
        void runMP4Stream(PResponse resp, const TRequestId reqId, const std::string& videoEp, std::uint32_t keyFrames, NWebBL::SAuxillarySources auxSources)
        {
            try {
                PClientContext cc = m_videoSourceCache->CreateMp4Context(resp, videoEp, auxSources.audioSource, auxSources.textSource,
                    boost::bind(&CVideoContentImpl::DeleteContext, this, reqId), keyFrames != 0);
                if (cc)
                {
                    cc->Init();
                    AddContext(reqId, cc);
                }
            }
            catch (const std::exception& e) {
                _err_ << e.what();
                Error(resp, IResponse::InternalServerError);
                return;
            }
        }

        NCorbaHelpers::PContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;
        const npu::PRigthsChecker m_rightsChecker;
        NPluginHelpers::HlsSourceManager m_hlsManager;

        mutable TContexts m_contexts;
        mutable boost::mutex m_mutex;
        UrlBuilderSP m_rtspManager;
    };
}

namespace NHttp
{
    IServlet* CreateVideoServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager,
        const npu::PRigthsChecker rc, const std::string& hlsContentPath,
        UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache)
    {
        return new CVideoContentImpl(c, grpcManager, rc, hlsContentPath, rtspUrls, cache);
    }
}
