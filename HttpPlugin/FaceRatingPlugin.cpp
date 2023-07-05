#include "Constants.h"
#include "HttpPlugin.h"
#include "SearchPlugin.h"
#include "RegexUtility.h"

#include <ORM_IDL/ORM.h>
#include <Crypto/Crypto.h>
#include <CorbaHelpers/ResolveServant.h>

using namespace NHttp;
using namespace NPluginUtility;

namespace
{
    const char* const IMAGE_PARAMETER = "image";

    const int MAX_RESOLVE_TIMEOUT_MS = 10000; // 10 seconds.

    class CAlienStatusContentImpl : public CSearchPlugin<ORM::AsipDatabase>
    {
        NCorbaHelpers::WPContainer m_cont;

    public:
        CAlienStatusContentImpl(NCorbaHelpers::IContainer *c)
            : CSearchPlugin(c)
            , m_cont(c)
        {}

        void Get(const NHttp::PRequest, NHttp::PResponse resp)
        {
            Error(resp, IResponse::NotImplemented);
        }

        void Delete(const NHttp::PRequest, NHttp::PResponse resp)
        {
            Error(resp, IResponse::NotImplemented);
        }

    private:
        void DoSearch(const NHttp::PRequest req, NHttp::PResponse resp, DB_var db, const std::vector<std::string>& orgs,
            Json::Value& data, boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime)
        {
            TParams params;
            if (!ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            float accuracy = DEFAULT_ACCURACY;
            GetParam<float>(params, ACCURACY_PARAM, accuracy, DEFAULT_ACCURACY);

            size_t sz = 0;
            std::vector<uint8_t> body;
            if (data.isNull())
            {
                body = req->GetBody();
                sz = body.size();
            }
            else
            {
                if (!data.isMember(IMAGE_PARAMETER))
                {
                    _err_ << "Query does not contain image";
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                std::string base64image(data[IMAGE_PARAMETER].asString());
                std::string image(NCrypto::FromBase64(base64image.c_str(), base64image.size()).value_or(""));

                sz = image.size();
                std::copy(image.begin(), image.end(), std::back_inserter(body));
            }

            if (0 == sz)
            {
                _err_ << "Body does not contain image data";
                Error(resp, IResponse::BadRequest);
                return;
            }

            ORM::TimeRange range;
            ORM::StringSeq origins;
            PrepareAsipRequest(orgs, beginTime, endTime, origins, range);

            ORM::GuidSeq faceIds;

            NCorbaHelpers::PContainer c = m_cont;
            if (!c)
            {
                _err_ << "the container is already dead!";
                Error(resp, IResponse::InternalServerError);
                return;
            }

            ORM::ObjectSearcher_var faceSearcher = NCorbaHelpers::ResolveServant<ORM::ObjectSearcher>(c.Get(), "hosts/" + NCorbaHelpers::CEnvar::NgpNodeName() + "/ObjectSearcher.0/Searcher", MAX_RESOLVE_TIMEOUT_MS);
            if (CORBA::is_nil(faceSearcher) || faceSearcher->GetServiceReadinessStatus() != ServiceInfo::ESR_Ready)
            {
                _err_ << "Face searcher is not accessible or not ready yet";
                Error(resp, IResponse::InternalServerError);
                return;
            }

            if (accuracy < MIN_ACCURACY || accuracy > MAX_ACCURACY)
                accuracy = DEFAULT_ACCURACY;

            Notification::Guid sesionID = Notification::GenerateUUID();
            ORM::SimilarObjectSeq_var fs = faceSearcher->FindStrangersByObjects(sesionID, true, accuracy, ORM::OctetSeq(sz, sz, &body[0], 0), faceIds, origins);

            if (fs->length() == 0)
            {
                Error(resp, IResponse::NotFound);
                return;
            }

            Json::Value responseObject(Json::objectValue);
            responseObject["rate"] = fs[static_cast<CORBA::ULong>(0)].Score;

            NPluginUtility::SendText(req, resp, responseObject.toStyledString());
        }

        PSearchContext CreateSearchContext(const NHttp::PRequest req, DB_var orm, const std::string&, const std::vector<std::string>& origins,
            Json::Value& data, boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending)
        {
            return PSearchContext();
        }
    };
}

namespace NHttp
{
    IServlet* CreateFarStatusServlet(NCorbaHelpers::IContainer* c)
    {
        return new CAlienStatusContentImpl(c);
    }
}
