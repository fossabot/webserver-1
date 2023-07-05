#include <map>
#include <vector>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <CorbaHelpers/Reactor.h>
#include "ConnectionInitiator.h"

#include <MMIDL/MMEndpointC.h> // cookie length and greeting definition

struct SJob
{
    enum class Status { Proceeding, Succeeded, Failed, Canceled };

    SJob(const std::string& cookie_, NExecutors::PReactor reactor)
        :   cookie(cookie_)
        ,   resolver(reactor->GetIO())
        ,   socket(new NMMSS::TCPSocket_t{ reactor })
        ,   greeting(strlen(MMSS::CONNECTION_GREETING), 0)
        ,   status(Status::Proceeding)
    {
        socket->sock().open(boost::asio::ip::tcp::v4());
    }

    NMMSS::PTCPSocket finalize(bool unconditinallyCloseSucceeded)
    {
        if (Status::Proceeding == status)
            throw std::logic_error("job is not completed");
        if (Status::Succeeded != status || unconditinallyCloseSucceeded)
        {
            try
            {
                socket->sock().shutdown(boost::asio::ip::tcp::socket::shutdown_both);
            }
            catch (const std::runtime_error&){}
            try
            {
                socket->sock().close();
            }
            catch (const std::runtime_error&){}
            socket.reset();
        }
        return socket;
    }

    std::string cookie;
    boost::asio::ip::tcp::resolver resolver;
    NMMSS::PTCPSocket socket;
    std::string greeting;
    Status status;
};

typedef boost::shared_ptr<SJob> PJob;

class CConnectionInitiator: public NMMSS::IConnectionInitiator,
    public boost::enable_shared_from_this<CConnectionInitiator>
{
public:
    CConnectionInitiator(boost::mutex& mutex)
        :   m_mutex(mutex)
    {
    }
    virtual void InitiateConnection(const std::string& cookie,
        const std::vector<std::string>& addresses, unsigned short port,
        FHandler onConnected)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        if(m_jobs.end()!=m_jobs.find(cookie))
            throw std::runtime_error("attempt to connect a pending client");

        for (const auto& address : addresses)
        {
            PJob job(new SJob(cookie, NCorbaHelpers::GetReactorFromPool()));
            m_handlers.insert(std::make_pair(cookie, onConnected));
            m_jobs.insert(std::make_pair(cookie, job));

            boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(),
                address, boost::lexical_cast<std::string>(port),
                boost::asio::ip::tcp::resolver::query::address_configured);

            job->resolver.async_resolve(query,
                boost::bind(&CConnectionInitiator::handle_endpoint_resolved,
                shared_from_this(), _1, _2, job));
        }
    }
    virtual void Cancel(const std::string& cookie)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        tryFinalize(lock, cookie, CancelMode::CancelAll);
    }

private:
    void handle_endpoint_resolved(const boost::system::error_code& ec,
        boost::asio::ip::tcp::resolver::iterator it, PJob job)
    {
        if (ec || it == boost::asio::ip::tcp::resolver::iterator())
        {
            failed(job);
            return;
        }

        boost::mutex::scoped_lock lock(m_mutex);
        if (SJob::Status::Canceled != job->status)
        {
            job->socket->sock().async_connect(*it,
                boost::bind(&CConnectionInitiator::handle_connected,
                shared_from_this(), _1, job));
        }
    }
    void handle_connected(const boost::system::error_code& ec, PJob job)
    {
        if (ec)
        {
            failed(job);
            return;
        }

        boost::mutex::scoped_lock lock(m_mutex);
        if (SJob::Status::Canceled != job->status)
            job->socket->sock().async_send(boost::asio::buffer(job->cookie),
                boost::bind(&CConnectionInitiator::handle_cookie_sent, 
                    shared_from_this(), _1, _2, job));
    }
    void handle_cookie_sent(const boost::system::error_code& ec, size_t bytes_written, PJob job)
    {
        if (ec || job->cookie.size() != bytes_written)
        {
            failed(job);
            return;
        }

        boost::mutex::scoped_lock lock(m_mutex);
        if (SJob::Status::Canceled != job->status)
            job->socket->sock().async_receive(boost::asio::buffer(&job->greeting[0], job->greeting.size()),
                boost::bind(&CConnectionInitiator::handle_greeting_received,
                    shared_from_this(), _1, _2, job));
    }
    void handle_greeting_received(const boost::system::error_code& ec, size_t bytes_received, PJob job)
    {
        if(ec || job->greeting.size()!=bytes_received || job->greeting!=MMSS::CONNECTION_GREETING)
            failed(job);
        else
            succeeded(job);
    }
