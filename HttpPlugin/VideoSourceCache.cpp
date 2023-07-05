#include <mutex>
#include <atomic>

#include <boost/asio.hpp>

#include "Platform.h"
#include "Gstreamer.h"
#include "Constants.h"
#include "VideoSourceCache.h"
#include "DataSink.h"
#include "CommonUtility.h"
#include "EventStream.h"

#include "../PtimeFromQword.h"
#include "../ConnectionResource.h"
#include <MMCoding/Transforms.h>
#include <MMTransport/MMTransport.h>
#include <MMTransport/QualityOfService.h>

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/LoadShlib.h>
#include <CorbaHelpers/ResolveServant.h>

#include <json/json.h>

namespace
{
    const size_t BUFFER_THRESHOLD = 1000;
    const char* const SAMPLE_TIMESTAMP = "X-Video-Original-Time: ";

    struct SOrigin : public boost::noncopyable
    {
        NMMSS::PSinkEndpoint endpoint;
        NMMSS::PPullFilter decoder;
        NMMSS::PDistributor distributor;
        NMMSS::CConnectionResource connection;

        SOrigin(DECLARE_LOGGER_ARG, const std::string& sourceAddress, NCorbaHelpers::IContainer* cont, float fps, bool keyFrames)
            : decoder(NMMSS::CreateVideoDecoderPullFilter(GET_LOGGER_PTR))
            , distributor(NMMSS::CreateDistributor(GET_LOGGER_PTR, NMMSS::NAugment::UnbufferedDistributor{}))
            , connection(decoder->GetSource(), distributor->GetSink(), GET_LOGGER_PTR)
        {
            auto qos = NMMSS::MakeQualityOfService (
                MMSS::QoSRequest::StartFrom{ MMSS::QoSRequest::StartFrom::Preroll }
            );
            if (fps > 0)
                NMMSS::SetRequest(qos, MMSS::QoSRequest::FrameRate{ fps, false });
            if (keyFrames)
                NMMSS::SetRequest(qos, MMSS::QoSRequest::OnlyKeyFrames{ true });

            endpoint = NMMSS::CreatePullConnectionByNsref(GET_LOGGER_PTR,
                sourceAddress.c_str(), cont->GetRootNC(), decoder->GetSink(),
                MMSS::EAUTO, &qos);
        }
        ~SOrigin()
        {
            endpoint->Destroy();
            connection = NMMSS::CConnectionResource();
        }
    };
    typedef boost::shared_ptr<SOrigin> POrigin;
    typedef boost::weak_ptr<SOrigin> WPOrigin;

    typedef std::map<std::string, WPOrigin> TOrigins;

    struct SAdapted : public boost::noncopyable
    {
        POrigin origin;
        NMMSS::CConnectionResource origin2scaler;
        NMMSS::PPullFilter scaler;
        NMMSS::CConnectionResource scaler2encoder;
        NMMSS::PPullFilter encoder;
        NMMSS::CConnectionResource encoder2distributor;
        NMMSS::PDistributor distributor;

        SAdapted(DECLARE_LOGGER_ARG, POrigin o, int width, int height, int compression)
            :origin(o)
            , scaler(NMMSS::CreateSizeFilter(GET_LOGGER_PTR, width, height))
            , encoder(NMMSS::CreateMJPEGEncoderFilter(GET_LOGGER_PTR, static_cast<NMMSS::EVideoCodingPreset>(compression)))
            , distributor(NMMSS::CreateDistributor(GET_LOGGER_PTR, NMMSS::NAugment::UnbufferedDistributor{}))
        {
            encoder2distributor = NMMSS::CConnectionResource(encoder->GetSource(), distributor->GetSink(), GET_LOGGER_PTR);
            scaler2encoder = NMMSS::CConnectionResource(scaler->GetSource(), encoder->GetSink(), GET_LOGGER_PTR);
            auto src = NMMSS::PPullStyleSource(origin->distributor->CreateSource());
            origin2scaler = NMMSS::CConnectionResource(src.Get(), scaler->GetSink(), GET_LOGGER_PTR);
        }
        ~SAdapted()
        {
            origin2scaler = NMMSS::CConnectionResource();
            scaler2encoder = NMMSS::CConnectionResource();
            encoder2distributor = NMMSS::CConnectionResource();
        }
    };
    typedef boost::shared_ptr<SAdapted> PAdapted;
    typedef boost::weak_ptr<SAdapted> WPAdapted;

