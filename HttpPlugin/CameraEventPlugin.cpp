#include <memory>

#include <openssl/sha.h>
#include <boost/make_shared.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/algorithm/string.hpp>

#include "HttpPlugin.h"
#include "CommonUtility.h"

#include "Constants.h"
#include "BLQueryHelper.h"
#include "GrpcHelpers.h"
#include "RegexUtility.h"
#include "WebSocketSession.h"
#include "MetaCredentialsStorage.h"

#include <Crypto/Crypto.h>
#include <CorbaHelpers/Uuid.h>
#include <HttpServer/BasicServletImpl.h>

#include <json/json.h>

#include <google/protobuf/util/json_util.h>

#include <axxonsoft/bl/events/Notification.grpc.pb.h>
#include <axxonsoft/bl/events/Events.Internal.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

namespace
{
    const char* const SALT_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    const char* const JSON_QUERY_INCLUDE_FIELD = "include";
    const char* const JSON_QUERY_EXCLUDE_FIELD = "exclude";
    const char* const JSON_QUERY_TRACK_CONFIG_FIELD = "track";
    const char* const JSON_QUERY_UNTRACK_CONFIG_FIELD = "untrack";

    const char* const JSON_QUERY_METHOD_FIELD = "method";
    const char* const JSON_UPDATE_TOKEN_STATE = "update_token";
    const char* const PARAM_AUTH_TOKEN = "auth_token";

    const char* const PARAM_SCHEMA = "schema";
    const std::string NATIVE_SCHEMA = "native";
    const std::string PROTO_SCHEMA = "proto";


    using CameraEventReader_t = NWebGrpc::AsyncStreamReader < bl::events::DomainNotifier, bl::events::PullEventsRequest,
        bl::events::Events >;
    using PCameraEventReader_t = std::shared_ptr < CameraEventReader_t >;
    using CameraEventCallback_t = boost::function<void(const bl::events::Events&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    using UpdateSubscriptionReader_t = NWebGrpc::AsyncResultReader < bl::events::DomainNotifier, bl::events::UpdateSubscriptionRequest,
        bl::events::UpdateSubscriptionResponse >;
    using PUpdateSubscriptionReader_t = std::shared_ptr < UpdateSubscriptionReader_t >;

    using DisconnectChannelReader_t = NWebGrpc::AsyncResultReader < bl::events::DomainNotifier, bl::events::DisconnectEventChannelRequest,
        bl::events::DisconnectEventChannelResponse >;
    using PDisconnectChannelReader_t = std::shared_ptr < DisconnectChannelReader_t >;

    struct CameraState
    {
        bool m_hasArchives;
        bool m_active;
    };
    using PCameraState = std::shared_ptr<CameraState>;

    class CLatest
    {
    public:
        bool Newer(const std::string &access_point, std::chrono::steady_clock::time_point t)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            auto it = m_last.find(access_point);
            if (it == m_last.end())
            {
                m_last[access_point] = t;
                return true;
            }

            if (it->second < t)
            {
                it->second = t;
                return true;
            }

            return false;
        }
    private:
        std::mutex m_mutex;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_last;
    };

    class CameraStateTable
    {
    public:
        void Set(const std::string &camera, const CameraState &cameraState)
        {
            std::unique_lock<boost::shared_mutex> lock(m_mutex);
            m_map[camera] = cameraState;
        }

        boost::optional<CameraState> Get(const std::string &camera) const
        {
            boost::optional<CameraState> res;

            boost::shared_lock<boost::shared_mutex> lock(m_mutex);

            auto it = m_map.find(camera);
            if (it != m_map.end())
                res = it->second;

            return res;
        }

    private:
        std::unordered_map<std::string, CameraState> m_map; //camera->{defaultArchive, active}
        mutable boost::shared_mutex m_mutex;
    };


    class ArchivesRecordState
    {
    public:
        enum ERecordState { _NO, _ALWAYS};
        void Set(const std::string &camera, const std::string &archive, ERecordState state)
        {
            std::unique_lock<boost::shared_mutex> lock(m_mutex);
            m_map[camera][archive] = state;  // в случае если нет ключа camera - создаёт
        }

        bool ExistRecordingArchive(const std::string& camera) const  //существует хотя бы 1 записывающий архив
        {
            boost::shared_lock<boost::shared_mutex> lock(m_mutex);
            const auto t1 = m_map.find(camera);

            if (t1 != m_map.end())
                for (const auto &t2 : t1->second)
                    if (t2.second == _ALWAYS)
                        return true;

            return false;
        }
    private:
        std::unordered_map<std::string, std::unordered_map<std::string, ERecordState> > m_map; //camera->[archive->state]
        mutable boost::shared_mutex m_mutex;
    };

