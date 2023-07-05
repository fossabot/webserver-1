#include "HttpPlugin.h"
#include "SendContext.h"
#include "CommonUtility.h"
#include "BLQueryHelper.h"
#include "Constants.h"
#include "RegexUtility.h"

#include <HttpServer/BasicServletImpl.h>

#include <json/json.h>

#include <axxonsoft/bl/archive/ArchiveSupport.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;

namespace bl = axxonsoft::bl;

using CalendarReader_t = NWebGrpc::AsyncResultReader < bl::archive::ArchiveService, bl::archive::GetCalendarRequest,
    bl::archive::GetCalendarResponse >;
using PCalendarReader_t = std::shared_ptr < CalendarReader_t >;

namespace
{
    struct SCalendarContext
    {
        SCalendarContext()
            : responseObject(Json::arrayValue)
        {}

        boost::posix_time::ptime startTime, endTime;
        Json::Value responseObject;
    };
    typedef std::shared_ptr<SCalendarContext> PCalendarContext;

    const char* const CALENDAR = "calendar";

    class CArchiveCalendarContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CArchiveCalendarContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            PCalendarContext ctx = std::make_shared<SCalendarContext>();

            boost::posix_time::ptime startTime, endTime;

            PObjName ep = ObjName(3, "hosts/");
            PDate begin = Begin(ctx->startTime);
            PDate end = End(ctx->endTime);

            const std::string path = req->GetPathInfo();

            if (!Match(path, ep / end / begin))
            {
                _err_ << "Calendar request failed. Incorrect format: " << path;
                Error(resp, IResponse::BadRequest);
                return;
            }

            NPluginUtility::TParams params;
            if (!NPluginUtility::ParseParams(req->GetQuery(), params))
            {
                _err_ << "Calendar request failed: parse params";
                Error(resp, IResponse::BadRequest);
                return;
            }

            if (ctx->startTime > ctx->endTime)
                std::swap(ctx->startTime, ctx->endTime);

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            NPluginUtility::PEndpointQueryContext ctxOut = std::make_shared<NPluginUtility::SEndpointQueryContext>();
            ctxOut->endpoint = ep->Get().ToString();
            NPluginUtility::GetParam<std::string>(params, PARAM_ARCHIVE, ctxOut->archiveName, std::string());
            if (!ctxOut->archiveName.empty() && (std::string::npos == ctxOut->archiveName.find(HOST_PREFIX)))
                ctxOut->archiveName = HOST_PREFIX + ctxOut->archiveName;

            NWebBL::TEndpoints eps{ ep->Get().ToString() };
            NWebBL::FAction action = boost::bind(&CArchiveCalendarContentImpl::onCameraInfo, shared_from_base<CArchiveCalendarContentImpl>(),
                resp, metaCredentials, ctx, ctxOut, _1, _2,_3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, metaCredentials, eps, action);
        }

    private:
        void onCameraInfo(PResponse resp, NGrpcHelpers::PCredentials metaCredentials, PCalendarContext ctx, NPluginUtility::PEndpointQueryContext ctxOut,
            const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                return NPluginUtility::SendGRPCError(resp, grpcStatus);
            }
      
            NPluginUtility::GetEndpointStorageSource(GET_LOGGER_PTR, items, ctxOut, true, true);

            if (valid == NWebGrpc::_FINISH)
            {
                if (ctxOut->requestedItem.empty())
                {
                    _err_ << "/archive/calendar: Not found archive binding";
                    return sendResonse(resp, ctx);
                }

                PCalendarReader_t reader(new CalendarReader_t
                (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::archive::ArchiveService::Stub::AsyncGetCalendar));

                boost::posix_time::ptime const time_epoch(boost::gregorian::date(1900, 1, 1));

                bl::archive::GetCalendarRequest creq;
                creq.set_access_point(ctxOut->requestedItem);

                ctx->startTime = (ctx->startTime < time_epoch) ? time_epoch : ctx->startTime;
                ctx->endTime = (ctx->endTime < time_epoch) ? time_epoch : ctx->endTime;

                creq.set_begin_time((ctx->startTime - time_epoch).total_milliseconds());
                creq.set_end_time((ctx->endTime - time_epoch).total_milliseconds());

                auto pThis = shared_from_base<CArchiveCalendarContentImpl>();
                reader->asyncRequest(creq, [this, pThis, resp, ctx](const bl::archive::GetCalendarResponse& res, grpc::Status status)
                {
                    if (!status.ok())
                    {
                        return NPluginUtility::SendGRPCError(resp, status);
                    }

                    int dayCount = res.days_size();
                    for (int i = 0; i < dayCount; ++i)
                        ctx->responseObject.append(res.days(i));

                    sendResonse(resp, ctx);
                });                  
            }
        }

        void setHeader(PResponse resp, const std::string& text)
        {
            std::string contentType(NHttp::GetMIMETypeByExt("json"));
            contentType.append("; charset=utf-8");

            resp->SetStatus(IResponse::OK);
            resp << NHttp::ContentLength(text.size())
                << NHttp::ContentType(contentType)
                << NHttp::CacheControlNoCache();
            resp->FlushHeaders();
        }

        void sendResonse(PResponse resp, PCalendarContext ctx)
        {
            std::string text(ctx->responseObject.toStyledString());
            setHeader(resp, text);

            NContext::PSendContext sendCtx(NContext::CreateStringContext(resp, text));
            sendCtx->ScheduleWrite();
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateArchiveCalendarServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CArchiveCalendarContentImpl(GET_LOGGER_PTR, grpcManager);
    }
}
