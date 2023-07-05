#ifndef FFMPEG__CODEC__H__
#define FFMPEG__CODEC__H__

#include "../mIntTypes.h"
#include "Transforms.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <Logging/log2.h>
#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include "FFmpegAllocator.h"
#include "FFmpegMutex.h"
#include "FrameLagHandler.h"
#include "MMCodingExports.h"

namespace NMMSS
{

#ifdef _MSC_VER
#pragma warning(push)
#endif // _MSC_VER

    const int MY_AVCODEC_MAX_AUDIO_FRAME_SIZE = 192000; // deprecated contant replacement
    // Должна соответствовать константе из AllocatorImpl.cpp 
    const unsigned int FFMPEG_INPUT_BUFFER_TAIL_PADDING = 32;

    const int MAX_CODED_WIDTH  = 8192;
    const int MAX_CODED_HEIGHT = 8192;

    inline bool CheckCodedDimensionsSanity(int width, int height)
    {
        return true
            && width>0
            && width<=MAX_CODED_WIDTH
            && height>0
            && height<=MAX_CODED_HEIGHT
            ;
    }

struct AVFrameDtor
{
    void operator()(AVFrame* f) const
    {
        av_frame_free(&f);
    }
};
typedef std::unique_ptr<AVFrame, AVFrameDtor> AVFramePtr;

struct AVCodecContextDtor
{
    void operator()(AVCodecContext* c) const
    {
        // Clear any reference to our internal buffer to prevent double free of memory block.
        c->extradata = 0;
        c->extradata_size = 0;

        // To destroy codec context there is no need to call
        // avcodec_close() any more
        // see http://git.videolan.org/gitweb.cgi/ffmpeg.git/?a=commit;h=fd056029f45a9f6d213d9fce8165632042511d4f      
        // In fact, avcodec_free_context will call avcodec_close itself.
        boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
        avcodec_free_context(&c);
    }
};
typedef std::unique_ptr<AVCodecContext, AVCodecContextDtor> AVCodecContextPtr;

struct AVCodecContextClosingDtor
{
    void operator()(AVCodecContext* c) const
    {
        boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
        if (c->codec)
        {
            avcodec_flush_buffers(c);
            avcodec_close(c);
        }
    }
};
typedef std::unique_ptr<AVCodecContext, AVCodecContextClosingDtor> AVCodecContextClosingPtr;

struct AVFormatContextDtor
{
    void operator()(AVFormatContext* c) const
    {
        avformat_close_input(&c);
    }
};
typedef std::unique_ptr<AVFormatContext, AVFormatContextDtor> AVFormatContextPtr;

struct SwsContextDtor
{
    void operator()(SwsContext* c) const
    {
        sws_freeContext(c);
    }
};
typedef std::unique_ptr<SwsContext, SwsContextDtor> SwsContextPtr;

struct SwrContextDtor
{
    void operator()(SwrContext* c) const
    {
        swr_free(&c);
    }
};
typedef std::unique_ptr<SwrContext, SwrContextDtor> SwrContextPtr;

#if defined(_MSC_VER)
// disable warning C4275: non dll-interface class 'std::exception' used as base for dll-interface class 'NMMSS::AVException'
// because we don not expect AVException to be used outside of NGP or with a different compiler.
#pragma warning(push)
#pragma warning(disable: 4275)
#endif

class MMCODING_CLASS_DECLSPEC AVException: public std::exception
{
    struct Impl;
    Impl* m_impl;

public:
    explicit AVException(int avError, const char* method = nullptr) noexcept;
    virtual ~AVException();

    int avErrorCode() const noexcept;
    virtual const char* what() const noexcept override;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

class CFFMPEGBase
{
protected:
    DECLARE_LOGGER_HOLDER;

public:
    CFFMPEGBase(DECLARE_LOGGER_ARG)

    {
        INIT_LOGGER_HOLDER;
    }

    const AVCodecContext* const Context()
    {
        return this->GetContext();
    }

    static AVCodecContextPtr AllocContext(AVCodec* codec)
    {
        return AVCodecContextPtr( avcodec_alloc_context3(codec) );
    }
        
