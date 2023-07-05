#include <ace/OS.h>
#include <map>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <HttpServer/BasicServletImpl.h>
#include <MMClient/MMClient.h>
#include <MediaType.h>

#include "HttpPlugin.h"
#include "Tokens.h"
#include "DataSink.h"
#include "SendContext.h"
#include "Constants.h"
#include "RegexUtility.h"

#include "../MMCoding/Initialization.h"
#include "../MMCoding/Transforms.h"
#include "../MMTransport/MMTransport.h"
#include "../ConnectionBroker.h"
#include "../PullStylePinsBaseImpl.h"
#include "../PtimeFromQword.h"

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/RefcountedImpl.h>

using namespace NHttp;
namespace npu = NPluginUtility;
namespace bpt = boost::posix_time;

namespace
{
    const uint32_t SAMPLE_TIMEOUT_MS = 15000;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct ISnapshotSink : public virtual NMMSS::CPullStyleSinkBasePureRefcounted
    {
        virtual void Connect(const std::string &source, uint32_t width, uint32_t height, float, float, float, float) = 0;
        virtual void Disconnect() = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<ISnapshotSink> PSnapshotSink;

    struct ISnapshotContext : public virtual NCorbaHelpers::IWeakReferable
    {
        virtual void Execute(NHttp::PResponse, bool, uint32_t, uint32_t, float, float, float, float) = 0;
        virtual void ExecuteCb(std::function<void()> cb) = 0;
        virtual void Destroy() = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<ISnapshotContext> PSnapshotContext;
    typedef NCorbaHelpers::CWeakPtr<ISnapshotContext> WPSnapshotContext;

    typedef boost::function1<void, NMMSS::PSample> TOnSnapshotHandler;

    class CSnapshotSink : public virtual ISnapshotSink,
        public virtual NCorbaHelpers::CRefcountedImpl
    {
        DECLARE_LOGGER_HOLDER;

        typedef std::list<NCorbaHelpers::PRefcounted>             TFilters;
        typedef boost::shared_ptr<NMMSS::IConnectionBase>         PConnection;
        typedef std::list<PConnection>                            TConnections;

        NCorbaHelpers::PReactor     m_reactor;
        NCorbaHelpers::WPContainer  m_container;
        boost::asio::deadline_timer m_timer;
        TOnSnapshotHandler          m_handler;

        NMMSS::PPullFilter          m_decoder;
        TConnections                m_connections;
        NMMSS::PSinkEndpoint        m_sink;

        boost::mutex                m_mutex;       
        bool                        m_waiting;
        bool                        m_gotSnapshot;

        boost::mutex                m_destroyMutex;
        bool                        m_destroyed;

    public:
        CSnapshotSink(DECLARE_LOGGER_ARG, NCorbaHelpers::PReactor reactor
            , NCorbaHelpers::WPContainer c
            , TOnSnapshotHandler handler)
            :   m_reactor(reactor)
            ,   m_container(c)
            ,   m_timer(m_reactor->GetIO())
            ,   m_handler(handler)
            ,   m_waiting(true)
            ,   m_gotSnapshot(false)
            ,   m_destroyed(false)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
        {
            return NMMSS::SAllocatorRequirements();
        }

        void Receive(NMMSS::ISample* s)
        {
            {
                boost::mutex::scoped_lock lock(m_destroyMutex);
                if (m_destroyed)
                    return;
            }

            RequestNextSamples(1);
            m_waiting = false;
            if (m_decoder)
                m_decoder->GetSink()->Receive(s);
        }

        virtual void Connect(const std::string &source, uint32_t width, uint32_t height, float crop_x, float crop_y, float crop_width, float crop_height)
        {
            NCorbaHelpers::PContainer cont = m_container;
            if(!cont) return;

            using namespace NMMSS;
            NLogging::ILogger *const logger = cont->GetLogger();
            PPullFilter decoder = CreateDecoder(logger);

            PPullFilter scaler = PPullFilter(CreateSizeFilter(logger, width, height, true, crop_x, crop_y, crop_width, crop_height));
            PPullFilter encoder = CreateEncoder(logger);

            PPullStyleSink sink(NPluginHelpers::CreateSink(
                boost::bind(&CSnapshotSink::onSampleHandler, makeAutoPtr(), _1)));

            ConnectionBroker *const connBroker = GetConnectionBroker();
            PConnection conn1(connBroker->SetConnection(sink.Get(), encoder->GetSource(), logger),
                CSnapshotSink::destroyConnection);

            PConnection conn2(connBroker->SetConnection(encoder->GetSink(), scaler->GetSource(), logger),
                CSnapshotSink::destroyConnection);

            PConnection conn3(connBroker->SetConnection(scaler->GetSink(), decoder->GetSource(), logger),
                CSnapshotSink::destroyConnection);

            try
            {
                m_sink = PSinkEndpoint(
                    CreatePullConnectionByNsref(
                        logger,
                        source.c_str(),
                        cont->GetRootNC(),
                        this,
                        MMSS::EAUTO
                    ));

                m_connections.push_back(conn1);
                m_connections.push_back(conn2);
                m_connections.push_back(conn3);
            }
            catch(const std::exception &) {}

            m_decoder = decoder;

            m_timer.expires_from_now(boost::posix_time::milliseconds(SAMPLE_TIMEOUT_MS));
            m_timer.async_wait(boost::bind(&CSnapshotSink::noDataHandler, makeAutoPtr(), _1));          
        }

        virtual void Disconnect()
        {
            boost::mutex::scoped_lock lock(m_destroyMutex);
            m_destroyed = true;

            m_timer.cancel();
            m_sink->Destroy();
            m_sink.Reset();
            m_connections.clear();
        }

    private:
        virtual void onConnected(boost::mutex::scoped_lock& lock) 
        {
            requestNextSamples(lock, 1, false);
        }

        static NMMSS::PPullFilter CreateDecoder(DECLARE_LOGGER_ARG)
        {
            using namespace NMMSS;
            PPullFilter res(CreateVideoDecoderPullFilter(GET_LOGGER_PTR));
            return res;
        }

        static NMMSS::PPullFilter CreateEncoder(DECLARE_LOGGER_ARG)
        {
            using namespace NMMSS;
            return PPullFilter(CreateMJPEGEncoderFilter(GET_LOGGER_PTR, static_cast<EVideoCodingPreset>(0)));
        }

        static void destroyConnection(NMMSS::IConnectionBase *conn)
        {
            try
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(conn);
            }
            catch(const std::exception &) {}
        }

        void noDataHandler(boost::system::error_code ec)
        {
            if (!ec && m_waiting)
            {
                NMMSS::PSample s;
                sendSnapshot(s);
            }
        }

        void onSampleHandler(NMMSS::ISample* sample)
        {
            NMMSS::PSample s(sample, NCorbaHelpers::ShareOwnership());
            sendSnapshot(s);
        }

        void sendSnapshot(NMMSS::PSample s)
        {
            boost::mutex::scoped_lock lock(m_mutex);
            if (!m_gotSnapshot)
            {
                m_gotSnapshot = true;
                lock.unlock();

                m_handler(s);
            }
        }

        NCorbaHelpers::CAutoPtr<CSnapshotSink> makeAutoPtr(){ 
            return NCorbaHelpers::CAutoPtr<CSnapshotSink>(this, NCorbaHelpers::ShareOwnership()); }
    };

    class CSnapshotContext : public virtual ISnapshotContext,
        public virtual NCorbaHelpers::CWeakReferableImpl
    {
        DECLARE_LOGGER_HOLDER;

        NCorbaHelpers::PReactor m_reactor;
        NCorbaHelpers::WPContainer m_container;
        const std::string m_ep;
        typedef std::map<NHttp::PResponse, bool> TResponses;
        TResponses m_responses;
        bool m_headersOnly;
    public:
        CSnapshotContext(DECLARE_LOGGER_ARG, NCorbaHelpers::PReactor reactor
            , NCorbaHelpers::WPContainer c
            , const std::string& ep)
            : m_reactor(reactor)
            , m_container(c)
            , m_ep(ep)
            , m_processing(false)
            , m_stopping(false)
            , m_cacheTimer(m_reactor->GetIO())
        {
            INIT_LOGGER_HOLDER;
        }

        void Execute(NHttp::PResponse r, bool headersOnly, uint32_t width, uint32_t height, float crop_x, float crop_y, float crop_width, float crop_height)
        {
            boost::mutex::scoped_lock lock(m_mutex);
            while(m_processing)
                m_condition.wait(lock);

            if (m_cachedSample)
            {
                sendSample(r, headersOnly);
                return;
            }

            if (!m_stopping)
            {
                m_responses.insert(std::make_pair(r, headersOnly));
                if (!m_sink)
                {
                    m_sink = PSnapshotSink(new CSnapshotSink(GET_LOGGER_PTR, m_reactor, m_container,
                        boost::bind(&CSnapshotContext::OnSnapshot, makeSelfPtr(), _1)));
                    m_sink->Connect(m_ep, width, height, crop_x, crop_y, crop_width, crop_height);
                }
            }
        }

        void ExecuteCb(std::function<void()> cb)
        {
            m_sink = PSnapshotSink(new CSnapshotSink(GET_LOGGER_PTR, m_reactor, m_container,
                [self = makeSelfPtr(), cb](NMMSS::PSample )
             {
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(boost::bind(cb));
                self->Destroy();
             }
            ));

            m_sink->Connect(m_ep, 72, 0, 1,1,0,0);
        }

        void Destroy()
        {
            m_cacheTimer.cancel();

            boost::mutex::scoped_lock lock(m_mutex);
            m_stopping = true;

            if (m_sink)
            {
                m_sink->Disconnect();
                m_sink.Reset();
            }

            while(m_processing)
                m_condition.wait(lock);

            m_responses.clear();
        }

    private:
        void OnSnapshot(NMMSS::PSample s)
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_processing = true;
            if (!m_stopping)
            {
                m_sink->Disconnect();
                m_sink.Reset();
            }
            else
            {
                sendErrorToAll(IResponse::ServiceUnavailable);
                return;
            }

            if(!s || NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header()))
            {
                sendErrorToAll(IResponse::NoContent);
            }
            else
            {
                m_cachedSample = s;
                TResponses responses = m_responses;
                m_responses.clear();
                onNotify();
                lock.unlock();

                m_cacheTimer.expires_from_now(boost::posix_time::milliseconds(NCorbaHelpers::CEnvar::SnapshotTimeout()));
                m_cacheTimer.async_wait(boost::bind(&CSnapshotContext::invalidateCache, makeSelfPtr(), _1));

                TResponses::iterator it1 = responses.begin(), it2 = responses.end();
                for (; it1 != it2; ++it1)
                {
                    sendSample(it1->first, it1->second);
                }
            }
        }

