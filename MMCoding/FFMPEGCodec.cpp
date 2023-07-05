#include "FFMPEGCodec.h"
#include "LinkFFmpeg.h"

using namespace NMMSS;


void CFFMPEGVideoDecoder::setupMultithreaded(AVCodecContext* const codecCtx)
{
    const int MAGIC_FFMPEG_THREAD_COUNT = 9;
    if (m_concurrentCount > 0)
    {
        int threadCount = (MAGIC_FFMPEG_THREAD_COUNT + m_concurrentCount - 1) / m_concurrentCount;
        codecCtx->thread_count = threadCount;
        codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }
}



AVCodecContext* CFFMPEGVideoEncoder::GetMPEG4Context(int codedWidth, int codedHeight)
{
    return DoGetContext(AV_CODEC_ID_MPEG4, AV_PIX_FMT_YUV420P, codedWidth, codedHeight);
}

AVCodecContext* CFFMPEGVideoEncoder::GetH264Context(int codedWidth, int codedHeight)
{
    return DoGetContext(AV_CODEC_ID_H264, AV_PIX_FMT_YUV420P, codedWidth, codedHeight);
}

AVCodecContext* CFFMPEGVideoEncoder::GetJPEGContext(
    AVPixelFormat pixFmt,
    int codedWidth,
    int codedHeight)
{
    return DoGetContext(AV_CODEC_ID_MJPEG, pixFmt, codedWidth, codedHeight);
}

AVCodecContext* CFFMPEGVideoEncoder::DoGetContext(
    AVCodecID codecID,
    AVPixelFormat pixFmt,
    int codedWidth,
    int codedHeight)
{
    if (IsValidVideoContext(codecID, codedWidth, codedHeight))
        return m_context.get();

    ClearContext();

    {
        boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
        m_codec = avcodec_find_encoder(codecID);
    }
    if (0 == m_codec)
    {
        _log_ << "DoGetContext(): avcodec_find_encoder failed for codecID = " << codecID << std::endl;
        return 0;
    }

    if (!GetContext(m_codec))
    {
        return 0;
    }

    if (codecID == AV_CODEC_ID_H264)
    {
        m_context->qmax = 30;
        switch (m_quality)
        {
        case NMMSS::VCP_BestQuality: m_context->qmax = 10; break;
        case NMMSS::VCP_FineQuality: m_context->qmax = 16; break;
        case NMMSS::VCP_GoodQuality: m_context->qmax = 21; break;
        case NMMSS::VCP_Normal:      m_context->qmax = 30; break;
        case NMMSS::VCP_SmallSize:   m_context->qmax = 36; av_opt_set_int(m_context->priv_data, "crf", 25, 0); break;
        case NMMSS::VCP_TinySize:    m_context->qmax = 43; av_opt_set_int(m_context->priv_data, "crf", 29, 0); break;
        case NMMSS::VCP_BestSize:    m_context->qmax = 50; av_opt_set_int(m_context->priv_data, "crf", 33, 0); break;
        }
        av_opt_set(m_context->priv_data, "preset", "ultrafast", 0);
        av_opt_set(m_context->priv_data, "profile", "baseline", 0);
        av_opt_set(m_context->priv_data, "level", "3", 0);

        // to reduce cpu consumption
        m_context->thread_count = 2;
        m_context->thread_type = FF_THREAD_SLICE;
    }
    else
    {
        m_context->qmax = 8;
        switch (m_quality)
        {
        case NMMSS::VCP_BestQuality:
        {
            m_context->qmax = 2;
            m_context->gop_size = 1; //output only I-frames
        }break;
        case NMMSS::VCP_FineQuality: m_context->qmax = 2; break;
        case NMMSS::VCP_GoodQuality: m_context->qmax = 4; break;
        case NMMSS::VCP_Normal:      m_context->qmax = 8; break;
        case NMMSS::VCP_SmallSize:   m_context->qmax = 16; break;
        case NMMSS::VCP_TinySize:    m_context->qmax = 21; break;
        case NMMSS::VCP_BestSize:    m_context->qmax = 32; break;
        }
    }
    m_context->codec_id = codecID;
    m_context->codec_type = AVMEDIA_TYPE_VIDEO;
    m_context->time_base.den = 50;
    m_context->time_base.num = 1;
    m_context->pix_fmt = pixFmt;
    m_context->coded_width = codedWidth;
    m_context->coded_height = codedHeight;

    if (!OpenContext(m_codec))
    {
        return 0;
    }

    return m_context.get();
}

namespace NMMSS {

struct AVException::Impl {
    int m_avErrorCode;
    std::string m_errorText;
};

AVException::AVException(int avError, const char* method) noexcept
    : m_impl(new Impl())
{
    m_impl->m_avErrorCode = avError;

    if (method)
    {
        m_impl->m_errorText.assign(method);
        m_impl->m_errorText += ": ";
    }

    char errBuf[AV_ERROR_MAX_STRING_SIZE + 1]{0};

    if (av_strerror(m_impl->m_avErrorCode, errBuf, sizeof(errBuf)) < 0)
    {
        m_impl->m_errorText += "unknown libav error, code = ";
        m_impl->m_errorText += std::to_string(m_impl->m_avErrorCode);
    }
    else {
        m_impl->m_errorText += &errBuf[0];
    }
}

AVException::~AVException() {
    delete m_impl;
    m_impl = nullptr;
}

int AVException::avErrorCode() const noexcept
{
    return m_impl->m_avErrorCode;
}

const char* AVException::what() const noexcept
{
    return m_impl->m_errorText.c_str();
}

} // namespace NMMSS

