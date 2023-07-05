#include "ExynosVideoCodec.h"
#include "FrameGeometryAdvisor.h"
#include <V4L2Allocator.h>
#include <MediaType.h>
#include "FilterImpl.h"
#include "FFmpegFilter.h"
#include "Transforms.h"
#include <limits>
#include <atomic>

namespace 
{

template <typename v4l2_allocator>
struct v4l2_allocator_traits
{
    static v4l2_allocator* make_allocator_for_v4l2_device();
    static bool prepare_allocator_for_decoder_source_context_change(v4l2_allocator& allocator, NMMSS::CDeferredAllocSampleHolder& connectionAgent);
};
   
template <> inline
NMMSS::NV4L2::IMMapAllocator* v4l2_allocator_traits<NMMSS::NV4L2::IMMapAllocator>::make_allocator_for_v4l2_device()
{
    return NMMSS::NV4L2::CreateMMapAllocator();
}

template <> inline 
NMMSS::NV4L2::IUserPtrAllocator* v4l2_allocator_traits<NMMSS::NV4L2::IUserPtrAllocator>::make_allocator_for_v4l2_device()
{
    return NMMSS::NV4L2::CreateUserPtrAllocator();
}

template <> inline 
bool v4l2_allocator_traits<NMMSS::NV4L2::IMMapAllocator>::prepare_allocator_for_decoder_source_context_change(NMMSS::NV4L2::IMMapAllocator& allocator, NMMSS::CDeferredAllocSampleHolder&)
{
    return allocator.Reset(false);
}

template <> inline
bool v4l2_allocator_traits<NMMSS::NV4L2::IUserPtrAllocator>::prepare_allocator_for_decoder_source_context_change(NMMSS::NV4L2::IUserPtrAllocator& allocator, NMMSS::CDeferredAllocSampleHolder& connectionAgent)
{
    allocator.Reset();
    auto connectionAllocator = connectionAgent.GetAllocator();
    allocator.SetBuffersMemoryOwner(connectionAllocator.Get());
    return true;
}

class Decoder : private NLogging::WithLogger, private v4l2_allocator_traits<NExynos::VideoDecoder::allocator_type>
{
    std::unique_ptr<NExynos::VideoDecoder::allocator_type> m_allocator;
    NMMSS::IFrameGeometryAdvisor* m_advisor;
    NExynos::VideoDecoder m_impl;
    NMMSS::FFmpegFilter m_demuxer;
    NMMSS::PSample m_initData, m_initDataPreprocessed;

    static const uint32_t NOT_INITIALIZED = 0u;
    static const uint32_t ERROR_OBSERVED = std::numeric_limits<std::uint32_t>::max();
    uint32_t m_v4l2LastType;
    uint32_t m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart;

    unsigned m_prerollCounter;
    uint16_t m_outputWidth;
    uint16_t m_outputHeight;

    bool m_doNotPassDeviceSamplesToNGP;
    bool m_generateDiscontinuity;

    bool m_hwArbiterHasAllowedToUseDevice;
    static const unsigned sm_maxNumOfActiveDeviceContexts;
    static std::atomic<unsigned> sm_num_instances;

private:
    NMMSS::ETransformResult SetInitDataAndEnsureFallbackToSWDecoderWillNotMissInitData(NMMSS::ISample* initData, NMMSS::SMediaSampleHeader const& header)
    {
        m_initData = NCorbaHelpers::ShareRefcounted( initData );
        switch (header.nSubtype)
        {
        case NMMSS::NMediaType::Video::fccH264::ID:
            //m_demuxer.reset(GET_LOGGER_PTR, initData->GetBody(), header.nBodySize, NMMSS::FFmpegFilter::h264_mp4toannexb);
            m_initDataPreprocessed = m_demuxer.reset(GET_LOGGER_PTR, initData, NMMSS::FFmpegFilter::h264_mp4toannexb);
            break;
        default:
            break;
        }
        _dbg_ << "Video decoder has set " << MMSS_PARSEFOURCC(header.nSubtype) << " init data " << header.nBodySize << " bytes long";
        //return NMMSS::EIGNORED;
        return NMMSS::ETHROUGH;
    }

