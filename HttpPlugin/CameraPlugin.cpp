#include "HttpPlugin.h"
#include "Constants.h"
#include "GrpcReader.h"
#include "RegexUtility.h"
#include "CommonUtility.h"
#include "ListQueryHelper.h"

#include <HttpServer/BasicServletImpl.h>

#include <CorbaHelpers/Uuid.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

#include <json/json.h>

using namespace NHttp;

namespace bl = axxonsoft::bl;

namespace
{
    const char* const GROUPS_IDS_PARAMETER = "group_ids";
    const char* const NODES_PARAMETER = "nodes";

    const char* const VIDEO_SOURCE = "SourceEndpoint.video";
    const char* const AUDIO_SOURCE = "SourceEndpoint.audio";

    const char* const LIST_MASK = "list";
    const char* const BATCH_MASK = "batch";

    const int MAX_AGE = 3600;

    using ListCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::ListCamerasRequest,
        bl::domain::ListCamerasResponse >;
    using PListCameraReader_t = std::shared_ptr < ListCameraReader_t >;

    using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
        bl::domain::BatchGetCamerasResponse >;
    using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

    template <typename TResponse>
    using CameraListCallback_t = std::function<void(const TResponse&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    class CCameraListContentImpl : public NHttpImpl::CBasicServletImpl
    {
        struct SQueryContext
        {
            SQueryContext(PResponse resp)
                : m_response(resp)
                , m_responsePresentation(Json::objectValue)
            {
                m_responsePresentation["cameras"] = Json::Value(Json::arrayValue);
                m_responsePresentation["search_meta_data"] = Json::Value(Json::arrayValue);
            }
            PResponse m_response;
            Json::Value m_responsePresentation;
        };
        typedef std::shared_ptr<SQueryContext> PQueryContext;

        DECLARE_LOGGER_HOLDER;
    public:
        CCameraListContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER;
        }

        ~CCameraListContentImpl()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_responses.clear();
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            using namespace NPluginUtility;

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            boost::uuids::uuid queryId = NCorbaHelpers::GenerateUUID();
            {
                PQueryContext qc = std::make_shared<SQueryContext>(resp);
                std::unique_lock<std::mutex> lock(m_mutex);
                m_responses.insert(std::make_pair(queryId, qc));
            }

            NPluginUtility::TParams queryParams;
            if (!NPluginUtility::ParseParams(req->GetQuery(), queryParams))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            const std::string path = req->GetPathInfo();

            if (Match(path, Mask(LIST_MASK)))
                executeListCameras(req, resp, queryParams, metaCredentials, queryId);
            else if (Match(path, Mask(BATCH_MASK)))
                executeBatchGetCameras(req, resp, queryParams, metaCredentials, queryId);
            else
            {
                eraseResponseFromMap(queryId);
                Error(resp, IResponse::NotFound);
            }
        }

