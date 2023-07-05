#include "HttpPlugin.h"
#include "Constants.h"
#include "SendContext.h"
//#include "GrpcReader.h"

#include <HttpServer/BasicServletImpl.h>

#include <json/json.h>

#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/ResolveServant.h>

#include <SecurityManager/JWT.h>
#include <SecurityManager/SecurityManager.h>
#include <SecurityManager/ObjrefGetters.h>

#include <InfraServer_IDL/MainConfig.h>

//#include <axxonsoft/bl/layout/LayoutManager.grpc.pb.h>

using namespace NHttp;

//namespace bl = axxonsoft::bl;
namespace nis = InfraServer::New;

namespace
{
    const char* const AUTHORIZATION_HEADER = "Authorization";

    const char* const LAYOUT_DESCRIPTION_CONFIG = "roles/AppData/UserLayouts.v2.Descriptions";
    const char* const LAYOUT_CONFIG = "roles/AppData/UserLayouts.v2.Layout.";
    const char* const LAYOUT_CURRENT_CONFIG = "roles/AppData/UserLayouts.v2.CurrentLayout.";

    const char* const LAYOUT_ID_FIELD = "layout_id";
    const char* const OWNER_FIELD = "owner";
    const char* const SHARED_ROLES_FIELD = "shared_roles_privileges";
    const char* const KEY_FIELD = "Key";

    /*using ListLayoutReader_t = NWebGrpc::AsyncResultReader < bl::layout::LayoutManager, bl::layout::ListLayoutsRequest,
    bl::layout::ListLayoutsResponse>;
    using PListLayoutReader_t = std::shared_ptr < ListLayoutReader_t >;
    using ListLayoutCallback_t = std::function<void(const bl::layout::ListLayoutsResponse&, bool)>;*/

    class CLayoutContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;

