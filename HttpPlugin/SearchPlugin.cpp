#include "Constants.h"
#include "SearchPlugin.h"
#include "RegexUtility.h"
#include "CommonUtility.h"

#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/ResolveServant.h>

#include <vmda/vmdaC.h>
#include <HeatMap/HeatMapC.h>

#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>
#include "GrpcHelpers.h"

using namespace NHttp;
using namespace NPluginUtility;

namespace
{
    const char* const OFFSET_PARAMETER = "offset";
    const char* const LIMIT_PARAMETER = "limit";

    const char* const SOURCES_PARAMETER = "sources";

    const uint32_t DEFAULT_OFFSET_COUNT = 0;
    const uint32_t DEFAULT_LIMIT_COUNT = 0xFFFFFFFF;

    const boost::regex SOURCE_MASK("hosts/(.*)/.*/.*");
}

template <typename TDatabase>
CSearchPlugin<TDatabase>::~CSearchPlugin()
{
    std::lock_guard<std::mutex> lock(m_searchMutex);
    TSearches::const_iterator it1 = m_searches.begin(), it2 = m_searches.end();
    for (; it1 != it2; ++it1)
        it1->second->StopSearch();
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::Get(const NHttp::PRequest req, NHttp::PResponse resp)
{
    TParams params;
    if (!ParseParams(req->GetQuery(), params))
    {
        Error(resp, IResponse::BadRequest);
        return;
    }

    std::string searchId;
    PToken id = Token(searchId);
    PMask result = Mask(RESULT_MASK);

    if (!Match(req->GetPathInfo(), id / result))
    {
        _err_ << "Incorrectly formatted result request";
        Error(resp, IResponse::BadRequest);
        return;
    }

    uint32_t offset = 0;
    GetParam<uint32_t>(params, OFFSET_PARAMETER, offset, DEFAULT_OFFSET_COUNT);

    uint32_t limit = DEFAULT_LIMIT_COUNT;
    GetParam<uint32_t>(params, LIMIT_PARAMETER, limit, DEFAULT_LIMIT_COUNT);

    PSearchContext searchCtx;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        TSearches::const_iterator it = m_searches.find(searchId);
        if (m_searches.end() == it)
        {
            TDefferredIds::const_iterator it2 = m_defferredIds.find(searchId);
            if (m_defferredIds.end() == it2)
            {
                _err_ << "Requested search session (" << searchId << ") not found";
                Error(resp, IResponse::NotFound);
                return;
            }
            else
            {
                Error(resp, IResponse::PartialContent);
                return;
            }
        }
        searchCtx = it->second;
    }

    if (searchCtx)
        searchCtx->GetResult(req, resp, offset, limit);
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::Delete(const NHttp::PRequest req, NHttp::PResponse resp)
{
    std::string searchId;
    PToken id = Token(searchId);

    if (!Match(req->GetPathInfo(), id))
    {
        _err_ << "Incorrect stop command";
        Error(resp, IResponse::BadRequest);
        return;
    }

    std::lock_guard<std::mutex> lock(m_searchMutex);
    TSearches::const_iterator it = m_searches.find(searchId);
    if (m_searches.end() == it)
    {
        _err_ << "Requested search session (" << searchId << ") not found.";
        Error(resp, IResponse::NotFound);
        return;
    }

    it->second->StopSearch();
    m_searches.erase(it);

    Error(resp, IResponse::NoContent);
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::PrepareAsipRequest(const std::vector<std::string>& orgs,
    boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime,
    ORM::StringSeq& origins, ORM::TimeRange& range)
{
    size_t originCount = orgs.size();
    origins.length(originCount);

    for (size_t i = 0; i < originCount; ++i)
    {
        origins[i] = CORBA::string_dup(orgs[i].c_str());
    }

    range.Begin.value = boost::posix_time::to_iso_string(beginTime).c_str();
    range.End.value = boost::posix_time::to_iso_string(endTime).c_str();
}

template <typename TDatabase>
bool CSearchPlugin<TDatabase>::ProcessSources(const Json::Value& data, std::string& hostName, std::vector<std::string>& origins)
{
    if (!data.isMember(SOURCES_PARAMETER))
    {
        _err_ << "Body does not contain sources list";
        return false;
    }

    Json::Value sources = data[SOURCES_PARAMETER];
    for (const Json::Value& s : sources)
    {
        std::string source(s.asString());
        boost::smatch what;
        if (regex_match(source, what, SOURCE_MASK))
        {
            if (hostName.empty())
            {
                std::string h(what[1]);
                hostName = h;
            }                
            origins.push_back(source);
        }
        else
        {
            _err_ << "Source " << source << ": invalid endpoint format";
            return false;
        }
    }
    return true;
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::Post(const NHttp::PRequest req, NHttp::PResponse resp)
{
    NCorbaHelpers::PContainer cont = m_container;
    if (!cont)
    {
        Error(resp, IResponse::InternalServerError);
        return;
    }

    const std::string p(req->GetPathInfo());

    PObjName ep = ObjName(3, "hosts/");

    boost::posix_time::ptime startTime, endTime;
    PDate begin = Begin(startTime);
    PDate end = End(endTime);

    PToken host = Token();

    if (!(ParseSafely(p, end / begin) || 
          ParseSafely(p, host / end / begin) ||
          ParseSafely(p, ep / end / begin)
          ))
    {
        _err_ << "Invalid search request";
        Error(resp, IResponse::BadRequest);
        return;
    }

    Json::Value json(Json::nullValue);

    std::vector<uint8_t> body(req->GetBody());

    if (!body.empty())
    {
        std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

        Json::CharReaderBuilder reader;
        std::string err;
        std::istringstream is(bodyContent);
        if (!Json::parseFromStream(reader, is, &json, &err))
        {
            _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
            Error(resp, IResponse::BadRequest);
            return;
        }
    }

    std::vector<std::string> origins;

    std::string hostName;
    std::string origin(ep->Get().ToString());

    if (!origin.empty())
    {
        NPluginUtility::ParseHostname(origin, hostName);
        origins.push_back(origin);
    }
    else
    {
        hostName.assign(host->GetValue());
        if (hostName.empty() && !json.isNull())
        {
            if (!ProcessSources(json, hostName, origins))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }
        }
    }

    if (hostName.empty())
    {
        _err_ << "Can not resolve host name";
        Error(resp, IResponse::BadRequest);
        return;
    }
    const std::string reference = GetDatabaseReferenceName();

    DB_var db = reference.empty() ? nullptr : GetDatabaseReference(cont.Get(), hostName);

    if (!reference.empty() && CORBA::is_nil(db))
    {
        _err_ << "Database is not accessible";
        Error(resp, IResponse::InternalServerError);
        return;
    }

    bool descending = startTime < endTime;

    if (startTime > endTime)
        std::swap(startTime, endTime);

    DoSearch(req, resp, db, hostName, origins, json, startTime, endTime, descending);
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::DoSearch(const NHttp::PRequest req, NHttp::PResponse resp, DB_var db, 
    const std::string& hostName, const std::vector<std::string>& origins, Json::Value& data,
    boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending)
{
    if (m_defferredSearchCount >= m_maxDefferredCount)
    {
        _wrn_ << "Search query rejected. Too many requests";
        Error(resp, IResponse::InternalServerError);
        return;
    }

    std::string exportId(NCorbaHelpers::GenerateUUIDString());

    PSearchContext searchCtx(CreateSearchContext(req, db, hostName, origins, data, beginTime, endTime, descending));
    if (!searchCtx)
    {
        Error(resp, IResponse::BadRequest);
        return;
    }

    if (m_searchCount >= m_maxActiveCount)
    {
        ++m_defferredSearchCount;

        std::unique_lock<std::mutex> lock(m_searchMutex);
        m_defferredSearches.push_back(std::make_pair(exportId, searchCtx));
        m_defferredIds.insert(exportId);
    }
    else
    {
        ++m_searchCount;
        {
            std::unique_lock<std::mutex> lock(m_searchMutex);
            m_searches.insert(std::make_pair(exportId, searchCtx));
        }
            
        searchCtx->Init(boost::bind(&TThis::SearchDone,
            boost::weak_ptr<TThis>(shared_from_base<TThis>())));
        searchCtx->StartSearch();

    }

    NHttp::SHttpHeader contentDispositionHeader("Location", req->GetPrefix() + req->GetContextPath() + "/" + exportId);

    resp->SetStatus(IResponse::Accepted);
    resp << contentDispositionHeader
        << CacheControlNoCache();
    resp->FlushHeaders();
}

template <typename TDatabase>
const char* const CSearchPlugin<TDatabase>::GetDatabaseReferenceName()
{
    throw std::invalid_argument("Unsupported database type");
}

template <>
const char* const CSearchPlugin<ORM::AsipDatabase>::GetDatabaseReferenceName()
{
    return "/EventDatabase.0/AsipDatabase";
}

template <>
const char* const CSearchPlugin<vmda::Database>::GetDatabaseReferenceName()
{
    return "/VMDA_DB.0/Database";
}

template <>
const char* const CSearchPlugin<heatMap::HeatMapBuilder>::GetDatabaseReferenceName()
{
    return ""; 
}

template <typename TDatabase>
TDatabase* CSearchPlugin<TDatabase>::GetDatabaseReference(NCorbaHelpers::IContainer* cont, const std::string& hostName)
{
    DB_var db;
    {
        std::unique_lock<std::mutex> lock(m_dbMutex);
        typename TDatabases::iterator it = m_databases.find(hostName);
        if (m_databases.end() != it)
        {
            _dbg_ << "Got DB reference from cache";
            db = it->second;
            lock.unlock();

            try
            {
                if (!CORBA::is_nil(db) && !db->_non_existent())
                {
                    _dbg_ << "Return cached reference";
                    return db._retn();
                }
            }
            catch (const CORBA::Exception&)
            {
                _dbg_ << "DB reference from cache is invalid. Refreshing...";
            }


            lock.lock();
            _dbg_ << "Delete reference from cache";
            auto it1 = m_databases.find(hostName);
            // if m_databases record still exists and points to the same object
            // as obtained earlier, remove it
            if (it1 != m_databases.end() && it1->second.ptr() == db.ptr())
            {
                m_databases.erase(it);
            }

            db = TDatabase::_nil();
        }
    }

    std::string name = HOST_PREFIX + hostName + GetDatabaseReferenceName();
    db = NCorbaHelpers::ResolveServant<TDatabase>(cont, name);
    if (!CORBA::is_nil(db))
    {
        std::unique_lock<std::mutex> lock(m_dbMutex);
        m_databases.insert(std::make_pair(hostName, db));
    }

    return db._retn();
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::SearchDone(boost::weak_ptr<TThis> sp)
{
    if (auto plugin = sp.lock())
    {
        plugin->DoNextSearch();
    }
}

template <typename TDatabase>
void CSearchPlugin<TDatabase>::DoNextSearch()
{
    PSearchContext ctx;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        if (m_defferredSearches.empty())
        {
            --m_searchCount;
            _log_ << "No pending searches";
            return;
        }

        TSearchInfo si = m_defferredSearches.front();
        m_defferredSearches.pop_front();

        ctx = si.second;
        m_defferredIds.erase(si.first);
        m_searches.insert(std::make_pair(si.first, ctx));
    }

    --m_defferredSearchCount;

    if (ctx)
    {
        ctx->Init(boost::bind(&TThis::SearchDone,
            boost::weak_ptr<TThis>(shared_from_base<TThis>())));
        ctx->StartSearch();
    }
}

template class CSearchPlugin < ORM::AsipDatabase > ;
template class CSearchPlugin < vmda::Database >;
template class CSearchPlugin < heatMap::HeatMapBuilder >;