    static AVCodecContextPtr AllocContext()
    {
        AVCodecContextPtr context { avcodec_alloc_context3(nullptr) };

        context->codec = 0;

        return context;
    }

    void SetExtradata(
        const uint8_t* extradataPtr,
        uint32_t extradataSize)
    {
        // Для FFMPEG'а необходимо выделять буфер для входных данных с запасом,
        // иначе он может падать при чтении.
        // Размер "зазора" равен FFMPEG_INPUT_BUFFER_TAIL_PADDING.
        m_extradata.reserve(extradataSize + FFMPEG_INPUT_BUFFER_TAIL_PADDING);

        m_extradata.assign(extradataPtr, extradataPtr + extradataSize);
    }

protected:
        
    AVCodecContext* const GetContext(AVCodec* codec)
    {
        if(!m_context)
        {
            m_context = AllocContext(codec);
        }

        return m_context.get();
    }

    AVCodecContext* const GetContext()
    {
        if(!m_context)
        {
            m_context = AllocContext();
        }

        return m_context.get();
    }

    bool OpenContext(AVCodec* _codec)
    {
        if(!m_context || !_codec) return false;

        boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());

        if (0 != avcodec_open2(m_context.get(), _codec, 0))
        {
            _log_ << "OpenContext(): avcodec_open2 failed" << std::endl;
            return false;
        }

        return true;
    }

    void ClearContext()
    {
        m_context.reset();
    }

    bool IsValidVideoContext(AVCodecID codecID,
                             int codedWidth,
                             int codedHeight)
    {
        if (!m_context || !m_context->codec || codecID!=m_context->codec->id ||
                (m_context->lowres && (
                          ( ( -((-codedWidth) >> m_context->lowres)  != m_context->width) && (codedWidth > 0) )
                        ||( ( -((-codedHeight) >> m_context->lowres) != m_context->height) && (codedHeight > 0))
                    ) 
                )
           )
        {
            return false;
        }

        return true;
    }


    bool IsValidAudioContext(AVCodecID codecID,
                             int channels,
                             int sampleRate,
                             int bitRate)
    {
        if( !m_context || !(m_context->codec)
            || (codecID     != m_context->codec->id)
            || ( (channels  != m_context->channels) && (channels > 0) )
            || ( (sampleRate != m_context->sample_rate) && (sampleRate > 0) )
            || ( (bitRate != m_context->bit_rate) && (bitRate > 0) )
          )
        {
            return false;
        }

        return true;
    }


protected:
AVCodecContextPtr m_context;
std::vector<uint8_t> m_extradata;
};


class CFFMPEGVideoDecoder : public CFFMPEGBase
{
public:
    CFFMPEGVideoDecoder(DECLARE_LOGGER_ARG, IAllocator* allocator, int concurrentCount)
        : CFFMPEGBase(GET_LOGGER_PTR)
        , m_FFmpegAllocator(allocator)
        , m_swsAllocator(allocator)
        , m_concurrentCount(concurrentCount)
        , m_isFirstSuccessfulDecoding(false)
    {
    }

