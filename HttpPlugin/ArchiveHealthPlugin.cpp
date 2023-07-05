#include "HttpPlugin.h"
#include "SendContext.h"
#include "RegexUtility.h"
#include "CommonUtility.h"

#include "Constants.h"
#include "GrpcReader.h"

#include <ORM_IDL/ORMC.h>
#include <ORM_IDL/ORM.h>

#include <HttpServer/BasicServletImpl.h>

#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/ResolveServant.h>

#include <json/json.h>

#include <boost/range/irange.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>
#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>

#include <thread>

//tech push git

using namespace NHttp;
using namespace NPluginUtility;

namespace bl = axxonsoft::bl;

using ArchiveListReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::ListArchivesRequest,
    bl::domain::ListArchivesResponse > ;

using PArchiveListReader_t = std::shared_ptr < ArchiveListReader_t >;


namespace
{
    struct SHealthContext
    {
        SHealthContext()
            : m_healthEvents(Json::arrayValue)
        {}

        Json::Value m_healthEvents;
    };
    typedef std::shared_ptr<SHealthContext> PHealthContext;

    class CArchiveHealthContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CArchiveHealthContentImpl(NCorbaHelpers::IContainer *c,  const NWebGrpc::PGrpcManager grpcManager)
            : m_container(c)
            , m_grpcManager(grpcManager)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            bl::domain::ListArchivesRequest reqArc;

            reqArc.set_view(bl::domain::EViewMode::VIEW_MODE_NO_CHILD_OBJECTS);

