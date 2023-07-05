#include "HttpPlugin.h"
#include "SearchPlugin.h"
#include "GrpcReader.h"
#include "Constants.h"
#include "SendContext.h"

#include <CorbaHelpers/Reactor.h>

#include <vmda/vmdaC.h>

#include <json/json.h>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include <axxonsoft/bl/vmda/VMDA.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

namespace
{
    const std::uint32_t MAX_ACTIVE_VMDA_SEARCH_COUNT = 10000;

    const char* const AVDETECTOR_MASK = "AVDetector";
    const char* const OFFLINE_ANALYTICS_MASK = "OfflineAnalytics";

    const char* const QUERY_PARAMETER = "query";

    const Json::ArrayIndex PAIR_COUNT = 2;
    const Json::ArrayIndex TRANSITION_FIGURES_COUNT = 2;
    const Json::ArrayIndex ZONE_AND_LINE_FIGURES_COUNT = 1;
    const uint32_t MIN_POLY_POINT_COUNT = 3;
    const uint32_t LINE_POINT_COUNT = 2;
    const uint32_t MAX_SEARCH_COUNT = 20;
    const uint32_t MAX_DURATION_CALL = 20000; // ms = 20s

    const char* const QUERY_TYPE_PARAMETER = "queryType";
    const char* const CATEGORY_PARAMETER = "category";
    const char* const FIGURES_PARAMETER = "figures";
    const char* const SHAPE_PARAMETER = "shape";

    const char* const ZONE_QUERY = "zone";
    const char* const TRANSITION_QUERY = "transition";
    const char* const LINE_QUERY = "line";

    const char* const FACE_CATEGORY = "face";
    const char* const HUMAN_CATEGORY = "human";
    const char* const GROUP_CATEGORY = "group";
    const char* const VEHICLE_CATEGORY = "vehicle";
    const char* const ABANDONED_CATEGORY = "abandoned";

    const char* const OBJECT_PROPS_PARAMETER = "objectProperties";

    const char* const SIZE_PARAMETER = "size";
    const char* const WIDTH_PARAMETER = "width";
    const char* const HEIGHT_PARAMETER = "height";

    const char* const COLOR_PARAMETER = "color";
    const char* const HUE_PARAMETER = "hue";
    const char* const SATURATION_PARAMETER = "saturation";
    const char* const BRIGHTNESS_PARAMETER = "brightness";

    const char* const CONDITIONS_PARAMETER = "conditions";
    const char* const VELOCITY_PARAMETER = "velocity";
    const char* const DIRECTIONS_PARAMETER = "directions";
    const char* const DURATION_PARAMETER = "duration";
    const char* const COUNT_PARAMETER = "count";

    const char* const QUERY_PROPS_PARAMETER = "queryProperties";
    const char* const DIRECTION_PARAMETER = "direction";
    const char* const ACTION_PARAMETER = "action";

    const char* const ENTER_ACTION = "enter";
    const char* const EXIT_ACTION = "exit";

    const char* const LEFT_DIRECTION = "left";
    const char* const RIGHT_DIRECTION = "right";

    bool icase(unsigned char a, unsigned char b)
    {
        return std::tolower(a) == std::tolower(b);
    }

    bool icompare(std::string const& a, std::string const& b)
    {
        if (a.length() == b.length()) {
            return std::equal(b.begin(), b.end(),
                a.begin(), icase);
        }
        else {
            return false;
        }
    }
    constexpr auto DefaultVMDAServiceTimeout = std::chrono::minutes(10);
    using ExecuteQuery_t = NWebGrpc::AsyncStreamReader < bl::vmda::VMDAService, bl::vmda::ExecuteQueryRequest,
        bl::vmda::ExecuteQueryResponse>;

