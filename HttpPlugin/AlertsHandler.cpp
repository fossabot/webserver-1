#include "OrmHandler.h"
#include "Constants.h"
#include "CommonUtility.h"
#include "BLQueryHelper.h"

#include <google/protobuf/util/json_util.h>

#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>
#include <axxonsoft/bl/archive/ArchiveSupport.grpc.pb.h>

#include <json/json.h>
#include <fmt/format.h>

#include <boost/range/adaptor/reversed.hpp>

using namespace NHttp;
namespace nch = NCorbaHelpers;
namespace bl = axxonsoft::bl;
namespace bpt = boost::posix_time;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
    using AlertReader_t = NWebGrpc::AsyncStreamReader < bl::events::EventHistoryService, bl::events::ReadAlertsRequest,
        bl::events::ReadAlertsResponse >;
    using PAlertReader_t = std::shared_ptr < AlertReader_t >;

    using EventCallback_t = std::function<void(const bl::events::ReadAlertsResponse&, NWebGrpc::STREAM_ANSWER)>;

    typedef std::shared_ptr<Json::Value> PJsonValue;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CAlertsHandlerBase : public COrmHandler
    {
    protected:
        CAlertsHandlerBase(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const std::string arrayName)
            : COrmHandler(GET_LOGGER_PTR)
            , m_grpcManager(grpcManager)
            , m_arrayName(arrayName)
        {}

        void Process(const NHttp::PRequest req, NHttp::PResponse resp, const SParams &params) override
        {
            if (params.LimitToArchive)
            {
                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                NWebBL::GetArchiveDepth(GET_LOGGER_PTR, m_grpcManager, metaCredentials, params.Archive, params.Source,
                    [this, req, resp, params](bool ok, const boost::posix_time::ptime& tdepth)
                {
                    _log_ << "Alerts: archive depth for " << params.Source << " and " << params.Archive << ": " << (ok ? 1 : 0) << " = " << (ok ? tdepth : params.Begin);
                    execProcess(req, resp, params, ok ? std::max(tdepth, params.Begin) : params.Begin);
                });

                return;
            }

            execProcess(req, resp, params, params.Begin);
        }

        void execProcess(const NHttp::PRequest req, NHttp::PResponse resp, const SParams &params, bpt::ptime tBegin)
        {
            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            PJsonValue json = std::make_shared<Json::Value>(Json::objectValue);
            (*json)[m_arrayName] = Json::Value(Json::arrayValue);
            (*json)["more"] = false;

            bl::events::ReadAlertsRequest creq;
            creq.mutable_range()->set_begin_time(boost::posix_time::to_simple_string(params.Begin));
            creq.mutable_range()->set_end_time(boost::posix_time::to_simple_string(params.End));

            auto filter = creq.mutable_filters();
            bl::events::AlertsSearchFilter* alertsSearchFilter = filter->add_filters();
            if (!params.Source.empty())
                alertsSearchFilter->add_subjects(NPluginUtility::convertToMainStream(params.Source));

            creq.set_limit(params.Limit);
            creq.set_offset(params.Offset);
            creq.set_descending(!params.ReverseOrder);

            PAlertReader_t reader(new AlertReader_t
            (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::events::EventHistoryService::Stub::AsyncReadAlerts));

            auto obj = boost::weak_ptr<CAlertsHandlerBase>(shared_from_base<CAlertsHandlerBase>());
            reader->asyncRequest(creq, [req, resp, json, obj, params](const bl::events::ReadAlertsResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
                {
                    boost::shared_ptr<CAlertsHandlerBase> owner = obj.lock();
                    if (owner)
                    {
                        if (!grpcStatus.ok())
                        {
                            return NPluginUtility::SendGRPCError(resp, grpcStatus);
                        }

                        Json::Value& alerts = (*json)[owner->m_arrayName];
                        const ::google::protobuf::RepeatedPtrField<bl::events::Event> evRes = res.items();

                        for (const auto& ev : evRes)
                            owner->buildAlert(ev, alerts);

                        if (evRes.size() >= params.Limit)
                            (*json)["more"] = true;

                        if (NWebGrpc::_FINISH == status)
                        {
                            NPluginUtility::SendText(req, resp, json->toStyledString(), NHttp::IResponse::OK);
                            return;
                        }
                    }
                }
                );
        }

        virtual void buildAlert(const bl::events::Event& alertEvent, Json::Value& json) {}

    private:
        const NWebGrpc::PGrpcManager m_grpcManager;
        const std::string m_arrayName;
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CAlertsHandler
        :   public CAlertsHandlerBase
    {
        enum EAlertReason
        {
            EARMED = 1 << 0,
            EDISARMED = 1 << 1,
            EUSER_ALERT = 1 << 2,
            ERULE_ALERT = 1 << 3,
            EVIDEO_DETECTOR = 1 << 4,
            EAUDIO_DETECTOR = 1 << 5,
            ERAY = 1 << 6
        };

    public:
        CAlertsHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : CAlertsHandlerBase(GET_LOGGER_PTR, grpcManager, "events")
        {
        }

    private:
        void buildAlert(const bl::events::Event& alertEvent, Json::Value& alerts)
        {
            Json::Value a(Json::objectValue);

            bl::events::Alert alert;
            if (alertEvent.body().UnpackTo(&alert))
            {
                a["zone"] = alert.camera().access_point();
                a["id"] = alert.guid();
                a["raisedAt"] = alert.timestamp();
                a["type"] = "alert";

                setReasons(a, alert.reason_mask());
                //if (!alert.states().empty())
                    setAlertStates(a, alert.states());
                //if (alert.has_macro())
                    setEventDependentInfo(a, alert);
            }

            alerts.append(a);
        }

        static void setAlertStates(Json::Value& alert, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::events::AlertState >& stateEvents)
        {
            Json::Value reaction(Json::objectValue);
            for (const auto& state : stateEvents)
            {
                if (bl::events::AlertState::ST_CLOSED != state.state())
                    continue;

                reaction["user"] = state.reviewer();
                reaction["reactedAt"] = state.timestamp();
                reaction["severity"] = Convert(state.severity());
                reaction["message"] = state.bookmark().message();
            }

            alert["reaction"] = reaction;
        }

        static void setReasons(Json::Value& alert, std::int32_t value)
        {
            Json::Value reasons(Json::arrayValue);

            if(EARMED & value)           reasons.append("armed");
            if(EDISARMED & value)        reasons.append("disarmed");
            if(EUSER_ALERT & value)      reasons.append("userAlert");
            if(ERULE_ALERT & value)      reasons.append("ruleAlert");
            if(EVIDEO_DETECTOR & value)  reasons.append("videoDetector");
            if(EAUDIO_DETECTOR & value)  reasons.append("audioDetector");
            if(ERAY & value)             reasons.append("ray");

            alert["reasons"] = reasons;
        }

        static void setEventDependentInfo(Json::Value& a, const bl::events::Alert& alert)
        {
            std::string initiator(alert.initiator()), detectorName;
            Json::Value rects(Json::arrayValue);

            if (alert.initiator_type() == bl::events::Alert::AIT_MACRO)
            {
                if (alert.has_detector())
                {
                    initiator.assign(alert.detector().event_type());
                    detectorName.assign(alert.detector().detector_ext().friendly_name());
                    serializeDetails(rects, alert.detector().details());
                }                    
                else
                {
                    initiator.assign(alert.macro().macro().guid());
                }
            }

            a["initiator"] = initiator;
            a["detectorName"] = detectorName;
            a["rectangles"] = rects;
        }

        static void serializeDetails(Json::Value& rects, const ::google::protobuf::RepeatedPtrField < bl::events::DetectorEvent::Details>& details)
        {
            for (auto d: details)
            {
                if (d.has_rectangle())
                    serializeRectangle(rects, d.rectangle());
            }
        }

        static void serializeRectangle(Json::Value& rects, const bl::primitive::Rectangle& r)
        {
            Json::Value rect(Json::objectValue);
            rect["index"] =  fmt::format("{}", r.index());
            rect["left"] =   fmt::format("{}", r.x());
            rect["top"] =    fmt::format("{}", r.y());
            rect["right"] =  fmt::format("{}", r.x() + r.w());
            rect["bottom"] = fmt::format("{}", r.y() + r.h());

            rects.append(rect);
        }

        static std::string Convert(bl::events::AlertState::ESeverity value)
        {
            switch(value)
            {
            case bl::events::AlertState::SV_UNCLASSIFIED:  return "unclassified";
            case bl::events::AlertState::SV_FALSE:         return "false";
            case bl::events::AlertState::SV_WARNING:       return "warning";
            case bl::events::AlertState::SV_ALARM:         return "alarm";
            default:                                       return "unknown";
            }
        }
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CAlertsFullHandler
        : public CAlertsHandlerBase
    {
    public:
        CAlertsFullHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : CAlertsHandlerBase(GET_LOGGER_PTR, grpcManager, "alerts")
        {}

    private:
        void buildAlert(const bl::events::Event& alertEvent, Json::Value& alerts)
        {
            std::string json;
            google::protobuf::util::MessageToJsonString(alertEvent, &json);

            Json::Value alert;
            Json::Reader reader;
            if (reader.parse(json.c_str(), alert))
                alerts.append(alert);
        }
    };
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

POrmHandler CreateAlertsHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
{
    return POrmHandler(new CAlertsHandler(GET_LOGGER_PTR, grpcManager));
}

POrmHandler CreateAlertsFullHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
{
    return POrmHandler(new CAlertsFullHandler(GET_LOGGER_PTR, grpcManager));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
