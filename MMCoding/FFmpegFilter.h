#ifndef FFMPEGFILTER_H
#define FFMPEGFILTER_H

#include "MMCodingExports.h"

#include <MediaType.h>
#include <Logging/log2.h>
#include <CorbaHelpers/RefcountedImpl.h>

#include <memory>
#include <cstdint>
#include <cassert>
#include <type_traits>

extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace
{
    template <typename T> inline
    typename std::enable_if< std::is_integral<T>::value, typename std::make_unsigned<T>::type >::type make_unsigned(T value)
    {
        return static_cast<typename std::make_unsigned<T>::type>(value);
    }
}

namespace NMMSS
{

struct NullBufferDeleter
{
    void operator()(uint8_t const*) const {}
};

template < class BufferDtor = std::default_delete<std::uint8_t[]> >
class CPreprocessedSample : public NMMSS::ISample, public NCorbaHelpers::CRefcountedImpl
{
public:
    CPreprocessedSample(NMMSS::SSampleIndex index) : m_index(index) {}

    typedef std::unique_ptr<std::uint8_t[], BufferDtor> buffer_ptr_type;
    void reset(NMMSS::ISample* orig, buffer_ptr_type newBody, std::uint32_t newSize)
    {
        m_orig = NMMSS::PSample(orig, NCorbaHelpers::ShareOwnership());
        m_customBody = std::move(newBody);
        std::uint8_t* origHeader = orig->GetHeader();
        std::copy(origHeader, origHeader + SAMPLE_HEADER_SIZE, m_customHeader);
        Header().nBodySize = newSize;
    }

    NMMSS::SSampleIndex Index() const { return m_index; }
    std::uint8_t* GetHeader() { return m_customHeader; }
    const std::uint8_t* GetHeader() const { return m_customHeader; }
    std::uint8_t* GetBody() { return m_customBody.get(); }
    const std::uint8_t* GetBody() const { return m_customBody.get(); }

private:
    NMMSS::PSample m_orig;
    NMMSS::SSampleIndex m_index;
    std::uint8_t m_customHeader[SAMPLE_HEADER_SIZE];
    buffer_ptr_type m_customBody;
};

template < class BufferDtor >
class CPreprocessedSamplesPool
{
    typedef NCorbaHelpers::CAutoPtr< CPreprocessedSample<BufferDtor> > sample_ptr_type;
    std::vector< sample_ptr_type > m_storage;
public:
    sample_ptr_type getFreeSample(SAllocatorId const& allocId)
    {
        for ( auto const& sample : m_storage )
        {
            if (1 == sample->GetCounter())
                return sample;
        }
        m_storage.emplace_back( new CPreprocessedSample<BufferDtor>( { allocId, m_storage.size() } ) );
        return m_storage.back();
    }
};

class AvUtilBufferDtor
{
    bool m_adopted;
public:
    AvUtilBufferDtor(bool adopted = true) : m_adopted(adopted) {}
    void operator()(std::uint8_t* buf) const
    {
        if (m_adopted)
            av_free(buf);
    }
};

class MMCODING_CLASS_DECLSPEC FFmpegFilter
{
    struct ContextDtor
    {
        void operator()(AVCodecContext* ctx) const
        {
            av_free(ctx->extradata);
            av_free(ctx);
        }
    };
    struct FilterDtor
    {
        void operator()(AVBitStreamFilterContext* ctx) const
        {
            av_bitstream_filter_close(ctx);
        }
    };

    typedef std::unique_ptr<AVCodecContext, ContextDtor> context_ptr_type;
    typedef std::unique_ptr<AVBitStreamFilterContext, FilterDtor> filter_ptr_type;
    typedef std::unique_ptr<std::uint8_t[], AvUtilBufferDtor> buffer_ptr_type;
    context_ptr_type m_context;
    filter_ptr_type m_filter;
    const char* m_filter_args;
    bool m_workaroundExtradataLeak;
    SAllocatorId m_allocID;
    CPreprocessedSamplesPool<AvUtilBufferDtor> m_outSamples;

private:
    std::pair<buffer_ptr_type, std::uint32_t> applyFilter(DECLARE_LOGGER_ARG, const std::uint8_t data[], std::uint32_t size, bool isKeyFrame, bool reportError = true);

public:
    FFmpegFilter();
    ~FFmpegFilter();

    struct FilterDescription
    {
        const char* id;
        bool workaroundExtradataLeak;

        FilterDescription(const char* filter_id, bool knownToHaveExtradataLeak = false) : id(filter_id), workaroundExtradataLeak(knownToHaveExtradataLeak) {}
    };
    static const FilterDescription h264_mp4toannexb;
    static const FilterDescription dump_extra;
    static const FilterDescription aac_adtstoasc;

    void reset(DECLARE_LOGGER_ARG, const std::uint8_t* extradata, std::uint32_t extradata_size, FilterDescription const& filterDesc, const char* filter_args = nullptr);
    NMMSS::PSample reset(DECLARE_LOGGER_ARG, NMMSS::ISample* initData, FilterDescription const& filterDesc, const char* filter_args = nullptr);

    void reset();

    NMMSS::PSample operator()(DECLARE_LOGGER_ARG, NMMSS::ISample* orig);
};

} // namespace NMMSS

#endif // FFMPEGFILTER_H