    struct VMDASearchContext : public ISearchContext
    {
        DECLARE_LOGGER_HOLDER;
        VMDASearchContext(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
            const std::string& hostName, const std::vector<std::string>& detectors, std::string query, std::string beginTime, std::string endTime)
            : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_grpcManager(grpcManager)
            , m_metaCredentials(metaCredentials)
            , m_hostName(hostName)
            , m_detectors(detectors)
            , m_query(query)
            , m_beginTime(beginTime)
            , m_endTime(endTime)
            , m_intervals(Json::arrayValue)
            , m_errorOccurred(false)
            , m_searchStopped(false)
            , m_searchFinished(false)
            , m_searchCount(detectors.size())
        {
            INIT_LOGGER_HOLDER;
        }

        ~VMDASearchContext()
        {
            _dbg_ << "VMDA search context destroyed";
        }

        void StartSearch()
        {
            _dbg_ << "Starting VMDA search...";
            if (m_reactor)
            {
                auto it1 = m_detectors.begin(), it2 = m_detectors.end();
                for (; it1 != it2; ++it1)
                {
                    _dbg_ << "Start search for " << getVmdaEntryPoint(*it1);
                    m_reactor->GetIO().post(std::bind(&VMDASearchContext::SearchTask, shared_from_base<VMDASearchContext>(), getVmdaEntryPoint(*it1)));
                }
            }
        }

        void StopSearch()
        {
            _dbg_ << "Stopping VMDA search...";
            m_searchStopped = true;

            std::lock_guard<std::mutex> l(m_readerMutex);
            auto it1 = m_readersWeak.begin(), it2 = m_readersWeak.end();
            for (; it1 != it2; ++it1)
            {
                auto reader = it1->lock();
                if (reader)
                {
                    reader->asyncStop();
                }
            }
        }

        void GetResult(const PRequest req, PResponse resp, size_t offset, size_t limit) const
        {
            if (m_errorOccurred)
            {
                Error(resp, IResponse::ServiceUnavailable);
                return;
            }

            Json::Value intervals(Json::arrayValue);
            try
            {
                std::unique_lock<std::mutex> lock(m_intervalMutex);
                std::pair<Json::Value::const_iterator, Json::Value::const_iterator> its = SelectIteratorRange(m_intervals, offset, limit);
                if (its.first == m_intervals.begin() && its.second == m_intervals.end())
                    intervals = m_intervals;
                else
                {
                    for (; its.first != its.second; ++(its.first))
                        intervals.append(*(its.first));
                }
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::NotFound);
                return;
            }

            Json::Value responseObject(Json::objectValue);
            responseObject["intervals"] = intervals;

