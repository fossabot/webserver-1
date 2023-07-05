#include "../MediaType.h"

extern "C"
{
#include <libavcodec/avcodec.h>
}

#include "../FilterImpl.h"
#include "../FilterChainImpl.h"
#include "../PtimeFromQword.h"
#include "Transforms.h"
#include "FFmpegMutex.h"
#include "FFMPEGCodec.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // warning C4244: '=' : conversion from '__int64' to 'int', possible loss of data
#endif // _MSC_VER

namespace
{


class CAudioResampler
{
public:
    CAudioResampler(DECLARE_LOGGER_ARG,
                    unsigned int nSampleRate = 8000,
                    NMMSS::NMediaType::Audio::ESampleType
                    nType = NMMSS::NMediaType::Audio::ST_UINT8,
                    unsigned int nChannelsCount = 1)
        : m_result(NMMSS::EIGNORED)
        , m_outSampleRate(nSampleRate), m_inSampleRate(nSampleRate)
        , m_outType(nType), m_inType(nType)
        , m_outChannelsCount(nChannelsCount), m_inChannelsCount(nChannelsCount)
        , m_inBodySize(0)
    {
        INIT_LOGGER_HOLDER;
    }

    ~CAudioResampler()
    {
        m_pSwrCtx.reset();
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        m_inBodySize = in->Header().nBodySize;
        m_holder = &holder;
        NMMSS::NMediaType::ApplyMediaTypeVisitor(in, *this);

        if (m_result == NMMSS::ETRANSFORMED)
        {
            holder->Header().dtTimeBegin = in->Header().dtTimeBegin;
            holder->Header().dtTimeEnd = in->Header().dtTimeEnd;
        }

        return m_result;
    }

    template<typename TMediaTypeHeader>
    void operator()(TMediaTypeHeader* pHeader, uint8_t* pData)
    {
        m_result = NMMSS::EIGNORED;
    }

    void operator()(NMMSS::NMediaType::Audio::PCM::SubtypeHeader* pHeader, uint8_t* pData)
    {
        uint8_t **l_pIn = &pData;

        if ((pHeader->nSampleRate == m_outSampleRate) &&
            (pHeader->nSampleType == m_outType) &&
            (pHeader->nChannelsCount == m_outChannelsCount))
        {
            m_result = NMMSS::ETHROUGH;
            return;
        }

        if( !checkResampleContext(pHeader) )
        {
            _log_ << "Audio resample context check failed!!!" << std::endl;
            m_result = NMMSS::EIGNORED;
            return;
        }

        //DSPP(ACR-53572) preventing dividing by zero (probably should be rework)
        int nChannelsCount = pHeader->nChannelsCount;
        int typeSize = NMMSS::NMediaType::Audio::GetTypeSize(pHeader->nSampleType);
        if (!typeSize || nChannelsCount == 0)
        {
            m_result = NMMSS::EIGNORED;
            return;
        }

        int in_samples_count = m_inBodySize / (nChannelsCount * typeSize);

        unsigned int maxBodySize = std::max(2*in_samples_count*m_outChannelsCount*
                                   NMMSS::NMediaType::Audio::GetTypeSize(m_outType)*
                                  (m_outSampleRate / m_inSampleRate + 1),
                                  (unsigned) NMMSS::MY_AVCODEC_MAX_AUDIO_FRAME_SIZE);

        int l_iOutSamples = av_rescale_rnd(
            swr_get_delay(m_pSwrCtx.get(), m_inSampleRate) + in_samples_count,
            m_outSampleRate,
            m_inSampleRate,
            AV_ROUND_UP);

        if (!m_holder->Alloc(maxBodySize))
        {
            _err_ << "Audio processing memory allocation failed" << std::endl;
            m_result = NMMSS::EIGNORED;
            return;
        }

        NMMSS::ISample* pOutSample = m_holder->GetSample().Get();
        uint8_t *pBody = pOutSample->GetBody();

        int out_samples_count = swr_convert(m_pSwrCtx.get(),
            (uint8_t **)&pBody,
            l_iOutSamples, 
            (const uint8_t **)l_pIn,
            in_samples_count);

        if (out_samples_count <= 0)
        {
            _err_ << "Audio resample failed" << std::endl;
            m_result = NMMSS::EIGNORED;
            return;
        }

        NMMSS::NMediaType::Audio::PCM::SubtypeHeader *subheader = 0;
        NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::PCM>(
            pOutSample->GetHeader(), &subheader);

        subheader->nChannelsCount = m_outChannelsCount;
        subheader->nSampleRate    = m_outSampleRate;
        subheader->nSampleType    = m_outType;

        pOutSample->Header().nBodySize = out_samples_count * m_outChannelsCount *
            NMMSS::NMediaType::Audio::GetTypeSize(m_outType);

        if (pOutSample->Header().nBodySize > maxBodySize)
        {
            _err_ << "INVALID SIZE OF RESAMPLE BUFFER: " << pOutSample->Header().nBodySize <<
                "  maxBodySize = " << maxBodySize << std::endl;
        }

        m_result = NMMSS::ETRANSFORMED;
    }

