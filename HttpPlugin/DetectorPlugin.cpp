#include "HttpPlugin.h"
#include "Constants.h"
#include "BLQueryHelper.h"

#include "CommonUtility.h"

#include <HttpServer/BasicServletImpl.h>
#include <HttpServer/HttpRequest.h>
#include <HttpServer/HttpResponse.h>

#include <json/json.h>

using namespace NHttp;
namespace npu = NPluginUtility;

namespace bl = axxonsoft::bl;

namespace
{
    const char* const ORIGIN_CHANNEL = "/SourceEndpoint.video:0:0";

    using PDetectorContext = std::shared_ptr<std::vector<std::pair<std::string, std::string> > >;

    class CDetectorContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CDetectorContentImpl(NCorbaHelpers::IContainer *c, const NWebGrpc::PGrpcManager grpcManager)
            :m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            npu::PObjName camera = npu::ObjName(2, "hosts/");
            if (!npu::Match(req->GetPathInfo(), camera))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            PDetectorContext ctxOut = std::make_shared<std::vector<std::pair<std::string, std::string> > >();
            NWebBL::TEndpoints eps{ camera->Get().ToString() + ORIGIN_CHANNEL };
            NWebBL::FAction action = boost::bind(&CDetectorContentImpl::onCameraInfo, shared_from_base<CDetectorContentImpl>(),
                req, resp, ctxOut, _1, _2, _3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, metaCredentials, eps, action);
        }

    private:
        void onCameraInfo(const PRequest req, PResponse resp, PDetectorContext ctxOut,
            const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                _err_ << "/detectors: GetCamerasByComponents method failed";
                return NPluginUtility::SendGRPCError(resp, grpcStatus);
            }

            gatherDetectorInfo(ctxOut, cams);

            if (valid == NWebGrpc::_FINISH)
            {
                NPluginUtility::SendText(req, resp, convert(ctxOut));
            }
        }

        void gatherDetectorInfo(PDetectorContext ctxOut, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                Json::Value camera(Json::objectValue);

                const bl::domain::Camera& c = cams.Get(i);

                int detectorCount = c.detectors_size();
                for (int j = 0; j < detectorCount; ++j)
                {
                    const bl::domain::Detector& d = c.detectors(j);

                    if (d.is_activated())
                        ctxOut->push_back(std::make_pair(d.access_point(), NPluginUtility::Convert(d.events())));
                }
            }
        }

        static std::string convert(std::shared_ptr<std::vector<std::pair<std::string, std::string>>> ctxOut)
        {
            Json::Value res;
            res["detectors"] = Json::Value(Json::arrayValue);

            for (const auto &t : *ctxOut)
            {
                Json::Value det;
                det["name"] = t.first;
                det["type"] = t.second;
                res["detectors"].append(det);
            }

            return res.toStyledString();
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateDetectorServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CDetectorContentImpl(c, grpcManager);
    }
}
