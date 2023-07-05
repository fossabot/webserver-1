#include <deque>
#include <memory>
#include <forward_list>
#include <condition_variable>
#include <numeric>
#include <regex>

#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "Constants.h"
#include "DataSink.h"
#include "Gstreamer.h"
#include "GstreamerMeta.h"
#include "CommonUtility.h"
#include "GrpcReader.h"
#include "HttpPlugin.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // conversion from 'gssize' to 'guint', possible loss of data 
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtsp-server/rtsp-server.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <MediaType.h>
#include <PtimeFromQword.h>
#include <Logging/log2.h>
#include <MMCoding/Transforms.h>
#include <MMTransport/MMTransport.h>
#include <MMTransport/QualityOfService.h>
#include <Crypto/Crypto.h>
#include <HttpServer/HttpServer.h>
#include <HttpServer/BasicServletImpl.h>

#include <grpc++/grpc++.h>
#include <grpc++/security/credentials.h>

#include <NativeBLClient/AsyncEventReader.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>
#include <axxonsoft/bl/statistics/Statistics.grpc.pb.h>
#include <axxonsoft/bl/security/SecurityService.grpc.pb.h>
#include <axxonsoft/bl/events/Notification.grpc.pb.h>
#include <axxonsoft/bl/events/Events.Internal.grpc.pb.h>

#include <SecurityManager/SecurityManager.h>
#include <SecurityManager/JWT.h>

#include <json/json.h>

namespace
{
    const std::size_t BUC_TRESHOLD = 10;

    const uint32_t SAMPLE_TIMEOUT = 5000;
    const uint32_t AUDIO_SAMPLE_TIMEOUT = 1000;
    const std::uint16_t AUDIO_SAMPLE_RATE = 8000;

    const std::uint16_t LIST_CAMERA_CALL_TIMEOUT_MS = 3000;

    const char* const SPEED_PARAM = "speed";
    const std::uint8_t SPEED_PARAM_LEN = 6;

    const char* const HMAC_PARAM = "hmac";
    const std::uint8_t HMAC_PARAM_LEN = 5;

    const char* const EXP_PARAM = "exp";
    const std::uint8_t EXP_PARAM_LEN = 4;

    const char* const ARCHIVE_PREFIX = "archive";
    const std::size_t ARCHIVE_PREFIX_LEN = 8;

    const char* const ONVIF_PREFIX = "onvif";
    const std::size_t ONVIF_PREFIX_LENGTH = 6;

    const unsigned long long EPOCH = 2208988800ULL;

    const char* const VIDEO_STREAM_MASK = "/SourceEndpoint.video:";

    const char* const VIDEO_ARCHIVE_PIPELINE_MASK = "appsrc name=vsrc ! queue ! %1% ! %2% name=pay0 pt=96 ";
    const char* const VIDEO_LIVE_PIPELINE_MASK = "appsrc name=vsrc is-live=true do-timestamp=true ! queue ! %1% ! %2% name=pay0 pt=96 ";

    gboolean authentificateAndAuthorize(GstRTSPAuth *auth, GstRTSPContext *ctx);
    GstRTSPStatusCode close_callback(GstRTSPClient * self, gpointer user_data);

    std::string parseDataEndpoint(gchar* path, std::string& prefix);

    namespace bl = axxonsoft::bl;
    using namespace NSecurityManager;

    using StatisticsReader_t = NWebGrpc::AsyncResultReader < bl::statistics::StatisticService, bl::statistics::StatsRequest,
        bl::statistics::StatsResponse>;
    using PStatisticsReader_t = std::shared_ptr < StatisticsReader_t >;
    using StatisticsCallback_t = std::function<void(const bl::statistics::StatsResponse&, grpc::Status)>;

    using ListCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::ListCamerasRequest,
        bl::domain::ListCamerasResponse >;
    using PListCameraReader_t = std::shared_ptr < ListCameraReader_t >;
    using CameraListCallback_t = std::function<void(const bl::domain::ListCamerasResponse&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    using EventReader_t = NWebGrpc::AsyncStreamReader < bl::events::DomainNotifier, bl::events::PullEventsRequest,
        bl::events::Events >;
    using PEventReader_t = std::shared_ptr < EventReader_t >;
    using EventCallback_t = std::function<void(const bl::events::Events&, NWebGrpc::STREAM_ANSWER, grpc::Status)>;

    using SubscriptionReader_t = NWebGrpc::AsyncResultReader < bl::events::DomainNotifier, bl::events::UpdateSubscriptionRequest,
        bl::events::UpdateSubscriptionResponse>;
    using PSubscriptionReader_t = std::shared_ptr < SubscriptionReader_t >;
    using SubscriptionCallback_t = std::function<void(const bl::events::UpdateSubscriptionResponse&, grpc::Status)>;

    struct ConnectionInfo
    {
        ConnectionInfo(const std::string &path_, const std::string &user_) :
            path(path_),
            user(user_)
        {}
            
        std::string path;
        std::string user;
    };

    using Range_t = std::pair<std::string, std::string>;
    Range_t parseRangeHeader(DECLARE_LOGGER_ARG, const std::string& value)
    {
        // utc-range = "clock" ["=" utc-range-spec]
        // utc-range-spec = ( utc-time "-" [ utc-time ] ) / ( "-" utc-time )
        // utc-time = utc-date "T" utc-clock "Z"
        // utc-date = 8DIGIT
        // utc-clock = 6DIGIT [ "." 1*9DIGIT ]

        Range_t range;

        const std::regex RANGE_EXP("clock=(\\d{8}T+\\d{6}(.\\d{1,9})?)Z-((\\d{8}T+\\d{6}(.\\d{1,9})?)Z)?");
        const size_t BEGIN_TIME = 1;
        const size_t END_TIME = 4;

        std::smatch match;

        if (!std::regex_search(value, match, RANGE_EXP))
        {
            _err_ << "Can't parse range header with value: " << value;
            return range;
        }

        range.first = match[BEGIN_TIME];

        if (match.size() >= END_TIME)
            range.second = match[END_TIME];

        return range;
    }

    std::string constructVideoPipeline(std::uint32_t fourCC, const char* const pipelineMask)
    {
        if (fourCC == NMMSS::NMediaType::Video::fccH264::ID)
            return (boost::format(pipelineMask) % "h264parse" % "rtph264pay").str();
        else if (fourCC == NMMSS::NMediaType::Video::fccH265::ID)
            return (boost::format(pipelineMask) % "h265parse" % "rtph265pay").str();
        else if (fourCC == NMMSS::NMediaType::Video::fccJPEG::ID)
            return (boost::format(pipelineMask) % "jpegparse" % "rtpjpegpay").str();
        else if (fourCC == NMMSS::NMediaType::Video::fccMPEG4::ID)
            return (boost::format(pipelineMask) % "mpeg4videoparse" % "rtpmp4vpay").str();
        else
            return "";
    }

    void processCameras(std::vector<std::string> &ctxOut, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
    {
        int itemCount = cams.size();
        for (int i = 0; i < itemCount; ++i)
        {
            const bl::domain::Camera& c = cams.Get(i);

            int vsCount = c.video_streams_size();
            for (int j = 0; j < vsCount; ++j)
            {
                const bl::domain::VideoStreaming& vs = c.video_streams(j);
                ctxOut.push_back(vs.stream_acess_point());
            }
        }
    }

    bool verifyAudioFormat(std::uint32_t fourCC)
    {
        switch (fourCC)
        {
        case NMMSS::NMediaType::Audio::G711::ID:
        case NMMSS::NMediaType::Audio::G726::ID:
        case NMMSS::NMediaType::Audio::IMA_WAV::ID:
        case NMMSS::NMediaType::Audio::MP1::ID:
        case NMMSS::NMediaType::Audio::MP2::ID:
        case NMMSS::NMediaType::Audio::MP3::ID:
        case NMMSS::NMediaType::Audio::AAC::ID:
        case NMMSS::NMediaType::Audio::VORBIS::ID:
            return true;
        default:
            return false;
        }
    }

    int parseArchiveSpeed(const std::string& ctxPath)
    {
        size_t speedPos = ctxPath.find(SPEED_PARAM);
        if (std::string::npos == speedPos)
            throw std::logic_error("Incorrect archive query. Speed parameter is absent");

        size_t nextParamPos = ctxPath.find('&', speedPos);
        std::size_t eqSignPos = ctxPath.find('=', speedPos);

        return boost::lexical_cast<int>(ctxPath.substr(speedPos + SPEED_PARAM_LEN, nextParamPos - eqSignPos - 1));
    }

    bool parseKeyFrameFlag(const std::string& ctxQuery)
    {
        bool keyFrames = false;
        size_t keyFramePos = ctxQuery.find(KEY_FRAMES_PARAMETER);
        if (std::string::npos != keyFramePos)
            keyFrames = true;

        return keyFrames;
    }

    typedef gboolean(*FAuth)(GstRTSPAuth *auth, GstRTSPContext *ctx);

    struct ConnectionCtx
    {
        ConnectionCtx() :
            m_cameraAllowed(false)
            {
            }

        std::string m_audioEndpoint;
        std::string m_archiveVideoEp;
        std::string m_archiveAudioEp;
        bool m_cameraAllowed;
        NGrpcHelpers::PCredentials m_credentials;
    };

    typedef boost::shared_mutex TMutex;
    typedef boost::shared_lock<TMutex> TReadLock;
    typedef boost::unique_lock<TMutex> TWriteLock;

    struct SBaseRTSPStreamContext : public std::enable_shared_from_this<SBaseRTSPStreamContext>
    {
        SBaseRTSPStreamContext(DECLARE_LOGGER_ARG, GstRTSPMountPoints* mounts, GstRTSPMediaFactory* factory, const std::string resourcePath, int speed, bool onlyKeyFrames)
            : m_mounts(mounts)
            , m_factory(factory)
            , m_resourcePath(resourcePath)
            , m_videoSource(nullptr)
            , m_audioSource(nullptr)
            , m_streamStartTime(boost::posix_time::neg_infin)
            , m_videoLastTime(boost::posix_time::neg_infin)
            , m_videoHoleDuration(boost::posix_time::microseconds(0))
            , m_audioSampleCount(0)
            , m_prerollCount(0)
            , m_speed(speed)
            , m_onlyKeyFrames(onlyKeyFrames || (std::abs(speed) > 4))
            , m_pipelineConstructed(false)
            , m_audioSampleRate(AUDIO_SAMPLE_RATE * std::abs(m_speed))
        {
            INIT_LOGGER_HOLDER;
        }

        virtual ~SBaseRTSPStreamContext()
        {
            gst_rtsp_mount_points_remove_factory(m_mounts, m_resourcePath.c_str());
        }

        void SetDataSources(GstElement* videoSource, GstElement* audioSource)
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            m_videoSource = videoSource;
            m_audioSource = audioSource;
        }

        void SetFactoryPermissions(const std::string& user, const std::string& pass)
        {
            TWriteLock lock(m_userMutex);
            auto it = m_users.find(user);
            if (m_users.end() != it)
                return;
            m_users.insert(std::make_pair(user, 0));

            _log_ << "Set factory permissions for user " << user;

            GstRTSPPermissions* perm = gst_rtsp_permissions_new();

            auto it1 = m_users.begin(), it2 = m_users.end();
            for (; it1 != it2; ++it1)
                gst_rtsp_permissions_add_role(perm, it1->first.c_str(),
                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

            gst_rtsp_media_factory_set_permissions(m_factory, perm);
            gst_rtsp_permissions_unref(perm);
        }

        void CreatePipeline(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c, ConnectionCtx &connCtx)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_pipelineConstructed)
            {
                std::string rtspPipeline(connectDataSources(grpcManager, credentials, c, connCtx));
                _log_ << "Constructed pipeline: " << rtspPipeline << " | " << m_stream;

                gst_rtsp_media_factory_set_launch(m_factory, rtspPipeline.c_str());

                m_pipelineConstructed = true;
            }
        }

        void Cleanup()
        {
            _log_ << "Cleanup " << m_stream << " context...";

            {
                TWriteLock lock(m_userMutex);
                m_users.clear();
            }

            {
                std::unique_lock<std::mutex> lock(m_dataMutex);
                gst_object_unref(m_videoSource);
                gst_object_unref(m_audioSource);
                m_videoSource = nullptr;
                m_audioSource = nullptr;
            }

            std::unique_lock<std::mutex> lock(m_mutex);
            {
                destroyAudioConnection();
                destroyVideoConnection();

                m_pipelineConstructed = false;
            }

            m_streamStartTime = boost::posix_time::neg_infin;
            m_videoLastTime = boost::posix_time::neg_infin;
            m_videoHoleDuration = boost::posix_time::microseconds(0);
            m_audioSampleCount = 0;            

            cleanUpSpecial();
        }

        void AddConnection(const std::string &user)
        {
            TWriteLock lock(m_userMutex);

            auto it = m_users.find(user);
            if (it != m_users.end())
                it->second++;
        }

        void RemoveConnection(const std::string &user)
        {
            TWriteLock lock(m_userMutex);

            auto it = m_users.find(user);
            if (it != m_users.end())
                it->second--;
        }

        std::map<std::string, int> GetPayloadMap() const
        {
            std::map<std::string, int> res;

            {
                TReadLock lock(m_userMutex);
                for (const auto &t : m_users)
                    if (t.second > 0)
                        res.insert(t);
            }

            return res;
        }

        int GetNumConnection(const std::string &user) const
        {
            TReadLock lock(m_userMutex);

            auto it = m_users.find(user);
            return it!= m_users.end() ? it->second:0;
        }

        int GetSpeed() const
        {
            return m_speed;
        }

        NMMSS::PSample GetAudioSample()
        {
            NMMSS::PSample s;
            if (m_audioSink)
                s = m_audioSink->PeekSample();
            return s;
        }

        virtual void SendVideoSample() {}
        virtual void SendAudioSample() {}
        virtual void Seek(const std::string& beginTime) {}

    protected:
        virtual std::string connectDataSources(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c, ConnectionCtx &connCtx) = 0;
        virtual void cleanUpSpecial() = 0;

        virtual void destroyVideoConnection() = 0;
        virtual void destroyAudioConnection() = 0;

        void sendVideoSampleAsIs(NMMSS::ISample* sample)
        {
            GstBuffer* buf = gst_buffer_new_and_alloc(sample->Header().nBodySize);

            GstMapInfo map;
            gst_buffer_map(buf, &map, GST_MAP_WRITE);

            memcpy(map.data, sample->GetBody(), sample->Header().nBodySize);

            gst_buffer_unmap(buf, &map);

            GstFlowReturn ret;
            {
                std::unique_lock<std::mutex> lock(m_dataMutex);
                if (nullptr != m_videoSource)
                    g_signal_emit_by_name(m_videoSource, "push-buffer", buf, &ret);
            }

            gst_buffer_unref(buf);
        }

        void sendVideoSample(NMMSS::ISample* sample)
        {
            GstBuffer* buf = gst_buffer_new_and_alloc(sample->Header().nBodySize);

            boost::posix_time::ptime currentTime = NMMSS::PtimeFromQword(sample->Header().dtTimeBegin);

            if (sample->Header().eFlags & NMMSS::SMediaSampleHeader::EFDiscontinuity)
            {
                if (m_videoLastTime != boost::posix_time::neg_infin)
                {
                    m_videoHoleDuration += currentTime - m_videoLastTime;
                    m_videoLastTime = boost::posix_time::neg_infin;
                }
            }

            if (sample->Header().eFlags & NMMSS::SMediaSampleHeader::EFPreroll)
            {
                GST_BUFFER_DURATION(buf) = 1;
                GST_BUFFER_PTS(buf) = m_prerollCount++;
            }
            else if (m_videoLastTime == boost::posix_time::neg_infin)
            {
                GST_BUFFER_DURATION(buf) = 1;
                GST_BUFFER_PTS(buf) = m_prerollCount++;

                m_streamStartTime = m_videoLastTime = currentTime;
            }
            else
            {
                std::uint64_t pts = (m_speed < 0) ?
                    ((m_streamStartTime - currentTime).total_nanoseconds() - m_videoHoleDuration.total_nanoseconds() + m_prerollCount) / std::abs(m_speed) :
                    ((currentTime - m_streamStartTime).total_nanoseconds() - m_videoHoleDuration.total_nanoseconds() + m_prerollCount) / std::abs(m_speed);
                GST_BUFFER_PTS(buf) = pts;
                GST_BUFFER_DURATION(buf) = (currentTime - m_videoLastTime).total_nanoseconds() / std::abs(m_speed);

                m_videoLastTime = currentTime;
            }

            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);
            GST_META_MARKING_ADD(buf, &(sample->Header()));

            GstMapInfo map;
            gst_buffer_map(buf, &map, GST_MAP_WRITE);

            memcpy(map.data, sample->GetBody(), sample->Header().nBodySize);

            gst_buffer_unmap(buf, &map);

            GstFlowReturn ret;
            g_signal_emit_by_name(m_videoSource, "push-buffer", buf, &ret);
            gst_buffer_unref(buf);
        }