    AVFrame* Decode(
        bool& isKeyFrame,
        AVCodecID codecID,
        int codedWidth,
        int codedHeight,
        int lowres,
        uint8_t*& dataPtr,
        uint32_t& dataSize,
        uint64_t dts,
        bool& preroll)
    {
        if(!CheckCodedDimensionsSanity(codedWidth, codedHeight))
        {
            codedWidth = 0;
            codedHeight = 0;
        }

        if( !IsValidVideoContext(codecID, codedWidth, codedHeight)
            || (isKeyFrame && (lowres != GetContext()->lowres)) )
        {
            boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
            AVCodec* codec = avcodec_find_decoder(codecID);
            lock.unlock();

            if(!codec)
            {
                _err_ << "Couldn't find decoder for codecID = "
                      << codecID << std::endl;
                throw std::runtime_error("Couldn't find decoder");
            }

            m_frame.reset(av_frame_alloc());
            if(!m_frame)
            {
                _err_ << "Couldn't alloc frame" << std::endl;
                throw std::runtime_error("Couldn't alloc frame");
            }
            ClearContext();
            m_lagHandler.Clear();

            AVCodecContext* const codecCtx = GetContext(codec);
            codecCtx->coded_width    = codedWidth;
            codecCtx->coded_height   = codedHeight;
            codecCtx->get_buffer2 = CFFmpegAllocator::get_buffer2;
            codecCtx->opaque         = (void*)&m_FFmpegAllocator;
            codecCtx->flags2 |= AV_CODEC_FLAG2_FAST;
            codecCtx->thread_safe_callbacks = 1;

            setupMultithreaded(codecCtx);

            av_codec_set_lowres(codecCtx, lowres);

            if( m_extradata.empty() )
            {
                codecCtx->extradata      = 0;
                codecCtx->extradata_size = 0;
            }
            else
            {
                codecCtx->extradata      = &(m_extradata[0]);
                codecCtx->extradata_size = m_extradata.size();
            }

            if(!OpenContext(codec))
            {
                _err_ << "Couldn't open context for codecID = "
                      << codecID << std::endl;
                throw std::runtime_error("Couldn't open context");
            }
        }

        AVCodecContext* const codecCtx = GetContext();
        if(!codecCtx || !m_frame)
        {
            _err_ << "Bad codec context or frame container" << std::endl;
            throw std::runtime_error("Bad codec context or frame container");
        }

        int get_pic = 0;
        uint8_t* in_data = dataPtr;
        uint32_t in_size = dataSize;

        do
        {
            AVPacket pkt;
            av_init_packet(&pkt);
            pkt.pts = pkt.dts = AV_NOPTS_VALUE;
            pkt.data = in_data;
            pkt.size = in_size;
            pkt.flags |= isKeyFrame ? AV_PKT_FLAG_KEY : 0;

            int bytes_decoded = avcodec_decode_video2(
                codecCtx, m_frame.get(), &get_pic, &pkt);

            if(dataPtr && bytes_decoded <= 0)
            {
                char buf[AV_ERROR_MAX_STRING_SIZE + 1] = { 0 };
                av_strerror(bytes_decoded, buf, sizeof(buf) - 1);
                throw std::runtime_error(
                    std::string("Couldn't decode video frame data: ") + buf);
            }

            //FIXME!!!: Ugly hack, must be fixed in FFmpeg
            if (codecCtx->codec_id == AV_CODEC_ID_MJPEG || codecCtx->codec_id == AV_CODEC_ID_MPEG4)
            {
                bytes_decoded = in_size;
            }

            in_data += bytes_decoded;
            in_size -= bytes_decoded;
        }
        while(in_size > 0 && !get_pic);

        if (get_pic)
        {
            m_isFirstSuccessfulDecoding = true;
        }

        m_lagHandler.RegisterInputFrame(dts, preroll, isKeyFrame);

        if (m_isFirstSuccessfulDecoding)
        {
            preroll = !m_lagHandler.RegisterOutputFrame();
        }

        if(!get_pic && !dataSize)
        {
            avcodec_flush_buffers(codecCtx);
            m_lagHandler.Clear();
        }

        dataPtr  = in_data;
        dataSize = in_size;

        if ((preroll || !get_pic) && !m_lagHandler.Check())
            _err_ << "Too much frames in FrameLagHandler";

        if(!get_pic)
            return nullptr;

        m_frame->reordered_opaque = m_lagHandler.LastTimestamp();
        isKeyFrame = m_lagHandler.LastKeySample();
        return m_frame.get();
    }

