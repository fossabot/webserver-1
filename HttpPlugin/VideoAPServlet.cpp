#include "HttpPlugin.h"
#include "GrpcReader.h"
#include "Constants.h"
#include "CommonUtility.h"

#include <CorbaHelpers/Uuid.h>
#include <HttpServer/BasicServletImpl.h>

#include <json/json.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;

namespace bl = axxonsoft::bl;

namespace
{
    const char* const ORIGIN_TEMPLATE = "/video-origins";
    const char* const SOURCE_TEMPLATE = "/video-sources";

    using ListCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::ListCamerasRequest,
        bl::domain::ListCamerasResponse >;
    using PListCameraReader_t = std::shared_ptr < ListCameraReader_t >;

    template <typename TResponse>
    using CameraListCallback_t = std::function<void(const TResponse&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    class CVideoAccessPointContentImpl : public NHttpImpl::CBasicServletImpl
    {
        struct SQueryContext
        {
            SQueryContext(PResponse resp)
                : m_response(resp)
                , m_responsePresentation(Json::objectValue)
            {
            }
            PResponse m_response;
            Json::Value m_responsePresentation;
            bl::domain::EViewMode m_viewMode;
            std::string m_pageToken;
            std::string m_filter;
        };
        typedef std::shared_ptr<SQueryContext> PQueryContext;

        DECLARE_LOGGER_HOLDER;
    public:
        CVideoAccessPointContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            try
            {
                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                PQueryContext qc = std::make_shared<SQueryContext>(resp);
                boost::uuids::uuid queryId = NCorbaHelpers::GenerateUUID();
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_responses.insert(std::make_pair(queryId, qc));
                }

                PToken hostName = Token();
                PObjName endpoint = ObjName(3, "hosts/");
                PMask empty = Empty();

                const std::string path = req->GetPathInfo();
                if (Match(path, hostName))
                {
                    qc->m_filter.assign(hostName->GetValue());
                }
                else if (Match(path, endpoint))
                {
                    qc->m_filter.assign(endpoint->Get().ToString());
                }

                setRequestMode(req, qc);
                doNativeBLQuery(req, metaCredentials, queryId, qc);
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::InternalServerError);
            }
        }

    private:
        void setRequestMode(PRequest req, PQueryContext qc)
        {
            const std::string ctxPath(req->GetContextPath());

            if (ORIGIN_TEMPLATE == ctxPath)
                qc->m_viewMode = bl::domain::EViewMode::VIEW_MODE_NO_CHILD_OBJECTS;
            else if (SOURCE_TEMPLATE == ctxPath)
                qc->m_viewMode = bl::domain::EViewMode::VIEW_MODE_FULL;
        }

        void doNativeBLQuery(const PRequest req, NGrpcHelpers::PCredentials metaCredentials, boost::uuids::uuid queryId, PQueryContext qc)
        {
            PListCameraReader_t reader(new ListCameraReader_t
                (GET_LOGGER_PTR, m_grpcManager, metaCredentials, 
                    &bl::domain::DomainService::Stub::AsyncListCameras));

            std::string pageToken(std::move(qc->m_pageToken));

            bl::domain::ListCamerasRequest creq;
            creq.set_page_token(pageToken);
            creq.set_view(qc->m_viewMode);
            if (!qc->m_filter.empty())
            {
                auto query = creq.mutable_query();
                query->add_search_fields(axxonsoft::bl::domain::SearchQuery::ACCESS_POINT);
                query->set_search_type(axxonsoft::bl::domain::SearchQuery::SUBSTRING);
                query->set_query(qc->m_filter);
            }

            CameraListCallback_t<bl::domain::ListCamerasResponse> cb = std::bind(&CVideoAccessPointContentImpl::onListCamerasResponse,
                boost::weak_ptr<CVideoAccessPointContentImpl>(shared_from_base<CVideoAccessPointContentImpl>()), req, metaCredentials, queryId,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

            reader->asyncRequest(creq, cb);
        }

        PQueryContext getResponse(boost::uuids::uuid queryId)
        {
            PQueryContext qc;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                std::map<boost::uuids::uuid, PQueryContext>::iterator it = m_responses.find(queryId);
                if (m_responses.end() != it)
                    qc = it->second;
            }
            return qc;
        }

        static void onListCamerasResponse(boost::weak_ptr<CVideoAccessPointContentImpl> obj, const PRequest req, NGrpcHelpers::PCredentials metaCredentials,
            boost::uuids::uuid queryId, const bl::domain::ListCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CVideoAccessPointContentImpl> owner = obj.lock();
            if (owner)
            {
                PQueryContext qc = owner->getResponse(queryId);
                if (!qc)
                    return;

                if (!grpcStatus.ok())
                {
                    owner->clearQueryContext(queryId);
                    return NPluginUtility::SendGRPCError(qc->m_response, grpcStatus);
                }

                Json::Value& responseObject = qc->m_responsePresentation;

                owner->processCameras(qc, res.items());

                if (!res.next_page_token().empty())
                {
                    qc->m_pageToken = res.next_page_token();
                }

                if (!qc->m_pageToken.empty() && NWebGrpc::_FINISH == status)
                {
                    owner->doNativeBLQuery(req, metaCredentials, queryId, qc);
                }
                else if (NWebGrpc::_FINISH == status)
                {
                    owner->clearQueryContext(queryId);
                    owner->sendResponse(req, qc->m_response, responseObject);
                }
            }
        }

        void sendResponse(const PRequest req, NHttp::PResponse resp, const Json::Value& responseObject) const
        {
            NPluginUtility::SendText(req, resp, responseObject.toStyledString());
        }

        void processCameras(PQueryContext qc, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            Json::Value& responseObject = qc->m_responsePresentation;

            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);
                const std::string origin(stripHostPrefix(c.access_point()));

                const std::string longName(c.display_id() + "." + c.display_name());
                const std::string shortName(c.display_name());

                if (bl::domain::EViewMode::VIEW_MODE_FULL == qc->m_viewMode)
                {
                    int vsCount = c.video_streams_size();
                    for (int j = 0; j < vsCount; ++j)
                    {
                        const bl::domain::VideoStreaming& vs = c.video_streams(j);
                        const std::string source(stripHostPrefix(vs.stream_acess_point()));

                        addStreamInfo(responseObject, source, origin, shortName, longName, vs.is_activated());
                    }
                }
                else
                    addStreamInfo(responseObject, origin, origin, shortName, longName, c.is_activated());
            }
        }

        void addStreamInfo(Json::Value& ro, const std::string& source, const std::string& origin,
            const std::string& shortName, const std::string& longName, bool isActivated)
        {
            Json::Value cam(Json::objectValue);
            cam["friendlyNameLong"] = longName;
            cam["friendlyNameShort"] = shortName;
            cam["origin"] = origin;
            cam["state"] = isActivated ? "signal_restored" : "signal_lost";

            ro[source] = cam;
        }

        std::string stripHostPrefix(const std::string& ap)
        {
            std::size_t pos = ap.find(HOST_PREFIX);
            if (0 == pos)
                return ap.substr(strlen(HOST_PREFIX));
            return ap;
        }

        void clearQueryContext(boost::uuids::uuid queryId)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_responses.erase(queryId);
        }

        const NWebGrpc::PGrpcManager m_grpcManager;

        std::mutex m_mutex;
        std::map<boost::uuids::uuid, PQueryContext> m_responses;
    };
}

namespace NHttp
{
    IServlet* CreateVideoAccessPointServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CVideoAccessPointContentImpl(GET_LOGGER_PTR, grpcManager);
    }
}
