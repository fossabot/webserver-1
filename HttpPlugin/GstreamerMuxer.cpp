#include <unordered_map>
#include <boost/format.hpp>

#include "Platform.h"
#include "DataSink.h"
#include "Gstreamer.h"
#include "GstreamerMeta.h"
#include "CommonUtility.h"

#include <Sample.h>
#include <MediaType.h>
#include <PtimeFromQword.h>

#include <CorbaHelpers/Reactor.h>
#include <MMCoding/Transforms.h>

#include <gst/gst.h>
#include <gst/audio/audio.h>

namespace
{
    const std::uint64_t ATOM_TIMESTAMP = NMMSS::PtimeToQword(boost::posix_time::from_iso_string("14000101T000000"));

    class GstSampleData
    {
        GstSample* m_sample;
        GstMapInfo m_mapping;
    public:
        GstSampleData(GstSample* sample) : m_sample(sample), m_mapping() {}
        GstSampleData(const GstSampleData&) = delete;
        GstSampleData& operator=(const GstSampleData&) = delete;
        ~GstSampleData()
        {
            if (nullptr != m_mapping.data)
            {
                gst_buffer_unmap(gst_sample_get_buffer(m_sample), &m_mapping);
            }
            gst_sample_unref(m_sample);
        }
        GstMapInfo const& map()
        {
            if (nullptr == m_mapping.data)
            {
                gst_buffer_map(gst_sample_get_buffer(m_sample), &m_mapping, GST_MAP_READ);
            }
            return m_mapping;
        }
        std::uint64_t getTimestamp()
        {
            GstMetaMarking * meta = GST_META_MARKING_GET(gst_sample_get_buffer(m_sample));
            if (nullptr != meta)
                return meta->header.dtTimeBegin;
            return 0;
        }
        bool isPreroll() const
        {
            GstMetaMarking * meta = GST_META_MARKING_GET(gst_sample_get_buffer(m_sample));
            if (nullptr != meta)
                return meta->header.eFlags & NMMSS::SMediaSampleHeader::EFPreroll;
            return false;

        }
        bool isVideoData() const
        {
            GstMetaMarking * meta = GST_META_MARKING_GET(gst_sample_get_buffer(m_sample));
            if (nullptr != meta)
                return meta->header.nMajor == NMMSS::NMediaType::Video::ID;
            return false;
        }
        bool isEoS() const
        {
            GstMetaMarking * meta = GST_META_MARKING_GET(gst_sample_get_buffer(m_sample));
            if (nullptr != meta)
                return NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&(meta->header));
            return false;
        }
        bool isKeyData() const
        {
            GstMetaMarking * meta = GST_META_MARKING_GET(gst_sample_get_buffer(m_sample));
            if (nullptr != meta)
                return !(meta->header.eFlags & (NMMSS::SMediaSampleHeader::EFNeedKeyFrame
                    | NMMSS::SMediaSampleHeader::EFNeedPreviousFrame
                    | NMMSS::SMediaSampleHeader::EFNeedInitData));
            return false;
        }
        GstBuffer* getBuffer()
        {
            return gst_sample_get_buffer(m_sample);
        }
        bool isAtomSample() const
        {
            GstMetaMarking * meta = GST_META_MARKING_GET(gst_sample_get_buffer(m_sample));
            return nullptr == meta;
        }
    };
    using  WrappedGstSample = std::shared_ptr<GstSampleData>;

    class GstDataBuffer : public NHttp::IDataBuffer
    {
    public:
        GstDataBuffer(WrappedGstSample s, std::uint64_t ts)
            : m_sample(s)
            , m_data(nullptr)
            , m_dataSize(0)
            , m_timestamp(ts)
        {
            if (m_sample)
            {
                auto const& mapInfo = m_sample->map();
                m_data = mapInfo.data;
                m_dataSize = mapInfo.size;
            }
        }

        std::uint8_t* GetData() const override
        {
            return m_data;
        }

        std::uint64_t GetSize() const override
        {
            return m_dataSize;
        }

        std::uint64_t GetTimestamp() const override
        {
            return m_timestamp;
        }

        bool IsKeyData() const override
        {
            return m_sample->isKeyData();
        }

        bool IsEoS() const override
        {
            return m_sample->isEoS();
        }

        bool IsUrgent() const override
        {
            return m_sample->isAtomSample();
        }

        bool IsBinary() const override
        {
            return true;
        }
    private:
        WrappedGstSample m_sample;

        std::uint8_t* m_data;
        std::uint64_t m_dataSize;
        std::uint64_t m_timestamp;
    };

    class CMP4Muxer : public NPluginHelpers::IMuxerSource
        , public virtual NCorbaHelpers::CWeakReferableImpl
    {
    protected:
        DECLARE_LOGGER_HOLDER;

    public:
        CMP4Muxer(DECLARE_LOGGER_ARG, int speed, NPluginHelpers::EStreamContainer sc, bool hasSubtitles, const std::string& filePath)
            : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_finished(false)
            , m_initDataFlow(false)
            , m_eosSent(false)
            , m_processing(false)
            , m_speed(speed)
#ifdef ENV64
            , m_audioSampleCount(0)
            , m_audioConnection(nullptr)
#endif
            , m_streamContainer(sc)
            , m_filePath(filePath)
            , m_hasSubtitles(hasSubtitles)
            , m_textPts(0)
        {
            INIT_LOGGER_HOLDER;

            _log_ << "MP4 muxer initialization";

            m_videoSource = m_streamParser = m_mp4Muxer = m_appSink = m_queue = nullptr;
#ifdef ENV64
            m_audioSource = m_audioParser = m_typeFinder = m_audioConvert = m_audioResample = m_audioEnc = m_videoQueue = m_audioQueue = nullptr;
#endif
        }

        ~CMP4Muxer()
        {
            _log_ << "MP4 muxer dtor";
        }

        void Init(FOnData dp, FOnFinishProcessing fp, FOnFormat fmt) override
        {
            if (!m_videoSink)
                throw std::runtime_error("MP4 pipeline error: video sink is absent");

            m_dp = dp;
            m_fp = fp;
            m_fmt = fmt;

            NPluginHelpers::TContHandler ph = std::bind(&CMP4Muxer::CreatePipeline, 
                NCorbaHelpers::CWeakPtr<CMP4Muxer>(this),
                std::placeholders::_1, std::placeholders::_2);
            NPluginHelpers::TErrorHandler fh = std::bind(&CMP4Muxer::StopPipeline,
                NCorbaHelpers::CWeakPtr<CMP4Muxer>(this),
                NMMSS::PtimeToQword(boost::posix_time::max_date_time));

#ifdef ENV64
            if (m_audioSink)
                m_audioSink->SetContCallback(ph, fh, m_videoSink.Get());
            else
#endif // ENV64
                m_videoSink->SetInitCallback(std::bind(ph, NMMSS::PSample{}, std::placeholders::_1), fh);
        }

        void Stop() override
        {
            stopDataTransmit();
        }

        NMMSS::IPullStyleSink* GetVideoSink() override
        {
            m_videoSink = NPluginHelpers::PRequestSink(NPluginHelpers::CreateGstreamerSink(GET_LOGGER_PTR,
                []() {}));
            return m_videoSink.Get();
        }

        NMMSS::IPullStyleSink* GetAudioSink() override
        {
#ifdef ENV64
            m_audioDecoder = NMMSS::CreateAudioDecoderPullFilter(GET_LOGGER_PTR);
            m_audioSink = NPluginHelpers::PRequestSink(NPluginHelpers::CreateGstreamerSink(GET_LOGGER_PTR,
                []() {}));
            m_audioConnection = NMMSS::GetConnectionBroker()->SetConnection(m_audioDecoder->GetSource(), m_audioSink.Get(), GET_LOGGER_PTR);

            NMMSS::IPullStyleSink* adSink = m_audioDecoder->GetSink();
            adSink->AddRef();

            return adSink;
#else
            return nullptr;
#endif // ENV64
        }

        NMMSS::IPullStyleSink* GetTextSink() override
        {
            m_textSink = NPluginHelpers::PRequestSink(NPluginHelpers::CreateGstreamerSink(GET_LOGGER_PTR,
                []() {}));
            return m_textSink.Get();
        }

        void onRequested(TLock& lock, unsigned int count)
        {
            lock.unlock();

            if (!m_initDataFlow.exchange(true, std::memory_order_relaxed))
            {
                internalSendSample();
            }
        }

    private:
        void configurePipeline()
        {
            g_object_set(this->m_videoSource, "is-live", TRUE, NULL);
#ifdef ENV64
            if (nullptr != this->m_audioSource)
                g_object_set(this->m_audioSource, "is-live", TRUE, NULL);
#endif
        }

        static void CreatePipeline(NCorbaHelpers::CWeakPtr<CMP4Muxer> weakObj, NMMSS::PSample as, NMMSS::PSample vs)
        {
            NCorbaHelpers::CAutoPtr<CMP4Muxer> pThis(weakObj);
            if (pThis)
                pThis->onCreatePipeline(as, vs);
        }

        void onCreatePipeline(NMMSS::PSample as, NMMSS::PSample vs)
        {
            NPluginHelpers::EStreamFormat sf = verifyVideoFormat(vs);

            if (NPluginHelpers::EUNSUPPORTED == sf)
                return;

            if (m_fmt)
                m_fmt(m_streamContainer);

            {
                std::unique_lock<std::mutex> lock(m_muxerMutex);
                m_muxers.insert(std::make_pair(this, NCorbaHelpers::CAutoPtr<CMP4Muxer>(this, NCorbaHelpers::ShareOwnership())));
            }

            m_pipeline.reset(gst_pipeline_new("pipeline"));
            gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);

            switch (m_streamContainer)
            {
            case NPluginHelpers::EMP4_CONTAINER:
            {
                InitMP4Pipeline(sf, as);
                break;
            }
            case NPluginHelpers::ENO_CONTAINER:
            {
                InitJPEGPipeline();
                break;
            }
            case NPluginHelpers::EHLS_CONTAINER:
            {
                InitHlsPipeline();
                break;
            }
            default:
                throw std::runtime_error("Unsupported pipeline");
            }

            GstBus* bus = gst_element_get_bus(m_pipeline.get());
            gst_bus_add_signal_watch(bus);
            g_signal_connect(G_OBJECT(bus), "message::error", G_CALLBACK(errorCallback), this);
            g_signal_connect(G_OBJECT(bus), "message::eos", G_CALLBACK(eosCallback), this);
            gst_object_unref(bus);

            if (GST_STATE_CHANGE_FAILURE == gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING))
                throw std::runtime_error("MP4 pipeline error: failed to set pipeline into PLAYING state");

            m_lastSeenTime = vs->Header().dtTimeBegin;

            g_object_set(m_videoSource, "do-timestamp", TRUE, NULL);