        void sendAudioSampleAsIs(NMMSS::ISample* sample)
        {
            GstBuffer* buf = gst_buffer_new_and_alloc(sample->Header().nBodySize);

            GstMapInfo map;
            gst_buffer_map(buf, &map, GST_MAP_WRITE);

            memcpy(map.data, sample->GetBody(), sample->Header().nBodySize);

            gst_buffer_unmap(buf, &map);

            GstFlowReturn ret;
            {
                std::unique_lock<std::mutex> lock(m_dataMutex);
                if (nullptr != m_audioSource)
                    g_signal_emit_by_name(m_audioSource, "push-buffer", buf, &ret);
            }

            gst_buffer_unref(buf);
        }

        void sendAudioSample(NMMSS::ISample* sample)
        {
            GstBuffer* buf = gst_buffer_new_and_alloc(sample->Header().nBodySize);

            const auto* subheader = NMMSS::NMediaType::GetSampleOfSubtype<NMMSS::NMediaType::Audio::PCM::SubtypeHeader>(sample);

            std::uint32_t bSize = sample->Header().nBodySize;

            guint64 duration = gst_util_uint64_scale(bSize, GST_SECOND, subheader->nSampleRate * subheader->nChannelsCount * NMMSS::NMediaType::Audio::GetTypeSize(subheader->nSampleType));
            GST_BUFFER_DURATION(buf) = duration;
            GST_BUFFER_PTS(buf) = GST_BUFFER_DTS(buf) = m_audioSampleCount;

            m_audioSampleCount += duration;

            GstMapInfo map;
            gst_buffer_map(buf, &map, GST_MAP_WRITE);

            memcpy(map.data, sample->GetBody(), sample->Header().nBodySize);

            gst_buffer_unmap(buf, &map);

            GstFlowReturn ret;
            {
                std::unique_lock<std::mutex> lock(m_dataMutex);
                if (nullptr != m_audioSource)
                    g_signal_emit_by_name(m_audioSource, "push-buffer", buf, &ret);
            }

            gst_buffer_unref(buf);
        }

        DECLARE_LOGGER_HOLDER;
        GstRTSPMountPoints* m_mounts;
        GstRTSPMediaFactory* m_factory;
        std::string m_stream;
        std::string m_resourcePath;
        std::string m_rtspPipeline;

        std::mutex m_dataMutex;
        GstElement* m_videoSource;
        GstElement* m_audioSource;

        std::mutex m_mutex;

        boost::posix_time::ptime m_streamStartTime;
        boost::posix_time::ptime m_videoLastTime;
        boost::posix_time::time_duration m_videoHoleDuration;
        std::uint64_t m_audioSampleCount;
        std::uint64_t m_prerollCount;

        const int m_speed;
        bool m_onlyKeyFrames;

        NPluginHelpers::PRTSPRequestSink m_videoSink;
        NPluginHelpers::PRTSPRequestSink m_audioSink;

    private:
        mutable TMutex m_userMutex;
        std::map<std::string, int> m_users;

        bool m_pipelineConstructed;
        unsigned long long m_NN = 0;

