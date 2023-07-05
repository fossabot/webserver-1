#include <mutex>
#include <atomic>
#include <condition_variable>

#include <ace/OS.h>
#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/asio.hpp>
#include <boost/format.hpp>

#include <ConnectionResource.h>
#include <PullStylePinsBaseImpl.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Reactor.h>
#include <PtimeFromQword.h>
#include "DataSink.h"
#include "SendContext.h"
#include "CommonUtility.h"
#include "../MediaType.h"
#include "../MMCoding/Transforms.h"

using namespace NPluginHelpers;
namespace bpt = boost::posix_time;

namespace
{
    const std::uint32_t SAMPLE_MIN_THRESHOLD = 400;
    const std::uint32_t SAMPLE_MAX_THRESHOLD = 500;

    const std::uint32_t UNKNOWN_STREAM_TYPE = MMSS_MAKEFOURCC('D', 'E', 'A', 'D');

    static boost::mutex g_sinkCountMutex;
    void nopSinkCountHandler(int){}
    static boost::function1<void, int> g_sinkCountHandler=boost::function1<void, int>(nopSinkCountHandler);


class SinkCounter
{
public:
    SinkCounter()
    {
        boost::mutex::scoped_lock lock(g_sinkCountMutex);
        g_sinkCountHandler(1);
    }
    ~SinkCounter()
    {
        boost::mutex::scoped_lock lock(g_sinkCountMutex);
        g_sinkCountHandler(-1);
    }
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CDummySink : public NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    CDummySink(DECLARE_LOGGER_ARG)
    {
        INIT_LOGGER_HOLDER;
    }

    ~CDummySink()
    {
        _log_ << "CDummySink dtor";
    }

    virtual void Receive(NMMSS::ISample*)
    {
        RequestNextSamples(1);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 1, false);
    }
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class COneShotSink : public NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    COneShotSink(DECLARE_LOGGER_ARG, const TOnSampleHandler& handler)
        : m_handler(handler)
    {
        INIT_LOGGER_HOLDER;
    }

    virtual void Receive(NMMSS::ISample* sample)
    {
        if (m_handler)
            m_handler(sample);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 1, false);
    }