#ifdef ENV64
            g_object_set(m_audioSource, "do-timestamp", TRUE, NULL);
#endif
        }

        static void StopPipeline(NCorbaHelpers::CWeakPtr<CMP4Muxer> weakObj, std::uint64_t ts)
        {
            NCorbaHelpers::CAutoPtr<CMP4Muxer> pThis(weakObj);
            if (pThis)
                pThis->onStopPipeline(ts);
        }

        void onStopPipeline(std::uint64_t ts)
        {
            if (m_dp)
                m_dp(NHttp::CreateEoSBuffer(ts));

            if (m_fp)
                m_fp();
        }

        void InitMP4Pipeline(NPluginHelpers::EStreamFormat sf, NMMSS::PSample as)
        {
            makeGstElement("appsrc", "videoSource", m_videoSource);
            makeGstElement("queue", "vq", m_videoQueue);
            makeStreamParser(sf);
            makeGstElement("mp4mux", "mp4Muxer", m_mp4Muxer);
            makeGstElement("queue", "queue", m_queue);
            makeGstElement("appsink", "mixerSink", m_appSink);

#ifdef ENV64
            bool withAudio = (m_speed > 0) && as && NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Audio::PCM>(&as->Header());

            if (withAudio)
            {
                makeGstElement("appsrc", "audioSource", m_audioSource);
                //makeGstElement("typefind", "atf", m_typeFinder);

                makeGstElement("rawaudioparse", "audioParser", m_audioParser);
                makeGstElement("audioconvert", "audioConvert", m_audioConvert);
                makeGstElement("audioresample", "audioResample", m_audioResample);
                makeGstElement("voaacenc", "audioEnc", m_audioEnc);
                
                makeGstElement("queue", "aq", m_audioQueue);
            }
#endif // ENV64

#undef MAKE_GST_ELEMENT

            gst_util_set_object_arg(G_OBJECT(m_videoSource), "format", "time");
            g_object_set(m_streamParser, "config-interval", -1, NULL);
            g_object_set(m_mp4Muxer, "fragment-duration", 100, "faststart", 1, NULL);
            g_object_set(m_appSink, "emit-signals", TRUE, NULL);
            g_object_set(m_appSink, "max-buffers", 100, "drop", TRUE, NULL);

            g_signal_connect(m_videoSource, "need-data", G_CALLBACK(needVideoData), this);

#ifdef ENV64
            if (withAudio)
            {
                const auto* subheader = NMMSS::NMediaType::GetSampleOfSubtype<NMMSS::NMediaType::Audio::PCM::SubtypeHeader>(as.Get());
                m_audioSampleRate = subheader->nSampleRate;

                gst_util_set_object_arg(G_OBJECT(m_audioSource), "format", "time");

                GstAudioInfo info;

                GstAudioChannelPosition pos = GST_AUDIO_CHANNEL_POSITION_MONO;
                if (1 != subheader->nChannelsCount)
                    gst_audio_channel_positions_from_mask(subheader->nChannelsCount, 3, &pos);
                
                gst_audio_info_set_format(&info, convertToGstreamerFormat(subheader->nSampleType), subheader->nSampleRate * std::abs(m_speed), subheader->nChannelsCount, &pos);
                GstCaps* audioCaps = gst_audio_info_to_caps(&info);

                g_object_set(m_audioParser, "use-sink-caps", TRUE, NULL);

                // AAC stream ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                //std::uint32_t len = NMMSS::NMediaType::Audio::AAC::CalcExtradata(nullptr, 44100, 2);

                //GstBuffer* buffer = gst_buffer_new_and_alloc(len);
                //GstMapInfo map;
                //gst_buffer_map(buffer, &map, GST_MAP_WRITE);
                //NMMSS::NMediaType::Audio::AAC::CalcExtradata(map.data, 44100, 2);

                //gst_buffer_unmap(buffer, &map);

                //GstCaps* audioCaps = gst_caps_new_simple("audio/mpeg",
                //    "format", G_TYPE_STRING, "F32LE",
                //    "rate", G_TYPE_INT, 44100,
                //    "channels", G_TYPE_INT, 2,
                //    "mpegversion", G_TYPE_INT, 4,
                //    //"stream-format", G_TYPE_STRING, "raw",
                //    "codec_data", GST_TYPE_BUFFER, buffer,
                //    NULL);

                // PCM stream ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                //GstCaps* audioCaps = gst_caps_new_simple("audio/x-raw",
                //    "format", G_TYPE_STRING, "S16LE",
                //    "rate", G_TYPE_INT, 48000,
                //    "channels", G_TYPE_INT, 2,
                //    //"layout", G_TYPE_STRING, "non-interleaved",
                //    "layout", G_TYPE_STRING, "interleaved",
                //    NULL);

                //g_object_set(m_audioParser, "use-sink-caps", TRUE, NULL);

                // MP2 stream ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
                /*GstCaps* audioCaps = gst_caps_new_simple("audio/mpeg",
                    "format", G_TYPE_STRING, "F32LE",
                    "rate", G_TYPE_INT, 48000,
                    "channels", G_TYPE_INT, 2,
                    "mpegversion", G_TYPE_INT, 1,
                    NULL);*/

                g_object_set(m_audioSource, "caps", audioCaps, NULL);
                gst_caps_unref(audioCaps);

                //g_signal_connect(m_typeFinder, "have-type", G_CALLBACK(cb_typefound), this);
                g_signal_connect(m_audioSource, "need-data", G_CALLBACK(needAudioData), this);
            }
#endif // ENV64

            configurePipeline();

            g_signal_connect(m_appSink, "new-sample", G_CALLBACK(newSample), this);

#ifdef ENV64
            if (withAudio)
            {
                /// PCM stream /////////
                gst_element_link_many(m_audioSource, m_audioQueue, m_audioParser, m_audioConvert, m_audioResample, m_audioEnc, m_mp4Muxer, NULL);

                /// AAC stream /////////
                //gst_element_link_many(m_audioSource, /*m_audioParser, m_audioConvert, m_audioEnc, m_audioQueue,*/ m_aacParser, m_mp4Muxer, NULL);

                /// MP2 Stream ////////
                //gst_element_link_many(m_audioSource, /*m_audioParser, m_audioConvert, m_audioEnc, m_audioQueue,*/ m_mp2Parser, m_mp4Muxer, NULL);
            }
#endif // ENV64

            gst_element_link_many(m_videoSource, m_videoQueue, m_streamParser, m_mp4Muxer, NULL);
            gst_element_link_many(m_mp4Muxer, m_queue, m_appSink, NULL);
        }

        void InitJPEGPipeline()
        {
            makeGstElement("appsrc", "videoSource", m_videoSource);
            makeGstElement("jpegparse", "streamParser", m_streamParser);
            makeGstElement("queue", "queue", m_queue);
            makeGstElement("appsink", "mixerSink", m_appSink);

#undef MAKE_GST_ELEMENT

            gst_util_set_object_arg(G_OBJECT(m_videoSource), "format", "time");
            g_object_set(m_appSink, "emit-signals", TRUE, NULL);
            g_object_set(m_appSink, "max-buffers", 100, "drop", TRUE, NULL);

            g_signal_connect(m_videoSource, "need-data", G_CALLBACK(needVideoData), this);

            configurePipeline();

            g_signal_connect(m_appSink, "new-sample", G_CALLBACK(newSample), this);

            gst_element_link_many(m_videoSource, m_streamParser, m_queue, m_appSink, NULL);
        }

        void InitHlsPipeline()
        {
            makeGstElement("appsrc", "videoSource", m_videoSource);
            makeGstElement("queue", "queue", m_queue);
            makeGstElement("h264parse", "streamParser", m_streamParser);
            makeGstElement("mpegtsmux", "mp4Muxer", m_mp4Muxer);
            makeGstElement("hlssink", "mixerSink", m_appSink);

#undef MAKE_GST_ELEMENT

            std::string pl_location = m_filePath + "/playlist.m3u8";
            std::string location = m_filePath + "/segment%05d.ts";

            gst_util_set_object_arg(G_OBJECT(m_videoSource), "format", "time");
            g_object_set(m_appSink, "playlist-location", pl_location.c_str(), NULL);
            g_object_set(m_appSink, "location", location.c_str(), NULL);
            g_object_set(m_appSink, "target-duration", 3, NULL);

            g_signal_connect(m_videoSource, "need-data", G_CALLBACK(needVideoData), this);

            configurePipeline();

            gst_element_link_many(m_videoSource, m_queue, m_streamParser, m_mp4Muxer, m_appSink, NULL);
        }

        static gboolean needVideoData(GstElement*, guint, gpointer h)
        {
            auto handler = GetMuxer(h);
            if (!handler)
                return FALSE;
            handler->onNeedVideoData();
            return TRUE;
        }

        void onNeedVideoData()
        {
            std::call_once(m_needVideoFlag, [this]() {
                if (m_videoSink)
                    m_videoSink->SetPushCallback(std::bind(&CMP4Muxer::prepareVideoData, NCorbaHelpers::CWeakPtr<CMP4Muxer>(this), std::placeholders::_1));
                if (m_hasSubtitles && m_textSink)
                    m_textSink->SetPushCallback(std::bind(&CMP4Muxer::prepareTextData, NCorbaHelpers::CWeakPtr<CMP4Muxer>(this), std::placeholders::_1));
            });
        }

