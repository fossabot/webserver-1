#include "Constants.h"
#include "HttpPlugin.h"
#include "SearchPlugin.h"
#include "RegexUtility.h"

#include <ORM_IDL/ORM.h>

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/ResolveServant.h>

#include <boost/asio.hpp>

#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;

namespace
{
    const int MAX_RESOLVE_TIMEOUT_MS = 10000; // 10 seconds.

    const float DEFAULT_THRESHOLD = 2.0;

    const uint32_t DEFAULT_PORTION_SIZE = 1000;

    const char* const THRESHOLD_PARAM = "threshold";
    const char* const OP_PARAM = "op";

    const char* const LT_OP = "lt";
    const char* const GT_OP = "gt";

    typedef std::function<bool(float)> CompareFunction;

    bool NoOpCompare(float) { return true; }
    bool LtCompare(float a, float b) { return a < b; }
    bool GtCompare(float a, float b) { return a > b; }

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
        float score;
        Position position;

        bool operator < (const SFaceEvent& fe) const
        {
            return score > fe.score;
        }
    };
    typedef std::multiset<SFaceEvent> TFaceEvents;

    struct StrangerSearchContext : public ISearchContext
    {
        DECLARE_LOGGER_HOLDER;
        StrangerSearchContext(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* c, ORM::AsipDatabase_var orm,
            ORM::StringSeq& origins, ORM::TimeRange& range, float accuracy, CompareFunction cf)
            : m_cont(c)
            , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_orm(orm)
            , m_origins(origins)
            , m_range(range)
            , m_accuracy(accuracy)
            , m_errorOccurred(false)
            , m_searchStopped(false)
            , m_searchFinished(false)
            , m_cf(cf)
        {
            INIT_LOGGER_HOLDER;
        }

        ~StrangerSearchContext()
        {
            _dbg_ << "Stranger search context destroyed";
        }

        void StartSearch()
        {
            _dbg_ << "Starting stranger search...";
            if (m_reactor)
            {
                m_reactor->GetIO().post(std::bind(&StrangerSearchContext::SearchTask, shared_from_base<StrangerSearchContext>(), DEFAULT_PORTION_SIZE, 0));
            }
        }

        void StopSearch()
        {
            _dbg_ << "Stopping stranger search...";
            m_searchStopped = true;
        }

        void GetResult(const PRequest req, PResponse resp, size_t offset, size_t limit) const
        {
            if (m_errorOccurred)
            {
                Error(resp, IResponse::ServiceUnavailable);
                return;
            }

            TFaceEvents::const_iterator it1, it2;
            try
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                std::pair<TFaceEvents::const_iterator, TFaceEvents::const_iterator> its = SelectIteratorRange(m_events, offset, limit);
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

            for (; it1 != it2; ++it1)
            {
                Json::Value ev(Json::objectValue);
                ev["timestamp"] = it1->timestamp;
                ev["origin"] = it1->origin;
                ev["rate"] = it1->score;

                Json::Value rect(Json::objectValue);
                rect["left"] = it1->position.left;
                rect["top"] = it1->position.top;
                rect["right"] = it1->position.right;
                rect["bottom"] = it1->position.bottom;

                ev["position"] = rect;

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
                    NCorbaHelpers::PContainer c = m_cont;
                    if (!c)
                    {
                        _err_ << "the container is already dead!";
                        return;
                    }

                    ORM::ObjectSearcher_var faceSearcher = NCorbaHelpers::ResolveServant<ORM::ObjectSearcher>(c.Get(), "hosts/" + NCorbaHelpers::CEnvar::NgpNodeName() + "/ObjectSearcher.0/Searcher", MAX_RESOLVE_TIMEOUT_MS);

                    if (CORBA::is_nil(faceSearcher) || faceSearcher->GetServiceReadinessStatus() != ServiceInfo::ESR_Ready)
                    {
                        _err_ << "Face searcher is not accessible or not ready yet";
                        return;
                    }

                    auto o = offset;
                    Notification::Guid sesionID = Notification::GenerateUUID();
                    ORM::SimilarObjectSeq_var res = faceSearcher->FindStrangers(sesionID, true, m_accuracy, m_range, m_origins, limit, o);

                    if (!m_searchStopped)
                    {
                        TFaceEvents events;
                        ConvertFaceEvents(res, events);

                        {
                            std::unique_lock<std::mutex> lock(m_mutex);
                            m_events.insert(events.begin(), events.end());
                        }

                        if (res->length() > 0) // the same as 'more == true'
                        {
                            m_reactor->GetIO().post(std::bind(&StrangerSearchContext::SearchTask, shared_from_base<StrangerSearchContext>(), limit, o));
                            return;
                        }
                    }
                }
                catch (const CORBA::Exception& e)
                {
                    m_errorOccurred = true;
                    _err_ << "Stranger search error: " << e._info();
                }
            }
            m_searchFinished = true;

            if (this->m_done)
                this->m_done();
        }

        void ConvertFaceEvents(const ORM::SimilarObjectSeq& es, TFaceEvents& events)
        {
            for (CORBA::ULong i = 0; i < es.length(); ++i)
            {
                axxonsoft::bl::events::DetectorEvent de;
                if (!google::protobuf::util::JsonStringToMessage(NCorbaHelpers::ToUtf8(es[i].event.in()), &de).ok())
                {
                    _err_ << "can't deterialize face detector event";
                    return;
                }

                if (m_cf(es[i].Score))
                {
                    SFaceEvent faceEvent;
                    faceEvent.timestamp = de.timestamp();
                    faceEvent.origin = de.origin_ext().access_point();
                    faceEvent.score = es[i].Score;

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
            }
        }

        NCorbaHelpers::WPContainer m_cont;

        NCorbaHelpers::PReactor m_reactor;

        ORM::AsipDatabase_var m_orm;
        ORM::StringSeq m_origins;
        ORM::TimeRange m_range;
        float m_accuracy;

        mutable std::mutex m_mutex;
        TFaceEvents m_events;

        std::atomic<bool> m_errorOccurred;
        std::atomic<bool> m_searchStopped;
        std::atomic<bool> m_searchFinished;

        CompareFunction m_cf;
    };

    class CStrangerSearchContentImpl : public CSearchPlugin<ORM::AsipDatabase>
    {
        NCorbaHelpers::PContainerNamed m_cont;
    public:
        CStrangerSearchContentImpl(NCorbaHelpers::IContainerNamed*c)
            : CSearchPlugin(c)
            , m_cont(c, NCorbaHelpers::ShareOwnership())
        {}

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

            float threshold = 2.0;
            GetParam<float>(params, THRESHOLD_PARAM, threshold, DEFAULT_THRESHOLD);

            if (!data.isNull())
            {
                if (data.isMember(ACCURACY_PARAM))
                {
                    accuracy = data[ACCURACY_PARAM].asFloat();
                }
            }
            
            std::string op;
            GetParam<std::string>(params, OP_PARAM, op, std::string());
            CompareFunction f = std::bind(NoOpCompare, std::placeholders::_1);
            if (threshold >= MIN_ACCURACY && threshold <= MAX_ACCURACY)
                f = getCompareFunction(op, threshold);

            if (accuracy < MIN_ACCURACY || accuracy > MAX_ACCURACY)
                accuracy = DEFAULT_ACCURACY;

            ORM::TimeRange range;
            ORM::StringSeq origins;
            PrepareAsipRequest(orgs, beginTime, endTime, origins, range);

            return PSearchContext(new StrangerSearchContext(GET_LOGGER_PTR, m_cont.Get(), orm, origins, range, accuracy, f));
        }

        CompareFunction getCompareFunction(const std::string& op, float threshold)
        {
            if (LT_OP == op)
                return std::bind(LtCompare, std::placeholders::_1, threshold);
            else if (GT_OP == op)
                return std::bind(GtCompare, std::placeholders::_1, threshold);
            else
                return std::bind(NoOpCompare, std::placeholders::_1);
        }
    };
}

namespace NHttp
{
    IServlet* CreateStrangerSearchServlet(NCorbaHelpers::IContainerNamed* c)
    {
        return new CStrangerSearchContentImpl(c);
    }
}