private:
    TOnSampleHandler m_handler;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CPullStyleSink : public NMMSS::CPullStyleSinkBase
{
public:
    explicit CPullStyleSink(const TOnSampleHandler &handler)
        : m_handler(handler)
    {}

    virtual void Receive(NMMSS::ISample *sample)
    {
        if(m_handler)
            m_handler(sample);
        RequestNextSamples(1);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    { 
        requestNextSamples(lock, 64, false);
    }
private:
    TOnSampleHandler m_handler;
    SinkCounter m__counter;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CWrapperSink : public NMMSS::CPullStyleSinkBase
{
public:
    CWrapperSink(NMMSS::IPullStyleSink* sink, const TOnDisconnected dh)
        : m_sink(sink, NCorbaHelpers::TakeOwnership())
        , m_onDisconnected(dh)
    {}

    ~CWrapperSink()
    {
        //_log_ << "CWrapperSink dtor";
        //m_sink->Release();
    }

    virtual void Receive(NMMSS::ISample *sample)
    {
        if (m_sink)
            m_sink->Receive(sample);
        RequestNextSamples(1);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 64, false);
    }
    virtual void onDisconnected(boost::mutex::scoped_lock& lock)
    {
        if (m_onDisconnected)
            m_onDisconnected();
    }
private:
    NMMSS::PPullStyleSink m_sink;
    TOnDisconnected m_onDisconnected;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CThresholdSink : public IFeedbackSink
                     , public NMMSS::CPullStyleSinkBasePureRefcounted
                     , public NCorbaHelpers::CWeakReferableImpl
{
    DECLARE_LOGGER_HOLDER;
public:
    explicit CThresholdSink(DECLARE_LOGGER_ARG, const TOnDisconnected &onDisconnected, const TOnSampleHandler &handler)
        : m_handler(handler)
        , m_paused(false)
        , m_onDisconnected(onDisconnected)
    {
        INIT_LOGGER_HOLDER;
    }

    ~CThresholdSink()
    {
        _log_ << "CThresholdSink dtor";
    }

    virtual void Receive(NMMSS::ISample *sample)
    {
        if (m_handler)
            m_handler(sample);
        if (!m_paused)
            RequestNextSamples(1);
    }

    virtual void EnoughData()
    {
        m_paused = true;
    }

    virtual void NeedMoreData()
    {
        m_paused = false;
        RequestNextSamples(1);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 64, false);
    }
    virtual void onDisconnected(boost::mutex::scoped_lock& lock)
    {
        m_onDisconnected();
    }
private:
    TOnSampleHandler m_handler;
    SinkCounter m__counter;
    std::atomic<bool> m_paused;
    NPluginHelpers::TOnDisconnected m_onDisconnected;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CAudioSink : public NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    explicit CAudioSink(DECLARE_LOGGER_ARG, const TOnSampleHandler &handler)
        : m_decoder(NMMSS::CreateAudioDecoderPullFilter(GET_LOGGER_PTR))
        , m_encoder(NMMSS::CreateAudioEncoderFilter_G711_U(GET_LOGGER_PTR))
        , m_sink(new CPullStyleSink(handler))
    {
        INIT_LOGGER_HOLDER;

        m_decoder2encoder = NMMSS::CConnectionResource(m_decoder->GetSource(), m_encoder->GetSink(), GET_LOGGER_PTR);
        m_encoder2sink = NMMSS::CConnectionResource(m_encoder->GetSource(), m_sink.Get(), GET_LOGGER_PTR);
    }

    virtual void Receive(NMMSS::ISample *sample)
    {
        m_decoder->GetSink()->Receive(sample);
        RequestNextSamples(1);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 64, false);
    }

    NMMSS::PPullFilter m_decoder;
    NMMSS::PPullFilter m_encoder;
    NMMSS::PPullStyleSink m_sink;
    NMMSS::CConnectionResource m_decoder2encoder;
    NMMSS::CConnectionResource m_encoder2sink;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CLimitedSink
    :   public virtual NMMSS::IPullStyleSink
    ,   public virtual NCorbaHelpers::CRefcountedImpl
{
public:
    CLimitedSink(const TOnSampleHandler &onSample, const TOnCompleteHandler &onComplete,
        const bpt::ptime &begin, const bpt::ptime &end, size_t limit)
        :   m_onSample(onSample)
        ,   m_onComplete(onComplete)
        ,   m_begin(begin)
        ,   m_end(end)
        ,   m_reverse(m_begin > m_end)
        ,   m_limit(limit)
        ,   m_counter(0)
        ,   m_reactor(NCorbaHelpers::GetReactorInstanceShared())
    {}

    ~CLimitedSink()
    {
    }

    virtual void Receive(NMMSS::ISample *s)
    {
        if(!IsEOF(s))
        {
            const bpt::ptime ts = NMMSS::PtimeFromQword(s->Header().dtTimeBegin);
            if(IsValid(ts) && m_counter < m_limit)
            {
                if (!IsDiscard(ts))
                {
                    ++m_counter;
                    if(m_onSample)
                        m_onSample(s);
                }
                Request();
                return;
            }
        }
        if (m_onComplete) m_onComplete(m_counter >= m_limit);
    }

    virtual void OnConnected(TConnection *conn)
    {
        NMMSS::PPullStyleSource src = NMMSS::PPullStyleSource(conn->GetOtherSide(), NCorbaHelpers::ShareOwnership());
        boost::mutex::scoped_lock lock(m_mutex);
        std::swap(src, m_source);
        Request(lock);
    }

    virtual void OnDisconnected(TConnection *conn)
    {
        NMMSS::PPullStyleSource stale;
        boost::mutex::scoped_lock lock(m_mutex);
        std::swap(stale, m_source);
    }

private:
    bool IsValid(const bpt::ptime &ts) const
    {
        return m_reverse ? (m_end<=ts && ts<=m_begin) : (ts<m_end);
    }

    bool IsDiscard(const bpt::ptime &ts) const
    {
        return m_reverse ? false : (m_begin>ts);
    }

    static bool IsEOF(NMMSS::ISample *s)
    {
        return NMMSS::NMediaType::CheckMediaType<
            NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header());
    }

private:
    void Request()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        Request(lock);
    }

    void Request(const boost::mutex::scoped_lock &)
    {
        if(m_source)
            m_reactor->GetIO().post(boost::bind(&CLimitedSink::DoRequest, m_source));
    }

    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }

    static void DoRequest(NMMSS::PPullStyleSource src)
    {
        try
        {
            src->Request(1);
        }
        catch(const std::exception &) {}
    }