#ifdef ENV64
        static void
            cb_typefound(GstElement* typefind,
                guint       probability,
                GstCaps* caps,
                gpointer    h)
        {
            auto handler = GetMuxer(h);
            if (handler)
            {
                gchar* type = gst_caps_to_string(caps);
                handler->log(boost::str(boost::format("Media type %1% found, probability %2%") % type % probability).c_str());
                g_free(type);
            }
        }

        static gboolean needAudioData(GstElement*, guint, gpointer h)
        {
            auto handler = GetMuxer(h);
            if (!handler)
                return FALSE;
            handler->onNeedAudioData();
            return TRUE;
        }

        void onNeedAudioData()
        {
            std::call_once(m_needAudioFlag, [this]() {
                if (m_audioSink)
                    m_audioSink->SetPushCallback(std::bind(&CMP4Muxer::prepareAudioData, NCorbaHelpers::CWeakPtr<CMP4Muxer>(this), std::placeholders::_1));
            });
        }
#endif // ENV64

        static void prepareTextData(NCorbaHelpers::CWeakPtr<CMP4Muxer> weakObj, NMMSS::PSample sample)
        {
            NCorbaHelpers::CAutoPtr<CMP4Muxer> pThis(weakObj);
            if (pThis)
                pThis->onPrepareTextData(sample);
        }

        void onPrepareTextData(NMMSS::PSample sample)
        {
            if (!sample || NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&sample->Header()))
            {
                _log_ << "No text data";
                m_reactor->GetIO().post(std::bind(&CMP4Muxer::sendEoS,
                    NCorbaHelpers::CAutoPtr<CMP4Muxer>(this, NCorbaHelpers::ShareOwnership())));
                return;
            }

            //NMMSS::NMediaType::Application::TypedOctetStream::SubtypeHeader& subHeader =
            //    sample->SubHeader<NMMSS::NMediaType::Application::TypedOctetStream>();

            //if (MMSS_MAKEFOURCC('S', 'T', 'T', 'U') == subHeader.nTypeMagic) // is utf-8 subtitles subtype
            //{
            //    std::uint8_t* ptr = sample->GetBody();

            //    std::uint32_t textLen = *((std::uint32_t*)ptr);
            //    ptr += sizeof(std::uint32_t);

            //    GstBuffer* buf = gst_buffer_new_and_alloc(textLen);
            //    GST_BUFFER_PTS(buf) = m_textPts;
            //    GST_BUFFER_DURATION(buf) = 1000000000;
            //    m_textPts += 1000000000;

            //    GstMapInfo map;
            //    gst_buffer_map(buf, &map, GST_MAP_WRITE);

            //    memcpy(map.data, ptr, textLen);

            //    gst_buffer_unmap(buf, &map);

            //    GstFlowReturn ret;
            //    g_signal_emit_by_name(m_textSource, "push-buffer", buf, &ret);
            //    gst_buffer_unref(buf);
            //}

            NHttp::PDataBuffer db = NHttp::CreateDataBufferFromSample(sample.Get());
            if (db && m_dp)
                m_dp(db);
        }

        static void prepareVideoData(NCorbaHelpers::CWeakPtr<CMP4Muxer> weakObj, NMMSS::PSample sample)
        {
            NCorbaHelpers::CAutoPtr<CMP4Muxer> pThis(weakObj);
            if (pThis)
                pThis->onPrepareVideoData(sample);
        }

        void onPrepareVideoData(NMMSS::PSample sample)
        {
            if (!sample || NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&sample->Header()))
            {
                _log_ << "No video data";
                m_reactor->GetIO().post(std::bind(&CMP4Muxer::sendEoS,
                    NCorbaHelpers::CAutoPtr<CMP4Muxer>(this, NCorbaHelpers::ShareOwnership())));
                return;
            }

            GstBuffer* buf = gst_buffer_new_and_alloc(sample->Header().nBodySize);

            GST_META_MARKING_ADD(buf, &(sample->Header()));

            GstMapInfo map;
            gst_buffer_map(buf, &map, GST_MAP_WRITE);

            memcpy(map.data, sample->GetBody(), sample->Header().nBodySize);

            gst_buffer_unmap(buf, &map);

            GstFlowReturn ret;
            g_signal_emit_by_name(m_videoSource, "push-buffer", buf, &ret);
            gst_buffer_unref(buf);
        }