    AVFramePtr convertPixelFormat(AVPixelFormat toPixFmt)
    {
        if (!m_swsContext)
        {
            m_swsContext.reset(
                sws_getContext(
                    m_frame->width, m_frame->height, static_cast<AVPixelFormat>(m_frame->format),
                    m_frame->width, m_frame->height, toPixFmt,
                    SWS_BICUBIC, NULL, NULL, NULL)
            );
        }

        if (m_swsContext && m_frame)
        {
            AVFramePtr swsFrame;
            swsFrame.reset(av_frame_alloc());
            swsFrame->height = m_frame->height;
            swsFrame->width = m_frame->width;
            swsFrame->format = toPixFmt;

            m_swsAllocator.get_buffer2_impl(0, swsFrame.get(), 0);

            int ret = sws_scale(m_swsContext.get(),
                        m_frame->data, m_frame->linesize, 0, m_frame->height,
                        swsFrame->data, swsFrame->linesize);

            if (ret > 0)
            {
                return swsFrame;
            }
        }

        return 0;
    }

private:
    void setupMultithreaded(AVCodecContext* const codecCtx);

private:
    SwsContextPtr m_swsContext;
    NMMSS::CFFmpegAllocator m_FFmpegAllocator, m_swsAllocator;
    AVFramePtr m_frame;
    FrameLagHandler m_lagHandler;
    int m_concurrentCount;
    bool m_isFirstSuccessfulDecoding;
};

inline int getSampleTypeFromAVFormat(AVSampleFormat avFmt)
{
    switch (avFmt)
    {
        case AV_SAMPLE_FMT_U8: return NMMSS::NMediaType::Audio::ST_UINT8;
        case AV_SAMPLE_FMT_S16: return NMMSS::NMediaType::Audio::ST_INT16;
        case AV_SAMPLE_FMT_S32: return NMMSS::NMediaType::Audio::ST_INT32;
        case AV_SAMPLE_FMT_FLT: return NMMSS::NMediaType::Audio::ST_FLOAT32;
        default: return 0;
    }
}

class S16AudioResampler
{
    SwrContextPtr m_swr;
    decltype(AVFrame::channel_layout) m_inChannelLayout = ~decltype(AVFrame::channel_layout)();
    decltype(AVFrame::sample_rate) m_inSampleRate = ~decltype(AVFrame::sample_rate)();
    AVSampleFormat m_inFormat = AV_SAMPLE_FMT_NONE;

public:
    S16AudioResampler() = default;

    void operator()(AVFramePtr& frame)
    {
        AVFramePtr converted(av_frame_alloc());

        if (!m_swr ||
            m_inChannelLayout != frame->channel_layout ||
            m_inSampleRate != frame->sample_rate ||
            m_inFormat != (AVSampleFormat)frame->format)
        {
            m_inChannelLayout = frame->channel_layout;
            m_inSampleRate = frame->sample_rate;
            m_inFormat = (AVSampleFormat)frame->format;

            m_swr.reset(swr_alloc());

            av_opt_set_int(m_swr.get(), "in_channel_layout", frame->channel_layout, 0);
            av_opt_set_int(m_swr.get(), "out_channel_layout", frame->channel_layout, 0);
            av_opt_set_int(m_swr.get(), "in_sample_rate", frame->sample_rate, 0);
            av_opt_set_int(m_swr.get(), "out_sample_rate", frame->sample_rate, 0);
            av_opt_set_sample_fmt(m_swr.get(), "in_sample_fmt", (AVSampleFormat)frame->format, 0);
            av_opt_set_sample_fmt(m_swr.get(), "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            swr_init(m_swr.get());
        }

        converted->channel_layout = frame->channel_layout;
        converted->sample_rate = frame->sample_rate;
        converted->format = AV_SAMPLE_FMT_S16;

        int err = swr_convert_frame(m_swr.get(), converted.get(), frame.get());
        if (err >= 0) // In case if resampling failed, use initial frame.
        {
            frame.swap(converted);
        }
    }
};

class CFFMPEGAudioDecoder : public CFFMPEGBase
{
public:
    CFFMPEGAudioDecoder(DECLARE_LOGGER_ARG)
        : CFFMPEGBase(GET_LOGGER_PTR)
        , m_sampleRate(-1)
    {
    }


