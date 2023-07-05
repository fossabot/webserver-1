#ifndef RIGHTS_CHECKER_H__
#define RIGHTS_CHECKER_H__

#include "GrpcHelpers.h"
#include <CorbaHelpers/ObjectName.h>
#include <HttpServer/HttpRequest.h>

#include <boost/enable_shared_from_this.hpp>

#include <axxonsoft/bl/security/GlobalPermissions.pb.h>

namespace NPluginUtility
{
    using IsAllowCallback_t = boost::function<void(bool)>;
    using IsAllowCallbackInt_t = boost::function<void(int)>;
    using IsAllowedArcCallback_t = boost::function<void(const std::string &)>;

    class IRigthsChecker : public boost::enable_shared_from_this<IRigthsChecker>
    {
    public:
        typedef NHttp::IRequest::AuthSession AuthSession;
        typedef std::vector<axxonsoft::bl::security::EFeatureAccess> TPermissions;

        virtual ~IRigthsChecker() {}

        virtual void IsCameraAllowed(const std::string& cam, const AuthSession&, IsAllowCallbackInt_t) = 0;
        virtual void IsCommentAllowed(const AuthSession& session, NPluginUtility::IsAllowCallbackInt_t acb) = 0;
        virtual void IsAlertsAllowed(const AuthSession& session, NPluginUtility::IsAllowCallbackInt_t acb) = 0;

        virtual void HasAlarmAccess(const AuthSession&, IsAllowCallback_t) = 0;
        virtual void HasGlobalPermissions(const TPermissions&, const AuthSession&, NPluginUtility::IsAllowCallback_t) = 0;

        template <typename Derived>
        boost::shared_ptr<Derived> shared_from_base()
        {
            return boost::dynamic_pointer_cast<Derived>(shared_from_this());
        }
    };
    typedef boost::shared_ptr<IRigthsChecker> PRigthsChecker;

    PRigthsChecker CreateRightsChecker(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
}

#endif // RIGHTS_CHECKER_H__
