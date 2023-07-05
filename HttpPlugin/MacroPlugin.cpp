#include "HttpPlugin.h"
#include "Constants.h"
#include "RegexUtility.h"
#include "CommonUtility.h"

#include <HttpServer/BasicServletImpl.h>

#include <BL_IDL/BusinessLayerC.h>

#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/ResolveServant.h>

#include <json/json.h>

#include <axxonsoft/bl/logic/LogicService.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

using MacroReader_t = NWebGrpc::AsyncResultReader < bl::logic::LogicService, bl::logic::ListMacrosRequest,
    bl::logic::ListMacrosResponse >;
using PMacroReader_t = std::shared_ptr < MacroReader_t >;

namespace
{
    const char* const LIST_MASK = "list";
    const char* const EXECUTE_MASK = "execute";

    class CMacroContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CMacroContentImpl(NCorbaHelpers::IContainer *c, const NWebGrpc::PGrpcManager grpcManager)
            : m_container(c)
            , m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            try
            {
                if (!(ParseListCommand(req, resp) ||
                    ParseExecCommand(req, resp))
                    )
                {
                    _err_ << "Unknown query: " << req->GetPathInfo();
                    Error(resp, IResponse::BadRequest);
                }
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << e._info();
                Error(resp, IResponse::InternalServerError);
            }
            catch (const std::invalid_argument& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::InternalServerError);
            }
        }

    private:
        bool ParseListCommand(const PRequest req, PResponse resp)
        {
            PMask list = Mask(LIST_MASK);

            if (!Match(req->GetPathInfo(), list))
                return false;

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            PMacroReader_t reader(new MacroReader_t
                (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::logic::LogicService::Stub::AsyncListMacros));

            bl::logic::ListMacrosRequest creq;
            creq.set_view(bl::logic::ListMacrosRequest::VIEW_MODE_STRIPPED);

            auto pThis = shared_from_base<CMacroContentImpl>();
            reader->asyncRequest(creq, [pThis, req, resp](const bl::logic::ListMacrosResponse& res, grpc::Status status)
            {
                if (!status.ok())
                {
                    return NPluginUtility::SendGRPCError(resp, status);
                }

                NPluginUtility::TParams queryParams;
                if (!NPluginUtility::ParseParams(req->GetQuery(), queryParams))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                bool excludeAuto = NPluginUtility::IsParamExists(queryParams, "exclude_auto");

                Json::Value commands(Json::arrayValue);

                int macroCount = res.items_size();
                for (int i = 0; i < macroCount; ++i)
                {
                    const bl::logic::MacroConfig& mc = res.items(i);

                    const bl::logic::MacroMode& mm = mc.mode();
                    if (!excludeAuto || mm.type_case() != bl::logic::MacroMode::kAutorule)
                    {
                        Json::Value obj(Json::objectValue);
                        obj["id"] = mc.guid();
                        obj["name"] = mc.name();

                        commands.append(obj);
                    }
                }

                Json::Value responseObject(Json::objectValue);
                responseObject["macroCommands"] = commands;

                NPluginUtility::SendText(req, resp, responseObject.toStyledString());
            });
            
            return true;
        }

        bool ParseExecCommand(const PRequest req, PResponse resp)
        {
            std::string macroId;
            PToken id = Token(macroId);
            PMask exec = Mask(EXECUTE_MASK);

            if (!Match(req->GetPathInfo(), exec / id))
                return false;

            boost::uuids::uuid mid = NCorbaHelpers::FlipByteOrder(NCorbaHelpers::UuidFromString(macroId.c_str()));
            if (mid.is_nil())
            {
                _err_ << "It is imposible to convert obtained id " << macroId << " to GUID";
                Error(resp, IResponse::NotFound);
                return true;
            }

            NVRBL::LogicServer_var ls = GetLogicServer();

            Notification::Guid nativeMacroId;
            std::copy(mid.begin(), mid.end(), nativeMacroId.value);

            IRequest::AuthSession authSession = req->GetAuthSession();

            ORM::RemoteInitiator initiator;
            initiator.UserName(NCorbaHelpers::FromUtf8(req->GetAuthSession().user).c_str());

            ls->LaunchMacro(nativeMacroId, initiator);

            Error(resp, IResponse::OK);
            return true;
        }

        NVRBL::LogicServer_ptr GetLogicServer()
        {
            NVRBL::LogicServer_var ls;
            NCorbaHelpers::PContainer cont = m_container;
            if (cont)
            {
                ls = NCorbaHelpers::ResolveServant<NVRBL::LogicServer>(cont.Get(), "NvrLogic.0/NvrLogicService");
            }
            if (CORBA::is_nil(ls))
                throw std::invalid_argument("NvrLogic is not accessible");
            return ls._retn();
        }

    private:
        NCorbaHelpers::WPContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateMacroServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CMacroContentImpl(c, grpcManager);
    }
}