    struct RequestParams
    {
        const std::string address;
        // codec also goes here when it's time
        const int width;
        const int height;
        const int compression;
        const float fps;
        bool operator <(const RequestParams& rhs) const
        {
            if (address<rhs.address)
                return true;
            else if (address == rhs.address)
            {
                if (width<rhs.width)
                    return true;
                else if (width == rhs.width)
                {
                    if (height<rhs.height)
                        return true;
                    else if (height == rhs.height)
                    {
                        if (compression < rhs.compression)
                            return true;
                        else if (compression == rhs.compression)
                            return fps < rhs.fps;
                    }
                }
            }
            return false;
        }
        RequestParams(const std::string& a, int w, int h, int c, float f)
            :address(a)
            , width(w)
            , height(h)
            , compression(c)
            , fps(f)
        {}
    };

    typedef std::map<RequestParams, WPAdapted> TAdapted;

    struct SClientContext : public NHttp::IClientContext
    {
        PAdapted adapted;
        NMMSS::PPullStyleSink sink;
        NMMSS::IConnectionBase* connection;
        SClientContext(PAdapted a, NMMSS::PPullStyleSink s, DECLARE_LOGGER_ARG)
            :adapted(a)
            , sink(s)
        {
            auto src = NMMSS::PPullStyleSource(adapted->distributor->CreateSource());
            connection = NMMSS::GetConnectionBroker()->SetConnection(src.Get(), sink.Get(), GET_LOGGER_PTR);
        }
        ~SClientContext()
        {
            NMMSS::GetConnectionBroker()->DestroyConnection(connection);
        }
    };

    struct SArchiveRawContext : public NHttp::IClientContext
    {
        SArchiveRawContext(const NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials credentials, NHttp::PResponse r, NCorbaHelpers::IContainer* cont, NHttp::PArchiveContext ctx, DECLARE_LOGGER_ARG)
            : m_grpcManager(grpcManager)
            , m_credentials(credentials)
            , m_container(cont)
            , m_response(r)
            , m_ctx(ctx)
        {
            INIT_LOGGER_HOLDER;
        }

        ~SArchiveRawContext()
        {
            _log_ << "Destroy archive raw context";
            if (m_videoEndpoint)
                m_videoEndpoint->Destroy();
        }

        void Init()
        {
            NMMSS::PPullStyleSink sink(NPluginHelpers::CreateTimeLimitedSink(GET_LOGGER_PTR, m_response,
                boost::bind(&SArchiveRawContext::onDisconnected, shared_from_base<SArchiveRawContext>()), m_ctx->stopTime));

            NCorbaHelpers::PContainer cont(m_container);
            if (cont)
            {
                MMSS::StorageEndpoint_var archiveEndpoint =
                    NPluginUtility::ResolveEndoint(m_grpcManager,
                        m_credentials,
                        cont.Get(),
                        m_ctx->videoEndpoint,
                        m_ctx->startTime,
                        axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS,
                        NMMSS::PMF_NONE);

                if (archiveEndpoint)
                {
                    _log_ << "Create sink connection";
                    m_videoEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, archiveEndpoint, sink.Get());
                }
            }
        }

    private:
        void onDisconnected()
        {
            _log_ << "Archive raw context disconnected";
        }

