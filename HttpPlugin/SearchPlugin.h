#include <mutex>
#include <atomic>

#include <Logging/log2.h>
#include <CorbaHelpers/Container.h>

#include "CommonUtility.h"
#include "Tokens.h"

#include <HttpServer/HttpServer.h>
#include <HttpServer/BasicServletImpl.h>
#include <HttpServer/HttpRequest.h>
#include <HttpServer/HttpResponse.h>

#include <ORM_IDL/ORMC.h>

#include <json/json.h>

namespace
{
    const std::uint32_t MAX_ACTIVE_SEARCH_COUNT = 8;
    const std::uint32_t MAX_DEFFERRED_SEARCH_COUNT = 200;
}

template <typename TResultContainer>
std::pair<typename TResultContainer::const_iterator, typename TResultContainer::const_iterator> SelectIteratorRange(const TResultContainer& c, size_t offset, size_t limit)
{
    typename TResultContainer::const_iterator it1, it2;
    {
        size_t eventCount = c.size();
        if (offset > eventCount)
        {
            throw std::invalid_argument("Given offset out of data range");
        }

        it1 = c.begin();
        std::advance(it1, offset);
        it2 = it1;
        size_t rest = std::distance(it1, c.end());

        if (rest < limit)
        {
            it2 = c.end();
        }
        else
        {
            std::advance(it2, limit);
        }
    }
    return std::make_pair(it1, it2);
}

typedef std::function<void()> FSearchDone;

struct ISearchContext : public std::enable_shared_from_this<ISearchContext>
{
    virtual ~ISearchContext() {}
    
    void Init(FSearchDone done)
    {
        m_done = done;
    }

    virtual void StartSearch() = 0;
    virtual void StopSearch() = 0;

    virtual void GetResult(const NHttp::PRequest req, NHttp::PResponse resp, size_t offset, size_t limit) const = 0;

    template <typename Derived>
    std::shared_ptr<Derived> shared_from_base()
    {
        return std::dynamic_pointer_cast<Derived>(shared_from_this());
    }

    void SendResponse(const NHttp::PRequest req, NHttp::PResponse resp, const Json::Value& responseObject, bool searchFinished) const
    {
        NPluginUtility::SendText(req, resp, responseObject.toStyledString(),
            searchFinished ? NHttp::IResponse::OK : NHttp::IResponse::PartialContent);
    }

    FSearchDone m_done;
};
typedef std::shared_ptr<ISearchContext> PSearchContext;

template <typename TDatabase>
class CSearchPlugin : public NHttpImpl::CBasicServletImpl
{
    typedef CSearchPlugin<TDatabase> TThis;
protected:
    DECLARE_LOGGER_HOLDER;
    typedef TAO_Objref_Var_T<TDatabase> DB_var;
public:
    CSearchPlugin(NCorbaHelpers::IContainer *c)
        : m_container(c)
        , m_searchCount(0)
        , m_defferredSearchCount(0)
        , m_maxActiveCount(MAX_ACTIVE_SEARCH_COUNT)
        , m_maxDefferredCount(MAX_DEFFERRED_SEARCH_COUNT)
    {
        INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
    }
    ~CSearchPlugin();

    void Post(const NHttp::PRequest req, NHttp::PResponse resp);
    void Get(const NHttp::PRequest req, NHttp::PResponse resp);
    void Delete(const NHttp::PRequest req, NHttp::PResponse resp);

protected:
    virtual void DoSearch(const NHttp::PRequest req, NHttp::PResponse resp, DB_var db,
        const std::string& hostName, const std::vector<std::string>& origins, Json::Value& data,
        boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending);
    virtual PSearchContext CreateSearchContext(const NHttp::PRequest req, DB_var db,
        const std::string& hostName, const std::vector<std::string>& origins, Json::Value& data,
        boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime, bool descending) = 0;

    void PrepareAsipRequest(const std::vector<std::string>& origins,
        boost::posix_time::ptime beginTime, boost::posix_time::ptime endTime,
        ORM::StringSeq& orgs, ORM::TimeRange& range);

    bool ProcessSources(const Json::Value& data, std::string& hostName, std::vector<std::string>& origins);

    const char* const GetDatabaseReferenceName();
    TDatabase* GetDatabaseReference(NCorbaHelpers::IContainer* cont, const std::string& hostName);

    static void SearchDone(boost::weak_ptr<TThis> sp);
    void DoNextSearch();

    NCorbaHelpers::WPContainer m_container;

    typedef std::map<std::string, PSearchContext> TSearches;
    typedef std::pair<std::string, PSearchContext> TSearchInfo;
    typedef std::deque<TSearchInfo> TDefferredSearches;
    typedef std::set<std::string> TDefferredIds;

    std::mutex m_searchMutex;
    TSearches m_searches;
    TDefferredSearches m_defferredSearches;
    TDefferredIds m_defferredIds;

    std::atomic<std::uint32_t> m_searchCount;
    std::atomic<std::uint32_t> m_defferredSearchCount;

    std::uint32_t m_maxActiveCount;
    std::uint32_t m_maxDefferredCount;

    std::mutex m_dbMutex;
    typedef std::map<std::string, DB_var> TDatabases;
    TDatabases m_databases;
};