        std::uint16_t m_audioSampleRate;
    };
    typedef std::shared_ptr<SBaseRTSPStreamContext> PRTSPStreamContext;

    struct SLiveRTSPStreamContext : public SBaseRTSPStreamContext
    {
        SLiveRTSPStreamContext(DECLARE_LOGGER_ARG, GstRTSPMountPoints* mounts, GstRTSPMediaFactory* factory, const std::string resourcePath,
            const std::string& pipeline, bool keyFramesOnly, NHttp::PMMCache cache)
            : SBaseRTSPStreamContext(GET_LOGGER_PTR, mounts, factory, resourcePath, 1, keyFramesOnly)
            , m_prerollCount(0)
            , m_mmCache(cache)
            , m_videoConnection(nullptr)
            , m_originConnection(nullptr)
            , m_audioConnection(nullptr)
        {
            this->m_stream.assign(resourcePath.substr(1));
            this->m_rtspPipeline.assign(pipeline);
        }
        ~SLiveRTSPStreamContext()
        {
            Cleanup();
        }
        void SendVideoSample() override
        {
            NPluginHelpers::PRTSPRequestSink videoSink;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!this->m_videoSink)
                    return;

                videoSink = this->m_videoSink;
            }

            NMMSS::PSample sample = this->m_videoSink->GetSample();
            while (sample && (sample->Header().eFlags & NMMSS::SMediaSampleHeader::EFPreroll) && !NPluginUtility::IsKeyFrame(sample.Get()))
            {
                sample = this->m_videoSink->GetSample();
            }

            if (sample)
                sendVideoSampleAsIs(sample.Get());
        }
        void SendAudioSample() override
        {
            NPluginHelpers::PRTSPRequestSink audioSink;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (!this->m_audioSink)
                    return;

                audioSink = this->m_audioSink;
            }

            NMMSS::PSample s = this->m_audioSink->GetSample();
            if (s)
                sendAudioSample(s.Get());
        }
    private:
        std::string connectDataSources(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c, ConnectionCtx &connCtx) override
        {
            this->m_videoSink = NPluginHelpers::CreateRTSPGstreamerSink(GET_LOGGER_PTR,
                []() {}, SAMPLE_TIMEOUT);

            std::string rtspPipeline(this->m_rtspPipeline);
            createLiveConnection(c, rtspPipeline, connCtx.m_audioEndpoint);

            gst_rtsp_media_factory_set_shared(this->m_factory, TRUE);

            return rtspPipeline;
        }
        void destroyVideoConnection() override
        {
            if (nullptr != m_videoConnection)
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(m_videoConnection);
                m_videoConnection = nullptr;
            }

            if (this->m_videoSink)
            {
                this->m_videoSink->Stop();
                this->m_videoSink.Reset();
            }
        }
        void destroyAudioConnection() override
        {
            if (nullptr != m_audioConnection)
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(m_audioConnection);
                m_audioConnection = nullptr;
            }
            if (nullptr != m_originConnection)
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(m_originConnection);
                m_originConnection = nullptr;
            }
            if (this->m_audioSink)
            {
                this->m_audioSink->Stop();
                this->m_audioSink.Reset();
            }
        }

        void cleanUpSpecial() override
        {
        }

        void createLiveConnection(NCorbaHelpers::IContainer* c, std::string& rtspPipeline, const std::string &aes)
        {
            auto qos = NMMSS::MakeQualityOfService();
            NMMSS::SetRequest(qos, MMSS::QoSRequest::StartFrom{ MMSS::QoSRequest::StartFrom::Preroll });
            if (this->m_onlyKeyFrames)
                NMMSS::SetRequest(qos, MMSS::QoSRequest::OnlyKeyFrames{ true });

            if (!aes.empty())
            {
                CORBA::Object_var obj = c->GetRootNC()->resolve_str(aes.c_str());
                if (!CORBA::is_nil(obj))
                {
                    MMSS::Endpoint_var endpoint = MMSS::Endpoint::_narrow(obj);
                    if (!CORBA::is_nil(endpoint))
                    {
                        MMSS::EndpointStatistics_var es = endpoint->GetStatistics();
                        std::uint32_t fourCC = es->streamType;
                        if (verifyAudioFormat(fourCC))
                        {
                            this->m_audioSink = NPluginHelpers::CreateRTSPGstreamerSink(GET_LOGGER_PTR,
                                []() {}, SAMPLE_TIMEOUT);

                            m_audioOrigin = m_mmCache->GetMMOrigin(aes.c_str(), this->m_onlyKeyFrames);
         
                            m_audioDecoder = NMMSS::CreateAudioDecoderPullFilter(GET_LOGGER_PTR);

                            m_originConnection = NMMSS::GetConnectionBroker()->SetConnection(m_audioOrigin->GetSource(), m_audioDecoder->GetSink(), GET_LOGGER_PTR);
                            m_audioConnection = NMMSS::GetConnectionBroker()->SetConnection(m_audioDecoder->GetSource(), this->m_audioSink.Get(), GET_LOGGER_PTR);
                            rtspPipeline.append("appsrc name=asrc is-live=true do-timestamp=true ! queue ! rawaudioparse name=prsr ! audioconvert ! audioresample ! audio/x-raw, format=S16BE ! rtpL16pay name=pay1 pt=97");
                        }
                    }
                }
            }

            m_videoOrigin = m_mmCache->GetMMOrigin(m_stream.c_str(), this->m_onlyKeyFrames);
            m_videoConnection = NMMSS::GetConnectionBroker()->SetConnection(m_videoOrigin->GetSource(), this->m_videoSink.Get(), GET_LOGGER_PTR);
        }

        std::uint64_t m_prerollCount;
        NHttp::PMMCache m_mmCache;

        NHttp::PMMOrigin m_videoOrigin;
        NMMSS::IConnectionBase* m_videoConnection;

        NHttp::PMMOrigin m_audioOrigin;
        NMMSS::PPullFilter m_audioDecoder;
        NMMSS::IConnectionBase* m_originConnection;
        NMMSS::IConnectionBase* m_audioConnection;
    };

    struct SArchiveRTSPStreamContext : public SBaseRTSPStreamContext
    {
        SArchiveRTSPStreamContext(DECLARE_LOGGER_ARG, GstRTSPMountPoints* mounts, GstElement **pMediaElement, GstRTSPMediaFactory* factory, 
            const std::string resourcePath, int speed, bool onlyKeyFrames)
            : SBaseRTSPStreamContext(GET_LOGGER_PTR, mounts, factory, resourcePath, speed, onlyKeyFrames)
            , m_mediaElementCtx(pMediaElement)    
            , m_needChangeSpeed(needChangeSpeed(speed))
            , m_audioConnection(nullptr)
        {}

        ~SArchiveRTSPStreamContext()
        {
            Cleanup();
        }

        void SendVideoSample() override
        {          
            bool expected = true;
            if (*m_mediaElementCtx && std::atomic_compare_exchange_strong<bool>(&m_needChangeSpeed, &expected, false))
            {
                gint64 position = 0;

                if (gst_element_query_position(*m_mediaElementCtx, GST_FORMAT_TIME, &position))
                {
                    GstEvent *seek_event = gst_event_new_seek(std::abs(this->m_speed), GST_FORMAT_TIME, (GstSeekFlags)0,
                            GST_SEEK_TYPE_SET, position, GST_SEEK_TYPE_END, 0);

                    gst_element_send_event(*m_mediaElementCtx, seek_event);
                }
                else
                {
                    _err_ << "unable to retrieve current position";
                }
            }
            
            NMMSS::PSample sample = this->m_videoSink->GetSample();
            while (sample && (sample->Header().eFlags & NMMSS::SMediaSampleHeader::EFPreroll) && !NPluginUtility::IsKeyFrame(sample.Get()))
            {
                sample = this->m_videoSink->GetSample();
            }

            if (sample)
                sendVideoSample(sample.Get());
        }

        void SendAudioSample() override
        {
            NMMSS::PSample s = this->m_audioSink->GetSample();
            if (s && !(NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header())))
            {
                sendAudioSample(s.Get());
            }
        }
    private:
        std::string connectDataSources(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c, ConnectionCtx &connCtx) override
        {
            this->m_videoSink = NPluginHelpers::CreateRTSPGstreamerSink(GET_LOGGER_PTR,
                [](){}, SAMPLE_TIMEOUT);

            std::string archiveTime;
            std::string dataEndpoint(parseArchiveParams(this->m_resourcePath, archiveTime));
            _log_ << "Use data endpoint " << dataEndpoint << " for time " << archiveTime  << " |archiveVideoEp= " << connCtx.m_archiveVideoEp
                 << " |m_archiveAudioEp= " << connCtx.m_archiveAudioEp;

            createArchiveConnection(grpcManager, credentials, c, archiveTime, (this->m_speed != 1) ? "" : connCtx.m_audioEndpoint, connCtx.m_archiveVideoEp, connCtx.m_archiveAudioEp);

            boost::posix_time::ptime videoTime = boost::posix_time::pos_infin;
            if (this->m_videoEndpoint)
            {              
                NMMSS::PSample s = this->m_videoSink->PeekSample();
                if (s && !(NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header())))
                {
                    videoTime = NMMSS::PtimeFromQword(s->Header().dtTimeBegin);
                    this->m_rtspPipeline.assign(constructVideoPipeline(s->Header().nSubtype, VIDEO_ARCHIVE_PIPELINE_MASK));
                }
                else
                {
                    _err_ << "No video data for stream " << dataEndpoint;
                    return "";
                }
            }

            boost::posix_time::ptime audioTime = boost::posix_time::pos_infin;
            if (this->m_audioEndpoint)
            {
                NMMSS::PSample s = this->m_audioSink->PeekSample();
                if (s && !(NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header())))
                {
                    audioTime = NMMSS::PtimeFromQword(s->Header().dtTimeBegin);
                    this->m_rtspPipeline.append("appsrc name=asrc ! rawaudioparse name=prsr ! audioconvert ! voaacenc ! aacparse ! rtpmp4apay name=pay1 pt=97");
                }
                else
                {
                    _wrn_ << "No audio data for stream " << dataEndpoint;
                    this->destroyAudioConnection();
                }
            }

            this->m_streamStartTime = (audioTime.is_special()) ? videoTime : audioTime;
            if (this->m_streamStartTime.is_special())
                return "";

            return this->m_rtspPipeline;
        }
        void destroyVideoConnection() override
        {
            if (m_videoEndpoint)
            {
                m_videoEndpoint->Destroy();
                m_videoEndpoint.Reset();
            }

            if (this->m_videoSink)
            {
                this->m_videoSink->Stop();
                this->m_videoSink.Reset();
            }
        }
        void destroyAudioConnection() override
        {
            if (nullptr != m_audioConnection)
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(m_audioConnection);
                m_audioConnection = nullptr;
            }
            if (m_audioEndpoint)
            {
                m_audioEndpoint->Destroy();
                m_audioEndpoint.Reset();
            }
            if (this->m_audioSink)
            {
                this->m_audioSink->Stop();
                this->m_audioSink.Reset();
            }
        }

        void cleanUpSpecial() override
        {
            m_needChangeSpeed = needChangeSpeed(this->m_speed);
        }

        std::string parseArchiveParams(const std::string& resourcePath, std::string& archiveTime)
        {
            auto guidPos = resourcePath.find_last_of('/');
            auto timePos = resourcePath.substr(0, guidPos).find_last_of('/') + 1;

            archiveTime.assign(parseArchiveTime(resourcePath.substr(timePos, guidPos - timePos)));

            size_t aPos = resourcePath.find('/');
            size_t margin = ARCHIVE_PREFIX_LEN + 2;
            return resourcePath.substr(aPos + margin - 1, timePos - margin);
        }

        std::string parseArchiveTime(std::string&& archiveTime)
        {
            if (boost::iequals("past", archiveTime))
                return boost::posix_time::to_iso_string(boost::posix_time::ptime(boost::posix_time::min_date_time));
            else if (boost::iequals("future", archiveTime))
                return boost::posix_time::to_iso_string(boost::posix_time::ptime(boost::posix_time::max_date_time));
            else
                return archiveTime;
        }

        void createArchiveConnection(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c,
            const std::string& aTime, const std::string &aes, const std::string &archiveVideoEp, const std::string &archiveAudioEp)
        {
            auto qos = NMMSS::MakeQualityOfService();
            NMMSS::SetRequest(qos, MMSS::QoSRequest::StartFrom{ MMSS::QoSRequest::StartFrom::Preroll });

            long playFlags = NMMSS::PMF_NONE; 
            
            if (this->m_speed < 0)
                playFlags = NMMSS::PMF_REVERSE | NMMSS::PMF_KEYFRAMES;
            else if (this->m_onlyKeyFrames)
                playFlags = NMMSS::PMF_KEYFRAMES;

            if (this->m_speed == 1 && !aes.empty())
            {
                this->m_audioSink = NPluginHelpers::CreateRTSPGstreamerSink(GET_LOGGER_PTR,
                    []() {}, SAMPLE_TIMEOUT);

                createArchiveAudioConnection(grpcManager, credentials, c, aTime, &qos, archiveAudioEp, playFlags);
            }

            createArchiveVideoConnection(grpcManager, credentials, c, aTime, &qos, archiveVideoEp, playFlags);
        }

        void createArchiveVideoConnection(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c,
            const std::string& archiveTime, const MMSS::QualityOfService* qos, const std::string &archiveEp, long playFlags)
        {
            MMSS::StorageEndpoint_var endpoint = NPluginUtility::ResolveEndoint(grpcManager, credentials, c, archiveEp, archiveTime,
                bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS, playFlags);
            if (!CORBA::is_nil(endpoint))
            {
                this->m_videoEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, endpoint, this->m_videoSink.Get(), MMSS::EAUTO, qos);
            }
        }

        void createArchiveAudioConnection(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c,
            const std::string& archiveTime, const MMSS::QualityOfService* qos, const std::string &archiveEp, long playFlags)
        {
            MMSS::StorageEndpoint_var endpoint = NPluginUtility::ResolveEndoint(grpcManager, credentials, c, archiveEp, archiveTime, bl::archive::START_POSITION_AT_KEY_FRAME, playFlags);
            if (!CORBA::is_nil(endpoint))
            {
                this->m_audioDecoder = NMMSS::CreateAudioDecoderPullFilter(GET_LOGGER_PTR);
                this->m_audioEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, endpoint,
                    this->m_audioDecoder->GetSink(), MMSS::EAUTO, qos
                );
                m_audioConnection = NMMSS::GetConnectionBroker()->SetConnection(this->m_audioDecoder->GetSource(), this->m_audioSink.Get(), GET_LOGGER_PTR);
            }
        }

        static bool needChangeSpeed(int speed)
        {
            return std::abs(speed) != 1;
        }

    protected:
        GstElement **m_mediaElementCtx;
        std::atomic<bool> m_needChangeSpeed;

        NMMSS::PSinkEndpoint m_videoEndpoint;

        NMMSS::PSinkEndpoint m_audioEndpoint;
        NMMSS::PPullFilter m_audioDecoder;
        NMMSS::IConnectionBase* m_audioConnection;
    };

    struct SOnvifArchiveRTSPStreamContext : public SArchiveRTSPStreamContext
    {
        SOnvifArchiveRTSPStreamContext(DECLARE_LOGGER_ARG, GstRTSPMountPoints* mounts, GstElement **pMediaElement, GstRTSPMediaFactory* factory,
            const std::string resourcePath)
            : SArchiveRTSPStreamContext(GET_LOGGER_PTR, mounts, pMediaElement, factory, resourcePath, 1, false)
            , m_sessionId(0)
        {}

        void Seek(const std::string& beginTime)
        {
            if (!m_endpoint)
            {
                _err_ << "Invalid StorageEndpoint_var for" << m_resourcePath;
                return;
            }

            _log_ << "Seek for " << m_resourcePath << " with beginTime: " << beginTime;
            m_endpoint->Seek(beginTime.c_str(), MMSS::spAtKeyFrameOrAtEos, NMMSS::PMF_NONE, ++m_sessionId);
            this->m_videoSink->ClearBuffer();
        }

    private:
        std::string connectDataSources(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* c, ConnectionCtx &connCtx) override
        {
            this->m_videoSink = NPluginHelpers::CreateRTSPGstreamerSink(GET_LOGGER_PTR,
                []() {}, SAMPLE_TIMEOUT);

            auto qos = NMMSS::MakeQualityOfService();
            NMMSS::SetRequest(qos, MMSS::QoSRequest::StartFrom{ MMSS::QoSRequest::StartFrom::Preroll });

            m_endpoint = NPluginUtility::ResolveEndoint(grpcManager, credentials, c, connCtx.m_archiveVideoEp, "",
                bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS, NMMSS::PMF_NONE);
            
            if (!CORBA::is_nil(m_endpoint))
            {
                this->m_videoEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, m_endpoint, this->m_videoSink.Get(), MMSS::EAUTO, &qos);
            }
           
            boost::posix_time::ptime videoTime = boost::posix_time::pos_infin;
            if (this->m_videoEndpoint)
            {
                NMMSS::PSample s = this->m_videoSink->PeekSample();
                if (s && !(NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header())))
                {
                    auto ts = s->Header().dtTimeBegin;
                    videoTime = NMMSS::PtimeFromQword(ts);
                    this->m_rtspPipeline.assign(constructVideoPipeline(s->Header().nSubtype, VIDEO_ARCHIVE_PIPELINE_MASK));
                }
                else
                {
                    _err_ << "No video data for stream " << connCtx.m_archiveVideoEp;
                    return "";
                }
            }

            if (videoTime.is_special())
                return "";

            return this->m_rtspPipeline;
        }

        MMSS::StorageEndpoint_var   m_endpoint;
        std::size_t                 m_sessionId;
    };

