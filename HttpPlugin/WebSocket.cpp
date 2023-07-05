#include <boost/asio.hpp>
#include <openssl/sha.h>

#include "HttpPlugin.h"
#include "DataSink.h"
#include "Constants.h"
#include "RegexUtility.h"
#include "VideoSourceCache.h"
#include "WebSocketSession.h"
#include "GrpcReader.h"
#include "CommonUtility.h"
#include "BLQueryHelper.h"
#include "MetaCredentialsStorage.h"

#include "../MediaType.h"
#include "../ConnectionBroker.h"

#include <PtimeFromQword.h>
#include <MMClient/MMClient.h>
#include <MMTransport/MMTransport.h>
#include <MMTransport/QualityOfService.h>

#include <Crypto/Crypto.h>
#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/Reactor.h>
#include <HttpServer/BasicServletImpl.h>
#include <CorbaHelpers/ResolveServant.h>

#include <json/json.h>
#include <SecurityManager/SecurityManager.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>
#include <axxonsoft/bl/security/SecurityService.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
namespace bl = axxonsoft::bl;

using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
    bl::domain::BatchGetCamerasResponse >;

using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;


namespace
{
    const uint8_t FIRST_BIT_MASK = 0x80;
    const uint64_t LOW_DATA_THRESHOLD = 125;
    const uint64_t HIGH_DATA_THRESHOLD = 65535;

    const char* const SALT_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    const char* const JSON_QUERY_STREAMID_FIELD = "streamId";
    const char* const JSON_QUERY_METHOD_FIELD = "method";
    const char* const JSON_QUERY_ENDPOINT_FIELD = "endpoint";
    const char* const JSON_QUERY_ARCHIVE_FILED = "archive";
    const char* const JSON_QUERY_TIME_FIELD = "beginTime";
    const char* const JSON_QUERY_FORMAT_FIELD = "format";
    const char* const JSON_QUERY_SPEED_FIELD = "speed";
    const char* const JSON_QUERY_WIDTH_FIELD = "width";
    const char* const JSON_QUERY_HEIGHT_FIELD = "height";
    const char* const JSON_QUERY_CROP_X_FIELD = "crop_x";
    const char* const JSON_QUERY_CROP_Y_FIELD = "crop_y";
    const char* const JSON_QUERY_CROP_WIDTH_FIELD = "crop_width";
    const char* const JSON_QUERY_CROP_HEIGHT_FIELD = "crop_height";
    const char* const JSON_QUERY_VC_FIELD = "vc";
    const char* const JSON_QUERY_KEYFRAMES_FIELD = "keyFrames";
    const char* const JSON_QUERY_TIMESTAMP_LIST_FIELD = "timestampList";
    const char* const JSON_QUERY_ENTITIES_FIELD = "entities";
    const char* const JSON_QUERY_ENTITYID_FIELD = "id";
    const char* const JSON_QUERY_FORWARD_FIELD = "forward";

    const char* const METHOD_FIELD_PLAY_STATE = "play";
    const char* const METHOD_FIELD_STOP_STATE = "stop";
    const char* const METHOD_FIELD_BATCH_STATE = "batch";
    const char* const METHOD_FIELD_SYNC_STATE = "sync";
    const char* const METHOD_FIELD_TOKEN_STATE = "update_token";

    const char* const PARAM_AUTH_TOKEN = "auth_token";

    const size_t SAMPLE_THRESHOLD = 1000;

    const std::uint16_t DATA_WAIT_TIMEOUT = 30;

    using ArchiveCommand_t = std::function<void(const std::string&)>;

    using ArchiveEndpointInfoCollection_t = std::map<std::string, PEndpointQueryContext>;
    using PArchiveEndpointInfoCollection_t = std::shared_ptr<ArchiveEndpointInfoCollection_t>;

    class CWSContext : public NWebWS::IWebSocketClient
    {
        enum EStreamState
        {
            EUNKNOWN,
            EPLAY,
            ESTOP,
            EBATCH,
            ESYNC,
            ETOKEN
        };

        enum EPlayState
        {
            EUNKNOWN_PLAY_STATE,
            ELIVE,
            EARCHIVE
        };

        enum EStreamFormat
        {
            EUNKNOWN_FORMAT,
            EMP4,
            EJPEG
        };

        typedef std::shared_ptr<NCorbaHelpers::IResource> PSnapshot;
        typedef std::function<void()> TOnDataAbsentCallback;

        class SMediaContext: public std::enable_shared_from_this<SMediaContext>
        {
        protected:
            DECLARE_LOGGER_HOLDER;

        public:
            SMediaContext(DECLARE_LOGGER_ARG, const std::string& streamId)
                : m_streamId(streamId)
            {
                INIT_LOGGER_HOLDER;
            }
            virtual ~SMediaContext() 
            {
                _log_ << "SMediaContext dtor";
            }

            std::string GetStreamId() const
            {
                return m_streamId;
            }

            void AddData(NHttp::PDataBuffer s)
            {
                m_data.push_back(s);
            }

            NHttp::PDataBuffer GetData()
            {
                NHttp::PDataBuffer s;
                {
                    if (!m_data.empty())
                    {
                        s = m_data.front();
                        m_data.pop_front();
                    }
                }
                return s;
            }

            template <typename Derived>
            std::shared_ptr<Derived> shared_from_base()
            {
                return std::dynamic_pointer_cast<Derived>(shared_from_this());
            }

        private:
            std::string m_streamId;

            typedef std::deque<NHttp::PDataBuffer> TData;
            TData m_data;
        };
        typedef std::shared_ptr<SMediaContext> PMediaContext;
        typedef std::weak_ptr<SMediaContext> WMediaContext;

        class SBatchContext : public SMediaContext
        {
        public:
            SBatchContext(DECLARE_LOGGER_ARG, const std::string& streamId, 
                const Json::Value& data, axxonsoft::bl::archive::EStartPosition startPosition)
                : SMediaContext(GET_LOGGER_PTR, streamId)
                , m_startPosition(startPosition)
            {
                std::set<boost::posix_time::ptime> ts;
                Json::ArrayIndex count = data.size();
                _this_log_ << "Batch stream " << streamId << " has " << count << " elements";
                for (Json::ArrayIndex i = 0; i < count; ++i)
                {
                    ts.insert(boost::posix_time::from_iso_string(data[i].asString()));
                }
                std::copy(ts.begin(), ts.end(), std::back_inserter(m_timestamps));
            }

            ~SBatchContext()
            {
                _this_log_ << "SBatchContext dtor";
                Stop();
            }

            void Start(MMSS::StorageEndpoint_var ep, int width, int height, float crop_x, float crop_y, float crop_width, float crop_height, NPluginHelpers::TOnTimedSampleHandler sh)
            {
                m_dataEndpoint = ep;

                m_snapshotSink = NPluginHelpers::PBatchSink(NPluginHelpers::CreateBatchSink(GET_LOGGER_PTR, this->GetStreamId(), width, height, true, crop_x, crop_y, crop_width, crop_height, sh,
                    boost::bind(&SBatchContext::querySnapshot, std::weak_ptr<SBatchContext>(shared_from_base<SBatchContext>()), _1)));

                readSnapshot();
            }

        private:
            void Stop()
            {
                std::unique_lock<std::mutex> lock(m_stopMutex);
                if (m_sinkEndpoint)
                {
                    m_sinkEndpoint->Destroy();
                    m_sinkEndpoint.Reset();
                }
                if (m_snapshotSink)
                    m_snapshotSink.Reset();
            }

            static void querySnapshot(std::weak_ptr<SBatchContext> obj, bool)
            {
                std::shared_ptr<SBatchContext> owner = obj.lock();
                if (owner)
                    owner->readSnapshot();          
            }