private:
    void cancelJob(boost::mutex::scoped_lock &lock, PJob job)
    {
        job->status = SJob::Status::Canceled;
        try
        {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
//  By default, this function always fails with operation_not_supported when used on Windows XP, Windows Server 2003, or earlier. Consult documentation for details.
#endif // _MSC_VER
            job->socket->sock().cancel();
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER
        }
        catch (const boost::system::system_error&)
        {
            // socket op. cancellation is not supported on win xp, - but that doesn't matter, 
            // it'll just take additional time to complete current op and ignore its result.
            // the job is being held by shared ptr, so its lifetime is not an issue.
        }
    }
    enum class CancelMode { Preserve, CancelProceeding, CancelAll };

    void tryFinalize(boost::mutex::scoped_lock &lock, const std::string& cookie, CancelMode cancelMode)
    {
        std::vector<PJob> finalized;
        auto eqr(m_jobs.equal_range(cookie));
        for (auto it = eqr.first; it != eqr.second; ++it)
        {
            if (SJob::Status::Proceeding == it->second->status)
            {
                if (CancelMode::Preserve == cancelMode)
                    return; // at least one job is not completed and we're not forced to finalize
                else
                    cancelJob(lock, it->second); // forced to cancel uncompleted jobs
            }
            else if (CancelMode::CancelAll == cancelMode)
                cancelJob(lock, it->second); // forced to cancel all jobs

            finalized.push_back(it->second);
        }
        if (finalized.empty())
            return;
        auto hit = m_handlers.find(cookie);
        if (m_handlers.end() == hit)
            throw std::logic_error("handler not found for a cookie being finalized");
        NMMSS::IConnectionInitiator::FHandler handler(hit->second);
        m_handlers.erase(hit);
        m_jobs.erase(eqr.first, eqr.second);
        lock.unlock();
        NMMSS::PTCPSocket res;
        for (auto j : finalized)
        {
            NMMSS::PTCPSocket s(j->finalize(res.get()!=nullptr));
            if (s)
                res = s;
        }
        handler(res);
    }
    void failed(PJob job)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        if (SJob::Status::Proceeding != job->status) // already canceled, ignore actual result
            return;
        job->status = SJob::Status::Failed;
        tryFinalize(lock, job->cookie, CancelMode::Preserve);
    }
    void succeeded(PJob job)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        if (SJob::Status::Proceeding != job->status) // already canceled, ignore actual result
            return;
        job->status = SJob::Status::Succeeded;
        tryFinalize(lock, job->cookie, CancelMode::CancelProceeding);
    }
private:
    boost::mutex& m_mutex;
    std::multimap<std::string, PJob> m_jobs;
    std::map<std::string, NMMSS::IConnectionInitiator::FHandler> m_handlers;
};

namespace NMMSS
{

PConnectionInitiator GetConnectionInitiatorInstance()
{
    static boost::mutex mutex;
    static boost::weak_ptr<CConnectionInitiator> weak;

    boost::mutex::scoped_lock lock(mutex);
    boost::shared_ptr<CConnectionInitiator> res=weak.lock();
    if(!res)
    {
        res.reset(new CConnectionInitiator(mutex));
        weak=res;
    }
    return res;
}

}