class CGstreamerRTSPServer : public NPluginHelpers::IGstRTSPServer
{
    static void CleanStreamContext(gchar* resource)
    {
        auto handler = GetServer();
        if (!handler)
            return;
        handler->OnResourceCleanup(resource);
    }

    struct SResourceContext
    {
        std::map<std::string, NMMSS::PSinkEndpoint> m_videoStreams;
        std::vector<std::string> m_defferedStreams;
    };
    typedef std::shared_ptr<SResourceContext> PResourceContext;

    DECLARE_LOGGER_HOLDER;
public:
    CGstreamerRTSPServer(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed* c, int rtspPort,
        const NWebGrpc::PGrpcManager grpcManager, TokenAuthenticatorSP ta, FUserAuthenticator ua, NHttp::PMMCache cache)
        : m_container(c)
        , m_rtspPort(std::to_string(rtspPort))
        , m_grpcManager(grpcManager)
        , m_stopped(false)
        , m_hmacAuth(ta)
        , m_authentificator(ua)
        , m_mmCache(cache)
        , m_rtspServer (NULL)
        , m_mounts(NULL)
        , m_mediaElement(NULL)
        , m_authFunc(0)
        , m_checkingStreams(false)
        , m_statsTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
        , m_eventSubscriptionId(NCorbaHelpers::GenerateUUIDString())
        , m_resourceContext(std::make_shared<SResourceContext>())
    {
        INIT_LOGGER_HOLDER;
        m_systemToken = NSecurityManager::CreateSystemSession(GET_LOGGER_PTR, "HttpPlugin/GstreamerRTSPServer");
        m_systemCred = NGrpcHelpers::NGPAuthTokenCallCredentials(m_systemToken);
    }

    void OnResourceCleanup(gchar* resource)
    {
        RemoveContext(resource);
        g_free(resource);
    }

    void RemoveContext(const std::string& resourcePath)
    {
        TWriteLock lock(m_contextMutex);
        m_precreatedContexts.erase(resourcePath);
    }

    void Start() override
    {      
        m_stopped = false;
        cameraListRequest();
    }

    void Stop() override
    {
        m_stopped = true;
        m_statsTimer.cancel();

        if (m_reader)
            m_reader->asyncStop();

        {
            TWriteLock lock(m_contextMutex);
            m_precreatedContexts.clear();
        }

        if (m_rtspServer)
        {
            auto auth = gst_rtsp_server_get_auth(m_rtspServer);

            if (auth)
            {
                GstRTSPAuthClass* klass = GST_RTSP_AUTH_GET_CLASS(auth);

                if (klass)
                    klass->authenticate = m_authFunc;
            }

            gst_rtsp_server_client_filter(m_rtspServer, &onCleanup, nullptr);

            g_source_remove(m_sourceId);
            g_object_unref(m_rtspServer);
            m_rtspServer = NULL;            
        }

        if (m_mounts)
        {
            g_object_unref(m_mounts);
            m_mounts = NULL;
        }
    }

    NPluginHelpers::RtspStat GetStat() const override
    {
        NPluginHelpers::RtspStat res;

        {
            TReadLock lock(m_contextMutex);
            for (const auto &t : m_precreatedContexts)
            {
                std::map<std::string, int> mConn = t.second->GetPayloadMap();

                if (!mConn.empty())
                    res.mStat.insert(std::make_pair(t.first, mConn));
            }
        }

        return res;
    }

    void AddConnection(const std::string &path, const std::string &user)
    {
        TWriteLock lock(m_contextMutex);

        auto it = m_precreatedContexts.find(path);
        if (it != m_precreatedContexts.end())
            it->second->AddConnection(user);
    }

    void RemoveConnection(const std::string &path, const std::string &user)
    {
        TWriteLock lock(m_contextMutex);

        auto it = m_precreatedContexts.find(path);
        if (it != m_precreatedContexts.end())
            it->second->RemoveConnection(user);
    }

    void processEvents(NGrpcHelpers::PCredentials metaCredentials, const bl::events::Events& res, NWebGrpc::STREAM_ANSWER, grpc::Status)
    {
        bool needRequest = false;
        int itemCount = res.items_size();
        for (int i = 0; i < itemCount; ++i)
        {
            const bl::events::Event& ev = res.items(i);

            bl::events::IpDeviceStateChangedEvent deviceState;
            if (ev.body().UnpackTo(&deviceState))
            {
                std::string st(deviceState.State_Name(deviceState.state()));
                bl::events::IpDeviceStateChangedEvent_State state = deviceState.state();

                if (bl::events::IpDeviceStateChangedEvent_State_IPDS_DEVICE_CONFIGURATION_CHANGED == state)
                {
                    std::string objectId(deviceState.object_id_ext().access_point());
                    _log_ << "Device " << objectId << " configuration changed";

                    removeRTSPResource(objectId);

                    needRequest = true;

                    addStreamsForStaticticsCheck(metaCredentials, objectId);
                }

                if (bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_RESTORED == state ||
                    bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_LOST == state)
                {
                    std::string objectId(deviceState.object_id_ext().access_point());
                    if (std::string::npos != objectId.find(VIDEO_STREAM_MASK))
                    {
                        _log_ << "Object " << objectId << " change status to " << DeviceStatusToString(state);

                        if (bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_RESTORED == state)
                        {
                            needRequest = true;
                            std::unique_lock<std::mutex> lock(m_streamMutex);
                            m_checkedStreams.insert(objectId);
                        }
                        else
                        {
                            RemoveStatisticsSubscribeForStream(objectId);

                            removeRTSPResource(objectId);
                        }
                    }
                }
            }

            bl::events::CameraChangedEvent cameraEvent;
            if (ev.body().UnpackTo(&cameraEvent))
            {
                const bl::events::CameraChangedEvent_ChangeAction& action = cameraEvent.action();
                if (bl::events::CameraChangedEvent_ChangeAction_ADDED == action)
                {
                    _log_ << "Add camera " << cameraEvent.id() << " for tracking";

                    needRequest = true;

                    addStreamsForStaticticsCheck(metaCredentials, cameraEvent.id());

                    {
                        std::unique_lock<std::mutex> lock(m_resourceMutex);
                        m_resourceContext->m_videoStreams.insert(std::make_pair(cameraEvent.id(), NMMSS::PSinkEndpoint{}));
                    }

                    updateEventSubscription();
                }
                else if (bl::events::CameraChangedEvent_ChangeAction_REMOVED == action)
                {
                    _log_ << "Remove camera " << cameraEvent.id() << " from tracking";

                    std::unique_lock<std::mutex> lock(m_resourceMutex);
                    m_resourceContext->m_videoStreams.erase(cameraEvent.id());
                    //m_resourceContext->m_defferedStreams.erase(cameraEvent.id());
                }
            }
        }

        if (needRequest)
            doStatisticsRequest(metaCredentials);
    }

    void doStatisticsRequest(NGrpcHelpers::PCredentials creds)
    {
        bl::statistics::StatsRequest req;
        {
            std::unique_lock<std::mutex> lock(m_streamMutex);
            if (m_checkedStreams.empty() || m_checkingStreams)
            {
                _log_ << "Defer statistics request for  " << m_checkedStreams.size() << " streams";
                return;
            }
            
            m_checkingStreams = true;
            std::set<std::string>::iterator it1 = m_checkedStreams.begin(), it2 = m_checkedStreams.end();
            for (; it1 != it2; ++it1)
            {
                bl::statistics::StatPointKey* streamTypeKey = req.add_keys();
                streamTypeKey->set_type(bl::statistics::SPT_LiveStreamType);
                _log_ << "Prepare statistics request for stream " << *it1;
                streamTypeKey->set_name(*it1);
            }
        }
        PStatisticsReader_t reader(new StatisticsReader_t
            (GET_LOGGER_PTR, m_grpcManager, creds, &bl::statistics::StatisticService::Stub::AsyncGetStatistics));

        StatisticsCallback_t sc = std::bind(&CGstreamerRTSPServer::onStatisticsResponse,
            std::shared_ptr<CGstreamerRTSPServer>(shared_from_base<CGstreamerRTSPServer>()), creds,
            std::placeholders::_1, std::placeholders::_2);

        _log_ << "Send statistics request with " << req.keys_size() << " keys";
        reader->asyncRequest(req, sc);
    }

    void onStatisticsResponse(NGrpcHelpers::PCredentials metaCredentials, const bl::statistics::StatsResponse& res, grpc::Status)
    {
        int statSize = res.stats_size();
        for (int i = 0; i < statSize; ++i)
        {
            const bl::statistics::StatPoint& sp = res.stats(i);

            std::string resourcePath("/" + sp.key().name());

            {
                TWriteLock lock(m_contextMutex);
                GstRTSPMediaFactory* ff = gst_rtsp_mount_points_match(m_mounts, resourcePath.c_str(), NULL);
                if (nullptr != ff)
                {
                    RemoveStatisticsSubscribeForStream(sp.key().name());
                    continue;
                }

                std::uint32_t fourCC = sp.value_uint32();
                _log_ << "Process stream " << sp.key().name() << " with type " << NPluginUtility::parseForCC(fourCC);

                {
                    std::unique_lock<std::mutex> lock(m_resourceMutex);
                    auto it = m_resourceContext->m_videoStreams.find(sp.key().name());
                    if (m_resourceContext->m_videoStreams.end() != it)
                    {
                        if (it->second)
                        {
                            _log_ << "Reset BUC endpoint";
                            it->second->Destroy();
                            it->second.Reset();
                        }
                    }
                }

                std::string rtspPipeline(constructVideoPipeline(fourCC, VIDEO_LIVE_PIPELINE_MASK));
                if (rtspPipeline.empty())
                    continue;

                const auto insertResult = m_livePipelines.insert(std::make_pair(resourcePath, rtspPipeline));
                if (!insertResult.second)
                    insertResult.first->second = rtspPipeline;
            }

            RemoveStatisticsSubscribeForStream(sp.key().name());
        }

        setStatisticsTimer(metaCredentials);
    }