        DECLARE_LOGGER_HOLDER;
        const NWebGrpc::PGrpcManager m_grpcManager;
        const NGrpcHelpers::PCredentials m_credentials;
        NCorbaHelpers::WPContainer m_container;
        NHttp::PResponse m_response;
        NHttp::PArchiveContext m_ctx;
        NMMSS::PSinkEndpoint m_videoEndpoint;
    };

    std::string const HttpPluginModuleName = NGP_MODULE_NAME(HttpPlugin);

    struct SMp4ClientContext : public NHttp::IClientContext
    {
        DECLARE_LOGGER_HOLDER;

        SMp4ClientContext(NCorbaHelpers::IContainer* cont,
                          NHttp::PResponse resp,
                          NHttp::PMMOrigin videoSource,
                          NHttp::PMMOrigin audioSource,
                          NHttp::PMMOrigin textSource,
                          NHttp::IVideoSourceCache::TOnDisconnected f1,
                          DECLARE_LOGGER_ARG,
                          bool keyFrames,
                          NPluginHelpers::EStreamContainer sc,
                          const std::string& filePath)
            : m_response(resp)
            , m_videoOrigin(videoSource)
            , m_audioOrigin(audioSource)
            , m_textOrigin(textSource)
            , m_onDisconnected(f1)
            , m_eventStreamCreated(false)
            , m_muxerConnection(nullptr)
            , m_disconnected(false)
            , m_dataTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_dataWaitTimeout(keyFrames ? boost::posix_time::milliseconds(KEY_SAMPLE_TIMEOUT) : boost::posix_time::milliseconds(SAMPLE_TIMEOUT))
            , m_drop(false)
            , m_processing(false)
            , m_currentTs(0)
        {
            INIT_LOGGER_HOLDER;

            m_mp4Muxer = NPluginHelpers::PMuxerSource((NPluginHelpers::CreateMP4Muxer(GET_LOGGER_PTR,
                                                                                      1,
                                                                                      sc,
                                                                                      textSource ? true : false,
                                                                                      filePath)));

            m_videoConnection = NMMSS::CConnectionResource(videoSource->GetSource(), m_mp4Muxer->GetVideoSink(), GET_LOGGER_PTR);

#ifdef ENV64
            if (audioSource)
            {
                NMMSS::PPullStyleSink audioSink(m_mp4Muxer->GetAudioSink());
                m_audioConnection = NMMSS::CConnectionResource(audioSource->GetSource(), audioSink.Get(), GET_LOGGER_PTR);
            }
#endif // ENV64

            if (textSource)
            {
                m_textConnection = NMMSS::CConnectionResource(textSource->GetSource(), m_mp4Muxer->GetTextSink(), GET_LOGGER_PTR);
            }
        }

        SMp4ClientContext(const NWebGrpc::PGrpcManager grpcManager,
                          NGrpcHelpers::PCredentials credentials,
                          NCorbaHelpers::IContainer* cont,
                          NHttp::PResponse resp,
                          NHttp::PCountedSynchronizer sync,
                          NHttp::IVideoSourceCache::TOnDisconnected f1,
                          DECLARE_LOGGER_ARG,
                          NHttp::PArchiveContext ctx)
            : m_response(resp)
            , m_onDisconnected(f1)
            , m_eventStreamCreated(false)
            , m_muxerConnection(nullptr)
            , m_disconnected(false)
            , m_dataTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_dataWaitTimeout(ctx->keyFrames ? boost::posix_time::milliseconds(KEY_SAMPLE_TIMEOUT)
                                               : boost::posix_time::milliseconds(SAMPLE_TIMEOUT))
            , m_drop(false)
            , m_processing(false)
            , m_currentTs(0)
        {
            INIT_LOGGER_HOLDER;

            NHttp::SJpegArchiveContext* jpegCtx = nullptr;
            NHttp::SHlsArchiveContext* hlsCtx = nullptr;

            NPluginHelpers::EStreamContainer sc = getStreamContainer(ctx, jpegCtx, hlsCtx);

            m_mp4Muxer = NPluginHelpers::PMuxerSource((NPluginHelpers::CreateMP4Muxer(GET_LOGGER_PTR,
                                                                                      ctx->speed,
                                                                                      sc,
                                                                                      false,
                                                                                      (nullptr == hlsCtx) ? "" : hlsCtx->m_filePath)));

            const long playFlags = (ctx->speed < 0) ? NMMSS::PMF_REVERSE | NMMSS::PMF_KEYFRAMES
                                    : (ctx->keyFrames) ? NMMSS::PMF_KEYFRAMES
                                                       : NMMSS::PMF_NONE;

            MMSS::StorageEndpoint_var endpoint =
                NPluginUtility::ResolveEndoint(grpcManager,
                                               credentials,
                                               cont,
                                               ctx->videoEndpoint,
                                               ctx->startTime,
                                               ctx->forward ? axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME : axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS,
                                               playFlags);
            if (CORBA::is_nil(endpoint))
                throw std::runtime_error("Failed to resolve archive video source");

            auto qos = NMMSS::MakeQualityOfService();
            NMMSS::SetRequest(qos, MMSS::QoSRequest::StartFrom {MMSS::QoSRequest::StartFrom::Preroll});
            if ((ctx->speed < 0) || ctx->keyFrames)
                NMMSS::SetRequest(qos, MMSS::QoSRequest::OnlyKeyFrames {true});

            int syncIndex = sync->GetNextSyncIndex();
            NMMSS::PPullStyleSink syncSink(sync->GetSynchronizer()->GetInputSink(syncIndex));
            m_videoEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, endpoint, syncSink.Get(), MMSS::EAUTO, &qos);
            
            NMMSS::PPullStyleSink outputSink = getOutputSink(jpegCtx);
            sync->GetSynchronizer()->AttachOutputSink(syncIndex, outputSink.Get());

#ifdef ENV64
            if (!ctx->audioEndpoint.empty() && (ctx->speed > 0))
            {
                MMSS::StorageEndpoint_var endpoint =
                    NPluginUtility::ResolveEndoint(grpcManager,
                                                   credentials,
                                                   cont,
                                                   ctx->audioEndpoint,
                                                   ctx->startTime,
                                                   ctx->forward ? axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME : axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS,
                                                   playFlags);
                if (!CORBA::is_nil(endpoint))
                {
                    int audioSyncIndex = sync->GetNextSyncIndex();
                    NMMSS::PPullStyleSink audioSink(m_mp4Muxer->GetAudioSink());
                    NMMSS::PPullStyleSink syncSink(sync->GetSynchronizer()->GetInputSink(audioSyncIndex));
                    m_audioEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, endpoint, syncSink.Get(), MMSS::EAUTO, &qos);
                    sync->GetSynchronizer()->AttachOutputSink(audioSyncIndex, audioSink.Get());
                    m_detachAudio = true;

                    _log_ << "Synced archive audio connection successfully created";
                }
            }