#ifdef ENV64
        static void prepareAudioData(NCorbaHelpers::CWeakPtr<CMP4Muxer> weakObj, NMMSS::PSample sample)
        {
            NCorbaHelpers::CAutoPtr<CMP4Muxer> pThis(weakObj);
            if (pThis)
                pThis->onPrepareAudioData(sample);
        }

        void onPrepareAudioData(NMMSS::PSample sample)
        {
            if (!sample || NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&sample->Header()))
            {
                m_reactor->GetIO().post(std::bind(&CMP4Muxer::sendEoS,
                    NCorbaHelpers::CAutoPtr<CMP4Muxer>(this, NCorbaHelpers::ShareOwnership())));
                return;
            }

            GstBuffer* buf = gst_buffer_new_and_alloc(sample->Header().nBodySize);

            const auto* subheader = NMMSS::NMediaType::GetSampleOfSubtype<NMMSS::NMediaType::Audio::PCM::SubtypeHeader>(sample.Get());

            std::uint32_t bSize = sample->Header().nBodySize;

            guint64 duration = gst_util_uint64_scale(bSize, GST_SECOND, m_audioSampleRate * subheader->nChannelsCount * NMMSS::NMediaType::Audio::GetTypeSize(subheader->nSampleType));
            GST_BUFFER_DURATION(buf) = duration;
            GST_BUFFER_PTS(buf) = GST_BUFFER_DTS(buf) = m_audioSampleCount;

            m_audioSampleCount += duration;

            GST_META_MARKING_ADD(buf, &(sample->Header()));

            GstMapInfo map;
            gst_buffer_map(buf, &map, GST_MAP_WRITE);

            memcpy(map.data, sample->GetBody(), sample->Header().nBodySize);

            gst_buffer_unmap(buf, &map);

            GstFlowReturn ret;
            g_signal_emit_by_name(m_audioSource, "push-buffer", buf, &ret);
            gst_buffer_unref(buf);
        }
