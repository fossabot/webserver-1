#include "HttpPlugin.h"
#include "Constants.h"
#include "GrpcReader.h"

#include <HttpServer/HttpServer.h>

#include <GrpcHelpers/MetaCredentials.h>

#include <axxonsoft/bl/security/SecurityService.grpc.pb.h>

using namespace NHttp;
namespace bl = axxonsoft::bl;

namespace
{
    using PermissionsReader_t = NWebGrpc::AsyncResultReader < bl::security::SecurityService, ::google::protobuf::Empty,
        bl::security::ListUserGlobalPermissionsResponse >;
    using PPermissionsReader_t = std::shared_ptr < PermissionsReader_t >;
    using PermissionCallback_t = std::function < void(const bl::security::ListUserGlobalPermissionsResponse&, grpc::Status) >;

    class CRightsInterceptor : public NHttp::IAccessChecker
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CRightsInterceptor(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER;
        }

        void HasPermissions(const NHttp::PRequest creq, NHttp::AllowCallback_t ac, NHttp::DenyCallback_t dc) override
        {
            const IRequest::AuthSession& as = creq->GetAuthSession();
            if (TOKEN_AUTH_SESSION_ID == as.id)
            {
                ac();
                return;
            }

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials( as.data.first ? *(as.data.first) : "");

            PPermissionsReader_t reader(new PermissionsReader_t
            (GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::security::SecurityService::Stub::AsyncListUserGlobalPermissions));

            ::google::protobuf::Empty req;

            PermissionCallback_t cb = std::bind(&CRightsInterceptor::onPermissions,
                shared_from_base<CRightsInterceptor>(), ac, dc,
                std::placeholders::_1, std::placeholders::_2);

            reader->asyncRequest(req, cb);
        }

    private:
        void onPermissions(NHttp::AllowCallback_t ac, NHttp::DenyCallback_t dc,
            const bl::security::ListUserGlobalPermissionsResponse& resp, grpc::Status valid)
        {
            if (valid.ok())
            {
                google::protobuf::Map<std::string, bl::security::GlobalPermissions>::const_iterator it1 = resp.permissions().begin(),
                    it2 = resp.permissions().end();
                for (; it1 != it2; ++it1)
                {
                    const bl::security::GlobalPermissions& p = it1->second;

                    if (bl::security::UNRESTRICTED_ACCESS_YES == p.unrestricted_access())
                    {
                        ac();
                        return;
                    }

                    const ::google::protobuf::RepeatedField<int>& features = p.feature_access();
                    if (features.cend() != std::find(features.cbegin(), features.cend(), bl::security::FEATURE_ACCESS_WEB_UI_LOGIN))
                    {
                        ac();
                        return;
                    }
                }
            }

            dc();
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IAccessChecker* CreateRightsInterceptor(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CRightsInterceptor(GET_LOGGER_PTR, grpcManager);
    }
}