#endif  // ENV64

            _log_ << "Synced archive video connection version 2 successfully created";
        }

        ~SMp4ClientContext()
        {
            _log_ << "SMp4ClientContext dtor";
            Stop();
        }

        virtual void Init() override
        {
            m_onData = std::bind(&SMp4ClientContext::onMP4Data, this, std::placeholders::_1, std::placeholders::_2);

            m_mp4Muxer->Init(
                std::bind(&SMp4ClientContext::processSample, NHttp::WClientContext(shared_from_base<SMp4ClientContext>()), std::placeholders::_1),
                std::bind(&SMp4ClientContext::sourceFinishProcessing, NHttp::WClientContext(shared_from_base<SMp4ClientContext>())),
                std::bind(&SMp4ClientContext::streamFormat, NHttp::WClientContext(shared_from_base<SMp4ClientContext>()), std::placeholders::_1)
            );
        }

        void Init(NHttp::DataFunction df) override
        {
            m_onData = std::bind(&SMp4ClientContext::onMP4Data, this, std::placeholders::_1, std::placeholders::_2);

            m_mp4Muxer->Init(
                df,
                std::bind(&SMp4ClientContext::sourceFinishProcessing, NHttp::WClientContext(shared_from_base<SMp4ClientContext>())),
                {}
            );
        }

        void CreateEventStream(NHttp::PResponse r) override
        {
            std::unique_lock<std::mutex> lock(m_esMutex);
            if (!m_eventStreamCreated)
            {
                m_eventStream = NHttp::CreateEventStream(GET_LOGGER_PTR);
                m_eventStream->AddClient(r);
            }
        }

        void RemoveEventStream() override
        {
            std::unique_lock<std::mutex> lock(m_esMutex);
            m_eventStream.reset();
        }

        virtual void Stop() override
        {
            m_hasData.clear();
            m_dataTimer.cancel();

            {
                std::unique_lock<std::mutex> l(m_stopMutex);
                if (nullptr != m_muxerConnection)
                {
                    NMMSS::GetConnectionBroker()->DestroyConnection(m_muxerConnection);
                    m_muxerConnection = nullptr;
                }
            }

#ifdef ENV64
            if (m_audioEndpoint)
            {
                m_audioEndpoint->Destroy();
                m_audioEndpoint.Reset();
            }
#endif // #ifdef ENV64
            if (m_videoEndpoint)
            {
                m_videoEndpoint->Destroy();
                m_videoEndpoint.Reset();
            }

            if (m_mp4Muxer)
                m_mp4Muxer->Stop();
        }

        std::uint64_t GetTimestamp() const override
        {
            return m_currentTs;
        }

    private:
        static void streamFormat(NHttp::WClientContext ctx, NPluginHelpers::EStreamContainer sc)
        {
            NHttp::PClientContext cc = ctx.lock();
            if (cc)
                cc->onStreamFormat(sc);
        }

        void onStreamFormat(NPluginHelpers::EStreamContainer sc)
        {
            switch (sc)
            {
            case NPluginHelpers::ENO_CONTAINER:
            {
                _log_ << "JPEG stream";
                m_response->SetStatus(NHttp::IResponse::OK);
                m_response << NHttp::ContentType(std::string("multipart/x-mixed-replace; boundary=") + BOUNDARY_HEADER)
                    << NHttp::CacheControlNoCache();
                m_response->FlushHeaders();

                m_onData = std::bind(&SMp4ClientContext::onJPEGData, this, std::placeholders::_1, std::placeholders::_2);

                break;
            }
            case NPluginHelpers::EHLS_CONTAINER:
                break;
            default:
            {
                _log_ << "MP4 stream";
                m_response->SetStatus(NHttp::IResponse::OK);
                m_response << NHttp::ContentType("video/mp4");
                m_response << NHttp::SHttpHeader("Connection", "Keep-Alive");
                m_response->FlushHeaders();
            }
            }
        }

        static void processSample(NHttp::WClientContext ctx, NHttp::PDataBuffer s)
        {
            NHttp::PClientContext cc = ctx.lock();
            if (cc)
                cc->onSample(s);
        }

        void onSample(NHttp::PDataBuffer db) override
        {
            m_hasData.test_and_set();
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_dataToSend.size() == BUFFER_THRESHOLD)
                {
                    m_drop = true;
                    return;
                }

                if (m_drop && !db->IsKeyData())
                {
                    _wrn_ << "Drop delta frame";
                    return;
                }
                else if (m_drop)
                {
                    m_drop = false;
                }

                m_dataToSend.push_back(db);

                if (!m_processing)
                {
                    m_processing = true;
                    lock.unlock();

                    std::call_once(m_timerFlag, [this]() {
                        setDataTimer();
                    });

                    sendDataBuffer();
                }
            }
        }

        static void sourceFinishProcessing(NHttp::WClientContext ctx)
        {
            NHttp::PClientContext cc = ctx.lock();
            if (cc)
                cc->onSourceDataExhausted();
        }

        void onSourceDataExhausted() override
        {
            m_hasData.clear();
            m_dataTimer.cancel();

            finishProcessing();
        }

        void onMP4Data(NHttp::IResponse::TConstBufferSeq& buffs, NHttp::PDataBuffer db)
        {
            m_currentTs = db->GetTimestamp();

            buffs.push_back(boost::asio::buffer(db->GetData(), static_cast<size_t>(db->GetSize())));
        }

        void onJPEGData(NHttp::IResponse::TConstBufferSeq& buffs, NHttp::PDataBuffer db)
        {
            m_currentTs = db->GetTimestamp();

            std::uint64_t bodySize = db->GetSize();

            std::stringstream s;
            s << CR << LF
                << BOUNDARY << CR << LF
                << CONTENT_TYPE << "image/jpeg" << CR << LF
                << CONTENT_LENGTH << bodySize << CR << LF
                << SAMPLE_TIMESTAMP << boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(db->GetTimestamp())) << CR << LF
                << CR << LF;

            m_headerBuffer.assign(s.str());
            buffs.push_back(boost::asio::buffer(m_headerBuffer.c_str(), m_headerBuffer.size()));
            buffs.push_back(boost::asio::buffer(db->GetData(), static_cast<size_t>(bodySize)));
        }

        void sendDataBuffer()
        {
            NHttp::PDataBuffer db;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!m_dataToSend.empty())
                {
                    db = m_dataToSend.front();
                    m_dataToSend.pop_front();
                }

                if (!db)
                {
                    m_processing = false;
                    return;
                }
            }

            NHttp::IResponse::TConstBufferSeq buffs;
            m_onData(buffs, db);

            try
            {
                if (m_response)
                    m_response->AsyncWrite(
                        buffs,
                        std::bind(&SMp4ClientContext::onSentData, shared_from_base<SMp4ClientContext>(),
                            db, std::placeholders::_1)
                    );
            }
            catch (...)
            {
                _wrn_ << "MP4 consumer is not accessible";
                finishProcessing();
            }
        }

        void onSentData(NHttp::PDataBuffer db, const boost::system::error_code ec)
        {
            if (ec)
            {
                _wrn_ << "MP4 consumer is not accessible. Error code: " << ec.message();
                finishProcessing();
                return;
            }

            NHttp::PEventStream es;
            {
                std::unique_lock<std::mutex> lock(m_esMutex);
                es = m_eventStream;
            }
            if (es)
            {
                Json::Value v(Json::stringValue);
                v = boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(db->GetTimestamp()));
                es->SendEvent("timestamp", v);
            }

            sendDataBuffer();
        }

        void finishProcessing() override
        {
            if (!m_disconnected.exchange(true, std::memory_order_relaxed))
            {
                _log_ << "Gstreamer sink disconnected";
                if (!m_onDisconnected.empty())
                    m_onDisconnected();
                m_onDisconnected.clear();
            }
        }

        void setDataTimer()
        {
            m_dataTimer.expires_from_now(m_dataWaitTimeout);
            m_dataTimer.async_wait(std::bind(&SMp4ClientContext::handle_timeout,
                shared_from_base<SMp4ClientContext>(), std::placeholders::_1));
        }

        void handle_timeout(const boost::system::error_code& error)
        {
            if (!error)
            {
                if (m_hasData.test_and_set())
                {
                    m_hasData.clear();
                    setDataTimer();
                    return;
                }
            }

            finishProcessing();
        }

        NMMSS::PPullStyleSink getOutputSink(NHttp::SJpegArchiveContext* ctx) 
        {
            if (nullptr == ctx)
                return NMMSS::PPullStyleSink(m_mp4Muxer->GetVideoSink(), NCorbaHelpers::ShareOwnership());

            NPluginHelpers::IRequestSink* gSink = dynamic_cast<NPluginHelpers::IRequestSink*>(m_mp4Muxer->GetVideoSink());
            return NMMSS::PPullStyleSink(NPluginHelpers::CreateJPEGSink(GET_LOGGER_PTR, ctx->m_width, ctx->m_height, gSink));
        }

        NPluginHelpers::EStreamContainer getStreamContainer(NHttp::PArchiveContext ctx, NHttp::SJpegArchiveContext*& jpegCtx, NHttp::SHlsArchiveContext*& hlsCtx)
        {
            jpegCtx = dynamic_cast<NHttp::SJpegArchiveContext*>(ctx.get());
            if (nullptr != jpegCtx)
                return NPluginHelpers::ENO_CONTAINER;

            hlsCtx = dynamic_cast<NHttp::SHlsArchiveContext*>(ctx.get());
            if (nullptr != hlsCtx)
                return NPluginHelpers::EHLS_CONTAINER;

            return NPluginHelpers::EMP4_CONTAINER;
        }

        NHttp::PResponse m_response;

        NHttp::PMMOrigin m_videoOrigin;
        NMMSS::CConnectionResource m_videoConnection;
        NHttp::PMMOrigin m_audioOrigin;
        NMMSS::CConnectionResource m_audioConnection;
        NHttp::PMMOrigin m_textOrigin;
        NMMSS::CConnectionResource m_textConnection;

        std::mutex m_mutex;
        std::deque<NHttp::PDataBuffer> m_dataToSend;

        NHttp::IVideoSourceCache::TOnDisconnected m_onDisconnected;

        std::mutex m_esMutex;
        NHttp::PEventStream m_eventStream;

        bool m_eventStreamCreated;

        NPluginHelpers::PMuxerSource m_mp4Muxer;
        NMMSS::PSinkEndpoint m_videoEndpoint;
