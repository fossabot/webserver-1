#include "Decoder.h"
#include "Device.h"

#include "HWCodecs/DecoderPerformance.h"
#include "FilterImpl.h"
#include "../MediaType.h"
#include "Sample.h"

constexpr uint8_t MAX_CONSECUTIVE_REPORTS_WITH_NR_ERRORS_GROWTH = 3;

HADecoder::HADecoder(DECLARE_LOGGER_ARG, AscendDeviceSP device, const VideoPerformanceInfo& info,
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements):
    NLogging::WithLogger(GET_LOGGER_PTR),
    m_device(device),
    m_decoderErrorObserved(false),
    m_swsctx(0),
    m_performanceInfo(info),
    m_advisor(advisor, NCorbaHelpers::ShareOwnership())
{
    m_streamCtx = m_device->CreateStreamContext(info);
}

HADecoder::~HADecoder()
{
    sws_freeContext(m_swsctx);
}

void HADecoder::WriteNV12ToHolderAsYUV420(const VDecOutSample * frame, NMMSS::CDeferredAllocSampleHolder * holder)
{
    uint32_t nWidth = frame->width;
    uint32_t nHeight = frame->height;

    const size_t LINESIZE_ALIGN = 32;
    const size_t PLANE_PADDING = 16;

    auto align = [](size_t x, size_t a) { return (x + a - 1) & ~(a - 1); };
    const unsigned Y_lineSize = align(nWidth, LINESIZE_ALIGN);
    const auto Y_planeSize = Y_lineSize * nHeight + PLANE_PADDING;
    const unsigned U_lineSize = align((nWidth + 1) / 2, LINESIZE_ALIGN);
    const auto U_planeSize = U_lineSize * (nHeight + 1) / 2 + PLANE_PADDING;
    const unsigned V_lineSize = U_lineSize;
    const auto V_planeSize = U_planeSize;

    const uint8_t* frameDataY = frame->buffer.get();
    int nStrideY = m_device->GetPitch(nWidth);
    int nStrideUV = nStrideY;
    const uint8_t* frameDataUV = frameDataY + nStrideY * m_device->GetPitchH(nHeight);

    uint8_t * outFrameData = nullptr;
    auto const dstBodySize = Y_planeSize + U_planeSize + V_planeSize;
    NMMSS::NMediaType::Video::fccI420::SubtypeHeader *header = nullptr;
    if (!holder->Alloc(dstBodySize)) return;

    auto sample = holder->GetSample();
    outFrameData = sample->GetBody();
    sample->Header().nBodySize = dstBodySize;
    sample->Header().dtTimeBegin = frame->timestamp;
    sample->Header().eFlags = 0;

    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(sample->GetHeader(), &header);
    header->nOffset = 0;
    header->nWidth = nWidth;
    header->nHeight = nHeight;
    header->nPitch = Y_lineSize;
    header->nPitchU = U_lineSize;
    header->nPitchV = V_lineSize;
    header->nOffsetU = Y_planeSize;
    header->nOffsetV = Y_planeSize + U_planeSize;

    if (m_swsWidth != nWidth || m_swsHeight != nHeight)
    {
        AVPixelFormat inFormat = AV_PIX_FMT_NV12;
        AVPixelFormat outFormat = AV_PIX_FMT_YUV420P;
        m_swsctx = sws_getCachedContext( m_swsctx,
                nWidth, nHeight, inFormat,
                nWidth, nHeight, outFormat,
                0, 0, 0, 0
        );
        m_swsWidth = nWidth;
        m_swsHeight = nHeight;
    }

    const uint8_t* inData[] = { frameDataY,  frameDataUV };
    const int inStride[] = { nStrideY, nStrideUV };
    uint8_t* outData[] = { outFrameData, outFrameData + header->nOffsetU, outFrameData + header->nOffsetV};
    const int outStride[] = { (int)header->nPitch, (int)header->nPitchU, (int)header->nPitchV };
    sws_scale(m_swsctx, inData, inStride, 0, nHeight, outData, outStride);
}

void HADecoder::DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll)
{
    uint32_t resultWidth = 0, resultHeight = 0;
    if (m_advisor)
        m_advisor->GetAdvice(resultWidth, resultHeight);

    VDecInSample input_data;
    input_data.buffer = const_cast<uint8_t*>(data.Ptr);
    input_data.size = data.Size;
    input_data.timestamp = data.Timestamp;
    input_data.resizeWidth = resultWidth;
    input_data.resizeHeight = resultHeight;
    auto status = m_streamCtx->Decode(input_data, preroll);
    if (ASC_Process != status)
    {
        m_decoderErrorObserved = true;
        _err_ << "[AscendDecoder]: Decode has failed. Status = " << static_cast<int>(status);
        return;
    }

    while (true)
    {
        auto frame = m_streamCtx->GetBuffer();
        if (nullptr == frame) break;

        m_performanceInfo.Width = frame->width;
        m_performanceInfo.Height = frame->height;
        WriteNV12ToHolderAsYUV420(frame.get(), holder);
    }
}

bool HADecoder::IsValid() const
{
    return !m_decoderErrorObserved;
}

void HADecoder::ReleaseSamples()
{
    while (auto frame = m_streamCtx->GetBuffer());
}

const VideoPerformanceInfo& HADecoder::GetPerformanceInfo(std::chrono::milliseconds recalc_for_period) const
{
    if (auto diff_ms = recalc_for_period.count())
    {
        auto stats = m_streamCtx->TakeStats();
        m_performanceInfo.Fps = double(stats.nr_frames * 1000) / diff_ms;
        m_performanceInfo.Extra = {
            {"errors", stats.nr_errors},
            {"host2dev", stats.send},
            {"wait4vdec", stats.wait4vdec},
            {"vdec", stats.vdec},
            {"dev2host", stats.sendback},
            {"wait4recv", stats.wait4recv}
        };
        auto filterOutUnsupportedStats = [](auto& metrics)
        {
            metrics.erase(std::remove_if(metrics.begin(), metrics.end(), [](auto& kv){return kv.second < 0;}), metrics.end());
        };
        filterOutUnsupportedStats(m_performanceInfo.Extra);
        if (stats.nr_errors > m_nrErrorsReportedPreviously)
        {
            if (++m_nrErrorsGrowthAge >= MAX_CONSECUTIVE_REPORTS_WITH_NR_ERRORS_GROWTH)
                m_decoderErrorObserved = true;
            m_nrErrorsReportedPreviously = stats.nr_errors;
        }
        else
        {
            m_nrErrorsGrowthAge = 0;
        }
    }
    return m_performanceInfo;
}

HWDeviceSP HADecoder::Device() const
{
    return m_device;
}