private:
    const TOnSampleHandler m_onSample;
    const TOnCompleteHandler m_onComplete;
    const bpt::ptime m_begin;
    const bpt::ptime m_end;
    const bool m_reverse;
    const size_t m_limit;
    size_t m_counter;
    NCorbaHelpers::PReactor m_reactor;

    NMMSS::PPullStyleSource m_source;
    boost::mutex m_mutex;
    SinkCounter m__counter;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CFlowControlSink : public NMMSS::CPullStyleSinkBase
{
private:
    static const size_t BUFFER_SIZE = 2;

private:
    struct SSharedCtx
    {
        NHttp::PResponse                       Response;
        NPluginHelpers::TOnDisconnected        OnDisconnected;
        NPluginHelpers::TOnSampleHandler       OnSample;
        NCorbaHelpers::PReactor                Reactor;
        boost::circular_buffer<NMMSS::PSample> Buffer;
        bool                                   Writing;
        boost::mutex                           Mutex;

        SSharedCtx(NHttp::PResponse r, const NPluginHelpers::TOnDisconnected &h,
            const NPluginHelpers::TOnSampleHandler &s)
            :   Response(r)
            ,   OnDisconnected(h)
            ,   OnSample(s)
            ,   Reactor(NCorbaHelpers::GetReactorInstanceShared())
            ,   Buffer(BUFFER_SIZE)
            ,   Writing(false)
        {}
    };
    typedef boost::shared_ptr<SSharedCtx> PSharedCtx;

public:
    CFlowControlSink(NHttp::PResponse resp, const TOnDisconnected &onDisconnected, const TOnSampleHandler &onSample)
        :   m_ctx(new SSharedCtx(resp, onDisconnected, onSample))
    {
    }

    ~CFlowControlSink()
    {
    }

private:
    virtual void Receive(NMMSS::ISample *sample)
    {
        if(m_ctx->OnSample)
            m_ctx->OnSample(sample);

        NMMSS::PSample s(sample, NCorbaHelpers::ShareOwnership());
        {
            boost::mutex::scoped_lock lock(m_ctx->Mutex);
            if(!m_ctx->Writing)
            {
                m_ctx->Writing = true;
                Send(m_ctx, s);
            }
            else
            {
                m_ctx->Buffer.push_back(s);
            }
        }
        RequestNextSamples(1);
    }

    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 1, false);
    }
    virtual void onDisconnected(boost::mutex::scoped_lock& lock)
    {
        m_ctx->OnDisconnected();
    }

private:
    static void Send(PSharedCtx shared, NMMSS::PSample sample)
    {
        NContext::PSendContext ctx(NContext::CreateMultipartContext(
            shared->Response, sample.Get(), boost::bind(&CFlowControlSink::Done, shared, _1)));
        ctx->ScheduleWrite();
    }

    static void Done(PSharedCtx shared, boost::system::error_code error)
    {
        if (error)
        {
            shared->OnDisconnected();
            return;
        }

        NMMSS::PSample s;
        {
            boost::mutex::scoped_lock lock(shared->Mutex);
            if(shared->Buffer.empty() || error)
            {
                shared->Writing = false;
                return;
            }
            s = shared->Buffer.front();
            shared->Buffer.pop_front();
        }
        shared->Reactor->GetIO().post(boost::bind(&CFlowControlSink::Send, shared, s));
    }
private:
    PSharedCtx m_ctx;
    SinkCounter m__counter;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CTimeLimitedSink : public virtual NMMSS::CPullStyleSinkBase
{
public:
    CTimeLimitedSink(DECLARE_LOGGER_ARG, NHttp::PResponse resp, const TOnDisconnected& onDisconnected, const std::string& stopTime)
        : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
        , m_response(resp)
        , m_disconnected(onDisconnected)
        , m_stopTime(NMMSS::PtimeToQword(boost::posix_time::from_iso_string(stopTime)))
        , m_writing(false)
    {
        INIT_LOGGER_HOLDER;
    }

    ~CTimeLimitedSink()
    {
        _log_ << "CTimeLimitedSink dtor";
    }

private:
    virtual void Receive(NMMSS::ISample* sample)
    {
        NMMSS::PSample s(sample, NCorbaHelpers::ShareOwnership());
        if (NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header()) || (s->Header().dtTimeBegin > m_stopTime))
        {
            notifyOwner();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_writing)
            {
                m_writing = true;
                send(s);
            }
            else
            {
                m_samples.push_back(s);
            }
        }
        RequestNextSamples(1);
    }

    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 1, false);
    }
    virtual void onDisconnected(boost::mutex::scoped_lock& lock)
    {
    }

private:
    void send(NMMSS::PSample sample)
    {
        NCorbaHelpers::CAutoPtr<CTimeLimitedSink> pThis(this, NCorbaHelpers::ShareOwnership());
        NContext::PSendContext ctx(NContext::CreateSampleContext(
            m_response, sample.Get(), boost::bind(&CTimeLimitedSink::done, pThis, _1)));
        ctx->ScheduleWrite();
    }

    void done(boost::system::error_code error)
    {
        if (error)
        {
            notifyOwner();
            return;
        }

        NMMSS::PSample s;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_samples.empty())
            {
                m_writing = false;
                return;
            }
            s = m_samples.front();
            m_samples.pop_front();

            NCorbaHelpers::CAutoPtr<CTimeLimitedSink> pThis(this, NCorbaHelpers::ShareOwnership());
            m_reactor->GetIO().post(boost::bind(&CTimeLimitedSink::send, pThis, s));
        }
    }

    void notifyOwner()
    {
        NPluginHelpers::TOnDisconnected disconnected;
        {
            std::lock_guard<std::mutex> lock(m_stopMutex);
            if (m_disconnected)
            {
                disconnected = m_disconnected;
                m_disconnected.clear();
            }
        }

        if (disconnected)
            disconnected();
    }