    bool CheckIfOutputResolutionRequirementsHaveChanged(unsigned codedW, unsigned codedH)
    {
        if (nullptr == m_advisor)
            return false;
        uint32_t desiredW  = 0u, desiredH = 0;
        bool needResetScaling = false;
        switch ( m_advisor->GetAdvice(desiredW, desiredH) )
        {
        case NMMSS::IFrameGeometryAdvisor::ATSpecific:
            if (desiredW > codedW)
                desiredW = codedW;
            else if (desiredW < codedW)
                desiredW = codedW >> NMMSS::CalculateScalingShift(desiredW, codedW);
            if (desiredW != m_outputWidth)
            {
                m_outputWidth = desiredW;
                needResetScaling = true;
            }
            if (desiredH > codedH)
                desiredH = codedH;
            else if (desiredH < codedH)
                desiredH = codedH >> NMMSS::CalculateScalingShift(desiredH, codedH);
            if (desiredH != m_outputHeight)
            {
                m_outputHeight = desiredH;
                needResetScaling = true;
            }
            break;
        case NMMSS::IFrameGeometryAdvisor::ATLargest:
            if (codedW > m_outputWidth)
            {
                m_outputWidth = codedW;
                needResetScaling = true;
            }
            if (codedH > m_outputHeight)
            {
                m_outputHeight = codedH;
                needResetScaling = true;
            }
            break;
        case NMMSS::IFrameGeometryAdvisor::ATSmallest:
        {
            const int maxDownscalingShift = 2;
            auto const minW = codedW >> maxDownscalingShift;
            auto const minH = codedH >> maxDownscalingShift;
            if (minW < m_outputWidth)
            {
                m_outputWidth = minW;
                needResetScaling = true;
            }
            if (minH < m_outputHeight)
            {
                m_outputHeight = minH;
                needResetScaling = true;
            }
            break;
        }
        default:
            m_outputWidth = codedW;
            m_outputHeight = codedH;
            break;
        }
        return needResetScaling;
    }

    NMMSS::ETransformResult ResetAfterErrorAndTemporarilyFallbackToSW(bool isKeySample)
    {
        if (isKeySample)
        {
            m_v4l2LastType = NOT_INITIALIZED;
            return NMMSS::ETHROUGH;
        }
        m_v4l2LastType = ERROR_OBSERVED;
        return NMMSS::EFAILED;
    }

    bool RestartDecodingPipeline(NMMSS::ISample& sampleToStartWith, NMMSS::CDeferredAllocSampleHolder& holder, bool needToScale)
    {
        NMMSS::NV4L2::FormatHints hints{};
        hints.pixelFormat = m_v4l2LastType;
        hints.width = m_outputWidth;
        hints.height = m_outputHeight;
        return m_impl.Open(GET_LOGGER_PTR, hints, sampleToStartWith, *m_allocator, needToScale);
    }