            SendResponse(req, resp, responseObject, m_searchFinished);
        }
    private:
        void SearchTask(const std::string& detectorId)
        {
            if (!m_searchStopped)
            {
                try
                {
                    auto reader = std::make_shared<ExecuteQuery_t>(GET_LOGGER_PTR, m_grpcManager,
                        m_metaCredentials, &bl::vmda::VMDAService::Stub::AsyncExecuteQuery, DefaultVMDAServiceTimeout);

                    {
                        std::lock_guard <std::mutex> l(m_readerMutex);
                        m_readersWeak.push_back(reader);
                    }

                    bl::vmda::ExecuteQueryRequest creq;
                    creq.set_access_point("hosts/" + m_hostName + "/VMDA_DB.0/Database");
                    creq.set_schema_id("vmda_schema");
                    creq.set_language("EVENT_BASIC");
                    creq.set_camera_id(detectorId);
                    creq.set_dt_posix_start_time(m_beginTime);
                    creq.set_dt_posix_end_time(m_endTime);
                    creq.set_query(m_query);

                    auto obj = std::weak_ptr<VMDASearchContext>(shared_from_base<VMDASearchContext>());

                    reader->asyncRequest(creq, [obj](const bl::vmda::ExecuteQueryResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
                    {
                        std::shared_ptr<VMDASearchContext> owner = obj.lock();
                        if (owner)
                        {
                            owner->processResult(res, status, grpcStatus);
                        }
                    });

                    return;
                }
                catch (const std::exception& e)
                {
                    m_errorOccurred = true;
                    _err_ << "Vmda search error: " << e.what();
                }
            }

            m_searchFinished = true;

            if (this->m_done)
                this->m_done();
        }

        void processResult(const bl::vmda::ExecuteQueryResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                m_errorOccurred = true;
                _err_ << "Vmda search error: ";
            }
            else
            {
                _dbg_ << "Found " << res.intervals().size() << " items. Next iteration : " << (NWebGrpc::_PROGRESS == status) << ". Status: " << status;

                std::unique_lock<std::mutex> lock(m_intervalMutex);
                for (const auto& interval : res.intervals())
                {
                    Json::Value obj(Json::objectValue);
                    obj["startTime"] = interval.limit().begin_time();
                    obj["endTime"] = interval.limit().end_time();
                    obj["origin"] = res.origin();

                    obj["positions"] = Json::Value(Json::arrayValue);

                    for (const auto& value : interval.objects())
                    {
                        Json::Value rect(Json::objectValue);
                        rect["left"] = value.left();
                        rect["top"] = value.top();
                        rect["right"] = value.right();
                        rect["bottom"] = value.bottom();

                        obj["positions"].append(rect);
                    }

                    m_intervals.append(obj);
                }
            }

            if (NWebGrpc::_PROGRESS != status)
            {
                if (0 == --m_searchCount)
                {
                    _log_ << "Search finished";
                    m_searchFinished = true;
                }

                if (m_searchFinished && this->m_done)  
                    this->m_done();
            }
        }

        std::string getVmdaEntryPoint(const std::string& entryPoint)
        {
            size_t pos = entryPoint.find(OFFLINE_ANALYTICS_MASK);
            if (std::string::npos == pos)
            {
                pos = entryPoint.find(AVDETECTOR_MASK);
            }
            return pos != std::string::npos ? entryPoint.substr(pos) : "";
        }

        NCorbaHelpers::PReactor m_reactor;

        const NWebGrpc::PGrpcManager m_grpcManager;
        NGrpcHelpers::PCredentials m_metaCredentials;
        const std::string m_hostName;
        const std::vector<std::string> m_detectors;
        const std::string m_query;
        const std::string m_beginTime;
        const std::string m_endTime;

        mutable std::mutex m_readerMutex;
        typedef std::weak_ptr<ExecuteQuery_t> WExecuteQuery_t;
        std::vector< WExecuteQuery_t> m_readersWeak;

        mutable std::mutex m_intervalMutex;
        Json::Value m_intervals;