#endif // ENV64

        static GstFlowReturn newSample(GstElement* sink, gpointer h)
        {
            auto handler = GetMuxer(h);
            if (!handler)
            {
                return GST_FLOW_ERROR;
            }

            try
            {
                GstSample* sample;
                g_signal_emit_by_name(sink, "pull-sample", &sample);

                {
                    WrappedGstSample wgs = std::make_shared<GstSampleData>(sample);
                    std::unique_lock<std::mutex> lock(handler->m_cvMutex);
                    handler->selectSampleSend(wgs);
                }
            }
            catch (const boost::system::system_error&)
            {
                handler->log("Client connection is closed");
                return GST_FLOW_ERROR;
            }
            return GST_FLOW_OK;
        }

        void sendSample(WrappedGstSample sample)
        {
            std::uint64_t sTs = sample->getTimestamp();
            if (sTs != ATOM_TIMESTAMP)
                m_lastSeenTime = sTs;

            NHttp::PDataBuffer data = std::make_shared<GstDataBuffer>(sample, m_lastSeenTime);
            if (m_dp)
                m_dp(data);
            return;
        }

        void internalSendSample()
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            if (!m_samplesToSend.empty())
            {
                m_processing = true;
                WrappedGstSample sample = m_samplesToSend.front();
                m_samplesToSend.pop_front();
                lock.unlock();

                selectSampleSend(sample);
            }
            else
                m_processing = false;
        }

        void selectSampleSend(WrappedGstSample s)
        {
            switch (m_streamContainer)
            {
            case NPluginHelpers::ENO_CONTAINER:
            case NPluginHelpers::EMP4_CONTAINER:
            case NPluginHelpers::EHLS_CONTAINER:
            {
                sendSample(s);
                break;
            }
            default:
                _wrn_ << "No handler for sample";
            }
        }

        void makeGstElement(const char* const type, std::string&& name, GstElement*& member)
        {
            do {
                member = gst_element_factory_make(type, name.c_str());
                if (nullptr == member)
                    throw std::runtime_error("failed to create " + name + " element");
                if (!gst_bin_add(GST_BIN(m_pipeline.get()), member))
                    throw std::runtime_error("failed to add " + name + " element into pipeline");
            } while (false);
        }

        void makeStreamParser(NPluginHelpers::EStreamFormat sf)
        {
            switch (sf)
            {
            case NPluginHelpers::EH265:
                makeGstElement("h265parse", "streamParser", m_streamParser);
                break;
            case NPluginHelpers::EMPG4:
                makeGstElement("mpeg4videoparse", "streamParser", m_streamParser);
                break;
            default:
                makeGstElement("h264parse", "streamParser", m_streamParser);
            }
        }

        static NCorbaHelpers::CAutoPtr<CMP4Muxer> GetMuxer(gpointer h)
        {
            auto handle = reinterpret_cast<CMP4Muxer*>(h);
            std::unique_lock<std::mutex> lock(m_muxerMutex);
            TMuxers::iterator it = m_muxers.find(handle);
            if (m_muxers.end() != it)
                return it->second;
            return NCorbaHelpers::CAutoPtr<CMP4Muxer>();
        }

        static void errorCallback(GstBus*, GstMessage* msg, gpointer h)
        {
            if (auto handler = GetMuxer(h))
            {
                GError *err;
                gchar *debug_info;

                gst_message_parse_error(msg, &err, &debug_info);
                handler->log(boost::str(boost::format("Error received from element %1%: %2%") % GST_OBJECT_NAME(msg->src) % err->message).c_str());
                handler->log(boost::str(boost::format("Debugging information: %1%") % (debug_info ? debug_info : "none")).c_str());
                g_clear_error(&err);
                g_free(debug_info);

                handler->log("Gstreamer error: disconnected");
                handler->requestStopDataTransmit(false);
            }
        }

        static void eosCallback(GstBus*, GstMessage* msg, gpointer h)
        {
            if (auto handler = GetMuxer(h))
            {
                handler->log("Gstreamer: EOS");
                handler->requestStopDataTransmit(true);
            }
        }

        void log(const char* const msg)
        {
            _log_ << msg;
        }

        void audioSinkDisconnected()
        {
#ifdef ENV64
            if (nullptr != m_audioConnection)
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(m_audioConnection);
                m_audioConnection = nullptr;
            }
