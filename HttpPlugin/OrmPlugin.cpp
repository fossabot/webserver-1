#include "HttpPlugin.h"
#include "CommonUtility.h"
#include "RegexUtility.h"
#include "Constants.h"
#include "OrmHandler.h"

#include <CorbaHelpers/Envar.h>
#include <HttpServer/BasicServletImpl.h>

using namespace NHttp;
namespace npu = NPluginUtility;

namespace
{
    const int MAX_LIMIT = 100;

    class CEventContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CEventContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rc)
            : m_alertsHandler(CreateAlertsHandler(GET_LOGGER_PTR, grpcManager))
            , m_detectorsHandler(CreateDetectorsHandler(GET_LOGGER_PTR, grpcManager))
            , m_alertsFullHandler(CreateAlertsFullHandler(GET_LOGGER_PTR, grpcManager))
            , m_rightsChecker(rc)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual void Head(const PRequest req, PResponse resp)
        {
            Handle(req, resp);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Handle(req, resp);
        }

    private:
        void Handle(const PRequest req, PResponse resp)
        {
            std::string handler;
            COrmHandler::PParams params = std::make_shared<COrmHandler::SParams>();
            if(!Parse(req, handler, *params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            if ("alerts" == handler)
                HandleWithPermissions(req, resp, m_alertsHandler, params);
            else if ("detectors" == handler)
                m_detectorsHandler->Process(req, resp, *params);
            else if ("alertsfull" == handler)
                HandleWithPermissions(req, resp, m_alertsFullHandler, params);
            else
                resp->SetStatus(IResponse::NotImplemented);
        }

        void HandleWithPermissions(const PRequest req, PResponse resp, POrmHandler h, COrmHandler::PParams params)
        {
            m_rightsChecker->HasAlarmAccess(req->GetAuthSession(),
                [this, req, resp, h, params](bool valid)
            {
                if (!valid)
                {
                    Error(resp, IResponse::Forbidden);
                    return;
                }

                h->Process(req, resp, *params);
            });
        }

        bool Parse(const NHttp::PRequest req, std::string &handler, COrmHandler::SParams &data)
        {
            using namespace npu;
            PMask alerts    = Mask("alerts");
            PMask alertsFull = Mask("alertsfull");
            PMask detectors = Mask("detectors");
            PObjName ep = ObjName(3, "hosts/");
            PToken host     = Token();
            PDate begin     = Begin();
            PDate end       = End();

            const std::string p = req->GetPathInfo();

            if (npu::ParseSafely(p, alerts / end / begin) ||
                npu::ParseSafely(p, alerts / host / end / begin) ||
                npu::ParseSafely(p, alerts / ep / end / begin))
            {
                handler = "alerts";
            }
            else if (npu::ParseSafely(p, alertsFull / end / begin) ||
                npu::ParseSafely(p, alertsFull / host / end / begin) ||
                npu::ParseSafely(p, alertsFull / ep / end / begin))
            {
                handler = "alertsfull";
            }
            else
            {
                host->Reset();
                if (!npu::ParseSafely(p, detectors / end / begin) &&
                    !npu::ParseSafely(p, detectors / host / end / begin) &&
                    !npu::ParseSafely(p, detectors / ep / end / begin))
                    return false;

                handler = "detectors";
            }

            TParams queryParams;
            if (!ParseParams(req->GetQuery(), queryParams))
            {
                return false;
            }

            try
            {
                const NCorbaHelpers::CObjectName& on = ep->Get();

                data.Source = on.IsEmpty() ? "" : on.ToString();
                data.Begin = begin->Get();
                data.End = end->Get();
                data.Limit = GetParam(queryParams, LIMIT_MASK, MAX_LIMIT);
                data.Offset = GetParam(queryParams, OFFSET_MASK, 0);
                data.ReverseOrder = data.End < data.Begin;
                ReadTypes(queryParams, data.Type);
                data.JoinPhases = GetParam<bool>(queryParams, "join", false);
                data.LimitToArchive = GetParam<bool>(queryParams, "limit_to_archive", false);
                GetParam<std::string>(queryParams, PARAM_ARCHIVE, data.Archive, std::string());
                GetParam<std::string>(queryParams, PARAM_DETECTOR, data.Detector, std::string());

                ResolveHostName(data, host->GetValue());

                if (data.ReverseOrder)
                    std::swap(data.Begin, data.End);
            }
            catch (const std::exception& e)
            {
                _err_ << "Request parse error: " << e.what();
                return false;
            }

            return true;
        }

        void ResolveHostName(COrmHandler::SParams &data, const std::string& hostName)
        {        
            if (data.Source.empty())
                data.Host = hostName.empty() ? NCorbaHelpers::CEnvar::NgpNodeName() : hostName;
            else
            {
                std::size_t pos1 = data.Source.find_first_of('/');
                std::size_t pos2 = data.Source.find('/', ++pos1);
                data.Host = data.Source.substr(pos1, pos2 - pos1);
            }
        }

    private:

        void ReadTypes(const NPluginUtility::TParams& params, std::vector<std::string>& types)
        {
            auto param = npu::GetParam(params, "type", std::string());
            boost::split(types, param, boost::is_any_of(","));
        }
        POrmHandler m_alertsHandler;
        POrmHandler m_detectorsHandler;
        POrmHandler m_alertsFullHandler;
        const NPluginUtility::PRigthsChecker m_rightsChecker;
    };
}

namespace NHttp
{
    IServlet* CreateEventServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rc)
    {
        return new CEventContentImpl(GET_LOGGER_PTR, grpcManager, rc);
    }
}
