#include "RightsChecker.h"
#include "Constants.h"
#include "BLQueryHelper.h"
#include "CommonUtility.h"

#include <axxonsoft/bl/security/SecurityService.grpc.pb.h>

namespace bl = axxonsoft::bl;

namespace
{
    const char* const VIDEO_SOURCE = "SourceEndpoint.video";
    const char* const AUDIO_SOURCE = "SourceEndpoint.audio";

    using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
        bl::domain::BatchGetCamerasResponse >;
    using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;
    using CameraListCallback_t = std::function<void(const bl::domain::BatchGetCamerasResponse&, NWebGrpc::STREAM_ANSWER)>;

    using PermissionsReader_t = NWebGrpc::AsyncResultReader < bl::security::SecurityService, ::google::protobuf::Empty,
        bl::security::ListUserGlobalPermissionsResponse > ;
    using PPermissionsReader_t = std::shared_ptr < PermissionsReader_t > ;
    using PermissionCallback_t = std::function < void(const bl::security::ListUserGlobalPermissionsResponse&, grpc::Status) > ;

    class CRightsChecker : public NPluginUtility::IRigthsChecker
    {
        DECLARE_LOGGER_HOLDER;

        struct SCameraContext
        {
            SCameraContext()
                : m_cameraAccessLevel(axxonsoft::bl::security::CAMERA_ACCESS_UNSPECIFIED)
            {}
            int m_cameraAccessLevel;
        };
        typedef std::shared_ptr<SCameraContext> PCameraContext;
    public:
        CRightsChecker(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER;
        }

        void IsCameraAllowed(const std::string& cam, const AuthSession& session, NPluginUtility::IsAllowCallbackInt_t acb) override
        {
            if (TOKEN_AUTH_SESSION_ID == session.id)
            {
                acb(axxonsoft::bl::security::ARCHIVE_ACCESS_FULL);
                return;
            }

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials( session.data.first?*(session.data.first):"");

            PCameraContext ctxOut = std::make_shared<SCameraContext>();
            NWebBL::TEndpoints eps{ cam };
            NWebBL::FAction action = boost::bind(&CRightsChecker::onCameraInfo, shared_from_base<CRightsChecker>(),
                ctxOut, acb, _1, _2, _3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, metaCredentials, eps, action);
        }

        void HasAlarmAccess(const AuthSession& session, NPluginUtility::IsAllowCallback_t acb) override
        {
            if (TOKEN_AUTH_SESSION_ID == session.id)
            {
                acb(true);
            }

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials( session.data.first ? *(session.data.first) : "");

            PPermissionsReader_t reader(new PermissionsReader_t
                (GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::security::SecurityService::Stub::AsyncListUserGlobalPermissions));

            reader->asyncRequest(::google::protobuf::Empty(),
                [acb](const bl::security::ListUserGlobalPermissionsResponse& resp, grpc::Status valid)
            {
                if (!valid.ok() || (0 == resp.permissions_size()))
                {
                    acb(false);
                    return;
                }

                const bl::security::GlobalPermissions& p = resp.permissions().begin()->second;

                if (bl::security::UNRESTRICTED_ACCESS_YES == p.unrestricted_access())
                {
                    acb(true);
                    return;
                }

                acb((bl::security::ALERT_ACCESS_FORBID >= p.alert_access()) ? false : true);
            }
            );
        }

        void IsCommentAllowed(const AuthSession& session, NPluginUtility::IsAllowCallbackInt_t acb)
        {
            if (TOKEN_AUTH_SESSION_ID == session.id)
            {
                //TODO: BOOKMARK_ACCESS_CREATE_PROTECT_EDIT_DELETE here
                acb(true);
            }

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials( session.data.first ? *(session.data.first) : "");

            PPermissionsReader_t reader(new PermissionsReader_t
                (GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::security::SecurityService::Stub::AsyncListUserGlobalPermissions));

             reader->asyncRequest(::google::protobuf::Empty(),
                [acb](const bl::security::ListUserGlobalPermissionsResponse& resp, grpc::Status valid)
            {
                if (!valid.ok() || (0 == resp.permissions_size()))
                {
                    acb(axxonsoft::bl::security::BOOKMARK_ACCESS_NO);
                    return;
                }

                const bl::security::GlobalPermissions& p = resp.permissions().begin()->second;

                if (bl::security::UNRESTRICTED_ACCESS_YES == p.unrestricted_access())
                {
                    acb(axxonsoft::bl::security::BOOKMARK_ACCESS_CREATE_PROTECT_EDIT_DELETE);
                    return;
                }

                acb(p.bookmark_access());
            }
            );
        }