#ifdef ENV64
        NMMSS::PSinkEndpoint m_audioEndpoint;
#endif // ENV64

        std::mutex m_stopMutex;
        NMMSS::IConnectionBase* m_muxerConnection;

        std::atomic<bool> m_disconnected;

        std::once_flag m_timerFlag;
        boost::asio::deadline_timer m_dataTimer;
        boost::posix_time::time_duration m_dataWaitTimeout;
        std::atomic_flag m_hasData = ATOMIC_FLAG_INIT;

        bool m_drop;
        bool m_processing;

        bool m_detachAudio = false;

        std::string m_headerBuffer;

        std::function<void(NHttp::IResponse::TConstBufferSeq& buffs, NHttp::PDataBuffer db)> m_onData;

        std::uint64_t m_currentTs;
    };

    class CVideoSourceCache : public NHttp::IVideoSourceCache
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CVideoSourceCache(NCorbaHelpers::IContainer* c, NHttp::PMMCache cache)
            : m_container(c)
            , m_mmCache(cache)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        NHttp::PClientContext CreateClientContext(const std::string& address,
            int width, int height, int compression, 
            NMMSS::PPullStyleSink sink, bool keyFrames, float fps) override
        {
            boost::mutex::scoped_lock lock(m_mutex);
            PAdapted adapted(lookupAdapted(lock, RequestParams(address, width, height, compression, fps), keyFrames));
            lock.unlock();
            if (adapted)
                return NHttp::PClientContext(new SClientContext(adapted, sink, GET_LOGGER_PTR));
            else
                return NHttp::PClientContext();
        }

        NHttp::PClientContext CreateMp4Context(NHttp::PResponse r, const std::string& videoSource, const std::string& audioSource,
            const std::string& textSource, NHttp::IVideoSourceCache::TOnDisconnected f1, bool keyFrames) override
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                _wrn_ << "Empty client context";
                return NHttp::PClientContext();
            }

            boost::mutex::scoped_lock lock(m_mutex);
            NHttp::PMMOrigin videoOrigin(m_mmCache->GetMMOrigin(videoSource.c_str(), keyFrames));
            NHttp::PMMOrigin audioOrigin;
