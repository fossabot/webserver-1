#include "HttpPlugin.h"
#include "CommonUtility.h"
#include "Constants.h"

#include <HttpServer/BasicServletImpl.h>

#include <boost/format.hpp>

#include <json/json.h>
#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

namespace
{
    using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
        bl::domain::BatchGetCamerasResponse >;

    using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

    const char* const VIDEO_SOURCE = "SourceEndpoint.video";
    const char* const AUDIO_SOURCE = "SourceEndpoint.audio";

    class CArchiveListContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CArchiveListContentImpl(NCorbaHelpers::IContainer *c,  const NWebGrpc::PGrpcManager grpcManager)
            : m_container(c)
            , m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            try
            {
                NCorbaHelpers::PContainer cont = m_container;
                if (!cont)
                {
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                PObjName ep = ObjName(3, "hosts/");
                if (!Match(req->GetPathInfo(), ep))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                std::string targetEp(ep->Get().ToString());

                bl::domain::BatchGetCamerasRequest creq;                
                bl::domain::ResourceLocator* rl = creq.add_items();

                rl->set_access_point(convertToVideoMainStream(targetEp));

                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                PBatchCameraReader_t grpcReader(new BatchCameraReader_t
                    (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncBatchGetCameras));

                auto ctxOut = std::make_shared<Json::Value>();
                (*ctxOut)["archives"] = Json::Value(Json::arrayValue);
                grpcReader->asyncRequest(creq, [req, resp, targetEp, ctxOut](const bl::domain::BatchGetCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus) mutable
                    {
                        if (!grpcStatus.ok())
                        {
                            return NPluginUtility::SendGRPCError(resp, grpcStatus);
                        }

                        processCameras(targetEp, (*ctxOut)["archives"], res.items());
                        if (NWebGrpc::_FINISH == status)
                        {
                            NPluginUtility::SendText(req, resp, ctxOut->toStyledString());
                        }
                    }
                );

            }
            catch (const std::invalid_argument& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::InternalServerError);
            }
        }

    private:

        static void processCameras(const std::string &targetEp, Json::Value& responseObject, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);
                int arcCount = c.archive_bindings_size();
                for (int j = 0; j < arcCount; ++j)
                {
                    Json::Value arc(Json::objectValue);

                    const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                    auto it = std::find_if(ab.sources().begin(), ab.sources().end(),
                        [&](const bl::domain::StorageSource& sS)
                    {
                        return sS.media_source() == targetEp;
                    });

                    if (it == ab.sources().end())
                        continue;

                    arc["name"] = ab.storage();
                    arc["default"] = ab.is_default();

                    responseObject.append(arc);
                }
            }
        }

        static std::string convertToVideoMainStream(const std::string& cam)
        {
            std::size_t found = cam.rfind("/SourceEndpoint");
            return cam.substr(0, found) + "/SourceEndpoint.video:0:0";
        }

        NCorbaHelpers::WPContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateArchiveListServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CArchiveListContentImpl(c, grpcManager);
    }
}