            void readSnapshot()
            {
                if (!m_timestamps.empty())
                {
                    boost::posix_time::ptime seekTime = m_timestamps.front();
                    m_timestamps.pop_front();

                    std::uint32_t sessionId = ++m_sessionId;
                    m_snapshotSink->SetFragmentId(sessionId, seekTime);

                    _this_dbg_ << "Batch stream " << this->GetStreamId() << ": query time " <<
                        boost::posix_time::to_iso_string(seekTime) << " for fragment " << sessionId;

                    m_dataEndpoint->Seek(boost::posix_time::to_iso_string(seekTime).c_str(), NPluginUtility::ToCORBAPosition(m_startPosition), NMMSS::PMF_NONE, sessionId);

                    if (!m_sinkEndpoint)
                    {
                        auto qos = NMMSS::MakeQualityOfService();
                        if (axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME == m_startPosition)
                            NMMSS::SetRequest(qos, MMSS::QoSRequest::OnlyKeyFrames{ true });

                        m_sinkEndpoint = CreatePullConnectionByObjref(GET_LOGGER_PTR,
                            m_dataEndpoint, m_snapshotSink.Get(), MMSS::EAUTO, &qos);
                    }
                }
                else
                {
                    _this_log_ << "Batch stream " << this->GetStreamId() << " cleanup";
                    Stop();
                }
            }

            std::deque<boost::posix_time::ptime> m_timestamps;

            MMSS::StorageEndpoint_var m_dataEndpoint;
            NPluginHelpers::PBatchSink m_snapshotSink;
            NMMSS::PSinkEndpoint m_sinkEndpoint;

            static std::uint32_t m_sessionId;
            axxonsoft::bl::archive::EStartPosition m_startPosition;

            std::mutex m_stopMutex;
        };
        typedef std::shared_ptr<SBatchContext> PBatchContext;

        struct SStreamContext : public SMediaContext
        {
            DECLARE_LOGGER_HOLDER;

            SStreamContext(DECLARE_LOGGER_ARG, const std::string& streamId)
                : SMediaContext(GET_LOGGER_PTR, streamId)
                , m_muxerConnection(nullptr)
            {
                INIT_LOGGER_HOLDER;
            }

            ~SStreamContext()
            {
                _log_ << "SStreamContext dtor";

                if (m_clientContext)
                    m_clientContext->Stop();

                if (m_output)
                    m_output->Destroy();

                if (nullptr != m_muxerConnection)
                    NMMSS::GetConnectionBroker()->DestroyConnection(m_muxerConnection);

                if (m_muxer)
                    m_muxer->Stop();
            }

            void SetMuxerConnection(NMMSS::MediaMuxer::PMuxer m, NMMSS::IConnectionBase* mc)
            {
                m_muxer = m;
                m_muxerConnection = mc;
            }

            PClientContext m_clientContext;
            NMMSS::MediaMuxer::PMuxer m_muxer;
            NMMSS::IConnectionBase* m_muxerConnection;
            NMMSS::MediaMuxer::POutputStreamed m_output;
            PSnapshot m_snapshot;

            std::mutex m_mutex;
            typedef std::deque<NMMSS::PSample> TData;
            TData m_samples;
        };
        typedef std::shared_ptr<SStreamContext> PStreamContext;
        typedef std::weak_ptr<SStreamContext> WStreamContext;

        struct SSyncContext : public SMediaContext
        {
            DECLARE_LOGGER_HOLDER;

            SSyncContext(DECLARE_LOGGER_ARG, const std::string& streamId, NCorbaHelpers::IContainer* cont)
                : SMediaContext(GET_LOGGER_PTR, streamId)
                , m_sync(boost::make_shared<NHttp::SCountedSynchronizer>(GET_LOGGER_PTR, cont))
            {
                INIT_LOGGER_HOLDER;
            }

            ~SSyncContext()
            {
                _log_ << "SSyncContext dtor";
            }

            void AddStreamContext(PStreamContext sctx) 
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ctxs.emplace_back(sctx);
            }

            void Start(int speed) 
            {
                _log_ << "Start synchronizer";
                m_sync->Start(speed);
            }

            NHttp::PCountedSynchronizer m_sync;

            std::mutex m_mutex;
            std::vector<PStreamContext> m_ctxs;
            NMMSS::PSinkEndpoint m_timestampEndpoint;
        };
        typedef std::shared_ptr<SSyncContext> PSyncContext;
        typedef std::weak_ptr<SSyncContext> WSyncContext;

        struct SArchiveItem
        {
            std::string id;
            PEndpointQueryContext epCtx;
        };