    static AVSampleFormat getFFmpegType(NMMSS::NMediaType::Audio::ESampleType type)
    {
        switch(type)
        {
        case NMMSS::NMediaType::Audio::ST_UINT8:    return AV_SAMPLE_FMT_U8;
        case NMMSS::NMediaType::Audio::ST_INT16:    return AV_SAMPLE_FMT_S16;
        case NMMSS::NMediaType::Audio::ST_INT32:    return AV_SAMPLE_FMT_S32;
        case NMMSS::NMediaType::Audio::ST_FLOAT32:  return AV_SAMPLE_FMT_FLT;

        default: return AV_SAMPLE_FMT_NONE;
        }
    }

protected:
    bool checkResampleContext(const NMMSS::NMediaType::Audio::PCM::SubtypeHeader* pHeader)
    {
        if (!m_pSwrCtx || 
            (pHeader->nSampleRate != m_inSampleRate) ||
            (pHeader->nSampleType != m_inType) ||
            (pHeader->nChannelsCount != m_inChannelsCount))
        {

            m_inSampleRate = pHeader->nSampleRate;
            m_inType = pHeader->nSampleType;
            m_inChannelsCount = pHeader->nChannelsCount;

            m_pSwrCtx.reset(swr_alloc());
            if (!m_pSwrCtx)
            {
                return false;
            }

            int64_t in_ch_layout = AV_CH_LAYOUT_NATIVE;
            if (m_inChannelsCount == 1)
            {
                in_ch_layout = AV_CH_LAYOUT_MONO;
            }
            else if (m_inChannelsCount == 2)
            {
                in_ch_layout = AV_CH_LAYOUT_STEREO;
            }

            int64_t out_ch_layout = AV_CH_LAYOUT_NATIVE;
            if (m_outChannelsCount == 1)
            {
                out_ch_layout = AV_CH_LAYOUT_MONO;
            }
            else if (m_outChannelsCount == 2)
            {
                out_ch_layout = AV_CH_LAYOUT_STEREO;
            }

            av_opt_set_int(m_pSwrCtx.get(), "in_channel_layout", in_ch_layout, 0);
            av_opt_set_int(m_pSwrCtx.get(), "in_sample_fmt", getFFmpegType(m_inType), 0);
            av_opt_set_int(m_pSwrCtx.get(), "in_sample_rate", m_inSampleRate, 0);
            av_opt_set_int(m_pSwrCtx.get(), "out_channel_layout", out_ch_layout, 0);
            av_opt_set_int(m_pSwrCtx.get(), "out_sample_fmt", getFFmpegType(m_outType), 0);
            av_opt_set_int(m_pSwrCtx.get(), "out_sample_rate", m_outSampleRate, 0);


            if (swr_init(m_pSwrCtx.get()) < 0)
            {
                m_pSwrCtx.reset();
                return false;
            }
        }
        return true;
    }

private:
    DECLARE_LOGGER_HOLDER;
    NMMSS::ETransformResult m_result;
    NMMSS::SwrContextPtr m_pSwrCtx;
    unsigned int m_outSampleRate, m_inSampleRate;
    NMMSS::NMediaType::Audio::ESampleType m_outType, m_inType;
    unsigned int m_outChannelsCount, m_inChannelsCount;
    uint32_t m_inBodySize;
    NMMSS::CDeferredAllocSampleHolder* m_holder;
};

/// Used for holding sample extra data and checking if it is new.
class ExtraDataChecker
{
public:
    /// Returns true if sample extra data was changed.
    bool Process(const uint8_t* data, size_t bodySize, size_t extraDataSize)
    {
        bool result = false;
        const size_t extraDataOffset = bodySize - extraDataSize;
        const uint8_t* extraData = data + extraDataOffset;
        
        if (m_lastExtraData.size() != extraDataSize || std::memcmp(extraData, m_lastExtraData.data(), extraDataSize))
        {
            result = true;
        }
        
        m_lastExtraData.clear();
        std::copy(extraData, extraData + extraDataSize, std::back_inserter(m_lastExtraData));

        return result;
    }

    const uint8_t* GetLastExtraData() const
    {
        return m_lastExtraData.data();
    }

    size_t GetLastExtraDataSize() const
    {
        return m_lastExtraData.size();
    }

private:
    std::vector<uint8_t> m_lastExtraData;
};

class CAudioDecoder
{
public:
    CAudioDecoder(DECLARE_LOGGER_ARG) :
        m_lastType(0)
    {
        INIT_LOGGER_HOLDER;
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        m_holder = &holder;
        m_sampleHeader = &(in->Header());

        if(m_sampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFDiscontinuity)
        {
            //порождаем сбрасывание контекста декодера
            m_lastType = 0;
        }

        try
        {
            NMMSS::NMediaType::ApplyMediaTypeVisitor(in, *this);
        }
        catch(std::exception& e)
        {
            _log_ << e.what() << std::endl;
            return NMMSS::EFAILED;
        }

        return m_result;
    }

    template<typename TMediaTypeHeader>
    void operator()(TMediaTypeHeader* pHeader, uint8_t* pData)
    {
        m_result = NMMSS::ETHROUGH;
    }