private:
    DECLARE_LOGGER_HOLDER;
    NCorbaHelpers::PReactor m_reactor;
    NHttp::PResponse m_response;
    NPluginHelpers::TOnDisconnected m_disconnected;
    std::uint64_t m_stopTime;

    std::mutex m_stopMutex;
    std::mutex m_mutex;
    bool m_writing;
    std::deque<NMMSS::PSample> m_samples;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CGstreamerSink : public virtual IRequestSink
    , public NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    CGstreamerSink(DECLARE_LOGGER_ARG, const TOnDisconnected& cb)
        : m_callback(cb)
        , m_haveInitSample(false)
        , m_requester(nullptr)
        , m_dataTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
        , m_dataWaitTimeout(boost::posix_time::milliseconds(60000))
    {
        INIT_LOGGER_HOLDER;
    }

    ~CGstreamerSink()
    {
        _log_ << "GstreamerSink dtor";
    }

    void Stop()
    {
        _log_ << "Gstreamer sink stopped";
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            if (m_pushHandler)
                m_pushHandler(NMMSS::PSample{});
        }
    }

    void SetRequester(NPluginHelpers::IPullStyleSinkWithRequest* requester) override
    {
        m_requester = requester;
    }

    void SetContCallback(TContHandler ch, TErrorHandler eh, IRequestSink* sink) override
    {
        std::unique_lock<std::mutex> lock(m_initMutex);
        if (m_initSample)
        {
            sink->SetInitCallback(std::bind(ch, m_initSample, std::placeholders::_1), eh);
        }
        else
        {
            m_contHandler = ch;
            m_contSink = PRequestSink(sink, NCorbaHelpers::ShareOwnership());
            m_errorHandler = eh;
        }
    }

    void SetInitCallback(TPushHandler pushHandler, TErrorHandler eh) override
    {
        std::unique_lock<std::mutex> lock(m_initMutex);
        if (m_haveInitSample)
        {
            pushHandler(m_initSample);
        }
        else
        {
            m_initHandler = pushHandler;
            m_errorHandler = eh;
        }
    }

    void SetPushCallback(TPushHandler pushHandler) override
    {
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            m_pushHandler = pushHandler;
        }
        
        NMMSS::PSample initSample;
        {
            std::unique_lock<std::mutex> lock(m_initMutex);
            initSample.swap(m_initSample);
        }

        if (initSample)
        {
            pushHandler(initSample);
            DoRequest(1);
        }
    }

private:
    virtual void Receive(NMMSS::ISample* sample)
    {
        NMMSS::PSample s(sample, NCorbaHelpers::ShareOwnership());

        std::call_once(m_initFlag, [this, s]() mutable
            {
                std::unique_lock<std::mutex> lock(m_initMutex);
                if (m_contHandler)
                {
                    if (NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header()))
                        s.Reset();

                    m_contSink->SetInitCallback(std::bind(m_contHandler, s, std::placeholders::_1), m_errorHandler);

                    m_contHandler = nullptr;
                    m_contSink.Reset();
                }
                else if (m_initHandler)
                {
                    m_initHandler(s);
                    m_initHandler = nullptr;
                }

                m_initSample = s;
                m_haveInitSample = true;
            });

        TPushHandler pushHandler;
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            pushHandler = m_pushHandler;
        }

        if (pushHandler)
        {
            pushHandler(s);

            DoRequest(1);
        }
    }

    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        _log_ << "Gstreamer sink connected";
        requestNextSamples(lock, 64, false);
        setDataTimer();
    }

    virtual void onDisconnected(boost::mutex::scoped_lock& lock)
    {
        lock.unlock();

        _log_ << "Gstreamer sink disconnected";

        m_callback();
    }

    void DoRequest(unsigned int count)
    {
        if (nullptr != m_requester)
        {
            m_requester->Request(count);
        }

        RequestNextSamples(count);
    }

    void setDataTimer()
    {
        NCorbaHelpers::CAutoPtr<CGstreamerSink> pThis(this, NCorbaHelpers::ShareOwnership());
        m_dataTimer.expires_from_now(m_dataWaitTimeout);
        m_dataTimer.async_wait(std::bind(&CGstreamerSink::handle_timeout,
            pThis, std::placeholders::_1));
    }

    void handle_timeout(const boost::system::error_code& error)
    {
        std::unique_lock<std::mutex> lock(m_initMutex);
        if (!m_haveInitSample)
        {
            _log_ << "Finish processing on timeout";
            TErrorHandler errorHandler = nullptr;
            if (m_contHandler)
            {
                _log_ << "Do continuation on timeout";
                m_contSink->SetInitCallback(std::bind(m_contHandler, NMMSS::PSample{}, std::placeholders::_1), m_errorHandler);

                m_contHandler = nullptr;
                m_contSink.Reset();

            }
            else
            {
                errorHandler = m_errorHandler;
                m_errorHandler = nullptr;
            }

            m_haveInitSample = true;
            lock.unlock();

            if (errorHandler)
                errorHandler();
        }
    }

