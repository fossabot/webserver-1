
#include <boost/make_shared.hpp>

#include "GrpcReader.h"
#include <CorbaHelpers/Refcounted.h>
#include <CorbaHelpers/Container.h>

#include <WebSocketSession.h>
#include "MetaCredentialsStorage.h"

#include <axxonsoft/bl/security/SecurityService.grpc.pb.h>

namespace bl = axxonsoft::bl;

using PermissionsReader_t = NWebGrpc::AsyncResultReader < bl::security::SecurityService, ::google::protobuf::Empty,
    bl::security::ListUserGlobalPermissionsResponse >;
using PPermissionsReader_t = std::shared_ptr < PermissionsReader_t >;
using PermissionCallback_t = std::function<void(const bl::security::ListUserGlobalPermissionsResponse&, grpc::Status)>;


namespace
{
    const char* const PARAM_AUTH_TOKEN = "auth_token";
}

namespace NHttp
{
    class CMetaCredentialsStorage : public IMetaCredentialsStorage,
                                    public std::enable_shared_from_this<CMetaCredentialsStorage>
    {
        DECLARE_LOGGER_HOLDER;

    public:
        CMetaCredentialsStorage(DECLARE_LOGGER_ARG,
                                const NWebGrpc::PGrpcManager grpcManager,
                                NGrpcHelpers::PCredentials metaCredentials,
                                const PRequest req)
            : m_grpcManager(grpcManager)
            , m_metaCredentials(metaCredentials)
        {
            INIT_LOGGER_HOLDER;
            if (req)
            {
                m_hasAuthSession = true;
                m_authSession = req->GetAuthSession();
            }
        }

        ~CMetaCredentialsStorage() { }

        NGrpcHelpers::PCredentials GetMetaCredentials() override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_metaCredentials;
        }

        const NHttp::IRequest::AuthSession& GetAuthSession() override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_authSession;
        }

        void UpdateToken(const std::string& new_token, NHttp::DenyCallback_t dc) override
        {
            if (new_token.empty())
                return;

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials(new_token);
            PPermissionsReader_t reader(new PermissionsReader_t(GET_LOGGER_PTR,
                                            m_grpcManager,
                                            metaCredentials,
                                            &bl::security::SecurityService::Stub::AsyncListUserGlobalPermissions));

            ::google::protobuf::Empty req;
            NHttp::AllowCallback_t ac = std::bind(&CMetaCredentialsStorage::ProcessUpdateToken,
                        this->shared_from_this(), new_token, metaCredentials);
            PermissionCallback_t cb =
                std::bind(&CMetaCredentialsStorage::onPermissions, this->shared_from_this(), ac, dc,
                    std::placeholders::_1, std::placeholders::_2);
            reader->asyncRequest(req, cb);
        }

        void ProcessUpdateToken(const std::string newToken, NGrpcHelpers::PCredentials newCreds)
        {
            _inf_ << "Token has updated successfull." << newToken;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_metaCredentials = newCreds;
            if (m_hasAuthSession)
                m_authSession.data.first = std::make_shared<NHttp::IRequest::AuthSessionData_t>(newToken);
        }

        void onPermissions(NHttp::AllowCallback_t ac,
                           NHttp::DenyCallback_t dc,
                           const bl::security::ListUserGlobalPermissionsResponse& resp,
                           grpc::Status valid)
        {
            if (valid.ok())
            {
                for (auto& permissionIt : resp.permissions())
                {
                    const bl::security::GlobalPermissions& p = permissionIt.second;
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

    private:
        NCorbaHelpers::PContainer m_container;
        NWebGrpc::PGrpcManager m_grpcManager;
        std::mutex m_mutex;
        NGrpcHelpers::PCredentials m_metaCredentials;
        bool m_hasAuthSession = false;
        NHttp::IRequest::AuthSession m_authSession;
     };
}

namespace NHttp
{
    PMetaCredentialsStorage CreateMetaCredentialsStorage(DECLARE_LOGGER_ARG,
                                                         const NWebGrpc::PGrpcManager grpcManager,
                                                         NGrpcHelpers::PCredentials metaCredentials,
                                                         const PRequest req)
    {
        return std::make_shared<CMetaCredentialsStorage>(GET_LOGGER_PTR, grpcManager, metaCredentials, req);
    }
}  // namespace NHttp