    NMMSS::ETransformResult Decode( NMMSS::ISample* sample, NMMSS::SMediaSampleHeader const& header,
        NMMSS::NMediaType::Video::SCodedHeader const& subheader, uint32_t v4l2PixFmt, 
        NMMSS::CDeferredAllocSampleHolder& holder )
    {
        using namespace NMMSS;
        if ( header.HasFlag(SMediaSampleHeader::EFDiscontinuity) )
        {
            m_v4l2LastType = NOT_INITIALIZED;
            m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart = NOT_INITIALIZED;
            m_prerollCounter = 0;
            m_demuxer.reset();
            m_impl.ClearDropMode();
            m_initData.Reset();
            m_initDataPreprocessed.Reset();
        }
        if ( header.HasFlag(SMediaSampleHeader::EFInitData) )
        {
            m_v4l2LastType = NOT_INITIALIZED;
            return SetInitDataAndEnsureFallbackToSWDecoderWillNotMissInitData(sample, header);
        }
        PSample toDecode = m_demuxer(GET_LOGGER_PTR, sample);
        bool sampleHasBeenPassedToDecoder = false;
        const bool isKeySample = header.IsKeySample();
        if ( isKeySample )
        {
            if ( NOT_INITIALIZED != m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart )
            {
                m_v4l2LastType = m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart;
                m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart = NOT_INITIALIZED;
            }
            if ( v4l2PixFmt != m_v4l2LastType )
            {
                if ( ! prepare_allocator_for_decoder_source_context_change(*m_allocator, holder) )
                    return ResetAfterErrorAndTemporarilyFallbackToSW(true);

                m_v4l2LastType = v4l2PixFmt;
                m_doNotPassDeviceSamplesToNGP = false;
                m_outputWidth = subheader.nCodedWidth;
                m_outputHeight = subheader.nCodedHeight;
                bool const needToScale = CheckIfOutputResolutionRequirementsHaveChanged(subheader.nCodedWidth, subheader.nCodedHeight);
                if ( !RestartDecodingPipeline( m_initDataPreprocessed ? *m_initDataPreprocessed : *toDecode, holder, needToScale ) )
                {
                    _wrn_ << "Failed to setup decoding pipeline. Just passing samples through till next key frame...";
                    return ResetAfterErrorAndTemporarilyFallbackToSW(true);
                }
                sampleHasBeenPassedToDecoder = !m_initDataPreprocessed;//true;
                m_generateDiscontinuity = true;
            }
            else if ( CheckIfOutputResolutionRequirementsHaveChanged(subheader.nCodedWidth, subheader.nCodedHeight)
                      || m_doNotPassDeviceSamplesToNGP )
            {
                const bool decoderSourceContextCanBeChanged = prepare_allocator_for_decoder_source_context_change(*m_allocator, holder);
                m_doNotPassDeviceSamplesToNGP = !decoderSourceContextCanBeChanged;
                if (decoderSourceContextCanBeChanged)
                {
                    _trc_ << "Changing output resolution from to " << m_outputWidth << 'x' << m_outputHeight << " (source is " << subheader.nCodedWidth << 'x' << subheader.nCodedHeight << ')';
                    bool changed = m_impl.ChangeOutputResolution(GET_LOGGER_PTR, m_outputWidth, m_outputHeight, *m_allocator);
                    if (!changed)
                    {
                        _trc_ << "Trying to re-initialize decoding pipeline...";
                        bool const needToScale = m_outputWidth != subheader.nCodedWidth || m_outputHeight != subheader.nCodedHeight;
                        if ( !RestartDecodingPipeline(m_initDataPreprocessed ? *m_initDataPreprocessed : *toDecode, holder, needToScale) )
                        {
                            _wrn_ << "Decoding pipeline re-initialization has failed. Just passing samples through till next key frame...";
                            return ResetAfterErrorAndTemporarilyFallbackToSW(true);
                        }
                        sampleHasBeenPassedToDecoder = !m_initDataPreprocessed;//true;
                    }
                    m_generateDiscontinuity = true;
                }
            }
        }
        const bool passThroughTillNextKeyFrame = NOT_INITIALIZED == m_v4l2LastType;
        if (passThroughTillNextKeyFrame)
            return ETHROUGH;
        const bool dropTillNextKeyFrame = ERROR_OBSERVED == m_v4l2LastType;
        if (dropTillNextKeyFrame)
            return EFAILED;

        if ( header.HasFlag(SMediaSampleHeader::EFPreroll) )
        {
            if (0 == m_prerollCounter++)
                m_impl.SetDropMode();
        }
        ETransformResult result = EIGNORED;
        if (!sampleHasBeenPassedToDecoder)
        {
            auto status = m_impl.Decode(GET_LOGGER_PTR, *toDecode);
            if (NExynos::VideoDecoder::EDROPPED_OUTPUT & status)
            {
                assert(m_prerollCounter > 0);
                if (0 == --m_prerollCounter)
                {
                    m_impl.ClearDropMode();
                }
            }
            if (NExynos::VideoDecoder::EFAILED & status)
            {
                result = ResetAfterErrorAndTemporarilyFallbackToSW(isKeySample);
            }
            else if (NExynos::VideoDecoder::EDROPPED_INPUT & status)
            {
                m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart = m_v4l2LastType;
                result = isKeySample ? ResetAfterErrorAndTemporarilyFallbackToSW(true) : EFAILED;
            }
            if (NExynos::VideoDecoder::ETRANSFORMED & status)
            {
                auto picture = NCorbaHelpers::TakeRefcounted(
                     m_doNotPassDeviceSamplesToNGP ? m_impl.CopyPicture(*holder.GetAllocator()) : m_impl.GetPicture(*m_allocator)
                );
                if (m_generateDiscontinuity)
                {
                    m_generateDiscontinuity = false;
                    if (m_initData)
                    {
                        m_initData->Header().eFlags |= SMediaSampleHeader::EFDiscontinuity;
                        holder.AddSample( m_initData );
                    }
                    else
                    {
                        picture->Header().eFlags |= SMediaSampleHeader::EFDiscontinuity;
                    }
                }
                holder.AddSample( picture );
                result = ETRANSFORMED;
            }
        }
        return result;
    }

public:
    Decoder(DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor = nullptr)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , m_allocator( make_allocator_for_v4l2_device() )
        , m_advisor(advisor)
        , m_impl()
        , m_demuxer()
        , m_v4l2LastType( NOT_INITIALIZED )
        , m_v4l2TypeToRestoreAfterInputDropToAvoidPipelineRestart( NOT_INITIALIZED )
        , m_prerollCounter()
        , m_outputWidth()
        , m_outputHeight()
        , m_doNotPassDeviceSamplesToNGP(false)
        , m_generateDiscontinuity(false)
        , m_hwArbiterHasAllowedToUseDevice(sm_maxNumOfActiveDeviceContexts >= ++sm_num_instances)
    {}