private:
    TOnDisconnected m_callback;

    std::mutex m_dataMutex;

    std::mutex m_initMutex;
    TPushHandler m_initHandler;
    TContHandler m_contHandler;
    TErrorHandler m_errorHandler;
    bool m_haveInitSample;
    PRequestSink m_contSink;

    NPluginHelpers::IPullStyleSinkWithRequest* m_requester;

    TPushHandler m_pushHandler;

    std::once_flag m_initFlag;
    NMMSS::PSample m_initSample;

    boost::asio::deadline_timer m_dataTimer;
    boost::posix_time::time_duration m_dataWaitTimeout;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CGstreamerRTSPSink : public virtual IRTSPRequestSink
    , public NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    CGstreamerRTSPSink(DECLARE_LOGGER_ARG, const TOnDisconnected& cb, std::uint32_t timeout)
        : m_callback(cb)
        , m_peekTimeout(std::chrono::milliseconds(500))
        , m_timeout(std::chrono::milliseconds(timeout))
        , m_disconnected(false)
        , m_requester(nullptr)
    {
        INIT_LOGGER_HOLDER;
    }

    ~CGstreamerRTSPSink()
    {
        _log_ << "CGstreamerRTSPSink dtor";
    }

    NMMSS::PSample GetSample()
    {
        NMMSS::PSample s;

        bool status = false;

        std::unique_lock<std::mutex> lock(m_dataMutex);
        if (status = m_condition.wait_for(lock, m_timeout, [this]() { return !m_samples.empty() || m_disconnected; }), status)
        {
            if (!m_disconnected)
            {
                s = m_samples.front();
                m_samples.pop_front();
                lock.unlock();

                DoRequest(1);
            }
            else
                _log_ << "CGstreamer RTSP sink in disconnected state";
        }
        return s;
    }

    NMMSS::PSample PeekSample()
    {
        NMMSS::PSample sample;

        bool status = false;
        std::unique_lock<std::mutex> lock(m_dataMutex);
        status = m_condition.wait_for(lock, m_peekTimeout, [this]() { return !m_samples.empty(); });

        if (status && !m_disconnected)
            sample = m_samples.front();

        return sample;
    }

    void Stop()
    {
        std::unique_lock<std::mutex> lock(m_dataMutex);
        if (!m_disconnected)
        {
            m_disconnected = true;
            lock.unlock();
        }
        m_condition.notify_all();
    }

    void SetRequester(NPluginHelpers::IPullStyleSinkWithRequest* requester) override
    {
        m_requester = requester;
    }

    void ClearBuffer()
    {
        std::unique_lock<std::mutex> lock(m_dataMutex);

        if (!m_disconnected)
            m_samples.clear();
        lock.unlock();

        DoRequest(1);
    }

private:
    virtual void Receive(NMMSS::ISample *sample)
    {
        NMMSS::PSample s(sample, NCorbaHelpers::ShareOwnership());
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            m_samples.push_back(s);
        }
        m_condition.notify_all();
    }

    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 64, false);
    }
    virtual void onDisconnected(boost::mutex::scoped_lock& lock)
    {
        lock.unlock();

        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            m_disconnected = true;
            lock.unlock();

        }
        m_condition.notify_all();
        m_callback();
    }

    void DoRequest(unsigned int count)
    {
        if (nullptr != m_requester)
        {
            m_requester->Request(count);
        }

        RequestNextSamples(count);
    }