    class CClientContext : public NWebWS::IWebSocketClient
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CClientContext(DECLARE_LOGGER_ARG,
                       const NWebGrpc::PGrpcManager grpcManager,
                       NGrpcHelpers::PCredentials metaCredentials,
                       NWebWS::WWebSocketSession wsSession,
                       const std::string& id,
                       bool protoSchema)
            : m_grpcManager(grpcManager)
            , m_wsSession(wsSession)
            , m_subscriptionId(id)
            , m_protoSchema(protoSchema)
        {
            INIT_LOGGER_HOLDER;
            m_metaCredentialsStorage =
                NHttp::CreateMetaCredentialsStorage(GET_LOGGER_PTR, m_grpcManager, metaCredentials);
        }

        ~CClientContext()
        {
        }

        void Init() override
        {
            CameraEventCallback_t cb = boost::bind(&CClientContext::onCameraEvents,
                boost::weak_ptr<CClientContext>(shared_from_base<CClientContext>()), _1, _2, _3);

            m_eventReader =
                PCameraEventReader_t(new CameraEventReader_t(GET_LOGGER_PTR,
                                                m_grpcManager,
                                                m_metaCredentialsStorage->GetMetaCredentials(),
                                                &bl::events::DomainNotifier::Stub::AsyncPullEvents));

            bl::events::PullEventsRequest req;
            req.set_subscription_id(m_subscriptionId);
            bl::events::EventFilters* filter = req.mutable_filters();

            addMandatorySubscription(filter);

            m_eventReader->asyncRequest(req, cb);
        }

        void OnMessage(const std::string& msg) override
        {
            Json::Value data;
            Json::CharReaderBuilder msgReader;
            std::string err;
            std::istringstream is(msg);
            if (!Json::parseFromStream(msgReader, is, &data, &err))
            {
                sendWSError("Events query error");
                return;
            }

            if (data.isMember(JSON_QUERY_METHOD_FIELD))
            {
                std::string method;
                method.assign(data[JSON_QUERY_METHOD_FIELD].asString());
                if (method == JSON_UPDATE_TOKEN_STATE)
                    ProcessUpdateToken(data);
            }
            else
            {
                ProcessEventsCommand(data);
            }
        }

        void Stop() override
        {
            PDisconnectChannelReader_t reader(new DisconnectChannelReader_t
                                                (GET_LOGGER_PTR, m_grpcManager,
                                                  m_metaCredentialsStorage->GetMetaCredentials(),
                                                  &bl::events::DomainNotifier::Stub::AsyncDisconnectEventChannel));

            bl::events::DisconnectEventChannelRequest creq;
            creq.set_subscription_id(m_subscriptionId);

            auto pThis = shared_from_base<CClientContext>();
            reader->asyncRequest(creq, [this, pThis](const bl::events::DisconnectEventChannelResponse& res, grpc::Status status)
            {
                if (!status.ok())
                {
                    _err_ << "GRPC: Disconnect event channel";
                }
            });

            if (m_eventReader)
            {
                m_eventReader->asyncStop();
            }
        }

    private:
        void ProcessEventsCommand(Json::Value& data)
        {
            PUpdateSubscriptionReader_t reader(new UpdateSubscriptionReader_t(GET_LOGGER_PTR,
                                                m_grpcManager,
                                                m_metaCredentialsStorage->GetMetaCredentials(),
                                                &bl::events::DomainNotifier::Stub::AsyncUpdateSubscription));

            bl::events::UpdateSubscriptionRequest creq;
            constructRequest(creq, data);

            auto pThis = shared_from_base<CClientContext>();
            reader->asyncRequest(creq, [this, pThis](const bl::events::UpdateSubscriptionResponse& res, grpc::Status status)
                {
                    if (!status.ok())
                    {
                        sendWSError("GRPC update subscription failed");
                    }
                });
        }

        void ProcessUpdateToken(const Json::Value& data)
        {
            _inf_ << "Events. Process update token";
            std::string new_token;
            if (data.isMember(PARAM_AUTH_TOKEN))
                new_token.assign(data[PARAM_AUTH_TOKEN].asString());

            if (new_token.empty())
            {
                sendWSError("Events. auth_token field is required");
                return;
            }

            NHttp::DenyCallback_t dc = std::bind(&CClientContext::ProcessRejectToken,
                                                 shared_from_base<CClientContext>(), new_token);
            m_metaCredentialsStorage->UpdateToken(new_token, dc);
        }

        void ProcessRejectToken(const std::string newToken)
        {
            sendWSError("Events. Rejected udpating token " + newToken);
        }