    void operator()(NMMSS::NMediaType::Audio::AAC::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::AAC::ID;

        if (m_extraDataChecker.Process(dataPtr, m_sampleHeader->nBodySize, subtypeHeader->nExtraDataSize) ||
            !m_codecFFMPEG.get() || m_lastType != type)
        {
            initialize(type);

            if (m_extraDataChecker.GetLastExtraDataSize())
            {
                m_codecFFMPEG->SetExtradata(m_extraDataChecker.GetLastExtraData(), m_extraDataChecker.GetLastExtraDataSize());
            }
        }

        DecodeWithFFMPEG(AV_CODEC_ID_AAC,
                         subtypeHeader->nChannelsCount,
                         subtypeHeader->nSampleRate,
                         0, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Audio::G711::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::G711::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        switch(subtypeHeader->eCodingLaw)
        {
        case NMMSS::NMediaType::Audio::G711::U_LAW:
            DecodeWithFFMPEG(AV_CODEC_ID_PCM_MULAW,
                             subtypeHeader->nChannelsCount,
                             subtypeHeader->nSampleRate,
                             0, dataPtr);
            break;

        case NMMSS::NMediaType::Audio::G711::A_LAW:
            DecodeWithFFMPEG(AV_CODEC_ID_PCM_ALAW,
                             subtypeHeader->nChannelsCount,
                             subtypeHeader->nSampleRate,
                             0, dataPtr);
            break;

        default: m_result = NMMSS::ETHROUGH;
        }
    }

    void operator()(NMMSS::NMediaType::Audio::G726::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::G726::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        DecodeWithFFMPEG(AV_CODEC_ID_ADPCM_G726,
                         subtypeHeader->nChannelsCount,
                         subtypeHeader->nSampleRate,
                         subtypeHeader->nBitRate,
                         dataPtr);
    }

    void operator()(NMMSS::NMediaType::Audio::IMA_WAV::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::IMA_WAV::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        DecodeWithFFMPEG(AV_CODEC_ID_ADPCM_IMA_WAV,
                         subtypeHeader->nChannelsCount,
                         subtypeHeader->nSampleRate,
                         0, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Audio::MP1::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::MP1::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        DecodeWithFFMPEG(AV_CODEC_ID_MP1, 
                         subtypeHeader->nChannelsCount,
                         subtypeHeader->nSampleRate,
                         0, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Audio::MP2::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::MP2::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        DecodeWithFFMPEG(AV_CODEC_ID_MP2,
                         subtypeHeader->nChannelsCount,
                         subtypeHeader->nSampleRate,
                         0, dataPtr);

    }

    void operator()(NMMSS::NMediaType::Audio::MP3::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::MP3::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        DecodeWithFFMPEG(AV_CODEC_ID_MP3,
                         subtypeHeader->nChannelsCount,
                         subtypeHeader->nSampleRate,
                         0, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Audio::VORBIS::SubtypeHeader* subtypeHeader,
                    uint8_t* dataPtr)
    {
        const ::uint32_t type = NMMSS::NMediaType::Audio::VORBIS::ID;
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            initialize(type);
        }

        DecodeWithFFMPEG(AV_CODEC_ID_VORBIS, 0, 0, 0, dataPtr);
    }

private:
    void initialize(uint32_t type)
    {
        m_codecFFMPEG.reset(new NMMSS::CFFMPEGAudioDecoder(GET_LOGGER_PTR));
        m_lastType = type;
    }

    void DecodeWithFFMPEG(
        AVCodecID codecID,
        int channels,
        int sampleRate,
        int bitRate,
        uint8_t* dataPtr)
    {
        uint32_t dataSize = m_sampleHeader->nBodySize - m_extraDataChecker.GetLastExtraDataSize();

        if(m_sampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFInitData)
        {
            m_codecFFMPEG->SetExtradata(dataPtr, dataSize);
            m_result = NMMSS::ETRANSFORMED;
            return;
        }

        try
        {
            while(dataSize > 0)
            {
                AVFrame *frame =
                    m_codecFFMPEG->Decode(
                        codecID,
                        channels,
                        sampleRate,
                        bitRate,
                        dataPtr, dataSize);

                if (frame && NMMSS::getSampleTypeFromAVFormat((AVSampleFormat)frame->format))
                {
                    NMMSS::NMediaType::Audio::ESampleType sType = 
                        static_cast<NMMSS::NMediaType::Audio::ESampleType>(NMMSS::getSampleTypeFromAVFormat((AVSampleFormat)frame->format));
                    int bodySize = NMMSS::NMediaType::Audio::GetTypeSize(sType) * frame->nb_samples * frame->channels;
                    NMMSS::PSample sample(m_holder->GetAllocator()->Alloc(bodySize));
                    sample->Header().nBodySize = bodySize;
                    memcpy(sample->GetBody(), frame->extended_data[0], bodySize);

                    NMMSS::NMediaType::Audio::PCM::SubtypeHeader *subheader = 0;
                    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::PCM>(sample->GetHeader(), &subheader);

                    subheader->nChannelsCount = frame->channels;
                    subheader->nSampleType = sType;
                    subheader->nSampleRate = frame->sample_rate;

                    uint64_t dtTimeBegin = m_sampleHeader->dtTimeBegin;
                    uint64_t dtTimeEnd = m_sampleHeader->dtTimeEnd;

                    if (frame->nb_samples)
                    {
                        boost::posix_time::time_duration duration = boost::posix_time::milliseconds(
                            static_cast<int64_t>((frame->nb_samples * 1000.0) / frame->sample_rate));

                        boost::posix_time::ptime posixTimeBegin = NMMSS::PtimeFromQword(dtTimeBegin);
                        boost::posix_time::ptime posixTimeEnd = NMMSS::PtimeFromQword(dtTimeEnd) + duration;

                        dtTimeBegin = NMMSS::PtimeToQword(posixTimeBegin);
                        dtTimeEnd = NMMSS::PtimeToQword(posixTimeEnd);
                    }

                    sample->Header().dtTimeBegin = dtTimeBegin;
                    sample->Header().dtTimeEnd   = dtTimeEnd;

                    m_holder->AddSample(sample);
                }
            }

            m_result = NMMSS::ETRANSFORMED;
        }
        catch(std::exception& e)
        {
            _err_ << "FFMPEG decoder get error: " << e.what() << std::endl;
            m_result = NMMSS::EFAILED;
        }
    }

private:
    DECLARE_LOGGER_HOLDER;

    NMMSS::CDeferredAllocSampleHolder* m_holder;
    const NMMSS::SMediaSampleHeader* m_sampleHeader;
    NMMSS::ETransformResult m_result;
    uint32_t m_lastType;

    std::auto_ptr<NMMSS::CFFMPEGAudioDecoder> m_codecFFMPEG;
    ExtraDataChecker m_extraDataChecker;
};

template<const char encoderName[]>
class CAudioEncoder_G7XX
{
public:
    CAudioEncoder_G7XX(DECLARE_LOGGER_ARG, int bitrate = 64000) :
        m_audioResampler(GET_LOGGER_PTR, 8000, NMMSS::NMediaType::Audio::ST_INT16, 1)
    {
        INIT_LOGGER_HOLDER;

        boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
        AVCodec* pCodec = avcodec_find_encoder_by_name(encoderName);

        if(!pCodec)
        {
            _err_ << __FUNCTION__ << ". Couldn't find " << encoderName << " audio encoder" << std::endl;
            throw std::runtime_error("Couldn't find audio encoder");
        }

        m_pFFmpegContext = NMMSS::CFFMPEGBase::AllocContext(pCodec);

        if (!m_pFFmpegContext)
        {
            _err_ << __FUNCTION__ << ". Could not allocate audio codec context";
            throw std::runtime_error("Could not allocate audio codec context");
        }

        //инициируем контекст кодера
        m_pFFmpegContext->channels    = 1;
        m_pFFmpegContext->sample_rate = 8000;
        m_pFFmpegContext->time_base   = {1,m_pFFmpegContext->sample_rate};
        m_pFFmpegContext->sample_fmt  = AV_SAMPLE_FMT_S16;
        m_pFFmpegContext->bit_rate    = bitrate;
        m_pFFmpegContext->channel_layout = av_get_default_channel_layout(m_pFFmpegContext->channels);

        if(avcodec_open2(m_pFFmpegContext.get(), pCodec, 0) < 0)
        {
            _err_ << __FUNCTION__ << ". Couldn't open encoding context" << std::endl;
            throw std::runtime_error("Couldn't open audio encoding context");
        }
    }


    ~CAudioEncoder_G7XX()
    {
    }


    NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        //преобразуем входной звук к формату: 8kHz, mono, s16
        NMMSS::CDeferredAllocSampleHolder resamplerHolder(holder);
        NMMSS::ETransformResult resampleResult =
            m_audioResampler.operator()(in, resamplerHolder);

        NMMSS::PSample rawSample;

        if (NMMSS::ETRANSFORMED == resampleResult)
        {
            rawSample = resamplerHolder.GetSample();
        }
        else if (NMMSS::ETHROUGH == resampleResult)
        {
            rawSample = NMMSS::PSample(in, NCorbaHelpers::ShareOwnership());
        }
        else
        {
            return NMMSS::ETHROUGH;
        }

        //выделяем место для кодированного фрейма
        if (!holder.Alloc(rawSample->Header().nBodySize))
        {
            _log_ << "Audio processing memory allocation failed" << std::endl;
            return NMMSS::EFAILED;
        }

        AVPacket pkt = {0};
        NMMSS::AVFramePtr frame;

        av_init_packet(&pkt);
        pkt.data = holder->GetBody();
        pkt.size = rawSample->Header().nBodySize / sizeof(int16_t);
        AVCodecContext* avctx = m_pFFmpegContext.get();

        const int16_t * samples = (int16_t*)rawSample->GetBody();
        if (samples)
        {
            frame.reset(av_frame_alloc());
            if (!frame)
            {
                _err_ << "Error in audio encoding. Insufficient memory." << std::endl;
                return NMMSS::EIGNORED;
            }

            if (avctx->frame_size)
            {
                frame->nb_samples = avctx->frame_size;
            }
            else
            {
                // Calculate number of samples from buffer size.
                int64_t nb_samples;
                if (!av_get_bits_per_sample(avctx->codec_id))
                {
                    _err_ << "Error in audio encoding. Unsupported codec " << avctx->codec_id << std::endl;
                    return NMMSS::EIGNORED;
                }
                nb_samples = (int64_t)pkt.size * 8 / (av_get_bits_per_sample(avctx->codec_id) * avctx->channels);
                if (nb_samples >= INT_MAX)
                {
                    _err_ << "Error in audio encoding. Incorrect samples size." << std::endl;
                    return NMMSS::EIGNORED;
                }
                frame->nb_samples = (int)nb_samples;
            }

            int samples_size = av_samples_get_buffer_size(NULL, avctx->channels, frame->nb_samples, avctx->sample_fmt, 1);
            if (avcodec_fill_audio_frame(frame.get(), avctx->channels, avctx->sample_fmt, (const uint8_t*)samples, samples_size, 1) < 0)
            {
                _err_ << "Error in audio encoding. Frame fill failed." << std::endl;
                return NMMSS::EIGNORED;
            }
        }

        int got_packet = 0;
        int ret = avcodec_encode_audio2(avctx, &pkt, frame.get(), &got_packet);
        if (!ret && got_packet && avctx->coded_frame)
        {
            avctx->coded_frame->pts = pkt.pts;
            avctx->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
        }

        av_packet_free_side_data(&pkt);
        if (frame && frame->extended_data != frame->data)
        {
            av_freep(&frame->extended_data);
        }

        if (pkt.size <= 0)
        {
            _err_ << "Error in audio encoding" << std::endl;
            return NMMSS::EIGNORED;
        }

        this->CreateHeader(holder.operator->());

        holder->Header().nBodySize = pkt.size;
        holder->Header().eFlags    = 0;

        holder->Header().dtTimeBegin = in->Header().dtTimeBegin;
        holder->Header().dtTimeEnd   = in->Header().dtTimeEnd;

        return NMMSS::ETRANSFORMED;
    }

protected:
    void CreateHeader(NMMSS::ISample* pSample);

private:
    DECLARE_LOGGER_HOLDER;
    boost::shared_ptr<AVCodecContext> m_pFFmpegContext;
    CAudioResampler m_audioResampler;
};



extern const char ENCODER_NAME_G711_A[]      = "pcm_alaw";
template<>
void CAudioEncoder_G7XX<ENCODER_NAME_G711_A>::CreateHeader(
    NMMSS::ISample* pSample)
{
    NMMSS::NMediaType::Audio::G711::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::G711>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->eCodingLaw     = NMMSS::NMediaType::Audio::G711::A_LAW;
    pOutHeader->nChannelsCount = 1;
    pOutHeader->nSampleRate    = 8000;
}

extern const char ENCODER_NAME_G711_U[]      = "pcm_mulaw";
template<>
void CAudioEncoder_G7XX<ENCODER_NAME_G711_U>::CreateHeader(
    NMMSS::ISample* pSample)
{
    NMMSS::NMediaType::Audio::G711::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::G711>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->eCodingLaw     = NMMSS::NMediaType::Audio::G711::U_LAW;
    pOutHeader->nChannelsCount = 1;
    pOutHeader->nSampleRate    = 8000;
}

template<const char encoderName[]>
class CAudioEncoder : public NMMSS::CPullFilterImpl<CAudioEncoder<encoderName> >
{
public:
    CAudioEncoder(DECLARE_LOGGER_ARG)
        : NMMSS::CPullFilterImpl< CAudioEncoder<encoderName> >(
            GET_LOGGER_PTR,
            NMMSS::SAllocatorRequirements(8, AV_INPUT_BUFFER_MIN_SIZE, 16),
            NMMSS::SAllocatorRequirements(0, 0, 16),
            this)
        , m_compression(0)
        , m_bitrate(0)
    {
        INIT_LOGGER_HOLDER;

        boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());

        m_pCodec = avcodec_find_encoder_by_name(encoderName);

        if(!m_pCodec)
        {
            _err_ << __FUNCTION__ << ". Couldn't find " << encoderName << " audio encoder" << std::endl;
            throw std::runtime_error("Couldn't find audio encoder");
        }

        m_pFFmpegContext = NMMSS::CFFMPEGBase::AllocContext(m_pCodec);

        if (!m_pFFmpegContext)
        {
            _err_ << __FUNCTION__ << ". Could not allocate audio codec context";
            throw std::runtime_error("Could not allocate audio codec context");
        }

        m_frame = av_frame_alloc();
        if (!m_frame)
        {
            _err_ << __FUNCTION__ << ". Could not allocate audio frame" << std::endl;
            throw std::runtime_error("Could not allocate audio frame");
        }
    }

    ~CAudioEncoder()
    {
        av_frame_free(&m_frame);
        this->NMMSS::CPullFilterImpl< CAudioEncoder<encoderName> >::m_transform.release();
    }


    void SetCompression(int compression)
    {
        m_compression = compression;
    }

    void SetBitrate(int bitrate)
    {
        m_bitrate = bitrate;
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* s, NMMSS::CDeferredAllocSampleHolder& holder)
    {

        if( !s || !(s->GetHeader()) ||
            !NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Audio::PCM>(&s->Header()) )
        {
            return NMMSS::ETHROUGH;
        }

        const NMMSS::NMediaType::Audio::PCM::SubtypeHeader *subheader =
            reinterpret_cast<const NMMSS::NMediaType::Audio::PCM::SubtypeHeader*>(
                s->GetHeader() + sizeof(NMMSS::SMediaSampleHeader));

        if(NMMSS::NMediaType::Audio::ST_INT16 != subheader->nSampleType)
        {
            _log_ << __FUNCTION__ << ". Unsupported sample type (not INT16): " << NMMSS::NMediaType::Audio::ST_INT16  << std::endl;
            return NMMSS::EIGNORED;
        }

        return doTransform(s, holder);
    }


    NMMSS::ETransformResult doTransform(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        const NMMSS::NMediaType::Audio::PCM::SubtypeHeader *pInHeader =
            reinterpret_cast<const NMMSS::NMediaType::Audio::PCM::SubtypeHeader*>(in->GetHeader() + sizeof(NMMSS::SMediaSampleHeader));

        if(pInHeader->nSampleType != NMMSS::NMediaType::Audio::ST_INT16)
        {
            _wrn_ << __FUNCTION__ << ". Unsupported sample type" << std::endl;
            return NMMSS::EFAILED;
        }

        if((pInHeader->nChannelsCount != m_pFFmpegContext->channels) ||
            (pInHeader->nSampleRate != (uint32_t)m_pFFmpegContext->sample_rate))
        {
            if (m_bitrate)
                m_pFFmpegContext->bit_rate   = m_bitrate;
            m_pFFmpegContext->sample_rate    = pInHeader->nSampleRate;
            m_pFFmpegContext->channels       = pInHeader->nChannelsCount;
            m_pFFmpegContext->channel_layout = av_get_default_channel_layout(m_pFFmpegContext->channels);
            m_pFFmpegContext->sample_fmt     = AV_SAMPLE_FMT_S16;
            m_pFFmpegContext->cutoff         = std::min(std::max(pInHeader->nSampleRate / (2 << m_compression), 188u), 20000u);
            m_pFFmpegContext->time_base      = {1, m_pFFmpegContext->sample_rate};

            boost::mutex::scoped_lock lock(NMMSS::CFFmpegMutex::Get());
            int ret;
            if((ret = avcodec_open2(m_pFFmpegContext.get(), m_pCodec, 0)) < 0)
            {
                _err_ << __FUNCTION__ << ". Couldn't open encoding context" << std::endl;
                return NMMSS::EFAILED;
            }

            //Данные для декодера
            boost::posix_time::ptime time = NMMSS::PtimeFromQword(in->Header().dtTimeBegin) - boost::posix_time::millisec(10);
            AddInitSample(time, holder);

            m_frame->nb_samples     = m_pFFmpegContext->frame_size;
            m_frame->format         = m_pFFmpegContext->sample_fmt;
            m_frame->channel_layout = m_pFFmpegContext->channel_layout;

            m_frameBufferSize = av_samples_get_buffer_size(
                NULL,
                m_pFFmpegContext->channels,
                m_pFFmpegContext->frame_size,
                m_pFFmpegContext->sample_fmt,
                0
                );
        }

        if(!m_pFFmpegContext->codec)
            return NMMSS::EFAILED;

        if (in->Header().nBodySize)
            m_cacheBuffer.insert(m_cacheBuffer.end(), in->GetBody(), in->GetBody() + in->Header().nBodySize);

        uint64_t timestamp = in->Header().dtTimeBegin;
        AVPacket pkt;
        int got_output, ret;

        while (m_cacheBuffer.size() >= m_frameBufferSize)
        {
            av_init_packet(&pkt);
            pkt.data = NULL;
            pkt.size = 0;
            
            ret = avcodec_fill_audio_frame(
                m_frame,
                m_pFFmpegContext->channels,
                m_pFFmpegContext->sample_fmt,
                (const uint8_t*)&m_cacheBuffer[0],
                m_frameBufferSize,
                0
                );

            if (ret < 0)
            {
                char errbuf[256];
                _err_ << __FUNCTION__  << ". Couldn't encode audio sample: " << av_strerror(ret, errbuf, 256) << std::endl;
                return NMMSS::EFAILED;
            }

            ret = avcodec_encode_audio2(m_pFFmpegContext.get(), &pkt, m_frame, &got_output);
            if (ret < 0 || !got_output)
            {
                _err_ << __FUNCTION__ << ". Couldn't encode audio sample" << std::endl;
                return NMMSS::EFAILED;
            }

            NMMSS::PSample sample(holder.GetAllocator()->Alloc(pkt.size));
            this->CreateHeader(sample.Get());
            sample->Header().nBodySize = pkt.size;
            sample->Header().dtTimeBegin = timestamp;
            sample->Header().dtTimeEnd = NMMSS::PtimeToQword(NMMSS::PtimeFromQword(timestamp) + boost::posix_time::millisec(10));
            timestamp = sample->Header().dtTimeEnd;

            memcpy(sample->GetBody(), pkt.data, pkt.size);
            m_cacheBuffer.erase(m_cacheBuffer.begin(), m_cacheBuffer.begin() + m_frameBufferSize);

            holder.AddSample(sample);
            av_free_packet(&pkt);
        }

        if (holder.GetSamples().empty())
            return NMMSS::EIGNORED;

        return NMMSS::ETRANSFORMED;
    }

protected:

    void CreateHeader(NMMSS::ISample*);

    void AddInitSample(boost::posix_time::ptime& time, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        uint32_t extradata_size = this->CalcExtradata(0);
        if(0 == extradata_size) return;

        NMMSS::PAllocator pAllocator(this->m_sourceImpl.GetAllocator());
        if (!pAllocator)
        {
            _err_ << __FUNCTION__ << ". Receive NULL allocator" << std::endl;
            throw std::runtime_error("Receive NULL allocator");
        }
        NMMSS::PSample pSample(pAllocator->Alloc(extradata_size));
        if(!pSample)
        {
            _err_ << __FUNCTION__ << ". Couldn't alloc extradata sample" << std::endl;
            throw std::runtime_error("Couldn't alloc extradata sample");
        }

        pSample->Header().nBodySize = this->CalcExtradata(pSample.Get());

        pSample->Header().eFlags = NMMSS::SMediaSampleHeader::EFInitData;
        pSample->Header().dtTimeBegin = NMMSS::PtimeToQword(time);
        pSample->Header().dtTimeEnd = NMMSS::PtimeToQword(time + boost::posix_time::millisec(10));

        holder.AddSample(pSample);
    }

    uint32_t CalcExtradata(NMMSS::ISample*)
    {
        return 0;
    }

    DECLARE_LOGGER_HOLDER;

private:
    
    boost::shared_ptr<AVCodecContext>   m_pFFmpegContext;
    AVCodec*                            m_pCodec;
    AVFrame*                            m_frame;
    std::vector<uint8_t>                m_cacheBuffer;
    uint32_t                            m_frameBufferSize;
    int                                 m_compression;
    int                                 m_bitrate;
};


extern const char ENCODER_NAME_MP2[] = "mp2";
template<>
void CAudioEncoder<ENCODER_NAME_MP2>::CreateHeader(NMMSS::ISample* pSample)
{
    NMMSS::NMediaType::Audio::MP2::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::MP2>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;
    pSample->Header().eFlags    =  NMMSS::SMediaSampleHeader::EFNeedInitData;
}

template<>
uint32_t CAudioEncoder<ENCODER_NAME_MP2>::CalcExtradata(NMMSS::ISample* pSample)
{
    if(!pSample) return m_pFFmpegContext->extradata_size;

    NMMSS::NMediaType::Audio::MP2::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::MP2>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;

    memcpy(pSample->GetBody(), m_pFFmpegContext->extradata,
           m_pFFmpegContext->extradata_size);

    return m_pFFmpegContext->extradata_size;
}



extern const char ENCODER_NAME_AAC[]    = "libfdk_aac";
template<>
void CAudioEncoder<ENCODER_NAME_AAC>::CreateHeader(NMMSS::ISample* pSample)
{

    NMMSS::NMediaType::Audio::AAC::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::AAC>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;
    pOutHeader->nExtraDataSize  =  0;
    pSample->Header().eFlags    =  NMMSS::SMediaSampleHeader::EFNeedInitData;
}

template<>
uint32_t CAudioEncoder<ENCODER_NAME_AAC>::CalcExtradata(
    NMMSS::ISample* pSample)
{
    uint32_t extradata_size = NMMSS::NMediaType::Audio::AAC::CalcExtradata(
            0,
            m_pFFmpegContext->sample_rate,
            m_pFFmpegContext->channels );

    if(!pSample) return extradata_size;

    NMMSS::NMediaType::Audio::AAC::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::AAC>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;
    pOutHeader->nExtraDataSize  =  0;

    NMMSS::NMediaType::Audio::AAC::CalcExtradata(
        pSample->GetBody(),
        m_pFFmpegContext->sample_rate,
        m_pFFmpegContext->channels );

    return extradata_size;
}


extern const char ENCODER_NAME_VORBIS[] = "libvorbis";
template<>
void CAudioEncoder<ENCODER_NAME_VORBIS>::CreateHeader(NMMSS::ISample* pSample)
{
    NMMSS::NMediaType::Audio::VORBIS::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::VORBIS>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;
    pSample->Header().eFlags = NMMSS::SMediaSampleHeader::EFNeedInitData;
}

template<>
uint32_t CAudioEncoder<ENCODER_NAME_VORBIS>::CalcExtradata(
    NMMSS::ISample* pSample)
{
    if(!pSample) return m_pFFmpegContext->extradata_size;

    NMMSS::NMediaType::Audio::VORBIS::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::VORBIS>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;


    memcpy(pSample->GetBody(), m_pFFmpegContext->extradata,
           m_pFFmpegContext->extradata_size);

    return m_pFFmpegContext->extradata_size;
}

extern const char ENCODER_NAME_G726[] = "g726";
template<>
void CAudioEncoder<ENCODER_NAME_G726>::CreateHeader(NMMSS::ISample* pSample)
{
    NMMSS::NMediaType::Audio::G726::SubtypeHeader* pOutHeader = 0;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Audio::G726>(
        pSample->GetHeader(), &pOutHeader);

    pOutHeader->nChannelsCount  =  m_pFFmpegContext->channels;
    pOutHeader->nSampleRate     =  m_pFFmpegContext->sample_rate;
    pOutHeader->nBitRate        =  m_pFFmpegContext->bit_rate;
    pSample->Header().eFlags = 0;
}



}


namespace NMMSS
{
    IFilter* CreateAudioResamplerPullFilter(
        DECLARE_LOGGER_ARG,
    unsigned int nSampleRate,
        unsigned int nChannelsCount,
        NMMSS::NMediaType::Audio::ESampleType nType)
    {
        return new CPullFilterImpl<CAudioResampler, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(4, NMMSS::MY_AVCODEC_MAX_AUDIO_FRAME_SIZE, 16),
            SAllocatorRequirements(0, 0, 16),
            new CAudioResampler(GET_LOGGER_PTR, nSampleRate,
            nType, nChannelsCount));
    }