private:
    TOnDisconnected m_callback;
    std::chrono::duration<std::uint32_t, std::milli> m_peekTimeout;
    std::chrono::duration<std::uint32_t, std::milli> m_timeout;

    std::mutex m_dataMutex;
    std::condition_variable m_condition;
    std::deque<NMMSS::PSample> m_samples;

    bool m_disconnected;

    NPluginHelpers::IPullStyleSinkWithRequest* m_requester;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CReusableSink : public virtual IReusableSink
                    , public virtual NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    CReusableSink(DECLARE_LOGGER_ARG, TOnTimedSampleHandler sh, TOnCompleteHandler ch)
        : m_sampleHandler(sh)
        , m_completeHandler(ch)
        , m_sent(true)
    {
        INIT_LOGGER_HOLDER;
    }

    ~CReusableSink()
    {
        _this_log_ << "CReusableSink dtor";
    }

    virtual void Receive(NMMSS::ISample *sample)
    {
        if (m_waitingTime <= NMMSS::PtimeFromQword(sample->Header().dtTimeBegin))
        {
            if (!m_sent.exchange(true, std::memory_order_relaxed))
            {
                m_sampleHandler(m_waitingTime, sample);
                m_completeHandler(false);
            }
        }

        RequestNextSamples(1);
    }

    void WakeUp(const boost::posix_time::ptime& sampleTime) override
    {
        m_waitingTime = sampleTime;
        m_sent.exchange(false, std::memory_order_relaxed);     
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 1, false);
    }

    TOnTimedSampleHandler m_sampleHandler;
    TOnCompleteHandler m_completeHandler;
    std::atomic<bool> m_sent;
    boost::posix_time::ptime m_waitingTime;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CBatchSink : public virtual IBatchSink,
                   public virtual NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    CBatchSink(DECLARE_LOGGER_ARG, const std::string& streamId, int width, int height, bool crop, float crop_x, float crop_y, float crop_width, float crop_height, TOnTimedSampleHandler sh, TOnCompleteHandler ch)
        : m_decoder(NMMSS::CreateVideoDecoderPullFilter(GET_LOGGER_PTR))
        , m_scaler(NMMSS::CreateSizeFilter(GET_LOGGER_PTR, width, height, crop, crop_x, crop_y, crop_width, crop_height))
        , m_encoder(NMMSS::CreateMJPEGEncoderFilter(GET_LOGGER_PTR))
        , m_sink(CreateReusableSink(GET_LOGGER_PTR, sh, ch))
        , m_fragmentId(0)
        , m_processing(false)
        , m_streamId(streamId)
        , m_converterInitialized(false)
        , m_videoStreamType(UNKNOWN_STREAM_TYPE)
        , m_width(width)
        , m_height(height)
        , m_crop(crop)
        , m_crop_x(crop_x)
        , m_crop_y(crop_y)
        , m_crop_width(crop_width)
        , m_crop_height(crop_height)
    {
        INIT_LOGGER_HOLDER;

        m_decoder->AddUnfilteredFrameType(
            NMMSS::NMediaType::Video::ID,
            NMMSS::NMediaType::Video::fccJPEG::ID);

        m_decoder2scaler = NMMSS::GetConnectionBroker()->SetConnection(m_decoder->GetSource(), m_scaler->GetSink(), GET_LOGGER_PTR);
        m_scaler2encoder = NMMSS::GetConnectionBroker()->SetConnection(m_scaler->GetSource(), m_encoder->GetSink(), GET_LOGGER_PTR);
        m_encoder2sink = NMMSS::GetConnectionBroker()->SetConnection(m_encoder->GetSource(), m_sink.Get(), GET_LOGGER_PTR);
    }

    ~CBatchSink()
    {
        _this_log_ << "CBatchSink dtor";
        resetConnections();
    }

    virtual void Receive(NMMSS::ISample *sample)
    {
        NMMSS::SessionIdExHeader* ext = NMMSS::FindExtensionHeader<NMMSS::SessionIdExHeader>(sample);

        if (nullptr != ext)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (ext->SessionId == m_fragmentId)
            {
                NMMSS::PSample s(sample, NCorbaHelpers::ShareOwnership());
                m_samplesToSend.push_back(s);

                if (!m_converterInitialized.exchange(true, std::memory_order_relaxed))
                {
                    std::uint32_t width = 0, height = 0;
                    auto sizeGetter = [&width, &height](const NMMSS::NMediaType::Video::SCodedHeader* subheader, const uint8_t*)
                    {
                        width = subheader->nCodedWidth;
                        height = subheader->nCodedHeight;
                    };
                    NMMSS::NMediaType::ProcessSampleOfSubtype<NMMSS::NMediaType::Video::SCodedHeader>(sample, sizeGetter);

                    if ((UNKNOWN_STREAM_TYPE != m_videoStreamType) &&
                        ((sample->Header().nSubtype != m_videoStreamType) || (width != m_codedWidth) || (height != m_codedHeight))
                       )
                    {
                        m_processing = true;
                        NCorbaHelpers::CAutoPtr<CBatchSink> pThis(this, NCorbaHelpers::ShareOwnership());
                        NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(
                            std::bind(&CBatchSink::reinitProcessing, pThis, s));                      
                    }

                    m_videoStreamType = sample->Header().nSubtype;
                    m_codedWidth = width;
                    m_codedHeight = height;
                }
               
                lock.unlock();

                if (!m_processing.exchange(true, std::memory_order_relaxed))
                {
                    NCorbaHelpers::CAutoPtr<CBatchSink> pThis(this, NCorbaHelpers::ShareOwnership());
                    NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(
                        std::bind(&CBatchSink::processSample, pThis));
                }
            }
        }
    }

    void SetFragmentId(std::uint32_t fid, const boost::posix_time::ptime& sampleTime) override
    {
        _this_dbg_ << "Stream " << m_streamId << " set to fragment " << fid;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_fragmentId = fid;
            m_samplesToSend.clear();
            m_converterInitialized = false;
        }

        m_sink->WakeUp(sampleTime);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        requestNextSamples(lock, 64, false);
    }
    void processSample()
    {
        NMMSS::PSample sample;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_samplesToSend.empty())
            {
                sample = m_samplesToSend.front();
                m_samplesToSend.pop_front();
            }
            else
                m_processing = false;
        }

        if (sample)
        {
            m_decoder->GetSink()->Receive(sample.Get());
            RequestNextSamples(1);

            NCorbaHelpers::CAutoPtr<CBatchSink> pThis(this, NCorbaHelpers::ShareOwnership());
            NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(
                std::bind(&CBatchSink::processSample, pThis));
        }
    }
    void reinitProcessing(NMMSS::PSample s)
    {
        _this_dbg_ << "Reinit (de)coding chain";

        resetConnections();

        m_decoder = NMMSS::CreateVideoDecoderPullFilter(GET_LOGGER_PTR);
        m_scaler = NMMSS::CreateSizeFilter(GET_LOGGER_PTR, m_width, m_height, m_crop, m_crop_x, m_crop_y, m_crop_width, m_crop_height);
        m_encoder = NMMSS::CreateMJPEGEncoderFilter(GET_LOGGER_PTR);

        m_decoder->AddUnfilteredFrameType(
            NMMSS::NMediaType::Video::ID,
            NMMSS::NMediaType::Video::fccJPEG::ID);

        m_decoder2scaler = NMMSS::GetConnectionBroker()->SetConnection(m_decoder->GetSource(), m_scaler->GetSink(), GET_LOGGER_PTR);
        m_scaler2encoder = NMMSS::GetConnectionBroker()->SetConnection(m_scaler->GetSource(), m_encoder->GetSink(), GET_LOGGER_PTR);
        m_encoder2sink = NMMSS::GetConnectionBroker()->SetConnection(m_encoder->GetSource(), m_sink.Get(), GET_LOGGER_PTR);

        NCorbaHelpers::CAutoPtr<CBatchSink> pThis(this, NCorbaHelpers::ShareOwnership());
        NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(
            std::bind(&CBatchSink::processSample, pThis));
    }
    void resetConnections()
    {
        if (nullptr != m_encoder2sink)
        {
            NMMSS::GetConnectionBroker()->DestroyConnection(m_encoder2sink);
            m_encoder2sink = nullptr;
        }

        if (nullptr != m_scaler2encoder)
        {
            NMMSS::GetConnectionBroker()->DestroyConnection(m_scaler2encoder);
            m_encoder2sink = nullptr;
        }

        if (nullptr != m_decoder2scaler)
        {
            NMMSS::GetConnectionBroker()->DestroyConnection(m_decoder2scaler);
            m_encoder2sink = nullptr;
        }
    }

    NMMSS::PPullFilter m_decoder;
    NMMSS::PPullFilter m_scaler;
    NMMSS::PPullFilter m_encoder;
    PReusableSink m_sink;
    NMMSS::IConnectionBase* m_decoder2scaler;
    NMMSS::IConnectionBase* m_scaler2encoder;
    NMMSS::IConnectionBase* m_encoder2sink;

    std::mutex m_mutex;
    std::uint32_t m_fragmentId;

    std::atomic<bool> m_processing;
    std::deque<NMMSS::PSample> m_samplesToSend;

    const std::string m_streamId;

    std::atomic_bool m_converterInitialized;
    std::uint32_t m_videoStreamType;
    std::uint32_t m_codedWidth, m_codedHeight;

    int m_width, m_height;
    bool m_crop;
    float m_crop_x, m_crop_y, m_crop_width, m_crop_height;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class CJPEGSink : public virtual NPluginHelpers::IPullStyleSinkWithRequest
    , virtual NMMSS::CPullStyleSinkBase
{
    DECLARE_LOGGER_HOLDER;
public:
    CJPEGSink(DECLARE_LOGGER_ARG, int width, int height, NPluginHelpers::IRequestSink* sink)
        : m_decoder(NMMSS::CreateVideoDecoderPullFilter(GET_LOGGER_PTR))
        , m_scaler(NMMSS::CreateSizeFilter(GET_LOGGER_PTR, width, height))
        , m_encoder(NMMSS::CreateMJPEGEncoderFilter(GET_LOGGER_PTR))
        , m_sink(sink, NCorbaHelpers::ShareOwnership())
    {
        INIT_LOGGER_HOLDER;

        m_decoder->AddUnfilteredFrameType(
            NMMSS::NMediaType::Video::ID,
            NMMSS::NMediaType::Video::fccJPEG::ID);

        m_decoder2scaler = NMMSS::CConnectionResource(m_decoder->GetSource(), m_scaler->GetSink(), GET_LOGGER_PTR);
        m_scaler2encoder = NMMSS::CConnectionResource(m_scaler->GetSource(), m_encoder->GetSink(), GET_LOGGER_PTR);
        m_encoder2sink = NMMSS::CConnectionResource(m_encoder->GetSource(), m_sink.Get(), GET_LOGGER_PTR);
    }

    ~CJPEGSink()
    {
        _this_log_ << "CJPEGSink dtor";
    }

    virtual void Receive(NMMSS::ISample *sample)
    {
        m_decoder->GetSink()->Receive(sample);
    }

    void Request(unsigned int count) override
    {
        RequestNextSamples(count);
    }

private:
    virtual NMMSS::SAllocatorRequirements GetAllocatorRequirements()
    {
        static NMMSS::SAllocatorRequirements req(0);
        return req;
    }
    virtual void onConnected(boost::mutex::scoped_lock& lock)
    {
        m_sink->SetRequester(this);
        requestNextSamples(lock, 64, false);
    }

    NMMSS::PPullFilter m_decoder;
    NMMSS::PPullFilter m_scaler;
    NMMSS::PPullFilter m_encoder;
    NPluginHelpers::PRequestSink m_sink;
    NMMSS::CConnectionResource m_decoder2scaler;
    NMMSS::CConnectionResource m_scaler2encoder;
    NMMSS::CConnectionResource m_encoder2sink;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}