            PArchiveListReader_t reader(new ArchiveListReader_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials,
                &bl::domain::DomainService::Stub::AsyncListArchives));
            
            PHealthContext healthCtx = std::make_shared<SHealthContext>();
            reader->asyncRequest(reqArc, std::bind(&CArchiveHealthContentImpl::onGetCb, shared_from_base<CArchiveHealthContentImpl>(),
                GET_LOGGER_PTR, req, resp, healthCtx, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

    private:
        void onGetCb(DECLARE_LOGGER_ARG, const PRequest req, PResponse resp, PHealthContext healthCtx, const bl::domain::ListArchivesResponse &res,
            NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                return NPluginUtility::SendGRPCError(resp, grpcStatus);
            }

            switch (valid)
            {
                case NWebGrpc::_PROGRESS:
                {
                    std::vector<std::string> vArchives;

                    int itemCount = res.items_size();
                    for (int i = 0; i < itemCount; ++i)
                    {
                        const bl::domain::Archive& c = res.items(i);
                        if (!c.is_embedded())
                        {
                            vArchives.push_back(c.access_point());
                        }
                    }

                    if (vArchives.empty())
                    {
                        sendEmptyJson(resp);
                        return;
                    }


                    NCorbaHelpers::PContainer cont = m_container;
                    if (!cont)
                    {
                        Error(resp, IResponse::InternalServerError);
                        return;
                    }

                    boost::posix_time::ptime startTime, endTime;
                    PDate begin = Begin(startTime);
                    PDate end = End(endTime);

                    PToken host = Token();

                    const std::string p(req->GetPathInfo());
                    if (!ParseSafely(p, host / end / begin))
                    {
                        _err_ << "Invalid audit request";
                        Error(resp, IResponse::BadRequest);
                        return;
                    }

                    NPluginUtility::TParams params;
                    if (!NPluginUtility::ParseParams(req->GetQuery(), params))
                    {
                        _err_ << "Invalid audit query: parse params";
                        Error(resp, IResponse::BadRequest);
                        return;
                    }

                    std::string archive = GetParam<std::string>(params, "archive", std::string());

                    if (!archive.empty())
                    {
                        if (inVec(vArchives, archive))
                        {
                            vArchives.clear();
                            vArchives.push_back(archive);
                        }
                        else
                        {
                            sendEmptyJson(resp);
                            return;
                        }
                    }

                    //Далее vArchives - непустой
                    ORM::AsipDatabase_var db = NPluginUtility::GetDBReference(GET_LOGGER_PTR, cont.Get(), host->GetValue());
                    if (CORBA::is_nil(db))
                    {
                        _err_ << "Database is not accessible for host " << host->GetValue();
                        Error(resp, IResponse::InternalServerError);
                        return;
                    }

                    if (startTime > endTime)
                        std::swap(startTime, endTime);

                    ORM::TimeRange range;
                    range.Begin.value = boost::posix_time::to_iso_string(startTime).c_str();
                    range.End.value = boost::posix_time::to_iso_string(endTime).c_str();

                    // TODO: replace this logic by request to NativeBL. Ask e-mironov
                    // https://support.axxonsoft.com/jira/browse/ACR-50179

                    std::string health = GetParam<std::string>(params, "health", std::string());
                    int iHealth;
                    bool bAll = true;

                    iHealth = stoi_smart(health, bAll);

                    google::protobuf::util::JsonPrintOptions jOpt;
                    jOpt.always_print_enums_as_ints = false;
                    jOpt.always_print_primitive_fields = true;
                    jOpt.preserve_proto_field_names = true;

                    axxonsoft::bl::events::SearchFilterArray filters;
                    for (const auto& archive : vArchives)
                    {
                        axxonsoft::bl::events::SearchFilter f;
                        f.set_type(axxonsoft::bl::events::ET_StorageVolumeHealth);
                        *f.add_subjects() = archive;
                        
                        if (!bAll)
                        {
                            *f.add_values() = axxonsoft::bl::events::StorageVolumeHealth_ESWHealthState_Name(static_cast<axxonsoft::bl::events::StorageVolumeHealth_ESWHealthState>(iHealth));
                        }

                        *filters.add_filters() = f;
                    }
                    
                    std::string jsonFilters;
                    google::protobuf::util::MessageToJsonString(filters, &jsonFilters, jOpt);
                    std::wstring jsonFiltersW = NCorbaHelpers::FromUtf8(jsonFilters);

                    try
                    {
                        Notification::Guid sesionID = Notification::GenerateUUID();
                        const ORM::WStringSeq_var res = db->ReadEvents(sesionID, range, jsonFiltersW.c_str(), 1000000, 0, true);
                        
                        auto& events = healthCtx->m_healthEvents;

                        google::protobuf::util::JsonParseOptions jpOpt;
                        jpOpt.ignore_unknown_fields = true;

                        CORBA::ULong eventCount = res->length();

                        for (CORBA::ULong i = eventCount - 1; i <= 0; --i) //because of descending sort 
                        {
                            axxonsoft::bl::events::Event evRaw;
                            auto status = google::protobuf::util::JsonStringToMessage(NCorbaHelpers::ToUtf8(res->operator[](i).in()), &evRaw, jpOpt);
                            if (status.ok() && evRaw.body().Is<axxonsoft::bl::events::StorageVolumeHealth>())
                            {
                                axxonsoft::bl::events::StorageVolumeHealth ev;
                                evRaw.body().UnpackTo(&ev);

                                std::string sArc = ev.archive().access_point(); // this variable wasn't use in original code f54bf223a00a, https://support.axxonsoft.com/jira/browse/ACR-38662

                                Json::Value jEvent{ Json::objectValue };
                                jEvent["timestamp"] = ev.timestamp();

                                Json::Value dEvent{ Json::objectValue };
                                dEvent["archive"] = ev.volume_id(); // this is not an archive, this is volume! but the value as in original code f54bf223a00a https://support.axxonsoft.com/jira/browse/ACR-38662
                                dEvent["health"] = (int)ev.state(); // but better will be send string value

                                jEvent["data"] = dEvent;

                                events.append(jEvent);
                            }
                            else
                            {
                                _wrn_ << "Can't deserialize storage volume health event json string to message";
                            }
                        }
                    }
                    catch (const CORBA::Exception& e)
                    {
                        _err_ << "CORBA exception: " << e._name() << " with info " << e._info();
                        Error(resp, IResponse::InternalServerError);
                    }
                    catch (...)
                    {
                        _err_ << "Common exception";
                        Error(resp, IResponse::InternalServerError);
                    }

                    break;
                }
                case NWebGrpc::_FINISH:
                {
                    Json::Value responseObject;
                    responseObject["events"] = healthCtx->m_healthEvents;

                    std::string text(responseObject.toStyledString());
                    setHeader(resp, text);

                    NContext::PSendContext ctx(NContext::CreateStringContext(resp, text));
                    ctx->ScheduleWrite();
                    break;
                }
            };
        }

        void setHeader(PResponse resp, const std::string &text)
        {
            std::string contentType(NHttp::GetMIMETypeByExt("json"));
            contentType.append("; charset=utf-8");

            resp->SetStatus(IResponse::OK);
            resp << NHttp::ContentLength(text.size())
                << NHttp::ContentType(contentType)
                << NHttp::CacheControlNoCache();
            resp->FlushHeaders();
        }

        void sendEmptyJson(PResponse resp)
        {
            try
            {
                Json::Value responseObject;
                responseObject["events"] = Json::Value(Json::arrayValue);
                
                std::string text(responseObject.toStyledString());
                setHeader(resp, text);

                NContext::PSendContext ctx(NContext::CreateStringContext(resp, text));
                ctx->ScheduleWrite();
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "CORBA exception: " << e._name() << " with info " << e._info();
                Error(resp, IResponse::InternalServerError);
            }
            catch (...)
            {
                _err_ << "Common exception";
                Error(resp, IResponse::InternalServerError);
            }
        }

        void setStringSeq(ORM::StringSeq &sSeq, const  std::vector<std::string> &vArchives)
        {
            sSeq.length(vArchives.size());

            for (int i = 0; i < (int)vArchives.size(); ++i)
            {
                sSeq[i] = CORBA::string_dup(vArchives[i].c_str());
            }
        }

        int stoi_smart(const std::string &health, bool &bAll)
        {
            int res = 0;
            try 
            {
                res = stoi(health);
            }
            catch (...)
            {
                return 0;
            }

            bAll = false;
            return res;
        }

        template <typename T>
        bool inVec(std::vector<T> vec, const T& val)
        {
            for (auto &t : vec)
                if (t == val)
                    return true;
            return false;
        }

        NCorbaHelpers::WPContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;
    };
}

namespace NHttp
{
    IServlet* CreateArchiveHealthServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager)
    {
        return new CArchiveHealthContentImpl(c, grpcManager);
    }
}