    private:
        void executeListCameras(const PRequest req,
                                PResponse resp,
                                const NPluginUtility::TParams& queryParams,
                                NGrpcHelpers::PCredentials metaCredentials,
                                boost::uuids::uuid queryId)
        {
            try
            {
                bl::domain::ListCamerasRequest request;

                // resolve query type
                if (!NPluginHelpers::resolveListQuery(request, queryParams))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                // resolve pagination
                NPluginHelpers::resolvePagination(request, queryParams, axxonsoft::bl::domain::EViewMode::VIEW_MODE_FULL);

                {  // resolve group ids
                    const auto groupIdsParameter(NPluginUtility::GetParam(queryParams, GROUPS_IDS_PARAMETER, std::string()));
                    if (!groupIdsParameter.empty())
                    {
                        std::vector<std::string> tokens;
                        for (const auto& token : boost::split(tokens, groupIdsParameter, boost::is_any_of("|")))
                        {
                            request.add_group_ids(boost::lexical_cast<std::string>(token));
                        }
                    }
                }

                {  // resolve nodes
                    const auto nodesParameter(NPluginUtility::GetParam(queryParams, NODES_PARAMETER, std::string()));
                    if (!nodesParameter.empty())
                    {
                        std::vector<std::string> tokens;
                        for (const auto& token : boost::split(tokens, nodesParameter, boost::is_any_of("|")))
                        {
                            request.add_nodes(boost::lexical_cast<std::string>(token));
                        }
                    }
                }

                CameraListCallback_t<bl::domain::ListCamerasResponse> callback =
                    std::bind(&CCameraListContentImpl::onListCamerasResponse,
                              boost::weak_ptr<CCameraListContentImpl>(shared_from_base<CCameraListContentImpl>()),
                              req,
                              queryId,
                              std::placeholders::_1,
                              std::placeholders::_2,
                              std::placeholders::_3);

                PListCameraReader_t reader(new ListCameraReader_t(GET_LOGGER_PTR,
                                                                  m_grpcManager,
                                                                  metaCredentials,
                                                                  &bl::domain::DomainService::Stub::AsyncListCameras));
                reader->asyncRequest(request, callback);
            }
            catch (const std::invalid_argument& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::InternalServerError);
            }
        }

        void executeBatchGetCameras(const PRequest req, PResponse resp, const NPluginUtility::TParams& queryParams, NGrpcHelpers::PCredentials metaCredentials, boost::uuids::uuid queryId)
        {
            try
            {
                PBatchCameraReader_t reader(new BatchCameraReader_t
                    (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncBatchGetCameras));

                bl::domain::BatchGetCamerasRequest creq;

                std::string filter(NPluginUtility::GetParam(queryParams, NPluginHelpers::SEARCH_FILTER_PARAMETER, std::string()));

                std::vector<std::string> aps;
                boost::split(aps, filter, boost::is_any_of(","));

                for (const auto& ap : aps)
                {
                    bl::domain::ResourceLocator* rl = creq.add_items();
                    rl->set_access_point(ap);
                }

                CameraListCallback_t<bl::domain::BatchGetCamerasResponse> cb = std::bind(&CCameraListContentImpl::onBatchGetCamerasResponse,
                    boost::weak_ptr<CCameraListContentImpl>(shared_from_base<CCameraListContentImpl>()), req, queryId,
                    std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

                reader->asyncRequest(creq, cb);
            }
            catch (const std::invalid_argument& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::InternalServerError);
            }
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

        void eraseResponseFromMap(boost::uuids::uuid queryId)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_responses.erase(queryId);
        }

        static void onListCamerasResponse(boost::weak_ptr<CCameraListContentImpl> obj, const PRequest req,
            boost::uuids::uuid queryId, const bl::domain::ListCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CCameraListContentImpl> owner = obj.lock();
            if (owner)
            {
                PQueryContext qc = owner->getResponse(queryId);
                if (!qc)
                    return;

                if (!grpcStatus.ok())
                {
                    owner->eraseResponseFromMap(queryId);
                    owner->SendResponse(req, qc->m_response, Json::Value(Json::arrayValue));
                    return;
                }

                Json::Value& responseObject = qc->m_responsePresentation;

                owner->processCameras(responseObject, res.items());
                owner->processSearchMetaData(responseObject, res.search_meta_data());

                if (!res.next_page_token().empty())
                    responseObject["nextPageToken"] = res.next_page_token();

                if (NWebGrpc::_FINISH == status)
                {
                    owner->eraseResponseFromMap(queryId);
                    owner->SendResponse(req, qc->m_response, responseObject, MAX_AGE);
                    return;
                }
            }
        }

        static void onBatchGetCamerasResponse(boost::weak_ptr<CCameraListContentImpl> obj, const PRequest req,
            boost::uuids::uuid queryId, const bl::domain::BatchGetCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CCameraListContentImpl> owner = obj.lock();
            if (owner)
            {
                PQueryContext qc = owner->getResponse(queryId);
                if (!qc)
                    return;

                if (!grpcStatus.ok())
                {
                    owner->eraseResponseFromMap(queryId);
                    owner->SendResponse(req, qc->m_response, Json::Value(Json::arrayValue));
                    return;
                }

                Json::Value& responseObject = qc->m_responsePresentation;

                owner->processCameras(responseObject, res.items());

                if (NWebGrpc::_FINISH == status)
                {
                    owner->eraseResponseFromMap(queryId);
                    owner->SendResponse(req, qc->m_response, responseObject, MAX_AGE);
                }
            }
        }

        void processCameras(Json::Value& responseObject, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                Json::Value camera(Json::objectValue);

                const bl::domain::Camera& c = cams.Get(i);
                camera["accessPoint"] = c.access_point();
                camera["displayId"] = c.display_id();
                camera["displayName"] = c.display_name();
                camera["enabled"] = c.enabled();
                camera["vendor"] = c.vendor();
                camera["model"] = c.model();
                camera["comment"] = c.comment();
                camera["camera_access"] = ECameraAccess_Name(c.camera_access());

                camera["ipAddress"] = c.ip_address();
                camera["isActivated"] = c.is_activated();

                camera["azimuth"] = c.geo_location_azimuth();
                camera["latitude"] = c.geo_location_latitude();
                camera["longitude"] = c.geo_location_longitude();
                camera["panomorph"] = c.panomorph().enabled();
                if (c.panomorph().enabled())
                    camera["panomorphPosition"] = c.panomorph().camera_position();

                camera["groups"] = Json::Value(Json::arrayValue);
                int gCount = c.group_ids_size();
                for (int g = 0; g < gCount; ++g)
                {
                    camera["groups"].append(c.group_ids(g));
                }

                camera["videoStreams"] = Json::Value(Json::arrayValue);
                int vsCount = c.video_streams_size();
                for (int j = 0; j < vsCount; ++j)
                {
                    const bl::domain::VideoStreaming& vs = c.video_streams(j);

                    Json::Value stream(Json::objectValue);
                    stream["accessPoint"] = vs.stream_acess_point();
                    camera["videoStreams"].append(stream);
                }

                camera["audioStreams"] = Json::Value(Json::arrayValue);
                int asCount = c.microphones_size();
                for (int j = 0; j < asCount; ++j)
                {
                    const bl::domain::AudioStreaming& as = c.microphones(j);

                    Json::Value stream(Json::objectValue);
                    stream["accessPoint"] = as.access_point();
                    stream["isActivated"] = as.is_activated();

                    camera["audioStreams"].append(stream);
                }

                camera["ptzs"] = Json::Value(Json::arrayValue);
                int ptzCount = c.ptzs_size();
                for (int j = 0; j < ptzCount; ++j)
                {
                    const bl::domain::Telemetry& tel = c.ptzs(j);
                    const bl::domain::TelemetryCapabilities& tc = tel.capabilities();

                    Json::Value ptz(Json::objectValue);
                    ptz["accessPoint"] = tel.access_point();
                    ptz["is_active"] = tel.is_activated();
                    processTelemetryMode("move", ptz, tc.move_supported());
                    processTelemetryMode("focus", ptz, tc.focus_supported());
                    processTelemetryMode("zoom", ptz, tc.zoom_supported());
                    processTelemetryMode("iris", ptz, tc.iris_supported());
                    ptz["areaZoom"] = tc.is_area_zoom_supported();
                    ptz["pointMove"] = tc.is_point_move_supported();

                    camera["ptzs"].append(ptz);
                }

                camera["textSources"] = Json::Value(Json::arrayValue);
                int textSourceSize = c.text_sources_size();
                for (int j = 0; j < textSourceSize; ++j)
                {
                    Json::Value textSource(Json::objectValue);
                    const bl::domain::TextSource& txtSource = c.text_sources(j);
                    textSource["accessPoint"] =  txtSource.access_point();
                    camera["textSources"].append(textSource);
                }

                camera["archives"] = Json::Value(Json::arrayValue);
                int arcCount = c.archive_bindings_size();
                for (int j = 0; j < arcCount; ++j)
                {
                    const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                    if (ab.has_archive() && (ab.archive().is_activated() || ab.archive().incomplete()))
                    {
                        auto it = std::find_if(ab.sources().begin(), ab.sources().end(),
                            [&](const bl::domain::StorageSource& c)
                        {
                            return boost::contains(c.media_source(), VIDEO_SOURCE); //web-client needs only accessPoints to video(!) Sources
                        });

                        if (it == ab.sources().end())
                            continue;

                        Json::Value arc(Json::objectValue);
                        arc["accessPoint"] = it->media_source();
                        arc["storage"] = ab.storage();
                        arc["storageDisplayName"] = (ab.has_archive() ? ab.archive().display_name() : std::string());
                        arc["isEmbedded"] = ab.has_archive() ? ab.archive().is_embedded() : false;
                        arc["default"] = ab.is_default();
                        arc["incomplete"] = ab.archive().incomplete();

                        camera["archives"].append(arc);
                    }
                }

                camera["detectors"] = Json::Value(Json::arrayValue);
                int detectorCount = c.detectors_size();
                for (int j = 0; j < detectorCount; ++j)
                {
                    const bl::domain::Detector& d = c.detectors(j);

                    Json::Value det(Json::objectValue);
                    det["accessPoint"] = d.access_point();
                    det["displayName"] = d.display_name();
                    det["parentDetector"] = d.parent_detector();
                    det["type"] = NPluginUtility::Convert(d.events());
                    det["isActivated"] = d.is_activated();

                    addDetectorEvents(det, d);

                    camera["detectors"].append(det);
                }

                camera["offlineDetectors"] = Json::Value(Json::arrayValue);
                int offlineCount = c.offline_detectors_size();
                for (int j = 0; j < offlineCount; ++j)
                {
                    const bl::domain::Detector& d = c.offline_detectors(j);

                    Json::Value det(Json::objectValue);
                    det["accessPoint"] = d.access_point();
                    det["displayName"] = d.display_name();

                    addDetectorEvents(det, d);

                    camera["offlineDetectors"].append(det);
                }

                responseObject["cameras"].append(camera);
            }
        }

        void processSearchMetaData(Json::Value& responseObject,
                                   const ::google::protobuf::RepeatedPtrField<::axxonsoft::bl::domain::SearchMetaData>& searchMetaData)
        {
            for (const auto& data : searchMetaData)
            {
                auto localData = Json::Value(Json::objectValue);
                localData["score"] = data.score();
                localData["matches"] = Json::Value(Json::arrayValue);
                for (const auto& match : data.matches())
                {
                    localData["matches"].append(match);
                }
                responseObject["search_meta_data"].append(localData);
            }
        }

        void addDetectorEvents(Json::Value& det, const bl::domain::Detector& d)
        {
            Json::Value dEvents(Json::arrayValue);
            int eventCount = d.events_size();
            for (int k = 0; k < eventCount; ++k)
            {
                const bl::domain::DetectorEventInfo& dei = d.events(k);
                dEvents.append(dei.id());
            }
            det["events"] = dEvents;
        }

        static void processTelemetryMode(const char* const ptzMode, Json::Value& ptz, const bl::ptz::Capabilities& caps)
        {
            Json::Value mode(Json::objectValue);
            mode["isRelative"] = caps.is_relative();
            mode["isContinous"] = caps.is_continuous();
            mode["isAbsolute"] = caps.is_absolute();
            mode["isAuto"] = caps.is_auto();

            ptz[ptzMode] = mode;
        }

        void SendResponse(const PRequest req, NHttp::PResponse resp, const Json::Value& responseObject, int maxAge = -1) const
        {
            if (-1 == maxAge)
                _wrn_ << "Camera list GRPC error";

            NPluginUtility::SendText(req, resp, responseObject.toStyledString(), NHttp::IResponse::OK, maxAge);
        }

        const NWebGrpc::PGrpcManager m_grpcManager;

        std::mutex m_mutex;
        std::map<boost::uuids::uuid, PQueryContext> m_responses;
    };
}

namespace NHttp
{
    IServlet* CreateCameraListServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CCameraListContentImpl(GET_LOGGER_PTR, grpcManager);
    }
}