    void RemoveStatisticsSubscribeForStream(const std::string& stream)
    {
        std::unique_lock<std::mutex> lock(m_streamMutex);
        m_checkedStreams.erase(stream);
    }
    
    std::string DeviceStatusToString(bl::events::IpDeviceStateChangedEvent_State status)
    {
        switch (status)
        {
        case bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_RESTORED:
            return "signal restored";
        case bl::events::IpDeviceStateChangedEvent_State_IPDS_SIGNAL_LOST:
            return "signal lost";
        default:
            return "unknown";
        }
    }

    int getNumConnections(const std::string &sUser) const
    {
        TReadLock lock(m_contextMutex);

        return std::accumulate(m_precreatedContexts.begin(), m_precreatedContexts.end(), 0, 
            [&sUser](int a, const std::pair<std::string, PRTSPStreamContext> b)
            {
                return a+b.second->GetNumConnection(sUser);
            });
    }

    //true - retrieved well. false - nativeBl problems
    bool getAllowedNumConnections(NGrpcHelpers::PCredentials metaCredentials, const std::string &sUser, int &nRes) const
    {
        auto channel = m_grpcManager->GetChannel();
        auto stub = channel->NewStub<bl::security::SecurityService>();
        grpc::ClientContext context;
        context.set_credentials(metaCredentials);

        bl::security::ListConfigResponse res;

        auto resp = stub->ListConfig(&context, bl::security::ListConfigRequest(), &res);

        if (resp.ok())
        {
            for (int i = 0; i < res.users_size(); ++i)
            {
                const auto &user = res.users().Get(i);
                if (user.name() == sUser)
                {
                    nRes = user.restrictions().web_count();
                    return true;
                }
            }
        }

        return false;
    }

    //true - retrieved well. false - nativeBl problems
    bool getWebUiAllowed(NGrpcHelpers::PCredentials metaCredentials,  bool &allowedWebUI)
    {
        allowedWebUI = false;
        auto channel = m_grpcManager->GetChannel();
        auto stub = channel->NewStub<bl::security::SecurityService>();
        grpc::ClientContext context;
        context.set_credentials(metaCredentials);

        bl::security::ListUserGlobalPermissionsResponse res;

        auto resp = stub->ListUserGlobalPermissions(&context, ::google::protobuf::Empty(), &res);

        if (resp.ok())
        {
            google::protobuf::Map<std::string, bl::security::GlobalPermissions>::const_iterator it1 = res.permissions().begin(),
                it2 = res.permissions().end();
            for (; it1 != it2; ++it1)
            {
                const bl::security::GlobalPermissions& p = res.permissions().begin()->second;

                if (bl::security::UNRESTRICTED_ACCESS_YES == p.unrestricted_access())
                {
                    allowedWebUI = true;
                    break;
                }

                const ::google::protobuf::RepeatedField<int>& features = p.feature_access();
                if (features.cend() != std::find(features.cbegin(), features.cend(), bl::security::FEATURE_ACCESS_WEB_UI_LOGIN))
                {
                    allowedWebUI = true;
                    break;
                }
            }
        }

        return allowedWebUI;
    }

    static GstRTSPResult getAuthorizationHeader(GstRTSPMessage* msg, gchar** value)
    {
        GstRTSPResult status = gst_rtsp_message_get_header_by_name(msg, AXXON_AUTHORIZATION, value, 0);
        if (GST_RTSP_OK != status)
            status = gst_rtsp_message_get_header(msg, GST_RTSP_HDR_AUTHORIZATION, value, 0);

        return status;
    }

    static void client_connected(GstRTSPServer * server, GstRTSPClient * client, gpointer)
    {
        g_signal_connect(client, "pre-describe-request", (GCallback)
            pre_describe_request, NULL);

        g_signal_connect(client, "pre-play-request", (GCallback)
            pre_play_request, NULL);
    }

    static GstRTSPStatusCode pre_describe_request(GstRTSPClient* client,
        GstRTSPContext* ctx, gpointer)
    {
        auto handler = GetServer();
        if (!handler)
            return GST_RTSP_STS_NOT_FOUND;

        return handler->onPreDescribeRequest(client, ctx);
    }

    static GstRTSPStatusCode pre_play_request(GstRTSPClient* client,
        GstRTSPContext* ctx, gpointer)
    {
        auto handler = GetServer();
        if (!handler)
            return GST_RTSP_STS_NOT_FOUND;

        return handler->onPrePlayRequest(client, ctx);
    }

    bool isLoopback(const std::string& ip, const std::string& user, const std::string& pass)
    {
        const char LOOPBACK[] = "loopback";
        if (user == LOOPBACK && pass == LOOPBACK)
        {
            static const std::unordered_set<std::string> LOCAL_HOSTS =
            {
                {"127.0.0.1"},
                {"::1"}
            };

            auto found = std::find(LOCAL_HOSTS.begin(), LOCAL_HOSTS.end(), ip);
            if (found != LOCAL_HOSTS.end())
                return true;
        }

        return false;
    }

    bool restrictedByPolicy(GstRTSPContext* ctx)
    {
        gchar* value = 0;
        GstRTSPResult res = getAuthorizationHeader(ctx->request, &value);

        if (GST_RTSP_OK == res)
        {
            std::string basicAuth(value + 6);
            const std::string decoded =
                NCrypto::FromBase64(basicAuth.c_str(), basicAuth.size()).value_or("");

            std::string::const_iterator colon =
                std::find(decoded.begin(), decoded.end(), ':');
            if (colon != decoded.end())
            {
                const std::string user(decoded.begin(), colon);
                const std::string pass(colon + 1, decoded.end());

                std::string ipUtf8;
                std::wstring ip;
                if (nullptr != ctx->conn)
                {
                    ipUtf8 = gst_rtsp_connection_get_ip(ctx->conn);
                    ip.assign(NCorbaHelpers::FromUtf8(ipUtf8));
                }

                // skip auth ckecks for loopback connection
                if (isLoopback(ipUtf8, user, pass))
                    return false;
       
                auto data = m_authentificator(ip, NCorbaHelpers::FromUtf8(user), NCorbaHelpers::FromUtf8(pass));
                if (!data.first)
                {
                    _err_ << "Authentication for user " << user << " failed";
                    return true;
                }
                NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials(*data.first);

                int numAllowedConn;
                bool allowedWebUi;

                //in case of nativeBl error we restrict access
                if (!getWebUiAllowed(metaCredentials, allowedWebUi) || !allowedWebUi || !getAllowedNumConnections(m_systemCred, user, numAllowedConn) || getNumConnections(user) >= numAllowedConn)
                {
                    _log_ << "restrictred for user:" << user;
                    return true;
                }
            }
        }

        return false;
    }

    GstRTSPStatusCode onPreDescribeRequest(GstRTSPClient* client, GstRTSPContext* ctx)
    {
        if (restrictedByPolicy(ctx)) //Call here, becuase here we have flexibility in status code. In authorization, we can only return 401
            return GST_RTSP_STS_FORBIDDEN;

        if (strstr(ctx->uri->abspath, "/onvif"))
        {
            std::ostringstream address;
            address << (void*)ctx;

            std::string ss = ctx->uri->abspath + std::string("/") + address.str();
            g_free(ctx->uri->abspath);
            ctx->uri->abspath = g_strdup(ss.c_str());

            gchar* path = g_strdup(ctx->uri->abspath);

            TWriteLock lock(m_contextMutex);
            GstRTSPMediaFactory* ff = gst_rtsp_mount_points_match(m_mounts, path, NULL);
            if (nullptr == ff)
            {
                GstRTSPMediaFactory* factory = gst_rtsp_onvif_media_factory_new();
                gst_rtsp_media_factory_set_media_gtype(factory, GST_TYPE_RTSP_ONVIF_MEDIA);
                gst_rtsp_onvif_media_factory_set_replay_support(GST_RTSP_ONVIF_MEDIA_FACTORY_CAST(factory), TRUE);
                gst_rtsp_media_factory_set_suspend_mode(factory, GST_RTSP_SUSPEND_MODE_RESET);

                PRTSPStreamContext sc = std::make_shared<SOnvifArchiveRTSPStreamContext>(GET_LOGGER_PTR, m_mounts, &m_mediaElement, factory, path);

                g_signal_connect(factory, "media-constructed", (GCallback)
                    archive_media_constructed, path);

                gst_rtsp_mount_points_add_factory(m_mounts, path, factory);
                m_precreatedContexts.insert(std::make_pair(path, sc));
            }
        }
        else if (strstr(ctx->uri->abspath, "/archive"))
        {
            if (nullptr == ctx->uri->query)
            {
                _err_ << "Archive request must contains query substring";
                return GST_RTSP_STS_BAD_REQUEST;
            }

            std::ostringstream address;
            address << (void*)ctx;

            std::string ss = ctx->uri->abspath + std::string("/") + address.str();
            g_free(ctx->uri->abspath);
            ctx->uri->abspath = g_strdup(ss.c_str());

            gchar* path = g_strdup(ctx->uri->abspath);

            int speed = 0;
            try
            {
                speed = parseArchiveSpeed(ctx->uri->query);
            }
            catch (const std::logic_error& e)
            {
                _err_ << "Parse archive request failed. Reason: " << e.what();
                return GST_RTSP_STS_BAD_REQUEST;
            }
            catch (const boost::bad_lexical_cast& e)
            {
                _err_ << "Parse archive request failed (speed param). Reason: " << e.what();
                return GST_RTSP_STS_BAD_REQUEST;
            }

            bool keyFramesOnly = parseKeyFrameFlag(ctx->uri->query);
            _log_ << "Process archive request " << path << " for speed " << speed;

            TWriteLock lock(m_contextMutex);
            GstRTSPMediaFactory* ff = gst_rtsp_mount_points_match(m_mounts, path, NULL);
            if (nullptr == ff)
            {
                GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();

                PRTSPStreamContext sc = std::make_shared<SArchiveRTSPStreamContext>(GET_LOGGER_PTR, m_mounts, &m_mediaElement, factory, path, speed, keyFramesOnly);

                g_signal_connect(factory, "media-constructed", (GCallback)
                    archive_media_constructed, path);

                gst_rtsp_mount_points_add_factory(m_mounts, path, factory);
                m_precreatedContexts.insert(std::make_pair(path, sc));
            }
        }
        else
        {
            gchar* path = g_strdup(ctx->uri->abspath);

            bool keyFramesOnly = (nullptr == ctx->uri->query) ? false : parseKeyFrameFlag(ctx->uri->query);

            _log_ << "Predescribe resource " << path;

            TWriteLock lock(m_contextMutex);
            GstRTSPMediaFactory* ff = gst_rtsp_mount_points_match(m_mounts, path, NULL);
            if (nullptr == ff)
            {
                _log_ << "Create new mount point for resource " << path;

                std::map<std::string, std::string>::iterator it = m_livePipelines.find(path);
                if (m_livePipelines.end() == it)
                {
                    _err_ << "Can not find live context " << path;
                    return GST_RTSP_STS_BAD_REQUEST;
                }

                GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();

                PRTSPStreamContext sc = std::make_shared<SLiveRTSPStreamContext>(GET_LOGGER_PTR, m_mounts, factory, path, it->second, keyFramesOnly, m_mmCache);

                g_signal_connect(factory, "media-constructed", (GCallback)
                    media_constructed, path);

                gst_rtsp_mount_points_add_factory(m_mounts, path, factory);
                m_precreatedContexts.insert(std::make_pair(path, sc));

                //Direct building of pipeline for live video
                //If true authentificateAndAuthorize will not be called
                if (NCorbaHelpers::CEnvar::IsArpagentBridge())
                {
                    // Dummy object, credentials are not used in PRTSPStreamContext::connectDataSources
                    ConnectionCtx connCtx;

                    NCorbaHelpers::PContainer cont = m_container;
                    if (cont)
                        sc->CreatePipeline(m_grpcManager, connCtx.m_credentials, cont.Get(), connCtx);
                }
            }
        }

        return GST_RTSP_STS_OK;
    }