#ifdef ENV64
            if (!audioSource.empty())
                audioOrigin = m_mmCache->GetMMOrigin(audioSource.c_str(), false);
#endif
            lock.unlock();

            NHttp::PMMOrigin textOrigin;
            if (!textSource.empty())
                textOrigin = m_mmCache->GetMMOrigin(textSource.c_str(), false);

            if (videoOrigin)
                return NHttp::PClientContext(new SMp4ClientContext(cont.Get(), r,
                                                                   videoOrigin,
                                                                   audioOrigin,
                                                                   textOrigin,
                                                                   f1,
                                                                   GET_LOGGER_PTR,
                                                                   keyFrames,
                                                                   NPluginHelpers::EMP4_CONTAINER,
                                                                   "" ));
            else
                return NHttp::PClientContext();
        }

        NHttp::PClientContext CreateArchiveMp4Context(const NWebGrpc::PGrpcManager grpcManager,
                                                      NGrpcHelpers::PCredentials credentials,
                                                      NHttp::PResponse r,
                                                      NHttp::PCountedSynchronizer sync,
                                                      NHttp::PArchiveContext ctx,
                                                      TOnDisconnected f1)
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                _wrn_ << "Empty client context";
                return NHttp::PClientContext();
            }

            return NHttp::PClientContext(new SMp4ClientContext(grpcManager,
                                                               credentials,
                                                               cont.Get(),
                                                               r,
                                                               sync,
                                                               f1,
                                                               GET_LOGGER_PTR,
                                                               ctx));
        }

        NHttp::PClientContext CreateHlsContext(NHttp::PResponse r,
                                               const std::string& videoSource,
                                               const std::string& rootPath,
                                               NHttp::IVideoSourceCache::TOnDisconnected f1) override
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                _wrn_ << "Empty client context";
                return NHttp::PClientContext();
            }

            boost::mutex::scoped_lock lock(m_mutex);
            NHttp::PMMOrigin videoOrigin(m_mmCache->GetMMOrigin(videoSource.c_str(), false));
            lock.unlock();

            if (videoOrigin)
                return NHttp::PClientContext(new SMp4ClientContext(cont.Get(), r,
                                                                   videoOrigin,
                                                                   {},
                                                                   {},
                                                                   f1,
                                                                   GET_LOGGER_PTR,
                                                                   false,
                                                                   NPluginHelpers::EHLS_CONTAINER,
                                                                   rootPath));
            else
                return NHttp::PClientContext();
        }

        NHttp::PClientContext CreateRawContext(const NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials credentials, NHttp::PResponse r,
            NHttp::PArchiveContext ctx)
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                _wrn_ << "Empty client context";
                return NHttp::PClientContext();
            }

            return NHttp::PClientContext(new SArchiveRawContext(grpcManager,
                credentials, r,
                cont.Get(),
                ctx, GET_LOGGER_PTR));
        }

    private:
        PAdapted lookupAdapted(boost::mutex::scoped_lock& lock, const RequestParams& r, bool keyFrames)
        {
            TAdapted::iterator it(m_adapted.find(r));
            PAdapted res;
            if (m_adapted.end() != it)
            {
                res = it->second.lock();
            }
            if (res.get())
            {
                return res;
            }
            POrigin o(lookupOrigin(lock, r.address, r.fps, keyFrames));
            if (o)
            {
                res.reset(new SAdapted(GET_LOGGER_PTR, o, r.width, r.height, r.compression));
                m_adapted[r] = res;
            }
            return res;
        }
        POrigin lookupOrigin(boost::mutex::scoped_lock& lock, const std::string& address, float fps, bool keyFrames)
        {
            POrigin res;

            NCorbaHelpers::PContainer cont = m_container;
            if (cont)
            {
                TOrigins::iterator it(m_origins.find(address));
                
                if (m_origins.end() != it)
                {
                    res = it->second.lock();
                }
                if (res.get())
                {
                    return res;
                }
                res.reset(new SOrigin(GET_LOGGER_PTR, address, cont.Get(), fps, keyFrames));
                m_origins[address] = res;
            }
            return res;
        }

        NCorbaHelpers::WPContainer m_container;
        NHttp::PMMCache m_mmCache;

        boost::mutex m_mutex;
        TOrigins m_origins;
        TAdapted m_adapted;
    };
}
namespace NHttp
{
    PVideoSourceCache GetVideoSourceCache(NCorbaHelpers::IContainer* c, PMMCache cache)
    {
        return PVideoSourceCache(new CVideoSourceCache(c, cache));
    }
}