        enum ECameraState {_OFF, _GRAY, _ON};
        static std::string Convert(bl::events::MacroEvent_ERuleActionType st)
        {
            switch (st)
            {
            case bl::events::MacroEvent_ERuleActionType_RAT_WRITE_ARCHIVE:
                return "write_archive";
            case bl::events::MacroEvent_ERuleActionType_RAT_RAISE_ALERT:
                return "raise_alert";
            case bl::events::MacroEvent_ERuleActionType_RAT_SWITCH_RELAY:
                return "switch_relay";
            case bl::events::MacroEvent_ERuleActionType_RAT_GOTO_PRESET:
                return "goto_preset";
            case bl::events::MacroEvent_ERuleActionType_RAT_VOICE_NOTIFICATION:
                return "voice_notification";
            case bl::events::MacroEvent_ERuleActionType_RAT_EMAIL_NOTIFICATION:
                return "email_notification";
            case bl::events::MacroEvent_ERuleActionType_RAT_SMS_NOTIFICATION:
                return "sms_notification";
            case bl::events::MacroEvent_ERuleActionType_RAT_GUI:
                return "gui";
            case bl::events::MacroEvent_ERuleActionType_RAT_ARM_STATE:
                return "arm_state";
            case bl::events::MacroEvent_ERuleActionType_RAT_MM_EXPORT:
                return "mm_export";
            case bl::events::MacroEvent_ERuleActionType_RAT_SINK_AUDIO_NOTIFICATION:
                return "sink_audio_notification";
            case bl::events::MacroEvent_ERuleActionType_RAT_GROUP_ACTION:
                return "group_action";
            case bl::events::MacroEvent_ERuleActionType_RAT_SPECIAL_ACTION:
                return "special_action";
            case bl::events::MacroEvent_ERuleActionType_RAT_REPLICATION:
                return "replication";
            case bl::events::MacroEvent_ERuleActionType_RAT_SERVICE_STATE:
                return "service_state";
            case bl::events::MacroEvent_ERuleActionType_RAT_CONFIGURE_ACTION:
                return "configure_action";
            case bl::events::MacroEvent_ERuleActionType_RAT_INVOKE_MACRO_ACTION:
                return "invoke_macro_action";
            case bl::events::MacroEvent_ERuleActionType_RAT_WEB_QUERY_ACTION:
                return "web_query_action";

            default:
                break;
            }

            return "";
        }

        static std::string Convert(bl::events::DetectorEvent_AlertState st)
        {
            switch (st)
            {
            case bl::events::DetectorEvent_AlertState_HAPPENED:
                return "happened";
            case bl::events::DetectorEvent_AlertState_BEGAN:
                return "began";
            case bl::events::DetectorEvent_AlertState_ENDED:
                return "ended";
            case bl::events::DetectorEvent_AlertState_SPECIFIED:
                return "specefied";
            default:
                break;
            }

            return "";
        }

        static std::string Convert(bl::events::AlertState_EState st)
        {
            switch (st)
            {
            case bl::events::AlertState_EState_ST_NONE:
                return "none";
            case bl::events::AlertState_EState_ST_WANT_REACTION:
                return "reaction";
            case bl::events::AlertState_EState_ST_WANT_PROCESSING:
                return "processing";
            case bl::events::AlertState_EState_ST_CLOSED:
                return "closed";
            default:
                break;
            }

            return "";
        }

        static std::string Convert(bl::events::AlertState_ESeverity st)
        {
            switch (st)
            {
            case bl::events::AlertState_ESeverity_SV_UNCLASSIFIED:
                return "unclassified";
            case bl::events::AlertState_ESeverity_SV_FALSE:
                return "false";
            case bl::events::AlertState_ESeverity_SV_WARNING:
                return "warning";
            case bl::events::AlertState_ESeverity_SV_ALARM:
                return "alarm";
            default:
                break;
            }

            return "";
        }

        static std::string Convert(bl::events::AlertState_EReviewerType st)
        {
            switch (st)
            {
            case bl::events::AlertState_EReviewerType_RT_SYSTEM:
                return "system";
            case bl::events::AlertState_EReviewerType_RT_USER:
                return "user";
            default:
                break;
            }

            return "";
        }

        static std::string Convert(bl::events::MacroEvent_EActionExecutionPhase st)
        {
            switch (st)
            {
            case bl::events::MacroEvent_EActionExecutionPhase_AEP_STARTED:
                return "started";
            case bl::events::MacroEvent_EActionExecutionPhase_AEP_COMPLETED:
                return "completed";
            case bl::events::MacroEvent_EActionExecutionPhase_AEP_INTERRUPTED:
                return "interrupted";
            case bl::events::MacroEvent_EActionExecutionPhase_AEP_FAILED:
                return "failed";
            default:
                break;
            }

            return "";
        }

        static void Convert(const bl::events::AlertState &deviceStateAlert, Json::Value &obj)
        {
            obj["type"] = "alert_state";
            obj["name"] = deviceStateAlert.camera().access_point();
            obj["reviewer"] = deviceStateAlert.reviewer();
            obj["severity"] = Convert(deviceStateAlert.severity());
            obj["state"] = Convert(deviceStateAlert.state());
            obj["reviewer_type"] = Convert(deviceStateAlert.reviewer_type());
            obj["id"] = deviceStateAlert.guid();
            obj["alert_id"] = deviceStateAlert.alert_id();
            obj["message"] = deviceStateAlert.bookmark().message();
        }