namespace NPluginHelpers
{
    NMMSS::IPullStyleSink* CreateDummySink(DECLARE_LOGGER_ARG)
    {
        return new CDummySink(GET_LOGGER_PTR);
    }

    NMMSS::IPullStyleSink* CreateOneShotSink(DECLARE_LOGGER_ARG, const TOnSampleHandler& h)
    {
        return new COneShotSink(GET_LOGGER_PTR, h);
    }

    NMMSS::IPullStyleSink* CreateSink(const TOnSampleHandler &handler)
    {
        return new CPullStyleSink(handler);
    }

    NMMSS::IPullStyleSink* CreateWrapperSink(NMMSS::IPullStyleSink* sink, const TOnDisconnected dh)
    {
        return new CWrapperSink(sink, dh);
    }

    IFeedbackSink* CreateThresholdSink(DECLARE_LOGGER_ARG, const TOnDisconnected &onDisconnected, const TOnSampleHandler &handler)
    {
        return new CThresholdSink(GET_LOGGER_PTR, onDisconnected, handler);
    }

    NMMSS::IPullStyleSink* CreateAudioSink(DECLARE_LOGGER_ARG, const TOnSampleHandler& handler)
    {
        return new CAudioSink(GET_LOGGER_PTR, handler);
    }

    NMMSS::IPullStyleSink* CreateLimitedSink(
        const TOnSampleHandler &onSample,
        const TOnCompleteHandler &onComplete,
        const bpt::ptime &begin,
        const bpt::ptime &end,
        size_t samplesCount)
    {
        return new CLimitedSink(onSample, onComplete, begin, end, samplesCount);
    }