        void IsAlertsAllowed(const AuthSession& session, NPluginUtility::IsAllowCallbackInt_t acb) override
        {
            if (TOKEN_AUTH_SESSION_ID == session.id)
            {
                acb(bl::security::ALERT_ACCESS_FULL);
            }

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials( session.data.first ? *(session.data.first) : "");

            PPermissionsReader_t reader(new PermissionsReader_t
            (GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::security::SecurityService::Stub::AsyncListUserGlobalPermissions));

            reader->asyncRequest(::google::protobuf::Empty(),
                [acb](const bl::security::ListUserGlobalPermissionsResponse& resp, grpc::Status valid)
            {
                if (!valid.ok() || (0 == resp.permissions_size()))
                {
                    acb(bl::security::ALERT_ACCESS_FORBID);
                    return;
                }

                const bl::security::GlobalPermissions& p = resp.permissions().begin()->second;

                if (bl::security::UNRESTRICTED_ACCESS_YES == p.unrestricted_access())
                {
                    acb(axxonsoft::bl::security::ALERT_ACCESS_FULL);
                    return;
                }

                acb(p.alert_access());
            }
            );
        }

        void HasGlobalPermissions(const TPermissions& permissions, const AuthSession& session, NPluginUtility::IsAllowCallback_t acb) override
        {
            if (TOKEN_AUTH_SESSION_ID == session.id)
            {
                acb(true);
            }

            NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials( session.data.first ? *(session.data.first) : "");

            PPermissionsReader_t reader(new PermissionsReader_t
                (GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::security::SecurityService::Stub::AsyncListUserGlobalPermissions));

            ::google::protobuf::Empty req;

            PermissionCallback_t cb = std::bind(&CRightsChecker::onHasPermissions,
                shared_from_base<CRightsChecker>(), permissions, acb,
                std::placeholders::_1, std::placeholders::_2);

            reader->asyncRequest(req, cb);
        }
    private:
        void onCameraInfo(PCameraContext ctxOut, NPluginUtility::IsAllowCallbackInt_t acb,
            const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, NWebGrpc::STREAM_ANSWER valid, grpc::Status status)
        {
            if (!status.ok())
            {
                _err_ << "RightsChecker: GetCamerasByComponents method failed";
                acb(axxonsoft::bl::security::CAMERA_ACCESS_UNSPECIFIED);
                return;
            }

            getCameraAccessLevel(ctxOut, cams);

            if (valid == NWebGrpc::_FINISH)
            {
                acb(ctxOut->m_cameraAccessLevel);
            }
        }

        void getCameraAccessLevel(std::shared_ptr<SCameraContext> ctxOut
            , const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            if (cams.size() > 0)
            {
                const bl::domain::Camera& c = cams.Get(0);
                ctxOut->m_cameraAccessLevel = c.camera_access();
            }
        }

        void onHasPermissions(const TPermissions& permissions, NPluginUtility::IsAllowCallback_t cb,
            const bl::security::ListUserGlobalPermissionsResponse& resp, grpc::Status valid)
        {
            if (!valid.ok() || (0 == resp.permissions_size()))
            {
                cb(false);
                return;
            }

            const bl::security::GlobalPermissions& p = resp.permissions().begin()->second;

            if (bl::security::UNRESTRICTED_ACCESS_YES == p.unrestricted_access())
            {
                cb(true);
                return;
            }

            bool hasPermissions = false;
            const ::google::protobuf::RepeatedField<int>& features = p.feature_access();
            if (std::all_of(permissions.begin(), permissions.end(),
                [features](const bl::security::EFeatureAccess& f )
            {
                return (features.cend() != std::find(features.cbegin(), features.cend(), f));
            }))
                hasPermissions = true;

            cb(hasPermissions);
        }

        void onGetAllowedArchiveByEndpoint(const std::string& cam, std::shared_ptr<std::string> ctx, NPluginUtility::IsAllowedArcCallback_t cb,
            const bl::domain::BatchGetCamerasResponse& resp, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                cb("");
            }

            procGetAllowedArchive(cam, ctx, resp.items());

            if (NWebGrpc::_FINISH == status)
            {
                return cb(*ctx);
            }
        }

        static void procGetAllowedArchive(const std::string& cam, std::shared_ptr<std::string> ctx, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);

                int arcCount = c.archive_bindings_size();
                for (int j = 0; j < arcCount; ++j)
                {
                    const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                    auto it = std::find_if(ab.sources().begin(), ab.sources().end(),
                        [&](const bl::domain::StorageSource& c)
                    {
                        return boost::contains(c.media_source(), VIDEO_SOURCE) || boost::contains(c.media_source(), AUDIO_SOURCE);
                    });

                    if (it == ab.sources().end())
                        continue;

                    if (cam == it->media_source())
                    {
                        *ctx = ab.storage();
                        return;
                    }
                }
            }
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NPluginUtility
{
    PRigthsChecker CreateRightsChecker(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
    {
        return PRigthsChecker(new CRightsChecker(GET_LOGGER_PTR, grpcManager));
    }
}