    AVFrame* Decode(
        AVCodecID codecID,
        int channels,
        int sampleRate,
        int bitRate,
        uint8_t*& dataPtr,
        uint32_t& dataSize)
    {
        // ACR-43347: workaround for correct checking of sample format changes in HE-AAC stream,
        // because ffmpeg corrects m_context->sample_rate field after first frame is decoded
        int currSampleRate = sampleRate;
        if (m_context && m_sampleRate == currSampleRate)
        {
            currSampleRate = m_context->sample_rate;
        }

        if (!IsValidAudioContext(codecID, channels, currSampleRate, bitRate))
        {
            m_sampleRate = sampleRate; // ACR-43347

            boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
            AVCodec* codec = avcodec_find_decoder(codecID);
            lock.unlock();
            if(!codec)
            {
                _err_ << "Couldn't find decoder for codecID = " << codecID << std::endl;
                throw std::runtime_error("Couldn't find decoder");
            }

            ClearContext();

            AVCodecContext* const codecCtx = GetContext(codec);
            codecCtx->channels    = channels;
            codecCtx->sample_rate = sampleRate;
            codecCtx->bit_rate    = bitRate;

            if (sampleRate > 0)
            {
                codecCtx->bits_per_coded_sample = bitRate / sampleRate;
            }

            if( m_extradata.empty() )
            {
                codecCtx->extradata      = 0;
                codecCtx->extradata_size = 0;
            }
            else
            {
                codecCtx->extradata      = &(m_extradata[0]);
                codecCtx->extradata_size = m_extradata.size();
            }

            if(!OpenContext(codec))
            {
                _err_ << "Couldn't open context for codecID = "
                      << codecID << std::endl;
                throw std::runtime_error("Couldn't open context");
            }

            m_frame.reset(av_frame_alloc());
            if(!m_frame)
            {
                _err_ << "Couldn't alloc frame" << std::endl;
                throw std::runtime_error("Couldn't alloc frame");
            }
        }

        AVCodecContext* const codecCtx = GetContext();

        int got_frame = 0;
        uint8_t* in_data = dataPtr;
        uint32_t in_size = dataSize;
        while(in_size > 0)
        {
            got_frame = 0;
            AVPacket pkt;
            av_init_packet(&pkt);
            pkt.pts = pkt.dts = AV_NOPTS_VALUE;
            pkt.data = in_data;
            pkt.size = in_size;

            int bytes_decoded = avcodec_decode_audio4(codecCtx, m_frame.get(), &got_frame, &pkt);
            if (bytes_decoded <= 0)
            {
                char buf[AV_ERROR_MAX_STRING_SIZE + 1] = { 0 };
                av_strerror(bytes_decoded, buf, sizeof(buf) - 1);
                throw std::runtime_error(std::string("Couldn't decode audio frame data") + buf);
            }

            in_data += bytes_decoded;
            in_size -= bytes_decoded;

            if(got_frame) break;
        }

        dataPtr  = in_data;
        dataSize = in_size;

        if(!got_frame) return 0;

        if (AV_SAMPLE_FMT_S16 != (AVSampleFormat)m_frame->format)
            m_s16Resampler(m_frame);

        return m_frame.get();
    }

    int m_sampleRate;
    AVFramePtr m_frame;
    S16AudioResampler m_s16Resampler;
};

class CFFMPEGVideoEncoder : public CFFMPEGBase
{
public:
    CFFMPEGVideoEncoder(DECLARE_LOGGER_ARG, NMMSS::EVideoCodingPreset quality)
        : CFFMPEGBase(GET_LOGGER_PTR)
        , m_quality(quality)
        , m_codec(0)
    {
    }

    AVCodecContext* GetMPEG4Context(int codedWidth, int codedHeight);
    AVCodecContext* GetH264Context(int codedWidth, int codedHeight);
    AVCodecContext* GetJPEGContext(AVPixelFormat pixFmt,
                                   int codedWidth,
                                   int codedHeight);

private:
    AVCodecContext* DoGetContext(AVCodecID codecID, AVPixelFormat pixFmt,
                                 int codedWidth, int codedHeight);

    const NMMSS::EVideoCodingPreset m_quality;
    AVCodec* m_codec;
};

#ifdef _MSC_VER
#pragma warning(pop) 
#endif // _MSC_VER

}

#endif // FFMPEG__CODEC__H__