    public:
        CLayoutContentImpl(NCorbaHelpers::IContainer *c, const NPluginUtility::PRigthsChecker rightsChecker)
            : m_container(c)
            , m_rightsChecker(rightsChecker)
            , m_sharedDb(CSharedRepoGetter(c))
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            NPluginUtility::IRigthsChecker::TPermissions perms = { axxonsoft::bl::security::FEATURE_ACCESS_LAYOUTS_TAB };
            m_rightsChecker->HasGlobalPermissions(perms, req->GetAuthSession(),
                boost::bind(&CLayoutContentImpl::onAccess, shared_from_base<CLayoutContentImpl>(),
                req, resp, _1));
        }

    private:
        void onAccess(const PRequest req, PResponse resp, bool hasPermissions)
        {
            if (!hasPermissions)
            {
                Error(resp, IResponse::Forbidden);
                return;
            }

            try
            {
                NCorbaHelpers::PContainer cont = m_container;
                if (!cont)
                {
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                NSecurityManager::PSecurityManagerImpl sm =
                    NSecurityManager::GetSecurityManagerImpl(cont.Get());
                if (!sm)
                {
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                InfraServer::SecurityManager::ISecurityManager_var secMan(NCorbaHelpers::ResolveServant<InfraServer::SecurityManager::ISecurityManager>(cont.Get(), "SecurityManager/Server"));
                if (CORBA::is_nil(secMan))
                {
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                const IRequest::AuthSession& as = req->GetAuthSession();
                NSecurityManager::SessionInfo si = sm->GetSessionInfo(*(as.data.first),
                    [this](const NSecurityManager::JWT::Header&, const NSecurityManager::JWT::NGPClaims&)
                {
                    return NSecurityManager::JWT::MakeDummyVerifier();
                });

                std::string userId(si.userId);

                std::shared_ptr<Json::Value> layouts = std::make_shared<Json::Value>(Json::arrayValue);

                nis::SConfigPacket_var packet(m_sharedDb->GetConfigPart(NCorbaHelpers::CObjectName::FromString(
                    LAYOUT_DESCRIPTION_CONFIG).ToCosNameVar(), nis::CONF_MAIN));

                XmlConfigUtils::FromMainConfigBuffer(packet->Main, [this, secMan, userId, layouts, &resp](std::istream& stream) {

                    Json::Value description(Json::nullValue);
                    Json::CharReaderBuilder reader;
                    std::string err;
                    if (Json::parseFromStream(reader, stream, &description, &err))
                    {
                        for (Json::Value::ArrayIndex i = 0; i != description.size(); i++)
                        {
                            if (description[i].isMember(LAYOUT_ID_FIELD) && description[i].isMember(OWNER_FIELD))
                            {
                                std::string owner(description[i][OWNER_FIELD].asString());

                                if ((owner == userId) || checkForSharedAccess(secMan.in(), description[i], userId))
                                {
                                    std::string layout(LAYOUT_CONFIG);
                                    layout.append(description[i][LAYOUT_ID_FIELD].asString());

                                    try
                                    {
                                        nis::SConfigPacket_var layoutPacket(m_sharedDb->GetConfigPart(NCorbaHelpers::CObjectName::FromString(
                                            layout).ToCosNameVar(), nis::CONF_MAIN));

                                        XmlConfigUtils::FromMainConfigBuffer(layoutPacket->Main, [this, &resp, owner, layouts](std::istream& layoutStream) {

                                            Json::Value layoutJson(Json::nullValue);
                                            Json::CharReaderBuilder reader;
                                            std::string err;
                                            if (Json::parseFromStream(reader, layoutStream, &layoutJson, &err))
                                            {
                                                layouts->append(layoutJson);
                                            }
                                        });
                                    }
                                    catch (const nis::ConfigRepository::XNotFound&)
                                    {
                                        _wrn_ << "Configuration for layout " << layout << " is not found";
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                });

                SendResponse(resp, *layouts);
            }
            catch (const nis::ConfigRepository::XNotFound&)
            {
                _wrn_ << "Configuration for layout description " << LAYOUT_DESCRIPTION_CONFIG << " is not found";
                SendResponse(resp, Json::Value(Json::arrayValue));
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "CORBA exception: " << e._info();
                Error(resp, IResponse::InternalServerError);
            }
        }
        /*void onLayoutResponse(PResponse resp, const bl::layout::ListLayoutsResponse& res, bool status)
        {
            if (status)
            {
                Json::Value responseObject(Json::arrayValue);

                int itemCount = res.items_size();
                for (int i = 0; i < itemCount; ++i)
                {
                    const bl::layout::LayoutFull& lf = res.items(i);
                    const bl::layout::Layout& l = lf.body();

                    Json::Value layout(Json::objectValue);
                    layout["alarm_mode"] = l.alarm_mode();


                    Json::Value alertPanelState(Json::objectValue);
                    const bl::layout::AlertPanelViewConfiguration& apc = l.alert_panel_state();
                    alertPanelState["AlertPanelViewState.AlertPanelFilter"] = apc.event_filter_mask();
                    alertPanelState["AlertPanelViewState.AlertPanelState"] = apc.panel_state();
                    alertPanelState["AlertPanelViewState.ControlCountInLine"] = apc.control_count_in_line();
                    alertPanelState["AlertPanelViewState.MiddlePercent"] = apc.middle_percent();
                    alertPanelState["AlertPanelViewState.OpeningState"] = apc.opening_state();

                    layout["alertPanelState"] = alertPanelState;

                    const google::protobuf::Map<google::protobuf::int32, bl::layout::Cell> cls = l.cells();

                    responseObject.append(layout);
                }

            SendResponse(resp, responseObject);
            }
        }*/

        void SendResponse(NHttp::PResponse resp, const Json::Value& responseObject) const
        {
            std::string text(responseObject.toStyledString());
            std::string contentType(NHttp::GetMIMETypeByExt("json"));
            contentType.append("; charset=utf-8");
            resp->SetStatus(NHttp::IResponse::OK);
            resp << NHttp::ContentLength(text.size())
                << NHttp::ContentType(contentType)
                << NHttp::CacheControlNoCache();
            resp->FlushHeaders();

            NContext::PSendContext ctx(NContext::CreateStringContext(resp, text, [](boost::system::error_code) {}));
            ctx->ScheduleWrite();
        }

        bool checkForSharedAccess(InfraServer::SecurityManager::ISecurityManager_ptr sm, Json::Value& v, const std::string userId)
        {
            if (v.isMember(SHARED_ROLES_FIELD))
            {
                Json::Value& roles = v[SHARED_ROLES_FIELD];
                for (Json::Value::ArrayIndex i = 0; i != roles.size(); i++)
                {
                    if (roles[i].isMember(KEY_FIELD))
                    {
                        const std::string roleId(roles[i][KEY_FIELD].asString());
                        InfraServer::SecurityManager::SConfig_var cfg = sm->GetConfig();
                        InfraServer::SecurityManager::TUserAssignments& ua = cfg->userAssignments;
                        for (CORBA::ULong j = 0; j < ua.length(); ++j)
                        {
                            if ((ua[j].roleId.in() == roleId) && (ua[j].userId.in() == userId))
                                return true;
                        }
                    }
                }
            }
            return false;
        }

        NCorbaHelpers::WPContainer m_container;
        const NPluginUtility::PRigthsChecker m_rightsChecker;
        CLazyConfigRepo m_sharedDb;
    };
}

namespace NHttp
{
    IServlet* CreateLayoutServlet(NCorbaHelpers::IContainer* c, const NPluginUtility::PRigthsChecker rightsChecker)
    {
        return new CLayoutContentImpl(c, rightsChecker);
    }
}