    IFilter* CreateAudioDecoderPullFilter(DECLARE_LOGGER_ARG)
    {
        return new CPullFilterImpl<CAudioDecoder, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(4, NMMSS::MY_AVCODEC_MAX_AUDIO_FRAME_SIZE, 16),
            SAllocatorRequirements(0, 0, 16),
            new CAudioDecoder(GET_LOGGER_PTR));
    }



    IFilter* CreateAudioEncoderFilter_G711_A(DECLARE_LOGGER_ARG)
    {
        return new CPullFilterImpl<CAudioEncoder_G7XX<ENCODER_NAME_G711_A>, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(4, NMMSS::MY_AVCODEC_MAX_AUDIO_FRAME_SIZE, 16),
            SAllocatorRequirements(0, 0, 16),
            new CAudioEncoder_G7XX<ENCODER_NAME_G711_A>(GET_LOGGER_PTR));
    }

    IFilter* CreateAudioEncoderFilter_G711_U(DECLARE_LOGGER_ARG)
    {
        return new CPullFilterImpl<CAudioEncoder_G7XX<ENCODER_NAME_G711_U>, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(4, NMMSS::MY_AVCODEC_MAX_AUDIO_FRAME_SIZE, 16),
            SAllocatorRequirements(0, 0, 16),
            new CAudioEncoder_G7XX<ENCODER_NAME_G711_U>(GET_LOGGER_PTR));

    }

    IFilter* CreateAudioEncoderFilter_G726(DECLARE_LOGGER_ARG, int bitrate)
    {
        NCorbaHelpers::CAutoPtr< CAudioEncoder<ENCODER_NAME_G726> > pEncoder(
            new CAudioEncoder<ENCODER_NAME_G726>(GET_LOGGER_PTR));
        if (pEncoder) pEncoder->SetBitrate(bitrate);

        NCorbaHelpers::CAutoPtr< IFilter > pResampler(
            CreateAudioResamplerPullFilter(GET_LOGGER_PTR, 8000, 1,
            NMMSS::NMediaType::Audio::ST_INT16));

        CPullFilterChainImpl* pChain = new CPullFilterChainImpl(GET_LOGGER_PTR);

        pChain->PushBack(pResampler);
        pChain->PushBack(pEncoder);

        return pChain;
    }

    IFilter* CreateAudioEncoderFilter_AAC(DECLARE_LOGGER_ARG, int compression)
    {
        NCorbaHelpers::CAutoPtr< CAudioEncoder<ENCODER_NAME_AAC>> pEncoder(
            new CAudioEncoder<ENCODER_NAME_AAC>(GET_LOGGER_PTR)
            );
        
        if (pEncoder) pEncoder->SetCompression(compression);

        NCorbaHelpers::CAutoPtr<IFilter> pResampler(
            CreateAudioResamplerPullFilter(GET_LOGGER_PTR, 44100, 2, NMMSS::NMediaType::Audio::ST_INT16));

        CPullFilterChainImpl* pChain = new CPullFilterChainImpl(GET_LOGGER_PTR);

        pChain->PushBack(pResampler);
        pChain->PushBack(pEncoder);

        return pChain;
    }

    IFilter* CreateAudioEncoderFilter_VORBIS(DECLARE_LOGGER_ARG, int compression)
    {
        CAudioEncoder<ENCODER_NAME_VORBIS>* pEncoder =
            new CAudioEncoder<ENCODER_NAME_VORBIS>(GET_LOGGER_PTR);
        if (pEncoder) pEncoder->SetCompression(compression);
        return pEncoder;
    }

    IFilter* CreateAudioEncoderFilter_MP2(DECLARE_LOGGER_ARG, int compression)
    {
        NCorbaHelpers::CAutoPtr< CAudioEncoder<ENCODER_NAME_MP2> > pEncoder(
            new CAudioEncoder<ENCODER_NAME_MP2>(GET_LOGGER_PTR));
        if (pEncoder)
        {
            pEncoder->SetCompression(compression);
            pEncoder->SetBitrate(128000);
        }

        NCorbaHelpers::CAutoPtr<IFilter> pResampler(
            CreateAudioResamplerPullFilter(GET_LOGGER_PTR, 44100, 2, NMMSS::NMediaType::Audio::ST_INT16)
            );

        CPullFilterChainImpl* pChain = new CPullFilterChainImpl(GET_LOGGER_PTR);

        pChain->PushBack(pResampler);
        pChain->PushBack(pEncoder);

        return pChain;
    }
}

#ifdef _MSC_VER
#pragma warning(pop) 
#endif // _MSC_VER
