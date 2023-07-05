#include "FFmpegFilter.h"
#include "FFmpegMutex.h"
#include <AllocatorImpl.h>

#include <boost/thread/locks.hpp>

namespace NMMSS
{

const FFmpegFilter::FilterDescription FFmpegFilter::h264_mp4toannexb{"h264_mp4toannexb"};
const FFmpegFilter::FilterDescription FFmpegFilter::dump_extra{"dump_extra"};
const FFmpegFilter::FilterDescription FFmpegFilter::aac_adtstoasc{"aac_adtstoasc", true};

enum {
    DEBUG_FFMPEG_FILTER = 0
};

FFmpegFilter::FFmpegFilter()
    : m_allocID( NAllocatorImpl::GetAllocatorUUIDGenerator()->CreateUUID() )
{
    boost::lock_guard<boost::mutex> lock( CFFmpegMutex::Get() );
    avcodec_register_all();
}

FFmpegFilter::~FFmpegFilter()
{}

void FFmpegFilter::reset()
{
    m_filter.reset();
}

void FFmpegFilter::reset(DECLARE_LOGGER_ARG, const std::uint8_t* extradata, std::uint32_t extradata_size, FFmpegFilter::FilterDescription const& filterDesc, const char* filter_args)
{
    if (!m_context)
        m_context.reset( avcodec_alloc_context3(nullptr) );
    m_context->extradata = reinterpret_cast<std::uint8_t*>( av_realloc(m_context->extradata, extradata_size + AV_INPUT_BUFFER_PADDING_SIZE) );
    memcpy(m_context->extradata, extradata, extradata_size);
    m_context->extradata_size = extradata_size;
    m_filter.reset( av_bitstream_filter_init(filterDesc.id) );
    m_filter_args = filter_args;
    m_workaroundExtradataLeak = filterDesc.workaroundExtradataLeak;
    _dbg_ << "Using ffmpeg filter " << filterDesc.id << " with extra data of " << extradata_size << " bytes";
}

NMMSS::PSample FFmpegFilter::reset(DECLARE_LOGGER_ARG, NMMSS::ISample* initData, FilterDescription const& filterDesc, const char* filter_args)
{
    auto const& header = initData->Header();
    assert( header.HasFlag(SMediaSampleHeader::EFInitData) );
    auto origData = initData->GetBody();
    reset( GET_LOGGER_PTR, origData, header.nBodySize, filterDesc, filter_args );
    auto const old_extradata = m_context->extradata;
    auto const old_extradata_size = m_context->extradata_size;

    auto const sizeOfDummyDataFedToMakeFilterInitializeExtraData = 0u;
    applyFilter(GET_LOGGER_PTR, origData, sizeOfDummyDataFedToMakeFilterInitializeExtraData, false, false);

    if (old_extradata == m_context->extradata)
    {
        if (old_extradata_size == m_context->extradata_size)
            return NCorbaHelpers::ShareRefcounted( initData );
    }
    _dbg_ << "ffmpeg filter " << filterDesc.id << " has modified extra data, extra data size is " << m_context->extradata_size << " bytes";
    typedef CPreprocessedSample<NullBufferDeleter> extradata_sample;
    NCorbaHelpers::CAutoPtr< extradata_sample > modifiedExtraData { new extradata_sample({m_allocID, make_unsigned(-1)}) };
    modifiedExtraData->reset(initData, extradata_sample::buffer_ptr_type(m_context->extradata), m_context->extradata_size);
    return modifiedExtraData;
}

std::pair< FFmpegFilter::buffer_ptr_type, std::uint32_t > FFmpegFilter::applyFilter(DECLARE_LOGGER_ARG, const std::uint8_t data[], std::uint32_t size, bool isKeyFrame, bool reportError)
{
    std::uint8_t* out = nullptr;
    int out_size = 0;
    std::uint8_t* const old_extradata = m_context->extradata;
    const int retcode = av_bitstream_filter_filter(m_filter.get(), m_context.get(), m_filter_args, &out, &out_size, data, size, isKeyFrame);
    if (m_workaroundExtradataLeak && old_extradata != m_context->extradata && old_extradata)
    {
        _dbg_ << "workarounding extradata leak after av_bitstream_filter_filter " << m_filter->filter->name;
        av_free( old_extradata );
    }
    switch(retcode)
    {
    case 1:
        if (out && out != data)
        {
            if (DEBUG_FFMPEG_FILTER)
                _dbg_ << "av_bitstream_filter_filter " << m_filter->filter->name << " generated frame " << (void*)out << "(" << out_size << ") from " << (void*)data << "(" << size << ")";
            return std::make_pair( buffer_ptr_type(out, true), out_size );
        }
        break;
    case 0:
        if (out && size != make_unsigned(out_size))
        {
            if (DEBUG_FFMPEG_FILTER)
                _dbg_ << "av_bitstream_filter_filter " << m_filter->filter->name << " cut frame " << (void*)out << "(" << out_size << ") from " << (void*)data << "(" << size << ")";
            return std::make_pair( buffer_ptr_type(out, false), out_size );
        }
        break;
    default:
        if (reportError)
        {
            char errmsg[128];
            _err_ << "av_bitstream_filter_filter " << m_filter->filter->name << " returned error (" << retcode << "): " << (0 == av_strerror(retcode, errmsg, sizeof(errmsg)) ? errmsg : "unknown error");
        }
    }
    return std::make_pair( buffer_ptr_type(), 0u );
}

NMMSS::PSample FFmpegFilter::operator()(DECLARE_LOGGER_ARG, NMMSS::ISample* orig)
{
    if (m_filter)
    {
        NMMSS::SMediaSampleHeader const& header = orig->Header();
        auto out = applyFilter( GET_LOGGER_PTR, orig->GetBody(), header.nBodySize, header.IsKeySample() );
        if (out.first)
        {
            auto sample = m_outSamples.getFreeSample(m_allocID);
            sample->reset(orig, std::move(out.first), out.second);
            return sample;
        }
    }
    return NCorbaHelpers::ShareRefcounted(orig);
}

} // namespace NMMSS