        static std::string  ConvertRecordState(ECameraState state)
        {
            switch (state)
            {
            case _OFF:
                return "off";
            case _GRAY:
                return "gray";
            case _ON:
                return "on";
            default:
                return "off";
            }

            return "off";
        }

        static Json::Value AddStates(const bl::events::Alert &deviceAlert)
        {
            Json::Value states(Json::arrayValue);
            for (int j = 0; j < deviceAlert.states_size();++j)
            {
                Json::Value simple_state(Json::objectValue);
                Convert(deviceAlert.states(j), simple_state);
                states.append(simple_state);
            }

            return states;
        }

        void sendWSError(std::string&& reason)
        {
            _err_ << reason;

            NWebWS::PWebSocketSession session = m_wsSession.lock();
            if (session)
            {
                session->SendError();
            }
        }

        static void onCameraEvents(boost::weak_ptr<CClientContext> obj, const bl::events::Events& evs, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CClientContext> owner = obj.lock();
            if (owner)
            {
                owner->processEvents(evs, status, grpcStatus);
            }
        }

        void processEvents(const bl::events::Events& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                sendWSError("GRPC pull events failed");
                return;
            }

            int itemCount = res.items_size();

            if (0 == itemCount)
                return;

            Json::Value state(Json::objectValue);
            Json::Value events(Json::arrayValue);

            for (int i = 0; i < itemCount; ++i)
            {
                const bl::events::Event& ev = res.items(i);

                if (m_protoSchema)
                    addProtoEvent(ev, events);
                else
                    addNativeEvent(ev, events);            
            }

