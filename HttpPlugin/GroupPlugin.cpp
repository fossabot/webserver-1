#include "HttpPlugin.h"
#include "GrpcReader.h"
#include "CommonUtility.h"
#include "Tokens.h"
#include "Constants.h"

#include <HttpServer/BasicServletImpl.h>

#include <json/json.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>
#include <axxonsoft/bl/groups/GroupManager.grpc.pb.h>

//#include <boost/format.hpp>

using namespace NHttp;
using namespace NPluginUtility;

namespace
{
    const char* const CONTAINS = "contains";

    namespace bl = axxonsoft::bl;
    using GroupReader_t = NWebGrpc::AsyncResultReader < bl::groups::GroupManager, bl::groups::ListGroupsRequest, bl::groups::ListGroupsResponse>;
    using PGroupReader_t = std::shared_ptr < GroupReader_t >;
    using GroupListCallback_t = std::function<void(const bl::groups::ListGroupsResponse&, grpc::Status)>;

    using ListCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::ListCamerasRequest,
        bl::domain::ListCamerasResponse >;
    using PListCameraReader_t = std::shared_ptr < ListCameraReader_t >;

    using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
        bl::domain::BatchGetCamerasResponse >;
    using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

    template <typename TResponse>
    using CameraListCallback_t = std::function<void(const TResponse&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    class CGroupContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;

        struct SQueryContext
        {
            SQueryContext(PResponse resp, const std::string& groupId, const std::string& arrayName)
                : m_response(resp)
                , m_groupId(groupId)
                , m_arrayName(arrayName)
                , m_responsePresentation(Json::objectValue)
            {
                m_responsePresentation[arrayName] = Json::Value(Json::arrayValue);
            }

            Json::Value& GetArray()
            {
                return m_responsePresentation[m_arrayName];
            }
            PResponse m_response;
            std::string m_nextPageToken;
            std::string m_groupId;
            std::string m_arrayName;
            Json::Value m_responsePresentation;
        };
        typedef std::shared_ptr<SQueryContext> PQueryContext;

    public:
        CGroupContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            std::string pathInfo(req->GetPathInfo());
            if (pathInfo.empty())
            {
                PGroupReader_t reader(new GroupReader_t
                    (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::groups::GroupManager::Stub::AsyncListGroups));

                bl::groups::ListGroupsRequest creq;
                creq.set_view(bl::groups::VIEW_MODE_TREE);

                GroupListCallback_t cb = std::bind(&CGroupContentImpl::onListGroupsResponse,
                    boost::weak_ptr<CGroupContentImpl>(shared_from_base<CGroupContentImpl>()), req, resp,
                    std::placeholders::_1, std::placeholders::_2);

                reader->asyncRequest(creq, cb);
            }
            else
            {
                PObjName ep = ObjName(3, "hosts/");

                std::string id;
                PToken tokenId = Token(id);

                if (Match(pathInfo, Mask(CONTAINS) / ep))
                {
                    PQueryContext qc = std::make_shared<SQueryContext>(resp, id, "groups");

                    PBatchCameraReader_t reader(new BatchCameraReader_t
                        (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncBatchGetCameras/*, true*/));

                    bl::domain::BatchGetCamerasRequest creq;
                    bl::domain::ResourceLocator* rl = creq.add_items();
                    std::string ap(ep->Get().ToString());
                    rl->set_access_point(ep->Get().ToString());

                    CameraListCallback_t<bl::domain::BatchGetCamerasResponse> cb = std::bind(&CGroupContentImpl::onBatchGetCamerasResponse,
                        boost::weak_ptr<CGroupContentImpl>(shared_from_base<CGroupContentImpl>()), req, qc,
                        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

                    reader->asyncRequest(creq, cb);
                }
                else if (Match(pathInfo, tokenId) && !id.empty())
                {
                    PQueryContext qc = std::make_shared<SQueryContext>(resp, id, "members");

                    sendListCamerasQuery(req, metaCredentials, qc);
                }
                else
                {
                    _err_ << "No handlers found";
                    Error(resp, IResponse::NotFound);
                }
            }
        }

    private:
        static void onListGroupsResponse(boost::weak_ptr<CGroupContentImpl> obj, const PRequest req, PResponse resp,
            const bl::groups::ListGroupsResponse& res, grpc::Status)
        {
            boost::shared_ptr<CGroupContentImpl> owner = obj.lock();
            if (owner)
            {
                Json::Value responseObject(Json::objectValue);
                Json::Value groups(Json::arrayValue);
                
                int groupCount = res.groups_size();
                for (int i = 0; i < groupCount; ++i)
                {
                    addGroup(groups, res.groups(i));                   
                }
                responseObject["groups"] = groups;

                WriteResponse(req, resp, responseObject);
            }
        }

        static void addGroup(Json::Value& groups, const bl::groups::Group& group)
        {
            Json::Value g(Json::objectValue);
            g["Brief"] = group.name();
            g["Description"] = group.description();
            g["Id"] = group.group_id();
            g["ObjectCount"] = 0;

            Json::Value nested(Json::arrayValue);
            
            int groupCount = group.groups_size();
            for (int i = 0; i < groupCount; ++i)
            {
                addGroup(nested, group.groups(i));
            }
            g["groups"] = nested;

            groups.append(g);        
        }

        void sendListCamerasQuery(const PRequest req, NGrpcHelpers::PCredentials metaCredentials, PQueryContext qc)
        {
            PListCameraReader_t reader(new ListCameraReader_t
                (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncListCameras));

            bl::domain::ListCamerasRequest creq;
            std::string* rl = creq.add_group_ids();
            *rl = qc->m_groupId;
            creq.set_page_token(qc->m_nextPageToken);
            creq.set_view(bl::domain::EViewMode::VIEW_MODE_FULL);

            CameraListCallback_t<bl::domain::ListCamerasResponse> cb = std::bind(&CGroupContentImpl::onListCamerasResponse,
                boost::weak_ptr<CGroupContentImpl>(shared_from_base<CGroupContentImpl>()), metaCredentials, req, qc,
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

            reader->asyncRequest(creq, cb);
        }

        static void onListCamerasResponse(boost::weak_ptr<CGroupContentImpl> obj, NGrpcHelpers::PCredentials metaCredentials, 
            const PRequest req, PQueryContext qc,
            const bl::domain::ListCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CGroupContentImpl> owner = obj.lock();
            if (owner)
            {
                if (!grpcStatus.ok())
                {
                    return NPluginUtility::SendGRPCError(qc->m_response, grpcStatus);
                }

                if (!res.next_page_token().empty())
                {
                    //owner->log(boost::str(boost::format("Set next page token: %1%") % res.next_page_token()).c_str());
                    qc->m_nextPageToken = res.next_page_token();
                }

                int itemCount = res.items_size();
                for (int i = 0; i < itemCount; ++i)
                {
                    const bl::domain::Camera& cam = res.items(i);
                    Json::Value v(Json::stringValue);
                    v = cam.access_point();
                    qc->GetArray().append(v);
                }

                if (!res.next_page_token().empty() && NWebGrpc::_FINISH == status)
                {
                    //owner->log(boost::str(boost::format("Send next query with token: %1%") % qc->m_nextPageToken).c_str());
                    owner->sendListCamerasQuery(req, metaCredentials, qc);
                }

                if (res.next_page_token().empty() && NWebGrpc::_FINISH == status)
                {
                    WriteResponse(req, qc->m_response, qc->m_responsePresentation);
                }
            }
        }

        /*void log(const char* const msg)
        {
            _log_ << msg;
        }*/

        static void onBatchGetCamerasResponse(boost::weak_ptr<CGroupContentImpl> obj, const PRequest req, PQueryContext qc,
            const bl::domain::BatchGetCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CGroupContentImpl> owner = obj.lock();
            if (owner)
            {
                if (!grpcStatus.ok())
                {
                    return NPluginUtility::SendGRPCError(qc->m_response, grpcStatus);
                }

                int itemCount = res.items_size();
                for (int i = 0; i < itemCount; ++i)
                {
                    const bl::domain::Camera& cam = res.items(i);
                    int groupCount = cam.group_ids_size();
                    for (int j = 0; j < groupCount; ++j)
                    {
                        Json::Value v(Json::stringValue);
                        v = cam.group_ids(j);
                        qc->GetArray().append(v);
                    }
                }

                if (NWebGrpc::_FINISH == status)
                    WriteResponse(req, qc->m_response, qc->m_responsePresentation);
            }
        }

        static void WriteResponse(const PRequest req, PResponse resp, Json::Value& v)
        {
            NPluginUtility::SendText(req, resp, v.toStyledString());
        }

        void sendError(PResponse resp, NHttp::IResponse::EStatus error)
        {
            Error(resp, error);
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateGroupServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CGroupContentImpl(GET_LOGGER_PTR, grpcManager);
    }
}