        void RequestFilled(NHttp::PResponse r, boost::system::error_code error)
        {
            if(error)
                Error(r, IResponse::InternalServerError);
        }

        void onNotify()
        {
            m_processing = false;
            m_condition.notify_one();
        }

        void sendErrorToAll(NHttp::IResponse::EStatus status)
        {
            TResponses::iterator it1 = m_responses.begin(), it2 = m_responses.end();
            for (; it1 != it2; ++it1)
            {
                NHttp::PResponse response = it1->first;
                Error(response, status);
            }
            m_responses.clear();
            onNotify();
        }

        void invalidateCache(boost::system::error_code ec)
        {
            if (!ec)
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_cachedSample.Reset();
            }
        }

        void sendSample(NHttp::PResponse response, bool headersOnly)
        {
            NMMSS::PSample s = m_cachedSample;
            response->SetStatus(IResponse::OK);
            response << ContentLength(s->Header().nBodySize)
                       << ContentType(GetMIMETypeByExt("jpeg"))
                       << CacheControlNoCache();

            response->FlushHeaders();

            if(!headersOnly)
            {
                NContext::PSendContext sendCtx(
                    NContext::CreateSampleContext(response, s.Get(),
                        boost::bind(&CSnapshotContext::RequestFilled, makeSelfPtr(), response, _1)));
                sendCtx->ScheduleWrite();
            }
        }

        NCorbaHelpers::CAutoPtr<CSnapshotContext> makeSelfPtr(){ 
            return NCorbaHelpers::CAutoPtr<CSnapshotContext>(this, NCorbaHelpers::ShareOwnership()); }

        boost::mutex m_mutex;
        PSnapshotSink m_sink;
        bool m_processing;
        bool m_stopping;
        boost::condition m_condition;
        NMMSS::PSample m_cachedSample;
        boost::asio::deadline_timer m_cacheTimer;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CLiveSnapshotServlet
        :   public NHttpImpl::CBasicServletImpl
    {
    public:
        CLiveSnapshotServlet(NCorbaHelpers::IContainer *c, const npu::PRigthsChecker rc)
            :   m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            ,   m_container(c)
            ,   m_rightsChecker(rc)
            ,   m_destroying(false)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
            m_mmcoding.reset(new NMMSS::CMMCodingInitialization(GET_LOGGER_PTR));
        }

        ~CLiveSnapshotServlet()
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_destroying = true;

            TContextMap::iterator it1 = m_contexts.begin(), it2 = m_contexts.end();
            for (; it1 != it2; ++it1)
                it1->second->Destroy();
            m_contexts.clear();
        }

    private:
        virtual void Head(const PRequest req, PResponse resp)
        {
            Process(req, resp, true);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Process(req, resp, false);
        }

    private:
        void Process(const PRequest req, PResponse resp, bool headersOnly)
        {
            std::string epId(GetEndpoint(req));
            if(epId.empty())
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            m_rightsChecker->IsCameraAllowed(epId, req->GetAuthSession(),
                boost::bind(&CLiveSnapshotServlet::isCameraAllowed,
                    boost::weak_ptr<CLiveSnapshotServlet>(shared_from_base<CLiveSnapshotServlet>()), req, 
                    resp, epId, headersOnly, boost::placeholders::_1));
        }

        static void isCameraAllowed(boost::weak_ptr<CLiveSnapshotServlet> owner, const PRequest req, PResponse resp,
            const std::string& endpoint, bool headersOnly, int cameraAccessLevel)
        {
            if (cameraAccessLevel < axxonsoft::bl::security::CAMERA_ACCESS_MONITORING_ON_PROTECTION)
            {
                Error(resp, IResponse::Forbidden);
                return;
            }

            boost::shared_ptr<CLiveSnapshotServlet> o = owner.lock();
            if (o)
                o->processRequest(req, resp, endpoint, headersOnly);
        }

        void processRequest(const PRequest req, PResponse resp, const std::string& endpoint, bool headersOnly)
        {
            npu::TParams params;
            if (!npu::ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            uint32_t width = npu::GetParam<int>(params, WIDTH_PARAMETER, 0);
            uint32_t height = npu::GetParam<int>(params, HEIGHT_PARAMETER, 0);

            float crop_x = npu::GetParam<float>(params, "crop_x", 0.f);
            float crop_y = npu::GetParam<float>(params, "crop_y", 0.f);
            float crop_width = npu::GetParam<float>(params, "crop_width", 1.f);
            float crop_height = npu::GetParam<float>(params, "crop_height", 1.f);

            boost::mutex::scoped_lock lock(m_mutex);
            if (m_destroying)
                Error(resp, IResponse::NotFound);
            else
            {
                PSnapshotContext ctx;
                SSnapshotContextKey sck(endpoint, width, height);
                TContextMap::iterator it = m_contexts.find(sck);
                if (m_contexts.end() == it)
                {
                    ctx = PSnapshotContext(new CSnapshotContext(GET_LOGGER_PTR, m_reactor,
                        m_container, endpoint));
                    m_contexts.insert(std::make_pair(sck, ctx));
                }
                else
                    ctx = m_contexts[sck];

                m_reactor->GetIO().post(boost::bind(&ISnapshotContext::Execute,
                    ctx, resp, headersOnly, width, height, crop_x, crop_y, crop_width, crop_height));
            }
        }

        static std::string GetEndpoint(const PRequest req)
            /*throw()*/
        {
            NCorbaHelpers::CObjectName res;
            if(npu::Match(req->GetPathInfo(), npu::ObjName(res, 3, "hosts/")))
                return res.ToString();
            return "";
        }

    private:

        NCorbaHelpers::PReactor m_reactor;
        NCorbaHelpers::WPContainer m_container;
        const npu::PRigthsChecker m_rightsChecker;

        DECLARE_LOGGER_HOLDER;
        std::auto_ptr<NMMSS::CMMCodingInitialization> m_mmcoding;

        struct SSnapshotContextKey
        {
            std::string m_ep;
            uint32_t m_width;
            uint32_t m_height;

            SSnapshotContextKey(const std::string& ep, uint32_t width, uint32_t height)
                : m_ep(ep)
                , m_width(width)
                , m_height(height)
            {}

            bool operator<(const SSnapshotContextKey& rhs) const
            {
                int res = strcmp(m_ep.c_str(), rhs.m_ep.c_str());
                if (0 != res)
                    return res < 0 ? true : false;
                if (m_width != rhs.m_width)
                    return m_width <= rhs.m_width;
                if (m_height != rhs.m_height)
                    return m_height <= rhs.m_height;
                return false;
            }
        };
        typedef std::map<SSnapshotContextKey, PSnapshotContext> TContextMap;
        TContextMap m_contexts;

        boost::mutex m_mutex;
        bool m_destroying;
    };

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

namespace NHttp
{
    IServlet* CreateLiveSnapshotServlet(NCorbaHelpers::IContainer *c, const npu::PRigthsChecker rc)
    {
        return new CLiveSnapshotServlet(c, rc);
    }
}
