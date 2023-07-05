#include "Constants.h"
#include "HttpPlugin.h"
#include "SearchPlugin.h"
#include "RegexUtility.h"

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Unicode.h>
#include <Crypto/Crypto.h>

#include <boost/asio.hpp>

#include <NativeBLClient/NativeBLClient.h>
#include <GrpcHelpers/GrpcSpawnClient2.h>
#include <GrpcHelpers/GrpcClient.h>
#include <GrpcHelpers/GrpcClientBasicResponseHandler.h>
#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;

namespace
{
    const char* const IMAGE_PARAMETER = "image";

    const uint32_t DEFAULT_PORTION_SIZE = 1000;

    std::chrono::milliseconds DEQUEUE_TIMEOUT = std::chrono::milliseconds(15 * 1000);

    struct SFaceEvent
    {
        struct Position
        {
            double left;
            double top;
            double right;
            double bottom;
        };

        std::string timestamp;
        std::string origin;
        std::string offlineAnalyticsSource;
        float score;
        Position position;

        SFaceEvent()
            : score(0)
        {}

        bool operator < (const SFaceEvent& fe) const
        {
            return score > fe.score;
        }
    };
    typedef std::multiset<SFaceEvent> TFaceEvents;

    template<typename TFaceEventCont>
    struct FaceSearchContext : public ISearchContext
    {
        DECLARE_LOGGER_HOLDER;
        FaceSearchContext(DECLARE_LOGGER_ARG, size_t bufSize, CORBA::Octet* buf, ORM::AsipDatabase_var orm, NGrpcHelpers::PBoundGrpcClient nativeBLClient,
            ORM::StringSeq& origins, ORM::TimeRange& range, float accuracy)
            : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_image(bufSize, bufSize, buf, 1)
            , m_origins(origins)
            , m_range(range)
            , m_accuracy(accuracy)
            , m_haveImage((buf != nullptr) ? true : false)
            , m_errorOccurred(false)
            , m_searchStopped(false)
            , m_searchFinished(false)
            , m_nativeBLClient(nativeBLClient)
        {
            INIT_LOGGER_HOLDER;
        }

        ~FaceSearchContext()
        {
            _dbg_ << "Face search context destroyed";
        }

        void StartSearch()
        {
            _dbg_ << "Starting face search...";
            if (m_reactor)
            {
                m_reactor->GetIO().post(std::bind(&FaceSearchContext::SearchTask, shared_from_base<FaceSearchContext>(), DEFAULT_PORTION_SIZE, 0U));
            }
        }

        void StopSearch()
        {
            _dbg_ << "Stopping face search...";
            m_searchStopped = true;
        }

        void GetResult(const PRequest req, PResponse resp, size_t offset, size_t limit) const
        {
            if (m_errorOccurred)
            {
                Error(resp, IResponse::ServiceUnavailable);
                return;
            }

            typename TFaceEventCont::const_iterator it1, it2;
            try
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                std::pair<typename TFaceEventCont::const_iterator, typename TFaceEventCont::const_iterator> its = SelectIteratorRange(m_events, offset, limit);
                it1 = its.first, it2 = its.second;
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::NotFound);
                return;
            }

            Json::Value responseObject(Json::objectValue);
            responseObject["events"] = Json::Value(Json::arrayValue);
            responseObject["more"] = offset + limit < m_events.size();

            for (; it1 != it2; ++it1)
            {
                Json::Value ev(Json::objectValue);

                getResultEvent(*it1, ev);
                responseObject["events"].append(ev);
            }