        DECLARE_LOGGER_HOLDER;
    public:
        CWSContext(NCorbaHelpers::IContainer* c,
                   PResponse resp,
                   const PRequest req,
                   NWebWS::PWebSocketSession wsSession,
                   const NWebGrpc::PGrpcManager grpcManager,
                   const NPluginUtility::PRigthsChecker rightsChecker,
                   NGrpcHelpers::PCredentials metaCredentials,
                   NHttp::PVideoSourceCache videoSourceCache,
                   const NHttp::PStatisticsCache& statisticsCache,
                   NHttp::SConfig& webCfg)
            : m_container(c)
            , m_response(resp)
            , m_wsSession(wsSession)
            , m_videoSourceCache(videoSourceCache)
            , m_statisticsCache(statisticsCache)
            , m_grpcManager(grpcManager)
            , m_rightsChecker(rightsChecker)
            , m_webCfg(webCfg)
            , m_drop(false)
            , m_processing(false)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
            m_metaCredentialsStorage =
                NHttp::CreateMetaCredentialsStorage(GET_LOGGER_PTR, m_grpcManager, metaCredentials, req);
        }

        ~CWSContext()
        {
            _log_ << "CWSContext dtor";
        }

        void Init() override
        {
            NWebWS::PWebSocketSession session = m_wsSession.lock();
            if (session)
            {
                session->SetWriteCallback(boost::bind(&CWSContext::onWrite,
                    boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>())));
            }
        }

        void Stop() override
        {
            TStreams streams;
            {
                std::unique_lock<std::mutex>  lock(m_streamMutex);
                streams.swap(m_streams);
            }

            for (auto s : streams)
            {
                ProcessStopCommand(s.first);
            }
        }

        void OnMessage(const std::string& msg) override
        {
            Json::Value data;
            Json::CharReaderBuilder reader;
            std::string err;
            std::istringstream is(msg);
            if (!Json::parseFromStream(reader, is, &data, &err))
            {
                sendWSError("Video processing query error");
                return;
            }

            std::string streamId;
            if (data.isMember(JSON_QUERY_STREAMID_FIELD))
            {
                streamId.assign(data[JSON_QUERY_STREAMID_FIELD].asString());
            }

            EStreamState st = parseStreamState(data);
            if (EUNKNOWN == st)
            {
                sendWSError("Unknown state was queried");
                return;
            }

            if (streamId.empty() && (st != ETOKEN))
            {
                sendWSError("streamId field is required");
                return;
            }

            switch (st)
            {
            case EPLAY:
                ProcessPlayCommand(streamId, data);
                break;
            case ESTOP:
                ProcessStopCommand(streamId);
                break;
            case EBATCH:
                ProcessBatchCommand(streamId, data);
                break;
            case ESYNC:
                ProcessSyncCommand(streamId, data);
                break;
            case ETOKEN:
                ProcessUpdateTokenCommand(data);
                break;
            default:
                break;
            }
        }

    private:
        static void onWrite(boost::weak_ptr<CWSContext> obj)
        {
            boost::shared_ptr<CWSContext> owner = obj.lock();
            if (owner)
            {
                owner->processNextSample();
            }
        }

        void processNextSample()
        {
            PMediaContext ctx;
            NHttp::PDataBuffer buf;

            std::unique_lock<std::mutex> lock(m_dataMutex);
            if (!m_dataToSend.empty())
            {
                while (!buf)
                {
                    if (m_dataToSend.empty())
                        break;

                    WMediaContext wctx = m_dataToSend.front();
                    m_dataToSend.pop_front();

                    ctx = wctx.lock();
                    if (ctx)
                        buf = ctx->GetData();
                }

                if (buf)
                {
                    sendFrame(ctx, buf);
                    return;
                }
            }

            m_processing = false;
        }

        void ProcessPlayCommand(const std::string& streamId, const Json::Value& data)
        {
            std::string format;
            if (data.isMember(JSON_QUERY_FORMAT_FIELD))
            {
                format.assign(data[JSON_QUERY_FORMAT_FIELD].asString());
            }

            if (format.empty())
            {
                _wrn_ << "format field is required";
                return;
            }

            EStreamFormat fmt = parseFormat(format);
            if (EUNKNOWN_FORMAT == fmt)
            {
                _wrn_ << "Format " << format << " is unsupported";
                return;
            }

            std::string ep;
            if (data.isMember(JSON_QUERY_ENDPOINT_FIELD))
            {
                ep.assign(data[JSON_QUERY_ENDPOINT_FIELD].asString());
            }

            if (ep.empty())
            {
                _err_ << "endpoint field is required";
                return;
            }

            EPlayState playMode = ELIVE;
            std::string archiveTime;
            if (data.isMember(JSON_QUERY_TIME_FIELD))
            {
                playMode = EARCHIVE;
                archiveTime.assign(data[JSON_QUERY_TIME_FIELD].asString());
            }

            switch (playMode)
            {
            case ELIVE:
            {
                PStreamContext sc = std::make_shared<SStreamContext>(GET_LOGGER_PTR, streamId);
                addContext(streamId, sc);

                ProcessLivePlayCommand(sc, streamId, fmt, ep, data);
                break;
            }
            case EARCHIVE:
            {
                NCorbaHelpers::PContainer cont(m_container);
                if (cont)
                {
                    ep = "hosts/" + ep;

                    ProcessArchivePlayCommand(cont, streamId, fmt, ep, archiveTime, data);
                }
                break;
            }

            default:
            {
                _wrn_ << "CMediaContext::ProcessPlayCommand: unhandled play mode: " << playMode;
                break;
            }
            }
        }

        void ProcessUpdateTokenCommand(const Json::Value& data)
        {
            _inf_ << "WS. Start update token";
            std::string new_token;
            if (data.isMember(PARAM_AUTH_TOKEN))
                new_token.assign(data[PARAM_AUTH_TOKEN].asString());

            if (new_token.empty())
            {
                sendWSError("WS. auth_token field is required");
                return;
            }

            NHttp::DenyCallback_t dc = std::bind(&CWSContext::ProcessRejectToken,
                                                 shared_from_base<CWSContext>(), new_token);
            m_metaCredentialsStorage->UpdateToken(new_token, dc);
        }

        void ProcessRejectToken(const std::string newToken)
        {
            sendWSError("WS. Rejected udpating token " + newToken);
        }

        void ProcessStopCommand(const std::string& streamId)
        {
            _inf_ << "Stop stream " << streamId;
            PMediaContext sc;
            {
                std::unique_lock<std::mutex> lock(m_streamMutex);
                TStreams::iterator it = m_streams.find(streamId);
                if (m_streams.end() != it)
                {
                    sc = it->second;
                    m_streams.erase(streamId);
                }
            }
        }

        void ProcessSyncCommand(const std::string& streamId, const Json::Value& data)
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
            {
                _err_ << "Sync: no available container";
                return;
            }

            std::string format;
            if (data.isMember(JSON_QUERY_FORMAT_FIELD))
            {
                format.assign(data[JSON_QUERY_FORMAT_FIELD].asString());
            }

            if (format.empty())
            {
                _err_ << "Sync: format field is required";
                return;
            }

            EStreamFormat fmt = parseFormat(format);
            if (EUNKNOWN_FORMAT == fmt)
            {
                _err_ << "Sync: format " << format << " is unsupported";
                return;
            }

            PArchiveEndpointInfoCollection_t syncItems = std::make_shared<ArchiveEndpointInfoCollection_t>();
            if (data.isMember(JSON_QUERY_ENTITIES_FIELD))
            {
                Json::Value::ArrayIndex count = data[JSON_QUERY_ENTITIES_FIELD].size();
                for (Json::Value::ArrayIndex i = 0; i < count; ++i)
                {
                    Json::Value entity = data[JSON_QUERY_ENTITIES_FIELD][i];

                    PEndpointQueryContext ctx = std::make_shared<SEndpointQueryContext>();
                    ctx->id.assign(entity[JSON_QUERY_ENTITYID_FIELD].asString());
                    ctx->endpoint.assign("hosts/" + entity[JSON_QUERY_ENDPOINT_FIELD].asString());
                    ctx->archiveName.assign("hosts/" + entity[JSON_QUERY_ARCHIVE_FILED].asString());

                    syncItems->insert(std::make_pair(ctx->id, ctx));
                }
            }

            if (syncItems->empty())
            {
                _err_ << "Sync: entities array can not be empty";
                return;
            }

            std::string archiveTime;
            if (data.isMember(JSON_QUERY_TIME_FIELD))
            {
                archiveTime.assign(data[JSON_QUERY_TIME_FIELD].asString());
            }

            if (archiveTime.empty())
            {
                _err_ << "Sync: time field is required";
                return;
            }

            PSyncContext sc = std::make_shared<SSyncContext>(GET_LOGGER_PTR, streamId, cont.Get());
            addContext(streamId, sc);

            NWebBL::StorageSourceCallback_t ssc = boost::bind(&CWSContext::getArchiveEndpoints,
                                                     shared_from_base<CWSContext>(),
                                                     cont,
                                                     sc,
                                                     data,
                                                     archiveTime, _1);

            NPluginUtility::PEndpointQueryCollection_t eqc = std::make_shared<std::vector<PEndpointQueryContext>>();
            
            for (auto item : *syncItems)
                eqc->push_back(item.second);

            NWebBL::ResolveStorageSources(GET_LOGGER_PTR, m_grpcManager,
                                          m_metaCredentialsStorage->GetMetaCredentials(), eqc, ssc);
        }

        void execBatchCommand(NCorbaHelpers::IContainer* cont, PBatchContext sc,
                              const Json::Value& data,
                              axxonsoft::bl::archive::EStartPosition startPosition,
                              const std::string& arc_accessPoint)
        {
            int width = 0, height = 0, vc = 1;
            float crop_x = 0.f, crop_y = 0.f, crop_width = 1.f, crop_height = 1.f;
            parseJpegParameters(data, width, height, crop_x, crop_y, crop_width, crop_height, vc);

            MMSS::StorageEndpoint_var endpoint =
                NPluginUtility::ResolveEndoint(m_grpcManager, m_metaCredentialsStorage->GetMetaCredentials(),
                                               cont, arc_accessPoint, "", startPosition, 0);
            if (!CORBA::is_nil(endpoint))
            {
                sc->Start(endpoint, width, height, crop_x, crop_y, crop_width, crop_height,
                    boost::bind(&CWSContext::onSampleHandler, boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()),
                        WMediaContext(sc), _1, _2));
            }
        }

        void ProcessBatchCommand(const std::string& streamId, const Json::Value& data)
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (!cont)
                return;

            std::string ep;
            if (data.isMember(JSON_QUERY_ENDPOINT_FIELD))
            {
                ep.assign(data[JSON_QUERY_ENDPOINT_FIELD].asString());
            }

            if (ep.empty())
            {
                _err_ << "endpoint field is required";
                return;
            }

            if (!data.isMember(JSON_QUERY_TIMESTAMP_LIST_FIELD))
            {
                _err_ << "Batch query must contain timestamp list";
                return;
            }

            if (!data.isMember(JSON_QUERY_ARCHIVE_FILED))
            {
                _err_ << "Batch query must contain archive reference";
                return;
            }

            bool keyFrames = false;
            if (data.isMember(JSON_QUERY_KEYFRAMES_FIELD))
            {
                keyFrames = data[JSON_QUERY_KEYFRAMES_FIELD].asBool();
            }

            axxonsoft::bl::archive::EStartPosition startPosition = keyFrames ? axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME : axxonsoft::bl::archive::START_POSITION_EXACTLY;

            PBatchContext sc = std::make_shared<SBatchContext>(GET_LOGGER_PTR, streamId, data[JSON_QUERY_TIMESTAMP_LIST_FIELD], startPosition);
            addContext(streamId, sc);

            ArchiveCommand_t ac = std::bind(&CWSContext::execBatchCommand, shared_from_base<CWSContext>(), cont.Get(), sc, data, startPosition, std::placeholders::_1);
            execArchiveCommand(data[JSON_QUERY_ARCHIVE_FILED].asString(), "hosts/" + ep, ac);
        }

        void runLiveMP4(PStreamContext sc, const std::string& streamId,
                        const Json::Value& data, const std::string& videoEp,
                        NWebBL::SAuxillarySources auxSources)
        {
            bool keyFrames = false;
            if (data.isMember(JSON_QUERY_KEYFRAMES_FIELD))
            {
                keyFrames = data[JSON_QUERY_KEYFRAMES_FIELD].asBool();
            }

            PClientContext cc;
            try {
                cc = m_videoSourceCache->CreateMp4Context(m_response, videoEp, auxSources.audioSource, auxSources.textSource,
                    boost::bind(&CWSContext::onDataSinkDisconnected,
                        boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), WStreamContext(sc)),
                    keyFrames != 0);
                if (cc)
                {
                    cc->Init(
                        boost::bind(&CWSContext::onDataBufferHandler,
                            boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), WStreamContext(sc), _1));
                    sc->m_clientContext = cc;
                }
            }
            catch (const std::exception& e) {
                _err_ << "Cannot create live MP4 context for stream " << streamId << ". Reason: " << e.what();
                NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                onSampleProcessing(sc, data);
            }
        }

        void prepareMP4Endpoints(PSyncContext sc, const std::string& streamId,
                                 const Json::Value& data, const std::string& archiveName,
                                 const std::string& archiveTime, const std::string& videoEp,
                                 NWebBL::SAuxillarySources auxSources)
        {
            NWebBL::EndpointCallback_t ec = boost::bind(&CWSContext::runArchiveMP4,
                shared_from_base<CWSContext>(), sc, streamId, data, archiveName, archiveTime, _1, _2);

            NWebBL::ResolveCameraStorageSources(GET_LOGGER_PTR, m_grpcManager,
                                                m_metaCredentialsStorage->GetMetaCredentials(),
                                                archiveName, videoEp, auxSources.audioSource, ec);
        }


        void getArchiveEndpoints(NCorbaHelpers::PContainer cont, PSyncContext syncCtx,
                                 const Json::Value& data, const std::string& archiveTime,
                                 NPluginUtility::PEndpointQueryCollection_t epColl)
        {
            bool keyFrames = false;
            if (data.isMember(JSON_QUERY_KEYFRAMES_FIELD))
            {
                keyFrames = data[JSON_QUERY_KEYFRAMES_FIELD].asBool();
            }

            int speed = 1;
            if (data.isMember(JSON_QUERY_SPEED_FIELD))
            {
                speed = data[JSON_QUERY_SPEED_FIELD].asInt();
            }

            std::string format;
            if (data.isMember(JSON_QUERY_FORMAT_FIELD))
            {
                format.assign(data[JSON_QUERY_FORMAT_FIELD].asString());
            }

            NHttp::PCountedSynchronizer sync = syncCtx->m_sync;

            for (auto ctx : *epColl)
            {
                _inf_ << "Entity id: " << ctx->id << ". Archive context " << ctx->requestedItem << " for endpoint " << ctx->endpoint;

                PStreamContext sc = std::make_shared<SStreamContext>(GET_LOGGER_PTR, ctx->id);

                NHttp::PArchiveContext aCtx = getArchiveContext(format);
                aCtx->videoEndpoint = std::move(ctx->requestedItem);
                aCtx->archiveName = std::move(ctx->archiveName);
                aCtx->startTime = archiveTime;
                aCtx->keyFrames = keyFrames;
                aCtx->speed = speed;

                PClientContext cc;
                try
                {
                    cc = m_videoSourceCache->CreateArchiveMp4Context(m_grpcManager,
                            m_metaCredentialsStorage->GetMetaCredentials(), m_response,
                            sync, aCtx,
                            boost::bind(&CWSContext::onDataSinkDisconnected,
                                boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), WStreamContext(sc)));

                    if (cc)
                    {
                        cc->Init(boost::bind(&CWSContext::onDataBufferHandler,
                                             boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()),
                                             WStreamContext(sc),
                                             _1));
                        sc->m_clientContext = cc;
                    }

                    syncCtx->AddStreamContext(sc);
                }
                catch (const std::exception& e)
                {
                    _err_ << "Cannot create live MP4 context for stream " << ctx->id << ". Reason: " << e.what();
                    NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                    onSampleProcessing(sc, data);
                }
            }

            syncCtx->Start(speed);
        }

        NHttp::PArchiveContext getArchiveContext(const std::string& mediaType)
        {
            if ("mp4" == mediaType)
                return boost::make_shared<NHttp::SArchiveContext>(GET_LOGGER_PTR);
            return boost::make_shared<NHttp::SJpegArchiveContext>(GET_LOGGER_PTR, 0, 0);
        }

        void runArchiveMP4(PSyncContext syncCtx, const std::string& streamId, const Json::Value& data,
                           const std::string& archiveName, const std::string& archiveTime,
                           const std::string& videoEp, const std::string& audioEp)
        {
            _inf_ << "Try run archive play for streamId: " << streamId << std::endl
                  << "\tVideo endpoint: " << videoEp << std::endl
                  << "\tAudio endpoint: " << audioEp;

            bool keyFrames = false;
            if (data.isMember(JSON_QUERY_KEYFRAMES_FIELD))
            {
                keyFrames = data[JSON_QUERY_KEYFRAMES_FIELD].asBool();
            }

            int speed = 1;
            if (data.isMember(JSON_QUERY_SPEED_FIELD))
            {
                speed = data[JSON_QUERY_SPEED_FIELD].asInt();
            }

            bool forward = false;
            if (data.isMember(JSON_QUERY_FORWARD_FIELD))
            {
                forward = data[JSON_QUERY_FORWARD_FIELD].asBool();
            }

            NHttp::PArchiveContext aCtx = boost::make_shared<NHttp::SArchiveContext>(GET_LOGGER_PTR);
            aCtx->videoEndpoint = std::move(videoEp);
            aCtx->audioEndpoint = std::move(audioEp);
            aCtx->archiveName = std::move(archiveName);
            aCtx->startTime = archiveTime;
            aCtx->keyFrames = keyFrames;
            aCtx->speed = speed;
            aCtx->forward = forward;

            if (!aCtx->audioEndpoint.empty())
            {
                const long playFlags = (aCtx->speed < 0) ? NMMSS::PMF_REVERSE | NMMSS::PMF_KEYFRAMES
                    : (aCtx->keyFrames) ? NMMSS::PMF_KEYFRAMES
                    : NMMSS::PMF_NONE;

                NCorbaHelpers::PContainer c(m_container);
                MMSS::StorageEndpoint_var endpoint =
                    NPluginUtility::ResolveEndoint(m_grpcManager,
                        m_metaCredentialsStorage->GetMetaCredentials(),
                        NCorbaHelpers::PContainer(m_container).Get(),
                        aCtx->videoEndpoint,
                        aCtx->startTime,
                        aCtx->forward ? axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME : axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS,
                        playFlags);

                NMMSS::PPullStyleSink dummy(NPluginHelpers::CreateOneShotSink(GET_LOGGER_PTR,
                    boost::bind(&CWSContext::adjustStreamTime, shared_from_base<CWSContext>(), syncCtx, streamId, aCtx, _1)));
                syncCtx->m_timestampEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR,
                    endpoint, dummy.Get(), MMSS::EAUTO);
            }
            else
            {
                runClientContext(syncCtx, streamId, aCtx);
            }
        }

        void adjustStreamTime(PSyncContext syncCtx, const std::string& streamId, NHttp::PArchiveContext aCtx, NMMSS::ISample* s)
        {
            if (syncCtx->m_timestampEndpoint)
            {
                syncCtx->m_timestampEndpoint->Destroy();
                syncCtx->m_timestampEndpoint.Reset();
            }

            if (!s || NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header()))
            {
                _err_ << "No data for archive " << aCtx->archiveName;
                return;
            }

            aCtx->startTime = boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(s->Header().dtTimeBegin));
            _log_ << "Calculated start time " << aCtx->startTime;

            runClientContext(syncCtx, streamId, aCtx);
        }

        void runClientContext(PSyncContext syncCtx, const std::string& streamId, NHttp::PArchiveContext aCtx)
        {
            PClientContext cc;
            try
            {
                PStreamContext sc = std::make_shared<SStreamContext>(GET_LOGGER_PTR, streamId);

                cc = m_videoSourceCache->CreateArchiveMp4Context(m_grpcManager, m_metaCredentialsStorage->GetMetaCredentials(), m_response, syncCtx->m_sync, aCtx,
                    boost::bind(&CWSContext::onDataSinkDisconnected, boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), WStreamContext(sc)));

                if (cc)
                {
                    cc->Init(
                        boost::bind(&CWSContext::onDataBufferHandler,
                            boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), WStreamContext(sc), _1));
                    sc->m_clientContext = cc;
                }

                syncCtx->AddStreamContext(sc);
            }
            catch (...)
            {
            }

            if (!cc)
            {
                _err_ << "Cannot create archive MP4 context for stream " << streamId;
                NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                onSampleProcessing(syncCtx, data);
                return;
            }

            syncCtx->Start(aCtx->speed);
        }

        void ProcessLivePlayCommand(PStreamContext sc, const std::string& streamId,
                                    EStreamFormat fmt, const std::string& ep, const Json::Value& data)
        {
            _log_ << "Process live stream " << streamId << " for source " << ep;

            if ((fmt == EStreamFormat::EJPEG) && !m_webCfg.RecodeVideoStream)
            {
                _log_ << "Live stream " << streamId << " requested format is jpeg, recoding video is not allowed";
                auto stat_data = m_statisticsCache->GetData("hosts/" + ep);
                // TODO
                // What do if we don't have statistics for device ep
                std::string stream_type = NPluginUtility::parseForCC(stat_data.m_streamType);
                std::transform(stream_type.begin(), stream_type.end(), stream_type.begin(), ::tolower);
                if (stream_type != "jpeg")
                {
                    _wrn_ << "Live stream " << streamId << " has stream type " << stream_type << ". It not requested jpeg and recoding video stream is not allowed -> we DO NOT start live stream.";
                    return;
                }
            }
            m_rightsChecker->IsCameraAllowed("hosts/" + ep, m_metaCredentialsStorage->GetAuthSession(),
                boost::bind(&CWSContext::sProcessLivePlayCommand,
                    boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), sc,
                    streamId, fmt, ep, data, boost::placeholders::_1));
        }

        static void sProcessLivePlayCommand(boost::weak_ptr<CWSContext> obj, PStreamContext sc,
                                            const std::string& streamId, EStreamFormat fmt,
                                            const std::string& ep, const Json::Value& data,
                                            int cameraAccessLevel)
        {
            boost::shared_ptr<CWSContext> owner = obj.lock();
            if (owner && cameraAccessLevel >= axxonsoft::bl::security::CAMERA_ACCESS_MONITORING_ON_PROTECTION)
                owner->execProcessLivePlayCommand(sc, streamId, fmt, ep, data);
            else if (owner)
            {
                NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                owner->onSampleProcessing(sc, data);
            }
        }

        bool execProcessLivePlayCommand(PStreamContext sc, const std::string& streamId, EStreamFormat fmt,
                                        const std::string& ep, const Json::Value& data)
        {
            switch (fmt)
            {
            case EMP4:
            {
                std::string videoEp("hosts/" + ep);
                NWebBL::AuxillaryCallback_t ac = boost::bind(&CWSContext::runLiveMP4, shared_from_base<CWSContext>(), sc, streamId, data, videoEp, _1);

                NWebBL::ResolveCameraAudioStream(GET_LOGGER_PTR, m_grpcManager,
                                                 m_metaCredentialsStorage->GetMetaCredentials(), videoEp, ac);
                break;
            }
            case EJPEG:
            {
                NMMSS::PPullStyleSink sink(NPluginHelpers::CreateSink(
                    boost::bind(&CWSContext::onSampleHandler, boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()),
                    WStreamContext(sc), boost::posix_time::max_date_time, _1)));

                int width = 0, height = 0, vc = 1;
                float crop_x = 0.f, crop_y = 0.f, crop_width = 1.f, crop_height = 1.f;
                parseJpegParameters(data, width, height, crop_x, crop_y, crop_width, crop_height, vc);

                PClientContext cc(m_videoSourceCache->CreateClientContext("hosts/" + ep, width, height, vc, sink));
                if (!cc)
                {
                    _err_ << "Cannot create JPEG context for stream " << streamId;
                    NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                    onSampleProcessing(sc, data);
                    return false;
                }
                sc->m_clientContext = cc;

                break;
            }

            default:
            {
                _wrn_ << "CMediaContext::ProcessLivePlayCommand: unhandled format: " << fmt;
                break;
            }

            } // switch

            return true;
        }

        static void processCameras(std::shared_ptr<std::string> ctxOut, std::string const &archiveName, const std::string &ep, const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);
                int arcCount = c.archive_bindings_size();
                for (int j = 0; j < arcCount; ++j)
                {
                    const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                    auto it = std::find_if(ab.sources().begin(), ab.sources().end(),
                        [&](const bl::domain::StorageSource& c)
                    {
                        return c.media_source() == ep && c.storage() == archiveName;
                    });

                    if (it == ab.sources().end())
                        continue;

                    *ctxOut = it->access_point();
                    return;
                }
            }
        }

        void execProcessArchivePlayCommand_JPEG(NCorbaHelpers::PContainer cont, const std::string& streamId,
                                                const Json::Value& data, const std::string& archiveName,
                                                const std::string& archiveTime, const std::string& arc_accessPoint)
        {
            try
            {
                MMSS::StorageSource_var storageSource = NCorbaHelpers::ResolveServant<MMSS::StorageSource>(cont.Get(), arc_accessPoint);

                if (!CORBA::is_nil(storageSource))
                {
                    int speed = 1;
                    if (data.isMember(JSON_QUERY_SPEED_FIELD))
                    {
                        speed = data[JSON_QUERY_SPEED_FIELD].asInt();
                    }

                    int width = 0, height = 0, vc = 1;
                    float crop_x = 0.f, crop_y = 0.f, crop_width = 1.f, crop_height = 1.f;
                    parseJpegParameters(data, width, height, crop_x, crop_y, crop_width, crop_height, vc);

                    if (0 == speed)
                    {
                        PStreamContext sc = std::make_shared<SStreamContext>(GET_LOGGER_PTR, streamId);
                        addContext(streamId, sc);

                        MMSS::StorageEndpoint_var endpoint =
                            NPluginUtility::ResolveEndoint(m_grpcManager,
                                                           m_metaCredentialsStorage->GetMetaCredentials(),
                                                           cont.Get(), arc_accessPoint,
                                                           archiveTime,
                                                           axxonsoft::bl::archive::START_POSITION_EXACTLY, 0);
                        if (!CORBA::is_nil(endpoint))
                        {
                            NMMSS::PPullStyleSink sink(NPluginHelpers::CreateSink(
                                boost::bind(&CWSContext::onSampleHandler, boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()),
                                    WStreamContext(sc), boost::posix_time::from_iso_string(archiveTime), _1)));

                            NMMSS::MediaMuxer::POutputStreamed os(
                                NMMSS::CreateOutputToPullSink(GET_LOGGER_PTR));

                            sc->m_output = os;

                            NMMSS::GetConnectionBroker()->SetConnection(os.Get(), sink.Get(), GET_LOGGER_PTR);

                            std::shared_ptr<NCorbaHelpers::IResource> snapshot(NMMSS::CreateSnapshot(GET_LOGGER_PTR,os.Get(),
                                endpoint.in(), vc, boost::posix_time::seconds(60), width, height));
                            sc->m_snapshot = snapshot;
                        }
                    }
                    else
                    {
                        PSyncContext syncCtx = std::make_shared<SSyncContext>(GET_LOGGER_PTR, streamId, cont.Get());
                        addContext(streamId, syncCtx);

                        PClientContext cc;
                        try {
                            bool keyFrames = false;
                            if (data.isMember(JSON_QUERY_KEYFRAMES_FIELD))
                            {
                                keyFrames = data[JSON_QUERY_KEYFRAMES_FIELD].asBool();
                            }

                            bool forward = false;
                            if (data.isMember(JSON_QUERY_FORWARD_FIELD))
                            {
                                forward = data[JSON_QUERY_FORWARD_FIELD].asBool();
                            }

                            PStreamContext sc = std::make_shared<SStreamContext>(GET_LOGGER_PTR, streamId);

                            NHttp::PArchiveContext aCtx = boost::make_shared<NHttp::SJpegArchiveContext>(GET_LOGGER_PTR, width, height);
                            aCtx->videoEndpoint = std::move(arc_accessPoint);
                            aCtx->archiveName = std::move(archiveName);
                            aCtx->startTime = archiveTime;
                            aCtx->keyFrames = keyFrames;
                            aCtx->speed = speed;
                            aCtx->forward = forward;

                            cc = m_videoSourceCache->CreateArchiveMp4Context(m_grpcManager, m_metaCredentialsStorage->GetMetaCredentials(),
                                            m_response, syncCtx->m_sync, aCtx,
                                            boost::bind(&CWSContext::onDataSinkDisconnected,
                                                        boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()),
                                                        WStreamContext(sc)));

                            if (cc)
                            {
                                cc->Init(
                                    boost::bind(&CWSContext::onDataBufferHandler,
                                        boost::weak_ptr<CWSContext>(shared_from_base<CWSContext>()), WStreamContext(sc), _1));
                                sc->m_clientContext = cc;
                            }

                            syncCtx->AddStreamContext(sc);
                        }
                        catch (...) {}

                        if (!cc)
                        {
                            _err_ << "Cannot create archive JPEG context for stream " << streamId;
                            NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                            onSampleProcessing(syncCtx, data);
                            return;
                        }

                        syncCtx->Start(speed);
                    }
                }
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "Cannot get archive JPEG. Reason: " << e._info();
            }
        }

        bool ProcessArchivePlayCommand(NCorbaHelpers::PContainer cont, const std::string& streamId,
                                       EStreamFormat fmt, const std::string& ep,
                                       const std::string& archiveTime, const Json::Value& data)
        {
            std::string archiveName;
            if (data.isMember(JSON_QUERY_ARCHIVE_FILED))
            {
                archiveName.assign(data[JSON_QUERY_ARCHIVE_FILED].asString());
            }

            switch (fmt)
            {
            case EMP4:
            {
                PSyncContext sc = std::make_shared<SSyncContext>(GET_LOGGER_PTR, streamId, cont.Get());
                addContext(streamId, sc);

                _log_ << "Prepare MP4 endpoints streamId " << streamId << " for archive " << archiveName << " with video source " << ep;
                NWebBL::AuxillaryCallback_t ac = boost::bind(&CWSContext::prepareMP4Endpoints,
                    shared_from_base<CWSContext>(), sc, streamId, data, archiveName, archiveTime, ep, _1);
                NWebBL::ResolveCameraAudioStream(GET_LOGGER_PTR, m_grpcManager,
                                                 m_metaCredentialsStorage->GetMetaCredentials(), ep, ac);
                return true;
            }
            case EJPEG:
            {
                ArchiveCommand_t ac = std::bind(&CWSContext::execProcessArchivePlayCommand_JPEG, shared_from_base<CWSContext>(),
                    cont, streamId, data, archiveName, archiveTime, std::placeholders::_1);
                execArchiveCommand(archiveName, ep, ac);

                return true;
            }
            default:
            {
                _wrn_ << "CMediaContext::ProcessArchivePlayCommand: unhandled format: " << fmt;
                break;
            }

            } // switch(fmt)

            return false;
        }

        static void onDataSinkDisconnected(boost::weak_ptr<CWSContext> obj, WStreamContext ctx)
        {
            boost::shared_ptr<CWSContext> owner = obj.lock();
            if (owner)
            {
                owner->dataSinkDisconnected(ctx);
            }
        }

        void dataSinkDisconnected(WStreamContext ctx)
        {
            PStreamContext streamContext = ctx.lock();
            if (streamContext)
            {
                _log_ << "WS: Data stream " << streamContext->GetStreamId() << " disconnected";
                NHttp::PDataBuffer data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(boost::posix_time::max_date_time));
                onSampleProcessing(streamContext, data);
            }
        }

        static void onDataBufferHandler(boost::weak_ptr<CWSContext> obj, WStreamContext ctx, NHttp::PDataBuffer buffer)
        {
            boost::shared_ptr<CWSContext> owner = obj.lock();
            if (owner)
            {
                PStreamContext streamContext = ctx.lock();
                if (streamContext)
                {
                    owner->onSampleProcessing(streamContext, buffer);
                }
            }
        }

        static void onSampleHandler(boost::weak_ptr<CWSContext> obj, WMediaContext ctx, boost::posix_time::ptime ts,
                                                NMMSS::ISample* sample)
        {
            boost::shared_ptr<CWSContext> owner = obj.lock();
            if (owner)
            {
                PMediaContext streamContext = ctx.lock();
                if (streamContext)
                {
                    NHttp::PDataBuffer data;
                    if (NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&(sample->Header())))
                        data = NHttp::CreateEoSBuffer(NMMSS::PtimeToQword(ts));
                    else
                        data = NHttp::CreateDataBufferFromSample(sample, true);
                    owner->onSampleProcessing(streamContext, data);
                }
            }
        }

        void onSampleProcessing(PMediaContext ctx, NHttp::PDataBuffer buffer)
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            if (!m_processing)
            {
                m_processing = true;
                lock.unlock();

                sendFrame(ctx, buffer);
            }
            else
            {
                if (!buffer->IsUrgent())
                {
                    if (m_dataToSend.size() >= SAMPLE_THRESHOLD)
                    {
                        _wrn_ << "Drop sample for stream " << ctx->GetStreamId();
                        m_drop = true;
                        return;
                    }

                    if (m_drop && !buffer->IsKeyData())
                    {
                        _wrn_ << "Drop delta frame for stream " << ctx->GetStreamId();
                        return;
                    }
                    else if (m_drop)
                    {
                        m_drop = false;
                    }
                }

                ctx->AddData(buffer);
                m_dataToSend.push_back(WMediaContext(ctx));
            }
        }

        void sendFrame(PMediaContext ctx, NHttp::PDataBuffer s)
        {
            NWebWS::WSData message = std::make_shared<NWebWS::WSDataPresentation>();

            message->push_back(FIRST_BIT_MASK | NWebWS::EBinaryFrame);

            const std::string streamId(ctx->GetStreamId());

            std::uint16_t nameLen = streamId.size();
            uint8_t* dataPtr = nullptr;

            bool isEos = s->IsEoS();

            std::uint8_t* body = isEos ? nullptr : s->GetData();
            std::uint64_t bodySize = isEos ? 0 : s->GetSize();

            std::uint8_t typeFlag = 0;

            if (!s->IsBinary() && (0 != bodySize))
            {
                bodySize = *((std::uint32_t*)body);
                body += sizeof(std::uint32_t);

                typeFlag = 1;
            }

            //_dbg_ << "Send frame for stream " << ctx->GetStreamId() << " with size " << bodySize;

            uint64_t size = bodySize + nameLen + sizeof(std::uint64_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t);

            if (size <= LOW_DATA_THRESHOLD)
            {
                message->push_back((uint8_t)size);
            }
            else if (size <= HIGH_DATA_THRESHOLD)
            {
                message->push_back((uint8_t)126);
                uint16_t dataSize = htons((uint16_t)size);
                dataPtr = (uint8_t*)&dataSize;
                message->insert(message->end(), dataPtr, dataPtr + sizeof(uint16_t));
            }
            else
            {
                message->push_back((uint8_t)127);
                uint64_t dataSize = NWebWS::hton64(size);
                dataPtr = (uint8_t*)&dataSize;
                message->insert(message->end(), dataPtr, dataPtr + sizeof(uint64_t));
            }

            message->push_back(typeFlag);

            std::uint16_t netNameLen = htons(nameLen);
            dataPtr = (uint8_t*)&netNameLen;
            message->insert(message->end(), dataPtr, dataPtr + sizeof(uint16_t));

            message->insert(message->end(), streamId.begin(), streamId.end());

            std::uint64_t dataTime = s->GetTimestamp();
            std::uint64_t netTS = NWebWS::hton64(dataTime);
            dataPtr = (uint8_t*)&netTS;
            message->insert(message->end(), dataPtr, dataPtr + sizeof(uint64_t));

            if (nullptr != body)
            {
                message->insert(message->end(), body, body + bodySize);
            }

            NWebWS::PWebSocketSession session = m_wsSession.lock();
            if (session)
            {
                session->SendData(message);
            }
        }

        EStreamFormat parseFormat(const std::string& format)
        {
            if ("mp4" == format)
                return EMP4;
            if ("jpeg" == format)
                return EJPEG;
            return EUNKNOWN_FORMAT;
        }

        EStreamState parseStreamState(const Json::Value& data)
        {
            std::string method;
            if (data.isMember(JSON_QUERY_METHOD_FIELD))
            {
                method.assign(data[JSON_QUERY_METHOD_FIELD].asString());
            }

            EStreamState st = EUNKNOWN;
            if (METHOD_FIELD_PLAY_STATE == method)
            {
                st = EPLAY;
            }
            else if (METHOD_FIELD_STOP_STATE == method)
            {
                st = ESTOP;
            }
            else if (METHOD_FIELD_BATCH_STATE == method)
            {
                st = EBATCH;
            }
            else if (METHOD_FIELD_SYNC_STATE == method)
            {
                st = ESYNC;
            }
            else if (METHOD_FIELD_TOKEN_STATE == method)
            {
                st = ETOKEN;
            }
            return st;
        }

        void parseJpegParameters(const Json::Value& data, int& width, int& height, 
                                 float &crop_x, float &crop_y, float &crop_width, float &crop_height,
                                 int& vc)
        {
            if (data.isMember(JSON_QUERY_WIDTH_FIELD))
                width = data[JSON_QUERY_WIDTH_FIELD].asInt();

            if (data.isMember(JSON_QUERY_HEIGHT_FIELD))
                height = data[JSON_QUERY_HEIGHT_FIELD].asInt();

            if (data.isMember(JSON_QUERY_CROP_X_FIELD))
                crop_x = data[JSON_QUERY_CROP_X_FIELD].asFloat();

            if (data.isMember(JSON_QUERY_CROP_Y_FIELD))
                crop_y = data[JSON_QUERY_CROP_Y_FIELD].asFloat();

            if (data.isMember(JSON_QUERY_CROP_WIDTH_FIELD))
                crop_width = data[JSON_QUERY_CROP_WIDTH_FIELD].asFloat();

            if (data.isMember(JSON_QUERY_CROP_HEIGHT_FIELD))
                crop_height = data[JSON_QUERY_CROP_HEIGHT_FIELD].asFloat();

            if (data.isMember(JSON_QUERY_VC_FIELD))
                vc = data[JSON_QUERY_VC_FIELD].asInt();
        }

        void sendWSError(std::string&& reason)
        {
            _err_ << reason;

            NWebWS::PWebSocketSession session = m_wsSession.lock();
            if (session)
            {
                session->SendError();
            }
        }

        void addContext(const std::string& streamId, PMediaContext sc)
        {
            std::unique_lock<std::mutex> lock(m_streamMutex);
            TStreams::iterator it = m_streams.find(streamId);
            if (m_streams.end() != it)
            {
                _wrn_ << "Try to play stream " << streamId << " again";
                return;
            }
            else
            {
                m_streams.insert(std::make_pair(streamId, sc));
            }
        }

        void execArchiveCommand(const std::string& archiveName, const std::string& ep, ArchiveCommand_t ac)
        {
            NGrpcHelpers::PCredentials systemCred = NGrpcHelpers::NGPAuthTokenCallCredentials(NSecurityManager::CreateSystemSession(GET_LOGGER_PTR, __FUNCTION__));

            PBatchCameraReader_t grpcReader(new BatchCameraReader_t
            (GET_LOGGER_PTR, m_grpcManager, systemCred, &bl::domain::DomainService::Stub::AsyncBatchGetCameras));

            bl::domain::BatchGetCamerasRequest creq;
            bl::domain::ResourceLocator* rl = creq.add_items();
            rl->set_access_point(NPluginUtility::convertToMainStream(ep));

            auto ctxOut = std::make_shared<std::string>();
            grpcReader->asyncRequest(creq, [this, archiveName, ep, ctxOut, ac](const bl::domain::BatchGetCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
            {
                if (!grpcStatus.ok())
                {
                    std::string s = "nativeBl returned error on AsyncBatchGetCmeras-1:" + ep + "|arc:" + archiveName;
                    _err_ << s;
                }

                processCameras(ctxOut, "hosts/" + archiveName, ep, res.items());

                if (NWebGrpc::_FINISH == status)
                {
                    std::string arc_accessPoint = *ctxOut;
                    if (!arc_accessPoint.empty())
                    {
                        NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(std::bind(ac, arc_accessPoint));
                    }
                    else
                    {
                        std::string s = "nativeBl returned error on AsyncBatchGetCmeras-2:" + ep + "|arc:" + archiveName;
                        _err_ << s;
                    }
                }
            }
            );
        }

        NCorbaHelpers::WPContainer m_container;
        PResponse m_response;

        NWebWS::WWebSocketSession m_wsSession;
        NHttp::PVideoSourceCache m_videoSourceCache;
        NHttp::PStatisticsCache m_statisticsCache;

        std::mutex m_streamMutex;
        typedef std::map<std::string, PMediaContext> TStreams;
        TStreams m_streams;

        //TODO: for logging. After fix some problem, unroll!!!
        typedef std::deque<std::pair<WMediaContext, unsigned long long>> TSampleContexts;
        TSampleContexts m_samplesToSend;
        std::deque<WMediaContext> m_dataToSend;
        std::mutex m_dataMutex;
        NWebGrpc::PGrpcManager m_grpcManager;
        PMetaCredentialsStorage m_metaCredentialsStorage;
        const NPluginUtility::PRigthsChecker m_rightsChecker;
        NHttp::SConfig& m_webCfg;

        bool m_drop;
        bool m_processing;
    };

    class CWebSocketContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CWebSocketContentImpl(NCorbaHelpers::IContainer *c,
                              UrlBuilderSP rtspUrls,
                              const NWebGrpc::PGrpcManager grpcManager,
                              const NPluginUtility::PRigthsChecker rightsChecker,
                              NHttp::PVideoSourceCache videoSourceCache,
                              const NHttp::PStatisticsCache& statisticsCache,
                              NHttp::SConfig& webCfg)
            : m_container(c, NCorbaHelpers::ShareOwnership())
            , m_uriBuilder(rtspUrls)
            , m_grpcManager(grpcManager)
            , m_rightsChecker(rightsChecker)
            , m_videoSourceCache(videoSourceCache)
            , m_statisticsCache(statisticsCache)
            , m_webCfg(webCfg)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            namespace npu = NPluginUtility;

            npu::TParams params;
            if (!npu::ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            int enableTokenAuth = 0;
            int validForHours = 0;
            npu::GetParam(params, PARAM_ENABLE_TOKEN_AUTH, enableTokenAuth, 0);
            npu::GetParam(params, PARAM_TOKEN_VALID_HOURS, validForHours, 12);
            auto expiresAt = boost::posix_time::second_clock::local_time() +
                boost::posix_time::hours(std::min(abs(validForHours), 24 * 7));  // Lease a week at max.]

            if (enableTokenAuth)
            {
                m_uriBuilder->handleHttpToken(resp, req, params, expiresAt);
                return;
            }

            boost::optional<const std::string&> h = req->GetHeader("Sec-WebSocket-Key");

            std::string wsKey;
            if (h)
                wsKey.assign(*h);

            if (wsKey.empty())
            {
                _err_ << "Query does not contain Sec-WebSocket-Key header";
                Error(resp, IResponse::BadRequest);
                return;
            }

            wsKey.append(SALT_GUID);

            uint8_t hash[SHA_DIGEST_LENGTH];
            calculateSHA1(&wsKey[0], wsKey.size(), hash);

            std::string acceptKey(NCrypto::ToBase64Padded(hash, SHA_DIGEST_LENGTH));

            resp->SetStatus(NHttp::IResponse::SwitchingProtocols);
            resp << SHttpHeader("Upgrade", "websocket");
            resp << SHttpHeader("Connection", "Upgrade");
            resp << SHttpHeader("Sec-WebSocket-Accept", acceptKey);
            resp->FlushHeaders();

            const std::string subscriptionId(NCorbaHelpers::GenerateUUIDString());

            std::string authToken;
            npu::GetParam(params, PARAM_AUTH_TOKEN, authToken, {});

            NWebWS::PWebSocketSession session =
                NWebWS::CreateWebSocketSession(GET_LOGGER_PTR,
                                               req,
                                               resp,
                                               std::bind(&CWebSocketContentImpl::onWSSessionClose,
                                                         boost::weak_ptr<CWebSocketContentImpl>(shared_from_base<CWebSocketContentImpl>()),
                                                         subscriptionId));

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_sessions.insert(std::make_pair(subscriptionId, session));
            }

            NGrpcHelpers::PCredentials metaCredentials = getCredentials(authToken, req);
            NWebWS::PWebSocketClient wsClient = boost::make_shared<CWSContext>(m_container.Get(),
                                                                               resp,
                                                                               req,
                                                                               session,
                                                                               m_grpcManager,
                                                                               m_rightsChecker,
                                                                               metaCredentials,
                                                                               m_videoSourceCache,
                                                                               m_statisticsCache,
                                                                               m_webCfg);

            try
            {
                session->Start(wsClient);
            }
            catch (...)
            {
                _err_ << "Cannot create web socket session";
            }
        }

    private:
        bool calculateSHA1(void* input, unsigned long length, unsigned char* md)
        {
            SHA_CTX context;
            if (!SHA1_Init(&context))
                return false;

            if (!SHA1_Update(&context, (unsigned char*)input, length))
                return false;

            if (!SHA1_Final(md, &context))
                return false;

            return true;
        }

        static void onWSSessionClose(boost::weak_ptr<CWebSocketContentImpl> obj, const std::string& id)
        {
            boost::shared_ptr<CWebSocketContentImpl> owner = obj.lock();
            if (owner)
            {
                owner->clearWSSession(id);
            }
        }

        void clearWSSession(const std::string& id)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_sessions.erase(id);
        }

        NGrpcHelpers::PCredentials getCredentials(const std::string& authToken, const NHttp::PRequest req)
        {
            if (!authToken.empty())
            {
                return NGrpcHelpers::NGPAuthTokenCallCredentials(authToken);
            }
            return NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, req->GetAuthSession());
        }

        NCorbaHelpers::PContainer m_container;
        UrlBuilderSP m_uriBuilder;

        std::mutex m_mutex;
        typedef std::map<std::string, NWebWS::PWebSocketSession> TWSSessions;
        TWSSessions m_sessions;
        NWebGrpc::PGrpcManager m_grpcManager;
        const NPluginUtility::PRigthsChecker m_rightsChecker;
        NHttp::PVideoSourceCache m_videoSourceCache;
        NHttp::PStatisticsCache m_statisticsCache;
        NHttp::SConfig& m_webCfg;
    };

    std::uint32_t CWSContext::SBatchContext::m_sessionId = 0;
}

namespace NHttp
{
    IServlet* CreateWebSocketServlet(NCorbaHelpers::IContainer* c,
                                     UrlBuilderSP rtspUrls,
                                     const NWebGrpc::PGrpcManager grpcManager,
                                     const NPluginUtility::PRigthsChecker rightsChecker,
                                     NHttp::PVideoSourceCache videoSourceCache,
                                     const NHttp::PStatisticsCache& statisticsCache,
                                     NHttp::SConfig& webCfg)
    {
        return new CWebSocketContentImpl(c, rtspUrls, grpcManager, rightsChecker, videoSourceCache, statisticsCache, webCfg);
    }
}