    GstRTSPStatusCode onPrePlayRequest(GstRTSPClient* client, GstRTSPContext* ctx)
    {
        if (restrictedByPolicy(ctx)) //Call here, becuase here we have flexibility in status code. In authorization, we can only return 401
            return GST_RTSP_STS_FORBIDDEN;

        // Process Range header only for 'onvif-replay' request
        // for other request just return GST_RTSP_STS_OK
        if (!strstr(ctx->uri->abspath, "/onvif"))
            return GST_RTSP_STS_OK;

        const std::string resorcePath = ctx->uri->abspath;

        GstRTSPMessage* msg = ctx->request;
        gchar* value = 0;

        if (GST_RTSP_OK != gst_rtsp_message_get_header(msg, GST_RTSP_HDR_RANGE, &value, 0))
        {
            _err_ << "Can not find 'Range' header in PLAY request for " << resorcePath;
            return GST_RTSP_STS_BAD_REQUEST;
        }

        PRTSPStreamContext streamCtx = getContext(resorcePath);
        if (!streamCtx)
        {
            _err_ << "Can not find context for " << resorcePath;
            return GST_RTSP_STS_NOT_FOUND;
        }

        auto range = parseRangeHeader(GET_LOGGER_PTR, value);

        if (!range.first.empty())
            streamCtx->Seek(range.first);

        return GST_RTSP_STS_OK;
    }

    static GstRTSPFilterResult onCleanup(GstRTSPServer *server,
        GstRTSPClient *client,
        gpointer user_data)
    {
        return GST_RTSP_FILTER_REMOVE;
    }

    bool AuthentificateBasic(GstRTSPMessage* msg, const std::string& res, ConnectionCtx& connCtx, const std::wstring& ip)
    {
        std::string resource;
        bool isArchive = (0 == res.find(ARCHIVE_PREFIX));
        if (isArchive)
        {
            auto guidPos = res.find_last_of('/');
            auto timePos = res.substr(0, guidPos).find_last_of('/');

            resource.assign(res.substr(ARCHIVE_PREFIX_LEN, timePos - ARCHIVE_PREFIX_LEN));
        }
        else if (0 == res.find(ONVIF_PREFIX))
        {
            auto guidPos = res.find_last_of('/');
            resource.assign(res.substr(ONVIF_PREFIX_LENGTH, guidPos - ONVIF_PREFIX_LENGTH));
            isArchive = true;
        }
        else
            resource = res;

        gchar* value = 0;
        GstRTSPResult status = getAuthorizationHeader(msg, &value);

        if (GST_RTSP_OK == status)
        {
            std::string basicAuth(value + 6);
            const std::string decoded =
                NCrypto::FromBase64(basicAuth.c_str(), basicAuth.size()).value_or("");

            std::string::const_iterator colon =
                std::find(decoded.begin(), decoded.end(), ':');
            if (colon != decoded.end())
            {
                const std::string user(decoded.begin(), colon);
                const std::string pass(colon + 1, decoded.end());

                std::string token;
                if (isLoopback(NCorbaHelpers::ToUtf8(ip), user, pass))
                {
                    token = m_systemToken;
                }
                else
                {
                    NHttp::IRequest::AuthSession session;
                    session.id = LOGIN_PASSWORD_AUTH_SESSION_ID;
                    session.data = m_authentificator(ip, NCorbaHelpers::FromUtf8(user), NCorbaHelpers::FromUtf8(pass));

                    if (!session.data.first)
                        return false;

                    token = *session.data.first;
                }

                getConnectionCtx(token, resource, isArchive, connCtx);

                if (isArchive)
                    return !connCtx.m_archiveVideoEp.empty();
            }
        }

        return connCtx.m_cameraAllowed;
    }

    bool AuthentificateHmac(const std::string& resource, const std::string& exp)
    {
        if (!m_hmacAuth->verify(resource))
            return false;
        
        if (exp.empty())
            return false;

        bool authenticated = false;
        try
        {
            auto expiresAt = boost::posix_time::from_iso_string(exp);
            authenticated = expiresAt > boost::posix_time::second_clock::local_time();
        }
        catch (std::exception&)
        {
        }
        return authenticated;
    }

    static std::shared_ptr<CGstreamerRTSPServer> GetServer()
    {
        std::shared_ptr<CGstreamerRTSPServer> s = s_server.lock();
        return s;
    }

    void SetAuthToken(const std::string& user, const std::string& pass)
    {
        GstRTSPAuth* auth = gst_rtsp_server_get_auth(m_rtspServer);
        gchar* basic = gst_rtsp_auth_make_basic(user.c_str(), pass.c_str());

        std::unique_lock<std::mutex> lock(m_authLock);
        TAuthMap::iterator it = m_basicAuthMap.find(user);
        if (m_basicAuthMap.end() != it)
        {           
            if (it->second == std::string(basic))
            {
                g_free(basic);
                g_object_unref(auth);
                return;
            }

            gst_rtsp_auth_remove_basic(auth, it->second.c_str());
        }

        GstRTSPToken* token =
            gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
            user.c_str(), NULL);

        gst_rtsp_auth_add_basic(auth, basic, token);
        m_basicAuthMap.insert(std::make_pair(user, basic));
        lock.unlock();

        g_free(basic);
        gst_rtsp_token_unref(token);
        g_object_unref(auth);
    }

    gboolean UseDefaultAuthentificate(GstRTSPAuth* auth, GstRTSPContext* ctx)
    {
        if (nullptr != m_authFunc)
            return m_authFunc(auth, ctx);
        return false;
    }

    void SetPermissions(const std::string& resource, const std::string& user, const std::string& pass, ConnectionCtx &connCtx)
    {
        PRTSPStreamContext ctx = getContext(resource);
        if (ctx)
        {
            SetAuthToken(user, pass);
            ctx->SetFactoryPermissions(user, pass);
            NCorbaHelpers::PContainer cont = m_container;
            if (cont)
                ctx->CreatePipeline(m_grpcManager, connCtx.m_credentials, cont.Get(), connCtx);
        }
    }