        std::atomic<bool> m_errorOccurred;
        std::atomic<bool> m_searchStopped;
        std::atomic<bool> m_searchFinished;
        std::atomic<std::uint32_t> m_searchCount;
    };

    class CVmdaSearchContentImpl : public CSearchPlugin<vmda::Database>
    {
        enum EQueryType
        {
            EZONE,
            ETRANSITION,
            ELINE
        };

        enum EAction
        {
            EENTER,
            EEXIT
        };

        enum ELineCrossing
        {
            ELEFT_RIGHT,
            ERIGHT_LEFT
        };

    public:
        CVmdaSearchContentImpl(NCorbaHelpers::IContainer *c, const NWebGrpc::PGrpcManager grpcManager)
            : CSearchPlugin(c)
            , m_grpcManager(grpcManager)
        {
            this->m_maxActiveCount = MAX_ACTIVE_VMDA_SEARCH_COUNT;
        }

        PSearchContext CreateSearchContext(const NHttp::PRequest req, DB_var db, const std::string& hostName, const std::vector<std::string>& origins,
            Json::Value& data, boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending)
        {
            if (data.isNull())
            {
                std::vector<uint8_t> body(req->GetBody());
                std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

                Json::CharReaderBuilder reader;
                std::string err;
                std::istringstream is(bodyContent);
                if (!bodyContent.empty() && !Json::parseFromStream(reader, is, &data, &err))
                {
                    _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                    return PSearchContext();
                }
            }

            try
            {
                std::string query(ParseParameter(data, QUERY_PARAMETER, std::string()));
                if (!query.empty())
                {
                    _dbg_ << "We work with raw query";
                }
                else
                {
                    query.assign(CreateQuery(db, data));
                }

                _dbg_ << "VMDA query: " << query;

                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                return PSearchContext(new VMDASearchContext(GET_LOGGER_PTR, m_grpcManager, metaCredentials, hostName,
                    origins, std::move(query),
                    boost::posix_time::to_iso_string(beginTime),
                    boost::posix_time::to_iso_string(endTime)));
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << e._info();
            }
            return PSearchContext();
        }

        std::string CreateQuery(DB_var db, Json::Value& data)
        {
            vmda::Queries::QueryConstructor_var queryBuilder = db->GetQueryConstructor();
            if (CORBA::is_nil(queryBuilder))
            {
                _err_ << "Can not create query constructor instance";
                return std::string();
            }

            vmda::Queries::QueryDecorator_var queryDecorator = queryBuilder->CreateQuery();
            if (CORBA::is_nil(queryDecorator))
            {
                _err_ << "Can not create query decorator object";
                return std::string();
            }
              
            EQueryType queryType;
            std::vector<vmda::Queries::Points> polygons;

            if (!data.isNull())
            {
                queryType = GetQueryType(ParseParameter(data, QUERY_TYPE_PARAMETER, std::string()));
                ParseFigures(data, queryBuilder.in(), polygons);
            }
            else
            {
                _dbg_ << "Query does not contain query type and figures. Search will result all tracks.";

                // To return all traks we should search in zone of full frame.
                queryType = EZONE;
                vmda::Queries::Points poly;
                poly.length(4);

                // Full frame rectangle.
                poly[0] = vmda::Queries::Point{ 0.0, 0.0 };
                poly[1] = vmda::Queries::Point{ 1.0, 0.0 };
                poly[2] = vmda::Queries::Point{ 1.0, 1.0 };
                poly[3] = vmda::Queries::Point{ 0.0, 1.0 };
                polygons.push_back(poly);
            }

            Json::Value queryProps = data[QUERY_PROPS_PARAMETER];

            CreateQuery(queryType, queryProps, polygons, queryBuilder.in(), queryDecorator.in());
            SetObjectProperties(data, queryDecorator.in());
            SetConditions(data, queryDecorator);

            return queryDecorator->GetResult();
        }

        EQueryType GetQueryType(const std::string& queryType)
        {
            if (icompare(ZONE_QUERY, queryType))
                return EZONE;
            else if (icompare(TRANSITION_QUERY, queryType))
                return ETRANSITION;
            else if (icompare(LINE_QUERY, queryType))
                return ELINE;
            else
                throw std::invalid_argument("Unknown query type");
        }

        void SetQueryCategory(const Json::Value& data, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (!data.isMember(CATEGORY_PARAMETER))
                return;
            Json::Value categories = data[CATEGORY_PARAMETER];
            if (!categories.isArray())
                throw std::invalid_argument("Category must be array member");

            CORBA::Long categoryMask = 0;
            Json::ArrayIndex count = categories.size();
            for (Json::ArrayIndex i = 0; i < count; ++i)
                categoryMask |= ParseCategory(categories[i], queryDecorator);

            queryDecorator->ObjectCategory(categoryMask);
            if (0 != categoryMask)
                queryDecorator->ObjectBehaviour(vmda::Queries::Behaviour_Moving);
        }

        CORBA::Long ParseCategory(const Json::Value& cat, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            std::string catType(cat.asString());

            if (icompare(FACE_CATEGORY, catType))
                return vmda::Queries::Category_Face;
            else if (icompare(HUMAN_CATEGORY, catType))
                return vmda::Queries::Category_Human;
            else if (icompare(GROUP_CATEGORY, catType))
                return vmda::Queries::Category_Group;
            else if (icompare(VEHICLE_CATEGORY, catType))
                return vmda::Queries::Category_Vehicle;
            else if (icompare(ABANDONED_CATEGORY, catType))
            {
                queryDecorator->ObjectBehaviour(vmda::Queries::Behaviour_Abandoned);
                return 0;
            }
            else
                return 0;
        }

        void ParseFigures(const Json::Value& data, vmda::Queries::QueryConstructor_ptr queryBuilder,
            std::vector<vmda::Queries::Points>& polygons)
        {
            if (!data.isMember(FIGURES_PARAMETER))
                throw std::invalid_argument("Figures does not set for vmda query");
            Json::Value figures = data[FIGURES_PARAMETER];
            if (!figures.isArray())
                throw std::invalid_argument("Figures must be array member");

            Json::ArrayIndex count = figures.size();

            if (0 == count)
                throw std::invalid_argument("VMDA query must contain one figure at least");

            for (Json::ArrayIndex i = 0; i < count; ++i)
                polygons.push_back(ParseFigure(figures[i]));
        }

        vmda::Queries::Points ParseFigure(const Json::Value& fig)
        {
            if (!fig.isMember(SHAPE_PARAMETER))
                throw std::invalid_argument("Figure does not contains polygon information");
            Json::Value shape = fig[SHAPE_PARAMETER];
            if (!shape.isArray())
                throw std::invalid_argument("Shape must be array member");

            Json::ArrayIndex count = shape.size();
            vmda::Queries::Points poly;
            poly.length(count);
            for (Json::ArrayIndex i = 0; i < count; ++i)
            {
                std::pair<double, double> pair = ParsePair(shape[i]);
                vmda::Queries::Point p{ pair.first, pair.second };
                poly[i] = p;
            }
            return poly;
        }

        void CreateQuery(EQueryType queryType, const Json::Value& queryProps,
            const std::vector<vmda::Queries::Points>& polygons,
            vmda::Queries::QueryConstructor_ptr queryBuilder,
            vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            size_t polyCount = polygons.size();
            if (EZONE == queryType)
            {
                if (ZONE_AND_LINE_FIGURES_COUNT != polyCount)
                    throw std::invalid_argument("Zone must contains one polygon exactly");
                if (MIN_POLY_POINT_COUNT > polygons[0].length())
                    throw std::invalid_argument("Polygon must contains three points as minimum");

                vmda::Queries::Figure_var figure = queryBuilder->CreatePolygon(polygons[0]);

                if (queryProps.isNull())
                    queryDecorator->Position(figure);
                else
                {
                    if (queryProps.isMember(ACTION_PARAMETER))
                    {
                        EAction action = ParseAction(ParseParameter(queryProps, ACTION_PARAMETER, std::string()));
                        if (EENTER == action)
                            queryDecorator->Transition2(queryBuilder->OuterFigure(figure), figure);
                        else if (EEXIT == action)
                            queryDecorator->Transition2(figure, queryBuilder->OuterFigure(figure));
                    }
                }
            }
            else if (ETRANSITION == queryType)
            {
                if (TRANSITION_FIGURES_COUNT != polyCount)
                    throw std::invalid_argument("Zone must contains one polygon exactly");
                if (MIN_POLY_POINT_COUNT > polygons[0].length() || MIN_POLY_POINT_COUNT > polygons[1].length())
                    throw std::invalid_argument("Polygon must contains three points as minimum");

                ELineCrossing lc = ELEFT_RIGHT;
                if (!queryProps.isNull())
                {
                    if (queryProps.isMember(DIRECTION_PARAMETER))
                    {
                        Json::Value dir = queryProps[DIRECTION_PARAMETER];
                        if (queryProps[DIRECTION_PARAMETER].isString())
                        {
                            if (ERIGHT_LEFT == ParseDirection(ParseParameter(queryProps, DIRECTION_PARAMETER, std::string())))
                                lc = ERIGHT_LEFT;
                        }
                    }
                }

                if (ELEFT_RIGHT == lc)
                    queryDecorator->Transition(queryBuilder->CreatePolygon(polygons[0]), queryBuilder->CreatePolygon(polygons[1]));
                else
                    queryDecorator->Transition(queryBuilder->CreatePolygon(polygons[1]), queryBuilder->CreatePolygon(polygons[0]));

            }
            else if (ELINE == queryType)
            {
                if (ZONE_AND_LINE_FIGURES_COUNT != polyCount)
                    throw std::invalid_argument("Zone must contains one polygon exactly");
                if (LINE_POINT_COUNT != polygons[0].length())
                    throw std::invalid_argument("Line must contains two points exactly");

                CORBA::Octet direction = vmda::Queries::Direction_Left | vmda::Queries::Direction_Right;
                if (!queryProps.isNull())
                {
                    if (queryProps.isMember(DIRECTION_PARAMETER))
                    {
                        Json::Value dir = queryProps[DIRECTION_PARAMETER];
                        if (queryProps[DIRECTION_PARAMETER].isString())
                        {
                            ELineCrossing lc = ParseDirection(ParseParameter(queryProps, DIRECTION_PARAMETER, std::string()));
                            direction = lc == ERIGHT_LEFT ? vmda::Queries::Direction_Left : vmda::Queries::Direction_Right;
                        }
                    }
                }

                queryDecorator->LineCrossing(polygons[0][0], polygons[0][1], direction);
            }
        }

        void SetObjectProperties(const Json::Value& data, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (data.isMember(OBJECT_PROPS_PARAMETER))
            {
                Json::Value objProps = data[OBJECT_PROPS_PARAMETER];
                SetQueryCategory(objProps, queryDecorator);
                SetSize(objProps, queryDecorator);
                SetColor(objProps, queryDecorator);                
            }
        }

        void SetSize(const Json::Value& props, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (props.isMember(SIZE_PARAMETER))
            {
                Json::Value size = props[SIZE_PARAMETER];
                if (!size.isMember(WIDTH_PARAMETER) && !size.isMember(HEIGHT_PARAMETER))
                    throw std::invalid_argument("Size must contain either width, height, or both");

                if (size.isMember(WIDTH_PARAMETER))
                {
                    Json::Value width = size[WIDTH_PARAMETER];
                    std::pair<double, double> w = ParsePair(width);
                    queryDecorator->Width(w.first, w.second);
                }

                if (size.isMember(HEIGHT_PARAMETER))
                {
                    Json::Value height = size[HEIGHT_PARAMETER];
                    std::pair<double, double> h = ParsePair(height);
                    queryDecorator->Height(h.first, h.second);
                }
            }
        }

        void SetColor(const Json::Value& props, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (props.isMember(COLOR_PARAMETER))
            {
                Json::Value color = props[COLOR_PARAMETER];
                if (!color.isMember(HUE_PARAMETER) || !color.isMember(SATURATION_PARAMETER) || !color.isMember(BRIGHTNESS_PARAMETER))
                    throw std::invalid_argument("Color must contains hue, saturation and brightness properties");

                Json::Value hue = color[HUE_PARAMETER];
                std::pair<double, double> h = ParsePair(hue);
                queryDecorator->ColorHue(h.first, h.second);

                Json::Value saturation = color[SATURATION_PARAMETER];
                std::pair<double, double> s = ParsePair(saturation);
                queryDecorator->ColorSaturation(s.first, s.second);

                Json::Value brightness = color[BRIGHTNESS_PARAMETER];
                std::pair<double, double> b = ParsePair(brightness);
                queryDecorator->ColorValue(b.first, b.second);
            }
        }

        void SetConditions(const Json::Value& data, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (data.isMember(CONDITIONS_PARAMETER))
            {
                Json::Value cond = data[CONDITIONS_PARAMETER];
                SetVelocity(cond, queryDecorator);
                SetDirections(cond, queryDecorator);
                SetDuration(cond, queryDecorator);
                SetEventCount(cond, queryDecorator);
            }
        }

        void SetVelocity(const Json::Value& cond, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (cond.isMember(VELOCITY_PARAMETER))
            {
                Json::Value vel = cond[VELOCITY_PARAMETER];
                std::pair<double, double> v = ParsePair(vel);
                queryDecorator->Velocity(v.first, v.second);
            }
        }

        void SetDirections(const Json::Value& cond, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (cond.isMember(DIRECTIONS_PARAMETER))
            {
                Json::Value dirs = cond[DIRECTIONS_PARAMETER];
                if (!dirs.isArray())
                    throw std::invalid_argument("Directions must be array member");

                Json::ArrayIndex count = dirs.size();

                for (Json::ArrayIndex i = 0; i < count; ++i)
                {
                    std::pair<double, double> d = ParsePair(dirs[i]);
                    queryDecorator->Direction(d.first, d.second);
                }
            }
        }

        void SetDuration(const Json::Value& cond, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (cond.isMember(DURATION_PARAMETER))
            {
                queryDecorator->Duration(ParseParameter(cond, DURATION_PARAMETER, 0.0));
            }
        }

        void SetEventCount(const Json::Value& cond, vmda::Queries::QueryDecorator_ptr queryDecorator)
        {
            if (cond.isMember(COUNT_PARAMETER))
            {
                queryDecorator->EventCount(ParseParameter(cond, COUNT_PARAMETER, 1u), 0);
            }
        }

        std::pair<double, double> ParsePair(const Json::Value& p)
        {
            if (!p.isArray())
                throw std::invalid_argument("Pair must be array");
            Json::ArrayIndex count = p.size();
            if (PAIR_COUNT != count)
                throw std::invalid_argument("Pair must contains 2 members");

            double min = p[0].asDouble();
            double max = p[1].asDouble();

            return std::make_pair(min, max);
        }

        EAction ParseAction(std::string action)
        {
            if (icompare(ENTER_ACTION, action))
                return EENTER;
            else if (icompare(EXIT_ACTION, action))
                return EEXIT;
            else
                throw std::invalid_argument("Valid values for parameter \'action\' are \'enter\' and \'exit\'");
        }

        ELineCrossing ParseDirection(std::string direction)
        {
            if (icompare(LEFT_DIRECTION, direction))
                return ERIGHT_LEFT;
            else if (icompare(RIGHT_DIRECTION, direction))
                return ELEFT_RIGHT;
            else
                throw std::invalid_argument("Valid values for parameter \'direction\' are \'left\' and \'right\'");
        }

        template <typename TRes, typename TParam>
        TRes ParseParameter(const Json::Value& data, TParam param, TRes defaultValue)
        {
            if (!data.isMember(param))
                return defaultValue;
            return getValue<TRes>(data[param]);
        }

        template <typename T>
        T getValue(const Json::Value& data);

    private:
        const NWebGrpc::PGrpcManager m_grpcManager;
    };

    template <typename T>
    T CVmdaSearchContentImpl::getValue(const Json::Value& data)
    {
        throw std::invalid_argument("Unsupported type");
    }

    template <>
    std::string CVmdaSearchContentImpl::getValue<std::string>(const Json::Value& data)
    {
        return data.asString();
    }

    template <>
    double CVmdaSearchContentImpl::getValue<double>(const Json::Value& data)
    {
        return data.asDouble();
    }

    template <>
    unsigned int CVmdaSearchContentImpl::getValue<unsigned int>(const Json::Value& data)
    {
        return data.asUInt();
    }
}

namespace NHttp
{
    IServlet* CreateVmdaSearchServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CVmdaSearchContentImpl(c, grpcManager);
    }
}
