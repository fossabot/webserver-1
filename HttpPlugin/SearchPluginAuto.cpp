#include "SearchPlugin.h"
#include "HttpPlugin.h"
#include "RegexUtility.h"
#include "Constants.h"

#include <ORM_IDL/ORM.h>

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Unicode.h>

#include <boost/asio.hpp>
#include <boost/foreach.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/locale/utf.hpp>

#include <axxonsoft/bl/config/ConfigurationService.grpc.pb.h>
#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

namespace
{
    const uint32_t DEFAULT_PORTION_SIZE = 1000;

    const char* const PLATE_PARAMETER = "plate";

    using ListUnitsReader_t = NWebGrpc::AsyncResultReader < bl::config::ConfigurationService, bl::config::ListUnitsRequest,
        bl::config::ListUnitsResponse>;
    using PListUnitsReader_t = std::shared_ptr < ListUnitsReader_t >;

    using ListUnitsReaderCallback_t = std::function<void(const bl::config::ListUnitsResponse&, bool)>;


    struct AutoSearchContext : public ISearchContext
    {
        DECLARE_LOGGER_HOLDER;
        AutoSearchContext(DECLARE_LOGGER_ARG, ORM::AsipDatabase_var orm,
            ORM::StringSeq& origins, ORM::TimeRange& range, const std::string& plate, const std::string& result_type
            , NGrpcHelpers::PCredentials metaCredentials, const NWebGrpc::PGrpcManager grpcManager, bool descending)
            : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_orm(orm)
            , m_origins(origins)
            , m_range(range)
            , m_plate(plate)
            , m_result_type(result_type)
            , m_events(Json::arrayValue)
            , m_errorOccurred(false)
            , m_searchStopped(false)
            , m_searchFinished(false)
            , m_metaCredentials(metaCredentials)
            , m_grpcManager(grpcManager)
            , m_descending(descending)
        {
            INIT_LOGGER_HOLDER;
        }

        ~AutoSearchContext()
        {
            _dbg_ << "Auto search context destroyed";
        }

        void StartSearch() override
        {
            _dbg_ << "Starting auto search...";
            if (m_reactor/* && m_origins.length() > 0*/)
            {
                /*bl::config::ListUnitsRequest creq;

                std::string *pU = creq.add_unit_uids();
                *pU = m_origins[0].in();

                PListUnitsReader_t reader(new ListUnitsReader_t
                    (GET_LOGGER_PTR, m_grpcManager, m_metaCredentials, &bl::config::ConfigurationService::Stub::AsyncListUnits));

                ListUnitsReaderCallback_t cb = std::bind(&AutoSearchContext::sExecStartSearch,
                    std::weak_ptr<AutoSearchContext>(shared_from_base<AutoSearchContext>()), m_origins[0].in(), 
                    std::placeholders::_1, std::placeholders::_2);

                reader->asyncRequest(creq, cb);*/

                google::protobuf::util::JsonPrintOptions jOpt;
                jOpt.always_print_enums_as_ints = false;
                jOpt.always_print_primitive_fields = true;
                jOpt.preserve_proto_field_names = true;

                bl::events::SearchFilterArray filters;
                for (CORBA::ULong i = 0; i < m_origins.length(); ++i)
                {
                    bl::events::SearchFilter f;
                    f.set_type(bl::events::ET_DetectorEvent);
                    *f.add_subjects() = m_origins[i].in();

                    *filters.add_filters() = f;
                }

                std::string jsonFilters;
                google::protobuf::util::MessageToJsonString(filters, &jsonFilters, jOpt);
                std::wstring jsonFiltersW = NCorbaHelpers::FromUtf8(jsonFilters);

                std::wstring plateFullWide(NCorbaHelpers::FromUtf8(m_plate));
                boost::algorithm::to_upper(plateFullWide);
                m_reactor->GetIO().post(std::bind(&AutoSearchContext::SearchTask, shared_from_base<AutoSearchContext>(), jsonFiltersW, plateFullWide, DEFAULT_PORTION_SIZE, 0));
            }
        }

