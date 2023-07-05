#include "HttpPlugin.h"
#include "SendContext.h"
#include "RegexUtility.h"
#include "CommonUtility.h"

#include <ORM_IDL/ORMC.h>
#include <ORM_IDL/ORM.h>

#include <HttpServer/BasicServletImpl.h>

#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/ResolveServant.h>

#include <json/json.h>

#include <boost/range/irange.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;

namespace
{
    const boost::regex CODE_MASK("^(\\d+)$");
    const boost::regex RANGE_MASK("^(\\d+)[:-](\\d+)$");

    class CAuditContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;

    public:
        CAuditContentImpl(NCorbaHelpers::IContainer *c, const NPluginUtility::PRigthsChecker rightsChecker)
            : m_container(c)
            , m_rightsChecker(rightsChecker)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            NPluginUtility::IRigthsChecker::TPermissions perms = { axxonsoft::bl::security::FEATURE_ACCESS_SYSTEM_JOURNAL };
            m_rightsChecker->HasGlobalPermissions(perms, req->GetAuthSession(),
                boost::bind(&CAuditContentImpl::processAuditRequest, shared_from_base<CAuditContentImpl>(),
                    req, resp, _1));
        }

    private:
        void processAuditRequest(const PRequest req, PResponse resp, bool hasPermissions)
        {
            if (!hasPermissions)
            {
                Error(resp, IResponse::Forbidden);
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

            ORM::AsipDatabase_var db = NPluginUtility::GetDBReference(GET_LOGGER_PTR, cont.Get(), host->GetValue());
            if (CORBA::is_nil(db))
            {
                _err_ << "Database is not accessible for host " << host->GetValue();
                Error(resp, IResponse::InternalServerError);
                return;
            }

            if (startTime > endTime)
                std::swap(startTime, endTime);

            std::vector<int> eventCodes;
            ReadEventCodes(params, eventCodes);

            ORM::TimeRange range;
            range.Begin.value = boost::posix_time::to_iso_string(startTime).c_str();
            range.End.value = boost::posix_time::to_iso_string(endTime).c_str();

            // TODO: replace this logic by request to NativeBL. Ask e-mironov
            // https://support.axxonsoft.com/jira/browse/ACR-50073

            google::protobuf::util::JsonPrintOptions jOpt;
            jOpt.always_print_enums_as_ints = false;
            jOpt.always_print_primitive_fields = true;
            jOpt.preserve_proto_field_names = true;

            axxonsoft::bl::events::SearchFilterArray filters;
            if (eventCodes.size() > 0)
            {
                for (const auto code : eventCodes)
                {
                    if (code != static_cast<int>(axxonsoft::bl::events::AuditEvent::AE_NOT_SPECIFIED) &&
                       (code < static_cast<int>(axxonsoft::bl::events::AuditEvent::AE_USER_ADD) ||
                        code >= static_cast<int>(axxonsoft::bl::events::AuditEvent::AE_ARCHIVE_COMMENT_EDIT)))
                    {
                        _err_ << "Unknown audit operation code: " << code;
                        continue;
                    }

                    axxonsoft::bl::events::SearchFilter f;
                    f.set_type(axxonsoft::bl::events::ET_Audit);
                    auto operation = static_cast<axxonsoft::bl::events::AuditEvent::EAuditEventType>(code);
                    *f.add_values() = axxonsoft::bl::events::AuditEvent_EAuditEventType_Name(operation);
                    *filters.add_filters() = f;
                }
            }
            else
            {
                axxonsoft::bl::events::SearchFilter f;
                f.set_type(axxonsoft::bl::events::ET_Audit);
                *filters.add_filters() = f;
            }

            std::string jsonFilters;
            google::protobuf::util::MessageToJsonString(filters, &jsonFilters, jOpt);
            std::wstring jsonFiltersW = NCorbaHelpers::FromUtf8(jsonFilters);

            try
            {
                Json::Value events(Json::arrayValue);

                Notification::Guid sesionID = Notification::GenerateUUID();
                ORM::WStringSeq_var res = db->ReadEvents(sesionID, range, jsonFiltersW.c_str(), 1000000, 0, true);

                google::protobuf::util::JsonParseOptions jpOpt;
                jpOpt.ignore_unknown_fields = true;
                for (CORBA::ULong i = res->length(); i-- != 0;) // because of descending sort
                {
                    axxonsoft::bl::events::Event evRaw;
                    auto status = google::protobuf::util::JsonStringToMessage(NCorbaHelpers::ToUtf8(res->operator[](i).in()), &evRaw, jpOpt);
                    if (status.ok() && evRaw.body().Is<axxonsoft::bl::events::AuditEvent>())
                    {
                        axxonsoft::bl::events::AuditEvent ev;
                        evRaw.body().UnpackTo(&ev);

                        Json::Value auditEvent(Json::objectValue);
                        auditEvent["timestamp"] = ev.timestamp();
                        auditEvent["eventType"] = (int)ev.operation();
                        auditEvent["data"]["user"] = ev.user().name();
                        if (!ev.ip_address().empty())
                            auditEvent["data"]["ip_address"] = ev.ip_address();
                        // TODO: fill role here if exists; ACR-48019

                        for (const auto& param : ev.params())
                        {
                            auditEvent["data"][param.name()] = param.value();
                        }

                        events[res->length() - i - 1] = auditEvent;
                    }
                    else
                    {
                        _wrn_ << "Can't deserialize audit event json string to message";
                    }
                }

                Json::Value responseObject;
                responseObject["events"] = events;

                NPluginUtility::SendText(req, resp, responseObject.toStyledString());
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "CORBA exception: " << e._name() << " with info " << e._info();
                Error(resp, IResponse::InternalServerError);
            }
        }

        void ReadEventCodes(const NPluginUtility::TParams& params, std::vector<int>& codes)
        {
            std::string filter(GetParam(params, "filter", std::string()));
            std::vector<std::string> typeCodes;
            boost::split(typeCodes, filter, boost::is_any_of(","));

            for (const auto i : typeCodes)
            {
                boost::smatch what;
                if (regex_match(i, what, RANGE_MASK))
                {
                    boost::integer_range<int> r(std::stoi(what[1]), std::stoi(what[2]) + 1);
                    std::copy(r.begin(), r.end(), std::back_inserter(codes));
                }
                else if (regex_match(i, what, CODE_MASK))
                {
                    codes.push_back(std::stoi(what[1]));
                }
                else
                {
                    _wrn_ << "Cannot interpret " << i << " as code or code range";
                }
            }
        }

        NCorbaHelpers::WPContainer m_container;
        const NPluginUtility::PRigthsChecker m_rightsChecker;
    };
}

namespace NHttp
{
    IServlet* CreateAuditServlet(NCorbaHelpers::IContainer* c, const NPluginUtility::PRigthsChecker rightsChecker)
    {
        return new CAuditContentImpl(c, rightsChecker);
    }
}
