#include "HttpPlugin.h"
#include "Constants.h"
#include "CommonUtility.h"

#include <HttpServer/BasicServletImpl.h>
#include "GrpcReader.h"
#include <axxonsoft/bl/discovery/Discovery.grpc.pb.h>
#include <json/json.h>

using namespace NHttp;

namespace bl = axxonsoft::bl;

namespace
{
    using DiscoveryReader_t = NWebGrpc::AsyncResultReader < bl::discovery::DiscoveryService, google::protobuf::Empty,
        google::protobuf::Empty >;
    using PDiscoveryReader_t = std::shared_ptr < DiscoveryReader_t >;

    using DiscoveryProgressReader_t = NWebGrpc::AsyncStreamReader < bl::discovery::DiscoveryService, google::protobuf::Empty,
        bl::discovery::GetDiscoveryProgressResponse >;
    using PDiscoveryProgressReader_t = std::shared_ptr < DiscoveryProgressReader_t >;


    const char* const DEVICES_FIELD = "devices";

    class CDiscoveryCamerasImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:

        CDiscoveryCamerasImpl(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
            : m_container(c)
            , m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        void Get(const PRequest req, PResponse resp) override
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                Error(resp, IResponse::InternalServerError);
                return;
            }

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            auto reader = std::make_shared<DiscoveryReader_t>(GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::discovery::DiscoveryService::Stub::AsyncDiscover);
            auto obj = boost::weak_ptr<CDiscoveryCamerasImpl>(shared_from_base<CDiscoveryCamerasImpl>());

            reader->asyncRequest(google::protobuf::Empty(), [obj, metaCredentials, req, resp](const google::protobuf::Empty&, grpc::Status grpcStatus)
            {
                boost::shared_ptr<CDiscoveryCamerasImpl> owner = obj.lock();
                if (owner)
                {
                    if (!grpcStatus.ok())
                    {
                        return NPluginUtility::SendGRPCError(resp, grpcStatus);
                    }

                    owner->onDiscovery(metaCredentials, resp);
                }
            });
        }

    private:
        void onDiscovery(NGrpcHelpers::PCredentials metaCredentials, PResponse resp)
        {
            _log_ << "Discovery process starting...";

            auto reader = std::make_shared<DiscoveryProgressReader_t>(GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::discovery::DiscoveryService::Stub::AsyncGetDiscoveryProgress);
            auto obj = boost::weak_ptr<CDiscoveryCamerasImpl>(shared_from_base<CDiscoveryCamerasImpl>());

            std::shared_ptr<Json::Value> ctx = std::make_shared<Json::Value>(Json::arrayValue);

            reader->asyncRequest(google::protobuf::Empty(), [obj, ctx, resp](const bl::discovery::GetDiscoveryProgressResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
            {
                boost::shared_ptr<CDiscoveryCamerasImpl> owner = obj.lock();
                if (owner)
                    owner->execDiscovery(resp, ctx, res, status, grpcStatus);
            });
        }

        void execDiscovery(PResponse resp,
                           std::shared_ptr<Json::Value> ctx,
                           const bl::discovery::GetDiscoveryProgressResponse& res,
                           NWebGrpc::STREAM_ANSWER status,
                           grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                _err_ << "NativeBL discovery error";
                return NPluginUtility::SendGRPCError(resp, grpcStatus);
            }

            processDiscovery(ctx, res);

            if (NWebGrpc::_FINISH == status)
            {
                NHttp::SHttpHeader connectionHeader(CONNECTION_HEADER, CONNECTION_TYPE);

                resp->SetStatus(IResponse::OK);
                resp << ContentType(NHttp::GetMIMETypeByExt("json")) << CacheControlNoCache() << connectionHeader;

                resp->FlushHeaders();

                sendDiscoveryData(ctx, resp);
            }
        }

        void processDiscovery(std::shared_ptr<Json::Value> ctxOut, const bl::discovery::GetDiscoveryProgressResponse& res)
        {
            int size = res.device_description_size();
            for (int i = 0; i < size; ++i)
            {
                Json::Value device(Json::objectValue);
                auto &dd = res.device_description(i);               
                
                device["driver"] = dd.driver();
                device["driver_version"] = dd.driver_version();                
                device["mac_address"] = dd.mac_address();
                device["ip_address"] = dd.ip_address();
                device["wan_address"] = dd.wan_address();
                device["ip_port"] = dd.ip_port();
                device["vendor"] = dd.vendor();
                device["model"] = dd.model();
                device["firmware_version"] = dd.firmware_version();
                device["device_description"] = dd.device_description();

                ctxOut->append(device);
            }
        }

        void sendDiscoveryData(std::shared_ptr<Json::Value> ctx, PResponse resp)
        {
            static const char CRLF[] = { '\r', '\n' };

            std::vector<boost::asio::const_buffer> buffers;
            buffers.push_back(boost::asio::buffer(ctx->toStyledString()));
            buffers.push_back(boost::asio::buffer(CRLF));
            buffers.push_back(boost::asio::buffer(CRLF));

            std::unique_lock<std::mutex> lock(m_mutex);
            try
            {
                resp->AsyncWrite(buffers, [ctx](boost::system::error_code){});
            }
            catch (...)
            {
                _wrn_ << "Failed to send discovery info";
            }
        }
    private:
        NCorbaHelpers::WPContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;

        std::mutex m_mutex;
    };
}

namespace NHttp
{
    IServlet* CreateDiscoverCamerasServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CDiscoveryCamerasImpl(c, grpcManager);
    }
}