private:

    bool createGstServer()
    {
        m_rtspServer = gst_rtsp_onvif_server_new();
        if (nullptr == m_rtspServer)
            _log_ << "Can not allocate rtsp server";

        g_object_set(m_rtspServer, "service", m_rtspPort.c_str(), NULL);

        m_mounts = gst_rtsp_server_get_mount_points(m_rtspServer);

        //As part of ACR - 62863, RTSP server authorization was disabled, provided that Axxon Next is running in Bridge mode.
        //Authorization callbacks will not be called, without authorization only live video will be available through the direct building pipeline
        if (!NCorbaHelpers::CEnvar::IsArpagentBridge())
        {
            GstRTSPAuth* auth = gst_rtsp_auth_new();

            GstRTSPAuthClass* klass = GST_RTSP_AUTH_GET_CLASS(auth);

            m_authFunc = klass->authenticate;
            klass->authenticate = authentificateAndAuthorize;

            gst_rtsp_server_set_auth(m_rtspServer, auth);
            g_object_unref(auth);
        }
        else
            _log_ << "Running in Bridge mode. Not using RTSP authorization for live video";

        g_signal_connect(m_rtspServer, "client-connected", (GCallback)
            client_connected, NULL);

        m_sourceId = gst_rtsp_server_attach(m_rtspServer, NULL);
        if (0 == m_sourceId)
        {
            _err_ << "Attach to rtsp server failed";
            return false;
        }

        s_server = std::weak_ptr<CGstreamerRTSPServer>(shared_from_base<CGstreamerRTSPServer>());

        _log_ << "RTSP server run successfully";

        GstRTSPThreadPool * tp = gst_rtsp_server_get_thread_pool(m_rtspServer);
        gst_rtsp_thread_pool_set_max_threads(tp, 8);
        _log_ << "RTSP server used " << gst_rtsp_thread_pool_get_max_threads(tp) << " threads for client connections";
        _log_ << "RTSP server bound port " << gst_rtsp_server_get_bound_port(m_rtspServer);
        _log_ << "RTSP server queue size " << gst_rtsp_server_get_backlog(m_rtspServer);
        g_object_unref(tp);
        
        return true;
    }

    void cameraListRequest()
    {
        if (!m_stopped)
        {
            _log_ << "RTSP: cameraListRequest()";
            PListCameraReader_t reader(new ListCameraReader_t
            (GET_LOGGER_PTR, m_grpcManager, m_systemCred, &bl::domain::DomainService::Stub::AsyncListCameras));

            bl::domain::ListCamerasRequest creq;
            creq.set_view(bl::domain::EViewMode::VIEW_MODE_FULL);

            CameraListCallback_t cb = std::bind(&CGstreamerRTSPServer::onListCamerasResponse,
                std::weak_ptr<CGstreamerRTSPServer>(shared_from_base<CGstreamerRTSPServer>()),
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

            reader->asyncRequest(creq, cb);
        }
    }

    static void onListCamerasResponse(std::weak_ptr<CGstreamerRTSPServer> obj,
        const bl::domain::ListCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
    {
        std::shared_ptr<CGstreamerRTSPServer> owner = obj.lock();
        if (owner)
        {
            if (!grpcStatus.ok())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(LIST_CAMERA_CALL_TIMEOUT_MS));
                NCorbaHelpers::GetReactorInstanceShared()->GetIO().post([obj]()
                {
                    std::shared_ptr<CGstreamerRTSPServer> ownr = obj.lock();
                    if (ownr)
                        ownr->cameraListRequest();
                });
                return;
            }

            const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera > cams = res.items();
            int camCount = cams.size();
            for (int i = 0; i < camCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);

                int vsCount = c.video_streams_size();
                for (int j = 0; j < vsCount; ++j)
                {
                    const bl::domain::VideoStreaming& vs = c.video_streams(j);

                    std::unique_lock<std::mutex> lock(owner->m_resourceMutex);
                    if (c.breaks_unused_connections())
                    {
                        owner->m_resourceContext->m_defferedStreams.push_back(vs.stream_acess_point());
                    }
                    else
                        owner->m_resourceContext->m_videoStreams.insert(std::make_pair(vs.stream_acess_point(), NMMSS::PSinkEndpoint{}));
                }
            }

            if (NWebGrpc::_FINISH == status)
            {
                owner->needActivateBUCDevices();
                owner->startRTSP();
            }
        }
    }

    bool needActivateBUCDevices()
    { 
        NCorbaHelpers::PContainer cont = m_container;
        if (cont)
        {
            std::unique_lock<std::mutex> lock(m_resourceMutex);

            if (m_resourceContext->m_defferedStreams.empty())
            {
                _log_ << "No BUC devices";
                return false;
            }

            _log_ << "We have " << m_resourceContext->m_defferedStreams.size() << " BUC devices";

            std::size_t maxPos = std::min(BUC_TRESHOLD, m_resourceContext->m_defferedStreams.size());
            std::vector<std::string>::iterator it1 = m_resourceContext->m_defferedStreams.begin(), it2 = it1 + maxPos;

            _log_ << "Max possible elements: " << maxPos << ". Calculated: " << std::distance(it2, it1);

            std::transform(it1, it2, std::inserter(m_resourceContext->m_videoStreams, m_resourceContext->m_videoStreams.end()),
                [cont](const std::string& s)
                { 
                    NMMSS::PPullStyleSink dummy(NPluginHelpers::CreateDummySink(cont->GetLogger()));
                    return std::make_pair(s,
                        NMMSS::PSinkEndpoint(NMMSS::CreatePullConnectionByNsref(cont->GetLogger(),
                            s.c_str(), cont->GetRootNC(), dummy.Get(),
                            MMSS::EAUTO)));
                });

            m_resourceContext->m_defferedStreams.erase(it1, it2);

            return true;
        }
        return false;
    }

    void startRTSP()
    {
        if (!createGstServer())
            return;

        subscribeEvents();
    }

    void subscribeEvents()
    {        
        m_reader.reset(new EventReader_t
            (GET_LOGGER_PTR, m_grpcManager, m_systemCred, &bl::events::DomainNotifier::Stub::AsyncPullEvents));

        bl::events::PullEventsRequest req;
        constructRequest(req);

        EventCallback_t cb = std::bind(&CGstreamerRTSPServer::processEvents,
            shared_from_base<CGstreamerRTSPServer>(), m_systemCred,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        
        m_reader->asyncRequest(req, cb);
    }

    void updateEventSubscription()
    {
        PSubscriptionReader_t reader(new SubscriptionReader_t
            (GET_LOGGER_PTR, m_grpcManager, m_systemCred, &bl::events::DomainNotifier::Stub::AsyncUpdateSubscription));

        bl::events::UpdateSubscriptionRequest req;
        constructRequest(req);

        SubscriptionCallback_t cb = std::bind(&CGstreamerRTSPServer::onUpdateSubscriptionResponse,
            std::shared_ptr<CGstreamerRTSPServer>(shared_from_base<CGstreamerRTSPServer>()), m_systemCred,
            std::placeholders::_1, std::placeholders::_2);

        reader->asyncRequest(req, cb);
    }

    template <typename T>
    void constructRequest(T& req)
    {
        req.set_subscription_id(m_eventSubscriptionId);

        bl::events::EventFilters* filter = req.mutable_filters();
        {
            std::unique_lock<std::mutex> lock(m_resourceMutex);
            for (auto stream : m_resourceContext->m_videoStreams)
            {
                bl::events::EventFilter* f = filter->add_include();
                f->set_event_type(bl::events::ET_IpDeviceStateChangedEvent);
                f->set_subject(stream.first);
            }
        }

        bl::events::EventFilter* ccef = filter->add_include();
        ccef->set_event_type(bl::events::ET_CameraChangedEvent);
    }

    void onUpdateSubscriptionResponse(NGrpcHelpers::PCredentials metaCredentials, const bl::events::UpdateSubscriptionResponse&, grpc::Status status)
    {
        if (!status.ok())
            _wrn_ << "Update subscription error";
        else
            _log_ << "Event subscription successfully updated";
    }

    void getConnectionCtx(const std::string &credData, const std::string &videoEp, bool isArchive,  ConnectionCtx &connCtx)
    {
        bl::domain::BatchGetCamerasRequest creq;
        bl::domain::ResourceLocator* rl = creq.add_items();
        rl->set_access_point(NPluginUtility::convertToMainStream(videoEp));

        NGrpcHelpers::PCredentials metaCredentials = NGrpcHelpers::NGPAuthTokenCallCredentials(credData);
        connCtx.m_credentials = metaCredentials;

        auto channel = m_grpcManager->GetChannel();
        auto stub = channel->NewStub<bl::domain::DomainService>();
        grpc::ClientContext context;
        context.set_credentials(metaCredentials);
        auto resp = stub->BatchGetCameras(&context, creq);

        bl::domain::BatchGetCamerasResponse res;
        for (; resp->Read(&res);)
        {
            // process res
            int size = res.items().size();
            if (size > 0)
            {                
                const bl::domain::Camera& c = res.items().Get(0);
                connCtx.m_cameraAllowed = c.camera_access() >= (isArchive ? axxonsoft::bl::security::CAMERA_ACCESS_ONLY_ARCHIVE : axxonsoft::bl::security::CAMERA_ACCESS_MONITORING_ON_PROTECTION);
                if (c.microphones_size() > 0 && c.microphones(0).is_activated())
                    connCtx.m_audioEndpoint = c.microphones(0).access_point();
            }

            if (isArchive)
            {
                for (int i = 0; i < size; ++i)
                {
                    const bl::domain::Camera& c = res.items().Get(i);
                    int arcCount = c.archive_bindings_size();
                    for (int j = 0; j < arcCount; ++j)
                    {
                        const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);

                        if (ab.has_archive() && ab.archive().is_activated())
                        {
                            auto it = std::find_if(ab.sources().begin(), ab.sources().end(),
                                [&](const bl::domain::StorageSource& sS)
                            {
                                return sS.media_source() == videoEp;
                            });

                            if (it != ab.sources().end())
                                connCtx.m_archiveVideoEp = it->access_point();

                            std::string audioEp = NPluginUtility::GetAudioEpFromVideoEp(videoEp);
                            it = std::find_if(ab.sources().begin(), ab.sources().end(),
                                [&](const bl::domain::StorageSource& sS)
                            {
                                return sS.media_source() == audioEp;
                            });

                            if (it != ab.sources().end())
                                connCtx.m_archiveAudioEp = it->access_point();
                        }
                    }
                }
            }
        }
    }

    std::vector<std::string> getStreamsFromCamera(NGrpcHelpers::PCredentials credData, const std::string &endpoint)
    {
        std::vector<std::string> streams;

        bl::domain::BatchGetCamerasRequest creq;
        bl::domain::ResourceLocator* rl = creq.add_items();
        rl->set_access_point(endpoint);

        auto channel = m_grpcManager->GetChannel();
        auto stub = channel->NewStub<bl::domain::DomainService>();
        grpc::ClientContext context;
        context.set_credentials(credData);
        auto resp = stub->BatchGetCameras(&context, creq);

        bl::domain::BatchGetCamerasResponse res;
        for (; resp->Read(&res);)
            processCameras(streams, res.items());

        return streams;
    }

    void setStatisticsTimer(NGrpcHelpers::PCredentials creds)
    {
        m_statsTimer.expires_from_now(boost::posix_time::seconds(15));
        m_statsTimer.async_wait(std::bind(&CGstreamerRTSPServer::handle_timeout,
            std::weak_ptr<CGstreamerRTSPServer>(shared_from_base<CGstreamerRTSPServer>()),
            creds, std::placeholders::_1));
    }

    static void handle_timeout(std::weak_ptr<CGstreamerRTSPServer> s, NGrpcHelpers::PCredentials creds, const boost::system::error_code& error)
    {
        if (!error)
        {
            std::shared_ptr<CGstreamerRTSPServer> cache = s.lock();
            if (cache)
            {
                cache->updateStatistics(creds);
            }
        }
    }

    void updateStatistics(NGrpcHelpers::PCredentials creds)
    {
        {
            std::unique_lock<std::mutex> lock(m_streamMutex);
            m_checkingStreams = false;
        }
  
        if (needActivateBUCDevices())
            updateEventSubscription();

        doStatisticsRequest(creds);
    }

    void removeRTSPResource(const std::string& objectId)
    {
        std::string resourcePath("/" + objectId);
        _log_ << "Remove resource " << objectId << " from RTSP server";

        TWriteLock lock(m_contextMutex);
        TContexts::iterator it = m_precreatedContexts.find(resourcePath);
        if (m_precreatedContexts.end() != it)
        {
            m_precreatedContexts.erase(it);
        }
        gst_rtsp_mount_points_remove_factory(m_mounts, resourcePath.c_str());
    }

    void addStreamsForStaticticsCheck(NGrpcHelpers::PCredentials metaCredentials, const std::string& objectId)
    {
        std::vector<std::string> streams = getStreamsFromCamera(metaCredentials, objectId);

        std::unique_lock<std::mutex> lock(m_streamMutex);

        for (const auto &t : streams)
        {
            _log_ << "Add stream: " << t << " for tracking";
            m_checkedStreams.insert(t);
        }
    }

    static GstPadProbeReturn
        cb_have_src_data(GstPad          *pad,
        GstPadProbeInfo *info,
        gpointer         user_data)
    {
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);

        GstRTPBuffer rtpBuffer = GST_RTP_BUFFER_INIT;
        gst_rtp_buffer_map(buffer, (GstMapFlags)GST_MAP_READWRITE, &rtpBuffer);
        gboolean res = gst_rtp_buffer_set_extension_data(&rtpBuffer, 0xABAC, 3);
        if (res)
        {
            guint16 bits;
            guint size;
            gpointer pointer;
            gst_rtp_buffer_get_extension_data(&rtpBuffer, &bits, &pointer, &size);

            uint32_t* extData = (uint32_t*)(pointer);
            GstMetaMarking * meta = GST_META_MARKING_GET(buffer);
            if (NULL != meta)
            {
                extData[0] = htonl(meta->ntp1);
                extData[1] = htonl(meta->ntp2);
                extData[2] = 0;
            }
        }
        gst_rtp_buffer_unmap(&rtpBuffer);

        GST_PAD_PROBE_INFO_DATA(info) = buffer;

        return GST_PAD_PROBE_OK;
    }

    static void
        media_constructed(GstRTSPMediaFactory * factory, GstRTSPMedia * media, gchar* resource)
    {
        auto handler = GetServer();
        if (!handler)
            return;
        handler->onMediaConstructed(factory, media, resource);
    }

    static void
        archive_media_constructed(GstRTSPMediaFactory * factory, GstRTSPMedia * media, gchar* resource)
    {
        auto handler = GetServer();
        if (!handler)
            return;
        handler->onArchiveMediaConstructed(factory, media, resource);
    }

    static gboolean  seek_data(GstElement * appsrc, guint64 position)
    {
        return TRUE;
    }

    void onArchiveMediaConstructed(GstRTSPMediaFactory * factory, GstRTSPMedia * media, gchar* resource)
    {
        m_mediaElement = gst_rtsp_media_get_element(media);

        GstElement *vsrc = nullptr, *asrc = nullptr;
        if (PRTSPStreamContext ctx = onConstructed(factory, media, resource, vsrc, asrc))
        {
            if (nullptr != vsrc)
            {
                g_object_set(vsrc, "stream-type", 1, NULL);
                g_signal_connect(vsrc, "seek-data", G_CALLBACK(seek_data), NULL);
            }

            ctx->SetDataSources(vsrc, asrc);
        }
    }

    void onMediaConstructed(GstRTSPMediaFactory * factory, GstRTSPMedia * media, gchar* resource)
    {
        GstElement *vsrc = nullptr, *asrc = nullptr;
        if (PRTSPStreamContext ctx = onConstructed(factory, media, resource, vsrc, asrc))
            ctx->SetDataSources(vsrc, asrc);
    }

    PRTSPStreamContext onConstructed(GstRTSPMediaFactory * factory, GstRTSPMedia * media, gchar* resource, GstElement*& vsrc, GstElement*& asrc)
    {
        PRTSPStreamContext ctx = getContext(resource);
        if (ctx)
        {
            /* get the element used for providing the streams of the media */
            GstElement* element = gst_rtsp_media_get_element(media);

            /* get our appsrc, we named it 'mysrc' with the name property */
            asrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "asrc");
            GstElement* prsr = gst_bin_get_by_name_recurse_up(GST_BIN(element), "prsr");

            /* make sure ther datais freed when the media is gone */
            g_object_set_data_full(G_OBJECT(media), "my-extra-data", resource,
                (GDestroyNotify)CleanStreamContext);

            if (nullptr != asrc)
            {
                gst_util_set_object_arg(G_OBJECT(asrc), "format", "time");
                g_signal_connect(asrc, "need-data", (GCallback)need_audio_data, resource);

                if (nullptr != prsr)
                {
                    NMMSS::PSample as = ctx->GetAudioSample();
                    if (!as)
                        return ctx;

                    const auto* subheader = NMMSS::NMediaType::GetSampleOfSubtype<NMMSS::NMediaType::Audio::PCM::SubtypeHeader>(as.Get());
                    if (nullptr == subheader)
                        return ctx;

                    GstAudioInfo info;

                    GstAudioChannelPosition pos = GST_AUDIO_CHANNEL_POSITION_MONO;
                    if (1 != subheader->nChannelsCount)
                        gst_audio_channel_positions_from_mask(subheader->nChannelsCount, 3, &pos);

                    gst_audio_info_set_format(&info, convertToGstreamerFormat(subheader->nSampleType), subheader->nSampleRate, subheader->nChannelsCount, &pos);
                    GstCaps* audioCaps = gst_audio_info_to_caps(&info);

                    g_object_set(prsr, "use-sink-caps", TRUE, NULL);
                    g_object_set(asrc, "caps", audioCaps, NULL);

                    gst_object_unref(prsr);
                    gst_caps_unref(audioCaps);
                }
            }

            vsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "vsrc");
            if (nullptr != vsrc)
            {
                gst_util_set_object_arg(G_OBJECT(vsrc), "format", "time");
                g_signal_connect(vsrc, "need-data", (GCallback)need_video_data, resource);
            }

            GstElement* videoPay = gst_bin_get_by_name_recurse_up(GST_BIN(element), "pay0");
            if (nullptr != videoPay)
            {
                GstPad* srcPad = gst_element_get_static_pad(videoPay, "src");
                gst_pad_add_probe(srcPad, GST_PAD_PROBE_TYPE_BUFFER,
                    (GstPadProbeCallback)cb_have_src_data, NULL, NULL);
                gst_object_unref(srcPad);

                gst_object_unref(videoPay);
            }

            GstElement* audioPay = gst_bin_get_by_name_recurse_up(GST_BIN(element), "pay1");
            if (nullptr != audioPay)
            {
                GstPad* srcPad = gst_element_get_static_pad(audioPay, "src");
                gst_pad_add_probe(srcPad, GST_PAD_PROBE_TYPE_BUFFER,
                    (GstPadProbeCallback)cb_have_src_data, NULL, NULL);
                gst_object_unref(srcPad);

                gst_object_unref(audioPay);
            }

            gst_object_unref(element);
        }

        return ctx;
    }

    GstAudioFormat convertToGstreamerFormat(NMMSS::NMediaType::Audio::ESampleType sampleType)
    {
        GstAudioFormat af = GST_AUDIO_FORMAT_UNKNOWN;

        switch (sampleType)
        {
        case NMMSS::NMediaType::Audio::ST_UINT8:
        {
            af = GST_AUDIO_FORMAT_U8;
            break;
        }
        case NMMSS::NMediaType::Audio::ST_INT16:
        {
            af = GST_AUDIO_FORMAT_S16;
            break;
        }
        case NMMSS::NMediaType::Audio::ST_INT32:
        {
            af = GST_AUDIO_FORMAT_S32;
            break;
        }
        case NMMSS::NMediaType::Audio::ST_FLOAT32:
        {
            af = GST_AUDIO_FORMAT_F32;
            break;
        }
        default: break;
        }

        return af;
    }

    static gboolean need_video_data(GstElement * appsrc, guint unused, gchar* resource)
    {
        auto handler = GetServer();
        if (!handler)
            return false;

        handler->onNeedVideoData(appsrc, resource);
        return true;
    }

    static gboolean need_audio_data(GstElement * appsrc, guint unused, gchar* resource)
    {
        auto handler = GetServer();
        if (!handler)
            return false;

        handler->onNeedAudioData(appsrc, resource);
        return true;
    }

    void onNeedVideoData(GstElement* appsrc, gchar* resource)
    {
        PRTSPStreamContext ctx = getContext(resource);
        if (ctx)
            ctx->SendVideoSample();
    }

    void onNeedAudioData(GstElement* appsrc, gchar* resource)
    {
        PRTSPStreamContext ctx = getContext(resource);
        if (ctx)
            ctx->SendAudioSample();
    }

    PRTSPStreamContext getContext(const std::string& resource)
    {
        PRTSPStreamContext ctx;

        {
            TReadLock lock(m_contextMutex);
            TContexts::iterator it = m_precreatedContexts.find(resource);
            if (m_precreatedContexts.end() != it)
                ctx = it->second;
        }

        return ctx;
    }

    NCorbaHelpers::WPContainer m_container;
    std::string m_systemToken;
    NGrpcHelpers::PCredentials m_systemCred;
    std::string m_rtspPort;
    NWebGrpc::PGrpcManager m_grpcManager;

    std::atomic<bool> m_stopped;
    TokenAuthenticatorSP m_hmacAuth;
    FUserAuthenticator m_authentificator;

    NHttp::PMMCache m_mmCache;

    GstRTSPServer* m_rtspServer;
    GstRTSPMountPoints* m_mounts;

    GstElement *m_mediaElement;
    guint m_sourceId;

    static std::weak_ptr<CGstreamerRTSPServer> s_server;

    FAuth m_authFunc;
    
    typedef std::map<std::string, PRTSPStreamContext> TContexts;
    TContexts m_precreatedContexts;
    mutable TMutex m_contextMutex;

    PEventReader_t m_reader;

    std::mutex m_streamMutex;
    std::set<std::string> m_checkedStreams;

    bool m_checkingStreams;

    std::mutex m_authLock;
    typedef std::map<std::string, std::string> TAuthMap;
    TAuthMap m_basicAuthMap;

    boost::asio::deadline_timer m_statsTimer;
    std::string m_eventSubscriptionId;

    std::mutex m_resourceMutex;
    PResourceContext m_resourceContext;

    std::map<std::string, std::string> m_livePipelines;
};