#endif // ENV64
        }

        void sendEoS()
        {
            if (!m_eosSent.exchange(true, std::memory_order_relaxed))
            {
                _log_ << "Stop stalled pipleline";
                requestStopDataTransmit(true);
            }
        }

        void stopDataTransmit()
        {
            if (!m_finished.exchange(true, std::memory_order_relaxed))
            {
#ifdef ENV64
                if (nullptr != m_audioConnection)
                    NMMSS::GetConnectionBroker()->DestroyConnection(m_audioConnection);
#endif // ENV64

                if (m_videoSink)
                    m_videoSink->Stop();
#ifdef ENV64
                if (m_audioSink)
                    m_audioSink->Stop();
#endif // ENV64
            }
        }

        void printAtomInfo(std::uint64_t atomInfo, std::uint32_t& atomSize)
        {
            atomSize = htonl(static_cast<std::uint32_t>(atomInfo)) - 8;
        }

    protected:
        void requestStopDataTransmit(bool eos)
        {
            onStopPipeline(eos ? m_lastSeenTime : NMMSS::PtimeToQword(boost::posix_time::max_date_time));

            GstBus* bus = gst_element_get_bus(m_pipeline.get());
            gst_bus_remove_signal_watch(bus);
            gst_object_unref(bus);

            gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);

            {
                std::unique_lock<std::mutex> lock(m_muxerMutex);
                m_muxers.erase(this);
            }
        }

        NPluginHelpers::EStreamFormat verifyVideoFormat(NMMSS::PSample s)
        {
            if (s)
            {
                _log_ << "Process stream with type " << NPluginUtility::parseForCC(s->Header().nSubtype);
                switch (s->Header().nSubtype)
                {
                case NMMSS::NMediaType::Video::fccH264::ID:
                {
                    return (m_streamContainer == NPluginHelpers::EMP4_CONTAINER || m_streamContainer == NPluginHelpers::EHLS_CONTAINER) ?
                        NPluginHelpers::EH264 : NPluginHelpers::EUNSUPPORTED;
                }
                case NMMSS::NMediaType::Video::fccH265::ID:
                {
                    return (m_streamContainer == NPluginHelpers::EMP4_CONTAINER) ?
                        NPluginHelpers::EH265 : NPluginHelpers::EUNSUPPORTED;
                }
                case NMMSS::NMediaType::Video::fccMPEG4::ID:
                {
                    return (m_streamContainer == NPluginHelpers::EMP4_CONTAINER) ?
                        NPluginHelpers::EMPG4 : NPluginHelpers::EUNSUPPORTED;
                }
                case NMMSS::NMediaType::Video::fccJPEG::ID:
                {
                    m_streamContainer = NPluginHelpers::ENO_CONTAINER;
                    return NPluginHelpers::EJPEG;
                }
                }
            }
            else
                _log_ << "Null data sample";

            onStopPipeline(NMMSS::PtimeToQword(boost::posix_time::max_date_time));

            return NPluginHelpers::EUNSUPPORTED;
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

        GstElement* m_videoSource;
        GstElement* m_streamParser;
        GstElement* m_mp4Muxer;
        GstElement* m_appSink;
        GstElement* m_videoQueue;
        GstElement* m_audioQueue;
        GstElement* m_queue;

#ifdef ENV64
        GstElement* m_audioSource;
        GstElement* m_typeFinder;
        GstElement* m_audioParser;
        GstElement* m_audioConvert;
        GstElement* m_audioResample;
        GstElement* m_audioEnc;
#endif // ENV64

        struct GstElementDtor
        {
            void operator()(GstElement* e) const
            {
                gst_object_unref(e);
            }
        };
        std::unique_ptr<GstElement, GstElementDtor> m_pipeline;

        NCorbaHelpers::PReactor m_reactor;

        typedef std::unordered_map<CMP4Muxer*, NCorbaHelpers::CAutoPtr<CMP4Muxer> > TMuxers;
        static TMuxers m_muxers;
        static std::mutex m_muxerMutex;

        NPluginHelpers::PRequestSink m_videoSink;
        NPluginHelpers::PRequestSink m_audioSink;
        NPluginHelpers::PRequestSink m_textSink;

        std::atomic<bool> m_finished;

        std::atomic<bool> m_initDataFlow;
        std::atomic<bool> m_eosSent;

        std::mutex m_cvMutex;
        std::deque<WrappedGstSample> m_samplesToSend;

        bool m_processing;

        FOnData m_dp;
        FOnFinishProcessing m_fp;
        FOnFormat m_fmt;

        std::uint16_t m_audioSampleRate;
        int m_speed;
        std::uint64_t m_lastSeenTime;

#ifdef ENV64
        std::uint64_t m_audioSampleCount;
        NMMSS::PPullFilter m_audioDecoder;
        NMMSS::IConnectionBase* m_audioConnection;
#endif // ENV64

        NPluginHelpers::EStreamContainer m_streamContainer;
        std::string m_filePath;

        std::once_flag m_needVideoFlag;
        std::once_flag m_needAudioFlag;
        std::once_flag m_needTextFlag;

        bool m_hasSubtitles;
        std::uint64_t m_textPts;
    };

    typename CMP4Muxer::TMuxers CMP4Muxer::m_muxers;
    std::mutex CMP4Muxer::m_muxerMutex; 
}

namespace NPluginHelpers
{
    IMuxerSource* CreateMP4Muxer(DECLARE_LOGGER_ARG, int speed, EStreamContainer sc, bool hasSubtitles, const std::string& filePath)
    {
        return new CMP4Muxer(GET_LOGGER_PTR, speed, sc, hasSubtitles, filePath);
    }
}