        /*void execStartSearch(const std::string country)
        {
            google::protobuf::util::JsonPrintOptions jOpt;
            jOpt.always_print_enums_as_ints = false;
            jOpt.always_print_primitive_fields = true;
            jOpt.preserve_proto_field_names = true;

            bl::events::SearchFilterArray filters;
            for (CORBA::ULong i = 0; i < m_origins.length(); ++i)
            {
                bl::events::SearchFilter f;
                f.set_type(bl::events::ET_DetectorEvent);
                *f.add_subjects() = m_origins[i].in();
                
                *filters.add_filters() = f;
            }

            std::string jsonFilters;
            google::protobuf::util::MessageToJsonString(filters, &jsonFilters, jOpt);
            std::wstring jsonFiltersW = NCorbaHelpers::FromUtf8(jsonFilters);
           
            std::wstring plateFullWide(NCorbaHelpers::FromUtf8(m_plate));
            boost::algorithm::to_upper(plateFullWide);
            m_reactor->GetIO().post(std::bind(&AutoSearchContext::SearchTask, shared_from_base<AutoSearchContext>(), jsonFiltersW, plateFullWide, DEFAULT_PORTION_SIZE, 0));
        }*/

        static std::wstring ConvertRecNumberFromLocalToLatin(const std::wstring& recNumber, const std::string country)
        {
            if (country != "ru" && country != "ua")
                return recNumber;
            std::wstring res;

            auto iNextCodePointStart = begin(recNumber);
            auto const iEnd = end(recNumber);
            std::uint32_t const U = 0x0;
            while (iNextCodePointStart != iEnd)
            {
                using namespace boost::locale::utf;
                auto iPrevCodePointStart = iNextCodePointStart;
                auto unicode_codepoint = utf_traits<wchar_t>::decode(iNextCodePointStart, iEnd);
                switch (unicode_codepoint)
                {
                case U + 0x0410 /*'À'*/: res += L'A'; break;
                case U + 0x0412 /*'Â'*/: res += L'B'; break;
                case U + 0x0415 /*'Å'*/: res += L'E'; break;
                case U + 0x041A /*'Ê'*/: res += L'K'; break;
                case U + 0x041C /*'Ì'*/: res += L'M'; break;
                case U + 0x041D /*'Í'*/: res += L'H'; break;
                case U + 0x041E /*'Î'*/: res += L'O'; break;
                case U + 0x0420 /*'Ð'*/: res += L'P'; break;
                case U + 0x0421 /*'Ñ'*/: res += L'C'; break;
                case U + 0x0422 /*'Ò'*/: res += L'T'; break;
                case U + 0x0423 /*'Ó'*/: res += L'Y'; break;
                case U + 0x0425 /*'Õ'*/: res += L'X'; break;
                case U + 0x0406 /*'²'*/: res += L'I'; break;
                case illegal: throw std::runtime_error("Invalid unicode sequence observed in recNumber"); break;
                case incomplete: throw std::runtime_error("Incomplete unicode sequence observed in recNumber"); break;
                default: res.insert(res.end(), iPrevCodePointStart, iNextCodePointStart); break;
                }
            }

            return res;
        }

        /*static std::string getCountry(const std::string &detector, const bl::config::ListUnitsResponse& resp)
        {
            for (int i = 0; i < resp.units_size(); ++i)
            {
                const auto &unit = resp.units(i);

                if (unit.uid() == detector)
                {
                    for (int j = 0; j < unit.properties_size(); ++j)
                    {
                        const auto &prop = unit.properties(j);

                        if (prop.id() == "country")
                        {
                            return prop.value_string();
                        }
                    }
                }
            }

            return "";
        }*/

        /*static void sExecStartSearch(std::weak_ptr<AutoSearchContext> s, std::string detector,  const bl::config::ListUnitsResponse& resp, bool r)
        {
            auto pImpl = s.lock();
            if (pImpl)
            {
                pImpl->execStartSearch(r ? getCountry(detector, resp) :"");
            }
        }*/

        void StopSearch() override
        {
            _dbg_ << "Stopping auto search...";
            m_searchStopped = true;
        }