    NMMSS::IPullStyleSink* CreateFlowControlSink(NHttp::PResponse resp,
        const TOnDisconnected &onDisconnected, const TOnSampleHandler &onSample)
    {
        return new CFlowControlSink(resp, onDisconnected, onSample);
    }

    void SetSinkCounterHandler(boost::function1<void, int> handler)
    {
        boost::mutex::scoped_lock lock(g_sinkCountMutex);
        g_sinkCountHandler=handler;
    }

    IRequestSink* CreateGstreamerSink(DECLARE_LOGGER_ARG, const TOnDisconnected& onDisconnected)
    {
        return new CGstreamerSink(GET_LOGGER_PTR, onDisconnected);
    }

    IRTSPRequestSink* CreateRTSPGstreamerSink(DECLARE_LOGGER_ARG, const TOnDisconnected &onDisconnected, std::uint32_t timeout)
    {
        return new CGstreamerRTSPSink(GET_LOGGER_PTR, onDisconnected, timeout);
    }

    IReusableSink* CreateReusableSink(DECLARE_LOGGER_ARG, TOnTimedSampleHandler sh, TOnCompleteHandler ch)
    {
        return new CReusableSink(GET_LOGGER_PTR, sh, ch);
    }

    IBatchSink* CreateBatchSink(DECLARE_LOGGER_ARG, const std::string& streamId, int width, int height, bool crop, float crop_x, float crop_y, float crop_width, float crop_height, TOnTimedSampleHandler sh, TOnCompleteHandler ch)
    {
        return new CBatchSink(GET_LOGGER_PTR, streamId, width, height, crop, crop_x, crop_y, crop_width, crop_height,sh, ch);
    }

    NMMSS::IPullStyleSink* CreateJPEGSink(DECLARE_LOGGER_ARG, int width, int height, NPluginHelpers::IRequestSink* sink)
    {
        return new CJPEGSink(GET_LOGGER_PTR, width, height, sink);
    }

    NMMSS::IPullStyleSink* CreateTimeLimitedSink(DECLARE_LOGGER_ARG, NHttp::PResponse resp, const TOnDisconnected& onDisconnected, const std::string& stopTime)
    {
        return new CTimeLimitedSink(GET_LOGGER_PTR, resp, onDisconnected, stopTime);
    }
}