std::weak_ptr<CGstreamerRTSPServer> CGstreamerRTSPServer::s_server;

    std::string parseDataEndpoint(gchar* path, std::string& prefix)
    {
        std::string videoEp;
        if (g_str_has_prefix(path, RTSP_PROXY_PATH))
        {
            prefix.assign(RTSP_PROXY_PATH);
            prefix.append("/");
            videoEp.assign(path + strlen(RTSP_PROXY_PATH) + 1);
        }
        else
        {
            prefix.assign("/");
            videoEp.assign(path + 1);
        }
        return videoEp;
    }

    gboolean authentificateAndAuthorize(GstRTSPAuth *auth, GstRTSPContext *ctx)
    {
        auto rtspServer = CGstreamerRTSPServer::GetServer();
        if (!rtspServer)
            return false;

        if (GST_RTSP_DESCRIBE != ctx->method)
            return rtspServer->UseDefaultAuthentificate(auth, ctx);

        /*bool ok = false;
        bool useHmac = false;

        std::string exp;
        std::string req(ctx->uri->abspath + 1);
        size_t pos = req.find('?');
        if (std::string::npos != pos)
        {
            size_t hmacPos = req.find(HMAC_PARAM, pos);
            if (std::string::npos != hmacPos)
            {
                size_t expPos = req.find(EXP_PARAM, pos);
                if (std::string::npos == expPos)
                    return false;

                size_t nextParamPos = req.find('&', expPos);
                exp.assign(req.substr(expPos + EXP_PARAM_LEN, nextParamPos));

                useHmac = true;
            }
        }

        if (useHmac)
        { 
        }
        else*/

        GstRTSPMessage* msg = ctx->request;
        gchar* value = 0;
        GstRTSPResult res = gst_rtsp_message_get_header(msg, GST_RTSP_HDR_AUTHORIZATION, &value, 0);
        if (GST_RTSP_OK != res)
            return false;

        std::string basicAuth(value + 6);
        const std::string decoded =
            NCrypto::FromBase64(basicAuth.c_str(), basicAuth.size()).value_or("");

        std::string::const_iterator colon =
            std::find(decoded.begin(), decoded.end(), ':');
        if (colon == decoded.end())
            return false;
        
        if (nullptr == ctx->conn)
            return false;

        std::wstring ip(NCorbaHelpers::FromUtf8(gst_rtsp_connection_get_ip(ctx->conn)));

        const std::string user(decoded.begin(), colon);
        const std::string pass(colon + 1, decoded.end());

        std::string prefix;
        std::string endpoint = parseDataEndpoint(ctx->uri->abspath, prefix);

        ConnectionCtx connCtx;

        bool ok = rtspServer->AuthentificateBasic(msg, endpoint, connCtx, ip);

        if (ok)
        {
            rtspServer->SetPermissions(ctx->uri->abspath, user, pass, connCtx);
            ok = rtspServer->UseDefaultAuthentificate(auth, ctx);

            if (ok)
            {
                ConnectionInfo *pRes = new ConnectionInfo(ctx->uri->abspath, user);
                g_signal_connect(ctx->client, "closed", (GCallback)close_callback, pRes);
                rtspServer->AddConnection(pRes->path, user);
            }
        }
        else
        {
            std::string ctxPath(ctx->uri->abspath);
            if (0 == ctxPath.find("/archive"))
                rtspServer->RemoveContext(ctx->uri->abspath);
        }
        return ok;
    }

    GstRTSPStatusCode close_callback(GstRTSPClient * self, gpointer user_data)
    {
        auto rtspServer = CGstreamerRTSPServer::GetServer();
        if (!rtspServer)
            return GST_RTSP_STS_INTERNAL_SERVER_ERROR;

        ConnectionInfo* pRes = (ConnectionInfo*)user_data;
        rtspServer->RemoveConnection(pRes->path, pRes->user);
        delete pRes;

        return GST_RTSP_STS_OK;
    }
}

namespace NPluginHelpers
{
    PGstRTSPServer CreateRTSPServer(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed* c, int rtspPort,
        const NWebGrpc::PGrpcManager grpcManager, TokenAuthenticatorSP ta, FUserAuthenticator ua, NHttp::PMMCache cache)
    {
        return PGstRTSPServer(new CGstreamerRTSPServer(GET_LOGGER_PTR, c, rtspPort, grpcManager, ta, ua, cache));
    }
}

///////////////////////////////////////////////////////////

std::string NPluginHelpers::RtspStat::GetStr() const
{
    Json::Value json(Json::arrayValue);

    for (const auto &t : mStat)
    {
        Json::Value urlJson;
        urlJson["url"] = t.first;
        urlJson["stat"] = Json::Value(Json::arrayValue);

        for (const auto &t2 : t.second)
        {
            Json::Value jsonUser;
            jsonUser["user"] = t2.first;
            jsonUser["num"] = t2.second;
            urlJson["stat"].append(jsonUser);
        }

        json.append(urlJson);
    }

    return Json::FastWriter().write(json);
}

namespace
{
    using namespace NHttp;

    class CRtspStatServletImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CRtspStatServletImpl(DECLARE_LOGGER_ARG, const NPluginHelpers::PGstRTSPServer rtspServer)
            :m_rtspServer(rtspServer)
        {
            INIT_LOGGER_HOLDER;
        }

        void Get(const PRequest req, PResponse resp) override
        {
            NPluginUtility::SendText(resp, m_rtspServer->GetStat().GetStr(), true);
        }

    private:
        const NPluginHelpers::PGstRTSPServer m_rtspServer;
    };
}


namespace NHttp
{
    IServlet*  CreateRtspStatServlet(DECLARE_LOGGER_ARG, const NPluginHelpers::PGstRTSPServer rtspServer)
    {
        return new CRtspStatServletImpl(GET_LOGGER_PTR, rtspServer);
    }
}