        void GetResult(const PRequest req, PResponse resp, size_t offset, size_t limit) const override
        {
            if (m_errorOccurred)
            {
                Error(resp, IResponse::ServiceUnavailable);
                return;
            }

            Json::Value events(Json::arrayValue);
            try
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                std::pair<Json::Value::const_iterator, Json::Value::const_iterator> its = SelectIteratorRange(m_events, offset, limit);
                if (its.first == m_events.begin() && its.second == m_events.end())
                    events = m_events;
                else
                {
                    for (; its.first != its.second; ++(its.first))
                        events.append(*(its.first));
                }
            }
            catch (const std::exception& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::NotFound);
                return;
            }

            Json::Value responseObject(Json::objectValue);
            responseObject["events"] = events;
            responseObject["more"] = offset + limit < m_events.size();

            SendResponse(req, resp, responseObject, m_searchFinished);
        }
    private:
        // TODO: use NativeBL EventHistory interface instead of CORBA ReadLprEvents
        // TODO: fix me - std::wstring are copies but not references as JE_Filter was
        void SearchTask(const std::wstring filters, const std::wstring searchPredicate, CORBA::ULong limit, CORBA::ULong offset)
        {
            if (!m_searchStopped)
            {
                _dbg_ << "Search continues from position " << offset;

                try
                {
                    Notification::Guid sesionID = Notification::GenerateUUID();
                    const ORM::WStringSeq_var result = m_orm->ReadLprEvents(sesionID, m_range, filters.c_str(), searchPredicate.c_str(), limit, offset, m_descending);

                    if (!m_searchStopped)
                    {
                        ConvertAutoEvents(result);

                        if (result->length() == limit) // is equal to 'more is true'
                        {
                            offset += limit;
                            m_reactor->GetIO().post(std::bind(&AutoSearchContext::SearchTask, shared_from_base<AutoSearchContext>(), filters, searchPredicate, limit, offset));
                            return;
                        }
                    }
                }
                catch (const CORBA::Exception & e)
                {
                    m_errorOccurred = true;
                    _err_ << "Auto search error: " << e._info();
                }
            }
            m_searchFinished = true;

            if (this->m_done)
                this->m_done();
        }

        void ConvertAutoEvents(const ORM::WStringSeq_var res)
        {
            google::protobuf::util::JsonParseOptions jpOpt;
            jpOpt.ignore_unknown_fields = true;

            std::unique_lock<std::mutex> lock(m_mutex);
            for (CORBA::ULong i = 0; i < res->length(); ++i)
            {
                axxonsoft::bl::events::Event evRaw;
                auto status = google::protobuf::util::JsonStringToMessage(NCorbaHelpers::ToUtf8(res->operator[](i).in()), &evRaw, jpOpt);
                if (status.ok() && evRaw.body().Is<axxonsoft::bl::events::DetectorEvent>())
                {
                    axxonsoft::bl::events::DetectorEvent ev;
                    evRaw.body().UnpackTo(&ev);

                    Json::Value obj{ Json::objectValue };
                    obj["timestamp"] = ev.timestamp();
                    
                    if (m_result_type == RESULT_TYPE_FULL) // TODO: the same 'all' data as it was befor with JsonEvent; standartize this response
                    {
                        obj["phase"] = static_cast<int>(ev.state()); // TODO: if you need pass this value you should use stringify enum
                        obj["origin_id"] = ev.origin_ext().access_point();
                        obj["detector_type"] = ev.event_type();
                        for (const auto& d : ev.details())
                        {
                            if (d.has_auto_recognition_result())
                            {
                                const auto& rRes = d.auto_recognition_result();

                                obj["TimeBegin"] = rRes.time_begin();
                                obj["TimeEnd"] = rRes.time_end();
                                obj["Direction"] = rRes.direction();

                                obj["Hypotheses"] = Json::arrayValue;
                                for (const auto& h : rRes.hypotheses())
                                {
                                    Json::Value hypo{ Json::objectValue };
                                    
                                    hypo["OCRQuality"] = h.ocr_quality();
                                    hypo["PlateCountry"] = h.country();
                                    hypo["PlateFull"] = h.plate_full();
                                    hypo["TimeBest"] = h.time_best();

                                    hypo["PlateRectangle"] = Json::arrayValue;

                                    hypo["PlateRectangle"].append(h.plate_rectangle().x());
                                    hypo["PlateRectangle"].append(h.plate_rectangle().y());
                                    hypo["PlateRectangle"].append(h.plate_rectangle().w() + h.plate_rectangle().x());
                                    hypo["PlateRectangle"].append(h.plate_rectangle().h() + h.plate_rectangle().y());

                                    obj["Hypotheses"].append(hypo);
                                }
                            }
                        }
                    }
                    else
                    {
                        obj["origin"] = ev.origin_ext().access_point(); // TODO: find out origin or origin_id?
                        obj["offlineAnalyticsSource"] = ev.offline_analytics_source();

                        Json::Value plates(Json::arrayValue);

                        int ocrQuality = 0;
                        bl::events::DetectorEvent::AutoRecognitionHypotheses bestHypo;

                        for (const auto& d : ev.details())
                        {
                            if (d.has_auto_recognition_result())
                            {
                                const auto& rRes = d.auto_recognition_result();
                                for (const auto& h : rRes.hypotheses())
                                {
                                    int curQuality = h.ocr_quality();
                                    if (ocrQuality < curQuality)
                                    {
                                        ocrQuality = curQuality;
                                        bestHypo = h;
                                    }
                                }
                            }
                        }

                        plates.append(bestHypo.plate_full());


                        Json::Value pos(Json::objectValue);
                        pos["left"] = bestHypo.plate_rectangle().x();
                        pos["top"] = bestHypo.plate_rectangle().y();
                        pos["right"] = bestHypo.plate_rectangle().w() + bestHypo.plate_rectangle().x();
                        pos["bottom"] = bestHypo.plate_rectangle().h() + bestHypo.plate_rectangle().y();

                        obj["position"] = pos;

                        obj["plates"] = plates;
                    }

                    m_events.append(obj);
                }
                else
                {
                    _wrn_ << "Can't deserialize lpr detector event json string to message";
                }
            }
        }

        NCorbaHelpers::PReactor m_reactor;

        ORM::AsipDatabase_var m_orm;
        ORM::StringSeq m_origins;
        ORM::TimeRange m_range;
        const std::string m_plate;
        const std::string m_result_type;

        mutable std::mutex m_mutex;
        Json::Value m_events;

        std::atomic<bool> m_errorOccurred;
        std::atomic<bool> m_searchStopped;
        std::atomic<bool> m_searchFinished;
        NGrpcHelpers::PCredentials m_metaCredentials;
        const NWebGrpc::PGrpcManager m_grpcManager;

        bool m_descending;
    };

    class CAutoSearchContentImpl : public CSearchPlugin<ORM::AsipDatabase>
    {
    public:
        CAutoSearchContentImpl(NCorbaHelpers::IContainer *c, const NWebGrpc::PGrpcManager grpcManager)
            : CSearchPlugin(c)
            , m_grpcManager(grpcManager)
        {}

        PSearchContext CreateSearchContext(const NHttp::PRequest req, DB_var orm, const std::string&, const std::vector<std::string>& orgs,
            Json::Value& data, boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending)
        {
            TParams params;
            if (!ParseParams(req->GetQuery(), params))
            {
                return PSearchContext();
            }

            std::string result_type;
            GetParam<std::string>(params, RESULT_TYPE_PARAM, result_type, std::string());

            if (data.isNull())
            {
                std::vector<uint8_t> body(req->GetBody());
                std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

                Json::CharReaderBuilder reader;
                std::string err;
                std::istringstream is(bodyContent);
                if (!body.empty() && !Json::parseFromStream(reader, is, &data, &err))
                {
                    _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                    return PSearchContext();
                }
            }

            // Default mask for return all plates.
            std::string plate = "*";

            if (data.isMember(PLATE_PARAMETER) && data[PLATE_PARAMETER].isString())
            {
                plate = data[PLATE_PARAMETER].asString();
            }
            else
            {
                _dbg_ << "Query does not contain plate parameter. Search will result all plates.";
            }

            ORM::TimeRange range;
            ORM::StringSeq origins;
            PrepareAsipRequest(orgs, beginTime, endTime, origins, range);

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            return PSearchContext(new AutoSearchContext(GET_LOGGER_PTR, orm, origins, range, plate, result_type, metaCredentials, m_grpcManager, descending));
        }

    private:
        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateAutoSearchServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CAutoSearchContentImpl(c, grpcManager);
    }
}
