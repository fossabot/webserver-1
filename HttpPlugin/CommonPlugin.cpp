
#include <CorbaHelpers/Container.h>
#include <CorbaHelpers/Uuid.h>
#include <HttpServer/json_oarchive.h>
#include <HttpServer/BasicServletImpl.h>

#include <Vendor/ngpVersion.h>

#include "HttpPlugin.h"
#include "CommonUtility.h"
#include "Tokens.h"

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

#include <json/json.h>

using namespace NHttp;
namespace npu = NPluginUtility;

namespace
{
    const char* const VERSION_FIELD = "version";
    const char* const LANGUAGES_FIELD = "languages";
    const char* const UUID_FIELD = "uuid";
    const char* const LOGOUT_FIELD = "logout";
    const char* const CURRENTUSER_FIELD = "currentuser";
    const char* const PORTS_FIELD = "ports";

    const char* const DRIVERPACK_VERSION = "addon__IPDriverPack";
    const char* const DETECTORPACK_VERSION = "addon__DetectorPack";

    namespace bl = axxonsoft::bl;

    using PlatformInfo_t = NWebGrpc::AsyncResultReader < bl::domain::DomainService, bl::domain::GetHostPlatformInfoRequest,
        bl::domain::GetHostPlatformInfoResponse>;
    using PPlatformInfo_t = std::shared_ptr < PlatformInfo_t >;

    class CCommonPlugin
        :   public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        explicit CCommonPlugin(NCorbaHelpers::IContainer *container, const NWebGrpc::PGrpcManager grpcManager)
            : m_container(container)
            , m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(container);
        }

    private:
        virtual void Head(const PRequest req, PResponse resp)
        {
            Process(req, resp, true);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Process(req, resp, false);
        }

        void Process(const PRequest req, PResponse resp, bool headersOnly)
        {
            using namespace npu;
            if (Match(req->GetContextPath(), Mask(UUID_FIELD)) && Match(req->GetPathInfo(), Empty()))
            {
                auto uuid = NCorbaHelpers::GenerateUUIDString();
                SendText(resp, toJson(UUID_FIELD, uuid), true, headersOnly);
            }
            else if (Match(req->GetContextPath(), Mask(LOGOUT_FIELD)) && Match(req->GetPathInfo(), Empty()))
            {
                Error(resp, IResponse::Unauthorized);
            }
            else if (Match(req->GetContextPath(), Mask(LANGUAGES_FIELD)) && Match(req->GetPathInfo(), Empty()))
            {
                boost::optional<const std::string&> h = req->GetHeader("Accept-Language");
                SendText(resp, toJson(LANGUAGES_FIELD, h ? *h : ""), true, headersOnly);
            }
            else if (Match(req->GetPathInfo(), Mask(VERSION_FIELD)))
            {
                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                PPlatformInfo_t reader(new PlatformInfo_t
                    (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncGetHostPlatformInfo));

                bl::domain::GetHostPlatformInfoRequest creq;

                auto pThis = shared_from_base<CCommonPlugin>();
                reader->asyncRequest(creq, [this, pThis, req, resp](const bl::domain::GetHostPlatformInfoResponse& res, grpc::Status status)
                    {
                        if (!status.ok())
                        {
                            return NPluginUtility::SendGRPCError(resp, status);
                        }                     

                        Json::Value obj(Json::objectValue);
                        obj["version"] = getProductVersion();

                        auto ai = res.additional_info();
                        auto v = ai.find(DRIVERPACK_VERSION);
                        if (ai.end() != v)
                            obj["DriverPackVersion"] = v->second;
                        v = ai.find(DETECTORPACK_VERSION);
                        if (ai.end() != v)
                            obj["DetectorPackVersion"] = v->second;

                        NPluginUtility::SendText(req, resp, obj.toStyledString());
                    });
            }
            else if (Match(req->GetContextPath(), Mask(CURRENTUSER_FIELD)) && Match(req->GetPathInfo(), Empty()))
            {
                SendText(resp, toJson(CURRENTUSER_FIELD, req->GetAuthSession().user), true, headersOnly);
            }
            else if (Match(req->GetContextPath(), Mask(PORTS_FIELD)) && Match(req->GetPathInfo(), Empty()))
            {
                SendText(resp, toJson("grpcwebproxy", std::to_string(NCorbaHelpers::CEnvar::GrpcWebProxyPort())), true, headersOnly);
            }
            else
                Error(resp, IResponse::BadRequest);
        }

		std::string getProductVersion()
		{
			return str(boost::format("%1% %2%.%3%.%4%.%5%")
				% NGPVersionInfo.ProductNameHttpApi
				% NGPVersionInfo.VersionMajor
				% NGPVersionInfo.VersionMinor
				% NGPVersionInfo.VersionBeta
				% NGPVersionInfo.BuildNumber);
		}

        std::string toJson(const char* const key, const std::string& value)
        {
            std::stringstream ss;
            {
                boost::archive::json_oarchive arch(ss);
                arch << boost::serialization::make_nvp(key, value);
            }
            return ss.str();
        }

    private:
        NCorbaHelpers::WPContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateCommonServlet(NCorbaHelpers::IContainer *container, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CCommonPlugin(container, grpcManager);
    }
}
