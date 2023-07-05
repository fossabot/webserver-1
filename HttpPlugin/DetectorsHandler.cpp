#include "OrmHandler.h"
#include "Constants.h"
#include "CommonUtility.h"
#include "BLQueryHelper.h"

#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>
#include <axxonsoft/bl/archive/ArchiveSupport.grpc.pb.h>

#include <json/json.h>

using namespace NHttp;
namespace bl = axxonsoft::bl;
namespace bpt = boost::posix_time;

namespace
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    using EventReader_t = NWebGrpc::AsyncStreamReader < bl::events::EventHistoryService, bl::events::ReadEventsRequest,
        bl::events::ReadEventsResponse >;
    using PEventReader_t = std::shared_ptr < EventReader_t >;

    using EventCallback_t = std::function<void(const bl::events::ReadEventsResponse&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    using IntervalReader_t = NWebGrpc::AsyncResultReader < bl::archive::ArchiveService, bl::archive::GetHistoryRequest,
        bl::archive::GetHistoryResponse >;
    using PIntervalReader_t = std::shared_ptr < IntervalReader_t >;

    typedef std::shared_ptr<Json::Value> PJsonValue;
    typedef std::map<std::string, bl::events::DetectorEvent> TPhaseMap;
    typedef std::shared_ptr<TPhaseMap> PPhaseMap;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CDetectorsHandler : public COrmHandler
    {
    public:
        explicit CDetectorsHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
            : COrmHandler(GET_LOGGER_PTR)
            , m_grpcManager(grpcManager)
        {}

    private:
        void Process(const NHttp::PRequest req, NHttp::PResponse resp, const SParams &params) override
        {
            if (params.LimitToArchive)
            {
                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                NWebBL::GetArchiveDepth(GET_LOGGER_PTR, m_grpcManager, metaCredentials, params.Archive, params.Source,
                    [this, req, resp, params](bool ok, const boost::posix_time::ptime& tdepth)
                {
                    _log_ << "Detectors: archive depth for " << params.Source  << " and " << params.Archive <<": " << (ok?1:0) << " = " << (ok ? tdepth : params.Begin) ;
                    execProcess(req, resp, params, ok ? std::max(tdepth, params.Begin) : params.Begin);
                });

                return;
            }

            execProcess(req, resp, params, params.Begin);
        }

        void execProcess(const NHttp::PRequest req, NHttp::PResponse resp, const SParams &params, bpt::ptime tBegin)
        {
            _dbg_ << "begin_time: "<< boost::posix_time::to_simple_string(tBegin); //TODO: del

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            PJsonValue json = std::make_shared<Json::Value>(Json::objectValue);
            (*json)["events"] = Json::Value(Json::arrayValue);
            (*json)["more"] = false;

            PPhaseMap phaseMap = params.JoinPhases ? std::make_shared<TPhaseMap>() : std::shared_ptr<TPhaseMap>();

            /*if (!params.Source.empty())
            {
                std::string* rl = creq.add_camera_names();
                *rl = params.Source;
            }*/

            /*if (params.Source.empty() && !params.Host.empty())
            {
                std::string* rl = creq.add_node_names();
                *rl = params.Host;
            }*/

            //bl::events::JEOrderBy* ob = filter->mutable_order_by();
            //ob->set_ascending(params.ReverseOrder);

            bl::events::ReadEventsRequest creq;
            creq.mutable_range()->set_begin_time(boost::posix_time::to_simple_string(tBegin));
            creq.mutable_range()->set_end_time(boost::posix_time::to_simple_string(params.End));

            auto filter = creq.mutable_filters();
            bl::events::SearchFilter* typeFilter = filter->add_filters();
            typeFilter->set_type(bl::events::ET_DetectorEvent);

            for (auto&& t : params.Type)
            {
                if (!t.empty())
                    typeFilter->add_values(t);
            }

            if (!params.Source.empty())
            {
                ::google::protobuf::RepeatedPtrField<::std::string>* subjs = typeFilter->mutable_subjects();
                std::string* subj = subjs->Add();
                *subj = params.Detector.empty() ? NPluginUtility::convertToMainStream(params.Source) : params.Detector;
            }

            creq.set_limit(params.Limit);
            creq.set_offset(params.Offset);
            creq.set_descending(!params.ReverseOrder);

            EventCallback_t cb = boost::bind(&CDetectorsHandler::onEventsResponse,
                boost::weak_ptr<CDetectorsHandler>(shared_from_base<CDetectorsHandler>()), req, resp, json, phaseMap, params.Limit,
                _1, _2, _3);

            PEventReader_t reader(new EventReader_t
            (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::events::EventHistoryService::Stub::AsyncReadEvents));

            reader->asyncRequest(creq, cb);
        }

        static void onEventsResponse(boost::weak_ptr<CDetectorsHandler> obj, const NHttp::PRequest req, NHttp::PResponse resp, PJsonValue json,
            PPhaseMap phaseMap, int expectedCount, const bl::events::ReadEventsResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            boost::shared_ptr<CDetectorsHandler> owner = obj.lock();
            if (owner)
            {
                if (!grpcStatus.ok())
                {
                    return NPluginUtility::SendGRPCError(resp, grpcStatus);
                }

                if (NWebGrpc::_FINISH == status)
                {
                    if ((*json)["events"].size() >= (Json::ArrayIndex)(expectedCount))
                        (*json)["more"] = true;
                    NPluginUtility::SendText(req, resp, json->toStyledString(), NHttp::IResponse::OK, 3600);
                    return;
                }

                Json::Value& events = (*json)["events"];
                const ::google::protobuf::RepeatedPtrField<bl::events::Event> evRes = res.items();
                int itemCount = evRes.size();
                for (int i = 0; i < itemCount; ++i)
                {
                    if (phaseMap)
                        owner->buildDurationEvent(phaseMap, evRes.Get(i), events);
                    else
                        owner->buildEvent(evRes.Get(i), events);
                }
                //(*json)["more"] = res.session().more();
            }
        }

        void buildEvent(const bl::events::Event& event, Json::Value& events)
        {
            Json::Value ev(Json::objectValue);

            bl::events::DetectorEvent de;
            if (event.body().UnpackTo(&de))
            {
                ev["alertState"] = Convert(de.state());
                if (!de.multi_phase_id().empty())
                    ev["multiPhaseSyncId"] = de.multi_phase_id();

                setEventInfo(de, ev, events);
            }
        }

        void buildDurationEvent(PPhaseMap phaseMap, const bl::events::Event& event, Json::Value& events)
        {
            Json::Value ev(Json::objectValue);

            bl::events::DetectorEvent de;
            if (event.body().UnpackTo(&de))
            {
                std::string MultiPhaseSyncId(de.multi_phase_id());
                if (!MultiPhaseSyncId.empty())
                {
                    TPhaseMap::iterator dit = phaseMap->find(MultiPhaseSyncId);
                    if (phaseMap->end() == dit)
                    {
                        phaseMap->insert(std::make_pair(MultiPhaseSyncId, de));
                        return;
                    }
                    else
                    {
                        bool isEndPhase = bl::events::DetectorEvent::BEGAN != de.state();
                        boost::posix_time::ptime p1 = boost::posix_time::from_iso_string(de.timestamp()),
                            p2 = boost::posix_time::from_iso_string(dit->second.timestamp());
                        boost::posix_time::time_duration td = isEndPhase ? p1 - p2 : p2 - p1;
                        ev["duration"] = boost::posix_time::to_iso_string(td);

                        if (isEndPhase)
                            de = dit->second;
                    }
                }
                setEventInfo(de, ev, events);
            }
        }

        void setEventInfo(const bl::events::DetectorEvent& de, Json::Value& ev, Json::Value& events)
        {
            ev["id"] = de.guid();
            ev["source"] = de.origin_ext().access_point();
            ev["origin"] = de.detector_ext().access_point();
            ev["type"] = de.event_type();
            ev["timestamp"] = de.timestamp();

            serializeDetails(ev, de.details());

            fillExtra(ev, de.details());

            events.append(ev);
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
            }
        }

        static void serializeFaceEvent(Json::Value& ev, const bl::events::DetectorEvent::FaceRecognitionResult frr) 
        {
            Json::Value faceInfo(Json::objectValue);

            faceInfo["age"] = frr.age();
            faceInfo["gender"] = frr.gender();

            ev["faceInfo"] = faceInfo;
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

            // TODO: Delete depricated list_id and list_name. See for details https://support.axxonsoft.com/jira/browse/ACR-55032
            {
                listedInfo["listId"] = lidi.list_id();
                listedInfo["listName"] = lidi.list_name();
            }

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

        void fillExtra(Json::Value& ev, const ::google::protobuf::RepeatedPtrField < bl::events::DetectorEvent::Details>& details)
        {
            Json::Value extraArray{ Json::arrayValue };
            for (auto d : details)
            {
                if (d.has_queue_detected_result())
                {
                    const bl::events::DetectorEvent::QueueDetectedInfo& qdi = d.queue_detected_result();

                    Json::Value minValue(Json::objectValue), maxValue(Json::objectValue);
                    minValue["queueMin"] = std::to_string(qdi.queue_min());
                    maxValue["queueMax"] = std::to_string(qdi.queue_max());

                    extraArray.append(minValue);
                    extraArray.append(maxValue);
                }
            }

            if (!extraArray.empty())
                ev["extra"] = extraArray;
        }

        static std::string Convert(bl::events::DetectorEvent::AlertState value)
        {
            switch (value)
            {
            case bl::events::DetectorEvent::HAPPENED:  return "happened";
            case bl::events::DetectorEvent::BEGAN:     return "began";
            case bl::events::DetectorEvent::ENDED:     return "ended";
            case bl::events::DetectorEvent::SPECIFIED: return "specified";
            default:                                   return "unknown";
            }
        }

        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

POrmHandler CreateDetectorsHandler(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager)
{
    return POrmHandler(new CDetectorsHandler(GET_LOGGER_PTR, grpcManager));
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