    ~Decoder()
    {
        m_allocator->Reset();
        --sm_num_instances;
    }

    NMMSS::SAllocatorRequirements GetAllocatorRequirements() const
    {
        return m_allocator->GetAllocatorRequirements();
    }

    NMMSS::ETransformResult PreventFromUselessFallackToSW(NMMSS::ETransformResult resultOfDecoding)
    {
        return NMMSS::EFAILED == resultOfDecoding ? NMMSS::EIGNORED : resultOfDecoding;
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        if (!m_hwArbiterHasAllowedToUseDevice)
            return NMMSS::ETHROUGH;
        auto const& header = sample->Header();
        using namespace NMMSS::NMediaType;
        if (Video::ID != header.nMajor)
            return NMMSS::ETHROUGH;

        uint32_t v4l2FourCC = 0u;
        Video::SCodedHeader* subheader = nullptr;

#define SUPPORT_FORMAT(mediaType, v4l2PixFmt) \
        case Video::mediaType::ID: \
            v4l2FourCC = v4l2PixFmt; \
            subheader = & sample->SubHeader<Video::mediaType>(); \
            break \

        switch (header.nSubtype)
        {
        SUPPORT_FORMAT(fccH264, V4L2_PIX_FMT_H264);
        SUPPORT_FORMAT(fccH263, V4L2_PIX_FMT_H263);
        SUPPORT_FORMAT(fccMPEG4, V4L2_PIX_FMT_MPEG4);
        SUPPORT_FORMAT(fccMPEG2, V4L2_PIX_FMT_MPEG2);
        SUPPORT_FORMAT(fccMPEG1, V4L2_PIX_FMT_MPEG1);
        case Video::fccJPEG::ID:
            v4l2FourCC = V4L2_PIX_FMT_MJPEG;
            subheader = & sample->SubHeader<Video::fccJPEG>();
            if (m_impl.IsMJPEGSupported())
                break;
        default:
            return NMMSS::ETHROUGH;
        }
        return PreventFromUselessFallackToSW( Decode(sample, header, *subheader, v4l2FourCC, holder) );
    }
};

std::atomic<unsigned> Decoder::sm_num_instances{0};

static unsigned getMaxNumOfActiveDeviceContexts()
{
    int const deviceCap = 4;
    if (const char* env = getenv("NGP_MAX_HW_DECODERS"))
    {
        auto val = atoi(env);
        if (val >= 0 && val < deviceCap)
            return val;
    }
    return deviceCap;
}
const unsigned Decoder::sm_maxNumOfActiveDeviceContexts = getMaxNumOfActiveDeviceContexts();

} // anonymous namespace

namespace NMMSS
{
    IFilter* CreateExynosVideoDecoderPullFilter(DECLARE_LOGGER_ARG, IFrameGeometryAdvisor* advisor)
    {
        Decoder* decoder{ new Decoder(GET_LOGGER_PTR, advisor) };
        return new CPullFilterImpl<Decoder, true>(GET_LOGGER_PTR, decoder->GetAllocatorRequirements(), SAllocatorRequirements(), decoder);
    }
}