            if (0 != events.size())
            {
                state["objects"] = events;

                NWebWS::PWebSocketSession session = m_wsSession.lock();
                if (session)
                {
                    _log_ << "to_websocket:" << Json::FastWriter().write(state).c_str(); //TODO: del
                    session->SendText(state.toStyledString());
                }
            }
        }

        void addNativeEvent(const bl::events::Event& ev, Json::Value& events)
        {
            bl::events::Alert deviceAlert;
            if (ev.body().UnpackTo(&deviceAlert))
            {
                Json::Value obj(Json::objectValue);

                obj["type"] = "alert";
                obj["source"] = deviceAlert.camera().access_point();
                obj["archive"] = deviceAlert.archive().access_point();
                obj["initiator"] = deviceAlert.initiator();
                obj["id"] = deviceAlert.guid();
                obj["states"] = AddStates(deviceAlert);
                obj["timestamp"] = deviceAlert.timestamp();

                if (bl::events::Alert_EInitiatorType::Alert_EInitiatorType_AIT_USER == deviceAlert.initiator_type())
                {
                    obj["initiator_type"] = "user";
                    obj["state_user"] = Convert(deviceAlert.detector().state());
                }
                else
                {
                    obj["initiator_type"] = "macro";
                    obj["state_macro"] = Convert(deviceAlert.macro().action_type());
                    obj["phase"] = Convert(deviceAlert.macro().phase());
                }

                if (deviceAlert.has_detector())
                {
                    Json::Value event(Json::objectValue);

                    event["event_id"] = deviceAlert.detector().guid();
                    event["event_type"] = deviceAlert.detector().event_type();
                    event["detector_access_point"] = deviceAlert.detector().detector_ext().access_point();

                    obj["event"] = event;
                }

                events.append(obj);
            }


            bl::events::AlertState deviceStateAlert;
            if (ev.body().UnpackTo(&deviceStateAlert))
            {
                Json::Value obj(Json::objectValue);
                Convert(deviceStateAlert, obj);
                events.append(obj);
            }

            bl::events::IpDeviceStateChangedEvent deviceState;
            if (ev.body().UnpackTo(&deviceState))
            {
                std::string st(deviceState.State_Name(deviceState.state()));
                bl::events::IpDeviceStateChangedEvent_State state = deviceState.state();

                std::string objectId(deviceState.object_id_ext().access_point());

                Json::Value obj(Json::objectValue);
                obj["type"] = "devicestatechanged";
                obj["name"] = objectId;
                obj["state"] = deviceStatusToString(state);
                events.append(obj);

                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(&CClientContext::sUpdateCameraState,
                    boost::weak_ptr<CClientContext>(shared_from_base<CClientContext>()), objectId, std::chrono::steady_clock::now()));
            }

            bl::events::CameraChangedEvent cameraChangeEv;
            if (ev.body().UnpackTo(&cameraChangeEv))
            {
                Json::Value obj(Json::objectValue);
                obj["type"] = "cameralistupdate";
                obj["name"] = cameraChangeEv.id();
                obj["state"] = cameraStatusToString(cameraChangeEv.action());
                events.append(obj);

                if (bl::events::CameraChangedEvent_ChangeAction_CHANGED == cameraChangeEv.action())
                    // событие отвечает за букву R (идет ли запись в архив). Событие: смена дефолтного архива
                    NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(&CClientContext::sUpdateCameraState,
                        boost::weak_ptr<CClientContext>(shared_from_base<CClientContext>()), cameraChangeEv.id(), std::chrono::steady_clock::now()));
            }

            bl::events::StateControlStateChangeEvent stateEv;
            if (ev.body().UnpackTo(&stateEv))
            {  //событие: в архиве поменять: постоянная запись: нет/всегда
                auto camera = stateEv.parent_source().access_point();
                m_archivesRecordStates.Set(camera, stateEv.object_id().access_point(),
                    stateEv.new_state() == bl::events::StateControlStateChangeEvent_SCSwitch_ON ? ArchivesRecordState::_ALWAYS : ArchivesRecordState::_NO);

                Json::Value obj = createCameraRecordStateObject(camera);
                events.append(obj);
            }

            bl::events::DetectorEvent detectorEvent;
            if (ev.body().UnpackTo(&detectorEvent))
            {
                Json::Value obj(Json::objectValue);
                obj["type"] = "detector_event";
                obj["id"] = detectorEvent.guid();
                obj["event_type"] = detectorEvent.event_type();
                obj["timestamp"] = detectorEvent.timestamp();
                auto source = detectorEvent.origin_ext().access_point();
                obj["source"] = source;
                obj["state"] = detectorEvent.state();

                serializeDetails(obj, detectorEvent.details());

                events.append(obj);
            }

            bl::events::SItemStatus statusEvent;
            if (ev.body().UnpackTo(&statusEvent))
            {
                if (bl::events::EStatus::S_Changed == statusEvent.status())
                {
                    Json::Value obj(Json::objectValue);
                    obj["type"] = "itemstatuschanged";
                    obj["name"] = statusEvent.item_name();

                    events.append(obj);
                }
            }

            bl::events::ConfigChangedEvent configEvent;
            if (ev.body().UnpackTo(&configEvent))
            {
                eventToJSON(configEvent, "ConfigChangedEvent", events);
            }

            bl::events::ConfigLinkageChangedEvent linkageEvent;
            if (ev.body().UnpackTo(&linkageEvent))
            {
                eventToJSON(linkageEvent, "ConfigLinkageChangedEvent", events);
            }
        }

        void addProtoEvent(const bl::events::Event& ev, Json::Value& events)
        {
            std::string output;
            google::protobuf::util::MessageToJsonString(ev, &output);

            Json::Value event;
            Json::Reader reader;
            bool parsingSuccessful = reader.parse(output, event);
            if (parsingSuccessful)
                events.append(event);
            else
                _err_ << "Proto event parsing failed";
        }

        void eventToJSON(const ::google::protobuf::Message& msg, const std::string& type, Json::Value& events)
        {
            std::string styledJson;

            google::protobuf::util::JsonPrintOptions options;
            options.always_print_enums_as_ints = true;
            options.always_print_primitive_fields = true;

            google::protobuf::util::MessageToJsonString(msg, &styledJson, options);

            Json::Value obj;
            Json::Reader reader;
            bool parsingSuccessful = reader.parse(styledJson.c_str(), obj);
            if (parsingSuccessful)
            {
                obj["type"] = type;
                events.append(obj);
            }
            else
                _wrn_ << type << " event parsing error";
        }

        static void serializeDetails(Json::Value& ev,
            const ::google::protobuf::RepeatedPtrField<bl::events::DetectorEvent::Details>& details)
        {
            for (auto d : details)
            {
                if (d.has_rectangle())
                    serializeRectangle(ev, d.rectangle());
                else if (d.has_auto_recognition_result())
                    serializeAutoEvent(ev, d.auto_recognition_result());
                else if (d.has_listed_item_detected_result())
                    serializeListedInfo(ev, d.listed_item_detected_result());
                else if (d.has_face_recognition_result())
                    serializeFaceEvent(ev, d.face_recognition_result());
                else if (d.has_lots_objects())
                    serializeLotsObjectsEvent(ev, d.lots_objects());
                else if (d.has_ppe_detector())
                    serializeVLPpeEvent(ev, d.ppe_detector());
            }
        }

        static void serializeFaceEvent(Json::Value& ev, const bl::events::DetectorEvent::FaceRecognitionResult frr)
        {
            Json::Value faceInfo(Json::objectValue);

            faceInfo["age"] = frr.age();
            faceInfo["gender"] = frr.gender();

            ev["faceInfo"] = faceInfo;
        }

        static void serializeLotsObjectsEvent(Json::Value& ev, const bl::events::DetectorEvent::LotsObjectsInfo loi)
        {
            Json::Value lotsObjects(Json::objectValue);

            lotsObjects["objectCount"] = loi.object_count();

            ev["lotsObjects"] = lotsObjects;
        }

        static void serializeVLPpeEvent(Json::Value& ev, const bl::events::DetectorEvent::VLPpeDetectorInfo vlpdi)
        {
            Json::Value vlPpeInfo(Json::objectValue);

            vlPpeInfo["ppeType"] = vlpdi.ppe_type();

            ev["vlPpeInfo"] = vlPpeInfo;
        }

        static void serializeAutoEvent(Json::Value& ev, const bl::events::DetectorEvent::AutoRecognitionResult arr)
        {
            std::int32_t quality = 0;
            bl::events::DetectorEvent::AutoRecognitionHypotheses bestHypo;
            const ::google::protobuf::RepeatedPtrField < bl::events::DetectorEvent::AutoRecognitionHypotheses> hypos = arr.hypotheses();
            int hypoCount = hypos.size();
            for (int i = 0; i < hypoCount; ++i)
            {
                const bl::events::DetectorEvent::AutoRecognitionHypotheses& hypo = hypos.Get(i);
                std::int32_t currentQuality = hypo.ocr_quality();
                if (currentQuality > quality)
                {
                    quality = currentQuality;
                    bestHypo = hypo;
                }
            }

            ev["plate_full"] = bestHypo.plate_full();            

            serializeRectangle(ev, bestHypo.plate_rectangle());
        }

        static void serializeRectangle(Json::Value& ev, const bl::primitive::Rectangle& r)
        {
            Json::Value rects(Json::arrayValue);

            Json::Value rect(Json::objectValue);
            rect["index"] = r.index();
            rect["left"] = r.x();
            rect["top"] = r.y();
            rect["right"] = r.x() + r.w();
            rect["bottom"] = r.y() + r.h();

            rects.append(rect);

            ev["rectangles"] = rects;
        }

        static void serializeListedInfo(Json::Value& ev, const bl::events::DetectorEvent::ListedItemDetectedInfo lidi)
        {
            Json::Value listedInfo(Json::objectValue);

            listedInfo["lists_Info"] = Json::arrayValue;
            for (const auto& l : lidi.lists_info())
            {
                Json::Value list{ Json::objectValue };
                list["list_id"] = l.list_id();
                list["list_name"] = l.list_name();

                listedInfo["lists_Info"].append(list);
            }

            listedInfo["itemId"] = lidi.listed_item_id();
            listedInfo["itemName"] = lidi.name();

            if (lidi.has_listed_plate_info())
                listedInfo["plate"] = lidi.listed_plate_info().plate();

            ev["listedInfo"] = listedInfo;
        }

        static void sUpdateCameraState(boost::weak_ptr<CClientContext> obj, const std::string &access_point, std::chrono::steady_clock::time_point t)
        {
            boost::shared_ptr<CClientContext> owner = obj.lock();
            if (owner)
            {
                owner->updateCameraState(access_point, t);
            }
        }

        void updateCameraState(const std::string &access_point, std::chrono::steady_clock::time_point t)
        {
            PCameraState ctxOut = std::make_shared<CameraState>();
            NWebBL::TEndpoints eps{ access_point };
            NWebBL::FAction action = boost::bind(&CClientContext::onCameraInfo, shared_from_base<CClientContext>(),
                access_point, t, ctxOut, _1, _2, _3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager,
                                     m_metaCredentialsStorage->GetMetaCredentials(), eps, action);
        }

        void onCameraInfo(std::string access_point, std::chrono::steady_clock::time_point timePoint, PCameraState ctxOut,
            const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                _err_ << "Update camera state: GetCamerasByComponents method failed";
                return;
            }

            getCameraInfo(ctxOut, cams);

            if (valid == NWebGrpc::_FINISH)
            {
                execUpdateCameraState(access_point, *ctxOut, timePoint);
            }
        }

        void getCameraInfo(PCameraState ctxOut, const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items)
        {
            if (items.size() > 0)
            {
                const bl::domain::Camera& c = items.Get(0);

                int arcCount = c.archive_bindings_size();
                ctxOut->m_hasArchives = false;
                for (int j = 0; j < arcCount; ++j)
                {
                    const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                    if (ab.archive().is_activated())
                        ctxOut->m_hasArchives = true;
                }

                ctxOut->m_active = c.is_activated();
            }
        }

        void execUpdateCameraState(const std::string &access_point, const CameraState &cameraState, std::chrono::steady_clock::time_point t)
        {
            NWebWS::PWebSocketSession session = m_wsSession.lock();
            if (session && m_latestEvent.Newer(access_point, t))
            {
                m_cameraStateTable.Set(access_point, cameraState);

                Json::Value state(Json::objectValue);
                Json::Value events(Json::arrayValue);

                Json::Value obj = createCameraRecordStateObject(access_point);
                events.append(obj);
                state["objects"] = events;

                session->SendText(state.toStyledString());
            }
        }

        Json::Value createCameraRecordStateObject(const std::string &camera)
        {
            Json::Value obj(Json::objectValue);
            obj["type"] = "camera_record_state";
            obj["source"] = camera;
            obj["state"] = ConvertRecordState(getCameraRecordState(camera));

            return obj;
        }

        ECameraState getCameraRecordState(const std::string &camera)
        {
            auto cameraState = m_cameraStateTable.Get(camera);

            if (!cameraState || !cameraState->m_active)
                return _OFF;

            if (m_archivesRecordStates.ExistRecordingArchive(camera))
                return _ON;
            
            return (cameraState->m_hasArchives) ? _GRAY : _OFF;
        }

        std::string deviceStatusToString(bl::events::IpDeviceStateChangedEvent_State status)
        {
            switch (status)
            {
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_CONNECTED:
                return "connected";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_IPINT_INTERNAL_FAILURE:
                return "internal failure";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_DISCONNECTED:
                return "disconnected";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_RESTORED:
                return "signal restored";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_LOST:
                return "signal lost";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_AUTHORIZATION_FAILED:
                return "authorization failed";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_ACCEPT_SETTINGS_FAILURE:
                return "accept settings failure";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_REBOOTED:
                return "rebooted";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_NETWORK_FAILURE:
                return "network failure";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_CONNECTION_ERROR:
                return "connection error";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_SERIAL_NUMBER_VALIDATION_FAILED:
                return "serial number validation failed";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_DEVICE_CONFIGURATION_CHANGED:
                return "configuration changed";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_ALL_CONSUMERS_DISCONNECTED:
                return "all consumers disconnected";
            case bl::events::IpDeviceStateChangedEvent_State_IPDS_NOT_ENOUGH_BANDWIDTH:
                return "not enough bandwidth";
            default:
                return "unknown";
            }
        }

        std::string cameraStatusToString(bl::events::CameraChangedEvent_ChangeAction status)
        {
            switch (status)
            {
            case bl::events::CameraChangedEvent_ChangeAction_ADDED:
                return "added";
            case bl::events::CameraChangedEvent_ChangeAction_REMOVED:
                return "removed";
            case bl::events::CameraChangedEvent_ChangeAction_CHANGED:
                return "changed";
            default:
                return "unknown";
            }
        }

        template <typename TRequest>
        void constructRequest(TRequest& req, const Json::Value& data)
        {
            req.set_subscription_id(m_subscriptionId);

            bl::events::EventFilters* filter = req.mutable_filters();

            if (data.isMember(JSON_QUERY_INCLUDE_FIELD))
            {
                const Json::Value& includes = data[JSON_QUERY_INCLUDE_FIELD];
                for (Json::Value::ArrayIndex i = 0; i < includes.size(); ++i)
                {
                    _log_ << "Include: " << includes[i].asString();

                    bl::events::EventFilter* f1 = filter->add_include();
                    f1->set_event_type(bl::events::ET_IpDeviceStateChangedEvent);
                    f1->set_subject(includes[i].asString());

                    bl::events::EventFilter* fst = filter->add_include();
                    fst->set_event_type(bl::events::ET_StateControlStateChangeEvent);
                    fst->set_subject(includes[i].asString());
                }
            }

            if (data.isMember(JSON_QUERY_EXCLUDE_FIELD))
            {
                const Json::Value& excludes = data[JSON_QUERY_EXCLUDE_FIELD];
                for (Json::Value::ArrayIndex i = 0; i < excludes.size(); ++i)
                {
                    _log_ << "Exclude: " << excludes[i].asString();

                    bl::events::EventFilter* f1 = filter->add_exclude();
                    f1->set_event_type(bl::events::ET_IpDeviceStateChangedEvent);
                    f1->set_subject(excludes[i].asString());

                    bl::events::EventFilter* fst = filter->add_exclude();
                    fst->set_event_type(bl::events::ET_StateControlStateChangeEvent);
                    fst->set_subject(excludes[i].asString());
                }
            }

            if (data.isMember(JSON_QUERY_TRACK_CONFIG_FIELD))
            {
                const Json::Value& watch = data[JSON_QUERY_TRACK_CONFIG_FIELD];
                for (Json::Value::ArrayIndex i = 0; i < watch.size(); ++i)
                {
                    _log_ << "Track: " << watch[i].asString();

                    bl::events::EventFilter* f1 = filter->add_include();
                    f1->set_event_type(bl::events::ET_SItemStatus);
                    f1->set_subject(watch[i].asString());
                }
            }

            if (data.isMember(JSON_QUERY_UNTRACK_CONFIG_FIELD))
            {
                const Json::Value& excludes = data[JSON_QUERY_UNTRACK_CONFIG_FIELD];
                for (Json::Value::ArrayIndex i = 0; i < excludes.size(); ++i)
                {
                    _log_ << "Untrack: " << excludes[i].asString();

                    bl::events::EventFilter* f1 = filter->add_exclude();
                    f1->set_event_type(bl::events::ET_SItemStatus);
                    f1->set_subject(excludes[i].asString());
                }
            }

            addMandatorySubscription(filter);
        }

        void addMandatorySubscription(bl::events::EventFilters* filter)
        {
            bl::events::EventFilter* f1 = filter->add_include();
            f1->set_event_type(bl::events::ET_CameraChangedEvent);

            bl::events::EventFilter* f2 = filter->add_include();
            f2->set_event_type(bl::events::ET_Alert);
            f2->set_domain_wide(true);

            bl::events::EventFilter* f3 = filter->add_include();
            f3->set_event_type(bl::events::ET_AlertState);

            bl::events::EventFilter* f4 = filter->add_include();
            f4->set_event_type(bl::events::ET_DetectorEvent);
            f4->set_domain_wide(true);

            bl::events::EventFilter* f5 = filter->add_include();
            f5->set_event_type(bl::events::ET_ConfigChangedEvent);

            bl::events::EventFilter* f6 = filter->add_include();
            f6->set_event_type(bl::events::ET_ConfigLinkageChangedEvent);
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
        NWebWS::WWebSocketSession m_wsSession;
        const std::string m_subscriptionId;
        bool m_protoSchema;

        PCameraEventReader_t m_eventReader;
        CameraStateTable m_cameraStateTable;
        ArchivesRecordState m_archivesRecordStates;
        CLatest m_latestEvent;
        PMetaCredentialsStorage m_metaCredentialsStorage;
    };

    class CCameraEventContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CCameraEventContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, UrlBuilderSP uriBuilder)
            : m_grpcManager(grpcManager)
            , m_uriBuilder(uriBuilder)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            namespace npu = NPluginUtility;

            npu::TParams params;
            if (!npu::ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            int enableTokenAuth = 0;
            int validForHours = 0;
            npu::GetParam(params, PARAM_ENABLE_TOKEN_AUTH, enableTokenAuth, 0);
            npu::GetParam(params, PARAM_TOKEN_VALID_HOURS, validForHours, 12);

            std::string schema;
            npu::GetParam(params, PARAM_SCHEMA, schema, NATIVE_SCHEMA);

            bool protoSchema = boost::algorithm::iequals(schema, PROTO_SCHEMA);
            _log_ << "Use proto schema " << protoSchema;

            auto expiresAt = boost::posix_time::second_clock::local_time() +
            boost::posix_time::hours(std::min(abs(validForHours), 24 * 7)); // Lease a week at max.

            if (enableTokenAuth)
            {
                m_uriBuilder->handleHttpToken(resp, req, params, expiresAt);
                return;
            }

            boost::optional<const std::string&> h = req->GetHeader("Sec-WebSocket-Key");

            std::string wsKey;
            if (h)
                wsKey.assign(*h);

            if (wsKey.empty())
            {
                _err_ << "Query does not contain Sec-WebSocket-Key header";
                Error(resp, IResponse::BadRequest);
                return;
            }

            wsKey.append(SALT_GUID);

            uint8_t hash[SHA_DIGEST_LENGTH];
            calculateSHA1(&wsKey[0], wsKey.size(), hash);

            std::string acceptKey(NCrypto::ToBase64Padded(hash, SHA_DIGEST_LENGTH));

            resp->SetStatus(NHttp::IResponse::SwitchingProtocols);
            resp << SHttpHeader("Upgrade", "websocket");
            resp << SHttpHeader("Connection", "Upgrade");
            resp << SHttpHeader("Sec-WebSocket-Accept", acceptKey);
            resp->FlushHeaders();

            const std::string subscriptionId(NCorbaHelpers::GenerateUUIDString());

            NWebWS::PWebSocketSession session = NWebWS::CreateWebSocketSession(GET_LOGGER_PTR, req, resp,
                std::bind(&CCameraEventContentImpl::onWSSessionClose,
                    boost::weak_ptr<CCameraEventContentImpl>(shared_from_base<CCameraEventContentImpl>()),
                    subscriptionId));

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_sessions.insert(std::make_pair(subscriptionId, session));
            }

            const IRequest::AuthSession& as = req->GetAuthSession();

            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            NWebWS::PWebSocketClient wsClient = boost::make_shared<CClientContext>(GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, NWebWS::WWebSocketSession(session), subscriptionId, protoSchema);

            session->Start(wsClient);
        }

    private:

        bool calculateSHA1(void* input, unsigned long length, unsigned char* md)
        {
            SHA_CTX context;
            if (!SHA1_Init(&context))
                return false;

            if (!SHA1_Update(&context, (unsigned char*)input, length))
                return false;

            if (!SHA1_Final(md, &context))
                return false;

            return true;
        }

        static void onWSSessionClose(boost::weak_ptr<CCameraEventContentImpl> obj, const std::string& id)
        {
            boost::shared_ptr<CCameraEventContentImpl> owner = obj.lock();
            if (owner)
            {
                owner->clearWSSession(id);
            }
        }

        void clearWSSession(const std::string& id)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_sessions.erase(id);
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
        UrlBuilderSP m_uriBuilder;

        std::mutex m_mutex;
        typedef std::map<std::string, NWebWS::PWebSocketSession> TWSSessions;
        TWSSessions m_sessions;
    };
}

namespace NHttp
{
    IServlet* CreateCameraEventServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, UrlBuilderSP uriBuilder)
    {
        return new CCameraEventContentImpl(GET_LOGGER_PTR, grpcManager, uriBuilder);
    }
}