            SendResponse(req, resp, responseObject, m_searchFinished);
        }
    private:
        void SearchTask(CORBA::ULong limit, CORBA::ULong offset)
        {
            if (!m_searchStopped)
            {
                _dbg_ << "Search continues from position " << offset;

                try
                {
                    namespace abe = axxonsoft::bl::events;

                    grpc::Status status;

                    if (m_haveImage)
                    {
                        abe::FindSimilarObjectsRequest request;
                        auto range = request.mutable_range();
                        range->set_begin_time(m_range.Begin.value.in());
                        range->set_end_time(m_range.End.value.in());

                        request.set_session(0U);
                        request.set_is_face(true);
                        request.set_minimal_score(m_accuracy);

                        const CORBA::ULong length = m_origins.length();
                        for (CORBA::ULong i = 0; i < length; ++i)
                        {
                            *request.add_origin_ids() = m_origins[i];
                        }

                        request.set_jpeg_image(std::string(reinterpret_cast<const char*>(m_image.get_buffer()), m_image.length()));
                        request.set_limit(limit);
                        request.set_offset(offset);

                        NGrpcHelpers::SimpleLambdaResponseHandler<abe::FindSimilarObjectsResponse> handler(
                            [this, &offset](std::shared_ptr<abe::FindSimilarObjectsResponse> const& response)
                            {
                                TFaceEventCont events;
                                this->ConvertFaceEvents(response->items(), events);
                                this->appendEvents(events);
                                offset += response->offset();
                                return true;
                            });

                        auto future = handler.getStatusFuture();

                        NGrpcHelpers::AsyncServerStreamingGrpcCall<abe::EventHistoryService>(
                            GET_THIS_LOGGER_PTR,
                            m_nativeBLClient,
                            &axxonsoft::bl::events::EventHistoryService::Stub::AsyncFindSimilarObjects,
                            &handler,
                            request,
                            DEQUEUE_TIMEOUT
                            );

                        status = future.get();
                    }
                    else
                    {
                        abe::ReadEventsRequest request;

                        auto range = request.mutable_range();
                        range->set_begin_time(m_range.Begin.value.in());
                        range->set_end_time(m_range.End.value.in());

                        abe::SearchFilterArray* filters = request.mutable_filters();
                        for (CORBA::ULong i = 0; i < m_origins.length(); ++i)
                        {
                            abe::SearchFilter* f = filters->add_filters();
                            f->set_type(abe::ET_DetectorEvent);
                            *f->add_subjects() = m_origins[i].in();
                            *f->add_texts() = "faceAppeared";
                        }

                        request.set_limit(limit);
                        request.set_offset(offset);

                        NGrpcHelpers::SimpleLambdaResponseHandler<abe::ReadEventsResponse> handler(
                            [this, &offset](std::shared_ptr<abe::ReadEventsResponse> const& response)
                            {
                                TFaceEventCont events;
                                this->ConvertFaceEvents(response->items(), events);
                                this->appendEvents(events);
                                offset += response->items_size();
                                return true;
                            });

                        auto future = handler.getStatusFuture();

                        NGrpcHelpers::AsyncServerStreamingGrpcCall<abe::EventHistoryService>(
                            GET_THIS_LOGGER_PTR,
                            m_nativeBLClient,
                            &axxonsoft::bl::events::EventHistoryService::Stub::AsyncReadEvents,
                            &handler,
                            request,
                            DEQUEUE_TIMEOUT
                            );

                        status = future.get();
                    }

                    if (!status.ok())
                        m_errorOccurred = true;
                }
                catch (const ORM::JsonEventInvalidJson& e)
                {
                    m_errorOccurred = true;
                    _err_ << "Face search error, invalid json: " << e.Message.in();
                }
                catch (const CORBA::Exception& e)
                {
                    m_errorOccurred = true;
                    _err_ << "Face search error: " << e._info();
                }
                catch (const std::exception& e)
                {
                    m_errorOccurred = true;
                    _err_ << "Face search error: " << e.what();
                }
            }
            m_searchFinished = true;

            if (this->m_done)
                this->m_done();
        }

        void appendEvents(const TFaceEvents& events)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_events.insert(events.begin(), events.end());
        }

        void appendEvents(const Json::Value& events)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            for (const auto& item : events)
                m_events.append(item);
        }

        void getResultEvent(const SFaceEvent& value, Json::Value& ev) const
        {
            ev["timestamp"] = value.timestamp;
            ev["origin"] = value.origin;
            ev["accuracy"] = value.score;
            ev["offlineAnalyticsSource"] = value.offlineAnalyticsSource;

            Json::Value rect(Json::objectValue);
            rect["left"] = value.position.left;
            rect["top"] = value.position.top;
            rect["right"] = value.position.right;
            rect["bottom"] = value.position.bottom;

            ev["position"] = rect;
        }

        void getResultEvent(const Json::Value& value, Json::Value& ev) const
        {
            for (const auto& item : value.getMemberNames())
                ev[item] = value.get(item.c_str(), value);
        }

        void addDetectorEvent(const axxonsoft::bl::events::DetectorEvent de, SFaceEvent& faceEvent, TFaceEvents& events)
        {
            faceEvent.timestamp = de.timestamp();
            faceEvent.origin = de.origin_ext().access_point();
            faceEvent.offlineAnalyticsSource = de.offline_analytics_source();

            for (const auto& d : de.details())
            {
                if (d.has_rectangle())
                {
                    const auto& r = d.rectangle();

                    faceEvent.position.left = r.x();
                    faceEvent.position.top = r.y();
                    faceEvent.position.right = r.w() + r.x();
                    faceEvent.position.bottom = r.h() + r.y();
                    break;
                }
            }

            events.insert(faceEvent);
        }

        void addFullDetectorEvent(const axxonsoft::bl::events::DetectorEvent de, Json::Value& faceEvent, Json::Value& events)
        {
            faceEvent["timestamp"] = de.timestamp();

            for (const auto& d : de.details())
            {
                if (d.has_rectangle())
                {
                    const auto& r = d.rectangle();
                    Json::Value rectangle{ Json::arrayValue };
                    rectangle.append(r.x());
                    rectangle.append(r.y());
                    rectangle.append(r.w() + r.x());
                    rectangle.append(r.h() + r.y());
                    rectangle.append(r.index());
                    continue;
                }

                if (d.has_face_recognition_result())
                {
                    const auto& frr = d.face_recognition_result();
                    faceEvent["BeginTime"] = frr.begin_time();
                    faceEvent["BestQuality"] = frr.best_quality();
                    faceEvent["FaceId"] = de.guid();
                    faceEvent["detector_type"] = de.event_type();
                    faceEvent["origin_id"] = de.origin_ext().access_point();
                    faceEvent["phase"] = 0;
                    faceEvent["Age"] = frr.age();
                    faceEvent["Gender"] = frr.gender() == axxonsoft::bl::events::DetectorEvent_FaceRecognitionResult_EGender_UNKNOWN
                        ? 0
                        : (frr.gender() == axxonsoft::bl::events::DetectorEvent_FaceRecognitionResult_EGender_MALE)
                        ? 1
                        : 2
                        ;
                    continue;
                }
            }

            events.append(faceEvent);
        }

        void ConvertFaceEvents(const ::google::protobuf::RepeatedPtrField<::axxonsoft::bl::events::Event>& es, TFaceEvents& events)
        {
            for (const auto& ev : es)
            {
                axxonsoft::bl::events::DetectorEvent de;
                if (ev.body().UnpackTo(&de))
                {
                    SFaceEvent faceEvent;
                    addDetectorEvent(de, faceEvent, events);
                }
            }
        }

        void ConvertFaceEvents(const ::google::protobuf::RepeatedPtrField<::axxonsoft::bl::events::Event>& es, Json::Value& events)
        {
            for (const auto& ev : es)
            {
                axxonsoft::bl::events::DetectorEvent de;
                if (ev.body().UnpackTo(&de))
                {
                    Json::Value faceEvent;
                    addFullDetectorEvent(de, faceEvent, events);
                }
            }
        }

        void ConvertFaceEvents(const ::google::protobuf::RepeatedPtrField<::axxonsoft::bl::events::SimilarObject>& es, TFaceEvents& events)
        {
            for (const auto& similarObject : es)
            {
                SFaceEvent faceEvent;
                faceEvent.score = similarObject.score();

                addDetectorEvent(similarObject.event(), faceEvent, events);
            }
        }

        void ConvertFaceEvents(const ::google::protobuf::RepeatedPtrField<::axxonsoft::bl::events::SimilarObject>& es, Json::Value& events)
        {
            for (const auto& similarObject : es)
            {              
                Json::Value faceEvent;
                faceEvent["accuracy"] = similarObject.score();

                addFullDetectorEvent(similarObject.event(), faceEvent, events);
            }
        }

        NCorbaHelpers::PReactor m_reactor;

        ORM::OctetSeq m_image;
        ORM::StringSeq m_origins;
        ORM::TimeRange m_range;
        float m_accuracy;
        bool m_haveImage;

        mutable std::mutex m_mutex;
        TFaceEventCont m_events;

        std::atomic<bool> m_errorOccurred;
        std::atomic<bool> m_searchStopped;
        std::atomic<bool> m_searchFinished;

        NGrpcHelpers::PBoundGrpcClient m_nativeBLClient;
    };

    typedef FaceSearchContext<TFaceEvents> SortedFaceSearchContext;
    typedef FaceSearchContext<Json::Value> ReplicationFaceSearchContext;

    class CFaceSearchContentImpl : public CSearchPlugin<ORM::AsipDatabase>
    {
    public:
        CFaceSearchContentImpl(NCorbaHelpers::IContainer *c)
            : CSearchPlugin(c)
        {
            m_grpcClientBase = NGrpcHelpers::CreateGrpcClientBase(GET_LOGGER_PTR, NCorbaHelpers::GetReactorInstanceShared()->GetIO());
            m_nativeBLClient = NNativeBL::Client::GetLocalBL(GET_LOGGER_PTR, m_grpcClientBase, "HttpPlugin/FaceSearch");
            m_grpcClientBase->Start();
        }

        ~CFaceSearchContentImpl()
        {
            if (m_nativeBLClient)
                m_nativeBLClient->Cancel();

            if (m_grpcClientBase)
            {
                m_grpcClientBase->Shutdown();
                m_grpcClientBase.reset();
            }
        }

    private:
        PSearchContext CreateSearchContext(const NHttp::PRequest req, DB_var orm, const std::string&, const std::vector<std::string>& orgs,
            Json::Value& data, boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending)
        {
            TParams params;
            if (!ParseParams(req->GetQuery(), params))
            {
                return PSearchContext();
            }

            float accuracy = DEFAULT_ACCURACY;
            GetParam<float>(params, ACCURACY_PARAM, accuracy, DEFAULT_ACCURACY);

            std::string result_type;
            GetParam<std::string>(params, RESULT_TYPE_PARAM, result_type, std::string());

            size_t sz = 0;
            CORBA::Octet* buf = nullptr;
            if (data.isNull())
            {
                std::vector<uint8_t> body = req->GetBody();
                sz = body.size();
                if (0 != sz)
                {
                    buf = ORM::OctetSeq::allocbuf(sz);
                    std::copy(body.begin(), body.end(), buf);
                }
            }
            else 
            {
                if (data.isMember(IMAGE_PARAMETER))
                {
                    std::string base64image(data[IMAGE_PARAMETER].asString());
                    std::string image(NCrypto::FromBase64(base64image.c_str(), base64image.size()).value_or(""));

                    sz = image.size();
                    buf = ORM::OctetSeq::allocbuf(sz);
                    std::copy(image.begin(), image.end(), buf);
                }

                if (data.isMember(ACCURACY_PARAM))
                {
                    accuracy = data[ACCURACY_PARAM].asFloat();
                }
            }

            if (buf == nullptr || sz == 0)
            {
                _dbg_ << "Query does not contain image data. Search will result all faces.";

                // To proceed all faces, accuracy should be zero.
                accuracy = 0;
            }

            if (accuracy < MIN_ACCURACY || accuracy > MAX_ACCURACY)
                accuracy = DEFAULT_ACCURACY;

            ORM::TimeRange range;
            ORM::StringSeq origins;
            PrepareAsipRequest(orgs, beginTime, endTime, origins, range);

            return RESULT_TYPE_FULL == result_type ? 
                PSearchContext(new ReplicationFaceSearchContext(GET_LOGGER_PTR, sz, buf, orm, m_nativeBLClient, origins, range, accuracy)) :
                PSearchContext(new SortedFaceSearchContext(GET_LOGGER_PTR, sz, buf, orm, m_nativeBLClient, origins, range, accuracy));
        }
    private:
        NGrpcHelpers::PGrpcClientBase m_grpcClientBase;
        NGrpcHelpers::PBoundGrpcClient m_nativeBLClient;
    };
}

namespace NHttp
{
    IServlet* CreateFaceSearchServlet(NCorbaHelpers::IContainer* c)
    {
        return new CFaceSearchContentImpl(c);
    }
}
