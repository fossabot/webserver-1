#include "ExynosVideoCodec.h"
#include <MakeFourCC.h>

#include <sys/ioctl.h>
#include <dirent.h>
#include <assert.h>
#include <typeinfo>
#include <type_traits>

#include <boost/format.hpp>
#include <boost/scope_exit.hpp>

#define STREAM_BUFFER_SIZE              1048576 // compressed frame size buffer. for unknown reason, possibly the firmware bug,
                                                // if set to lower values it corrupts adjacent value in the setup data structure for h264 streams
                                                // and leads to stream hangs on heavy frames
#define FIMC_CAPTURE_BUFFERS_CNT        3       // 2 begins to be slow.
#define MFC_OUTPUT_BUFFERS_CNT          3       // 1 doesn't work at all, 2 is enough most of the times, but in a rare case of interlaced video two buffers
                                                // must be queued all the time to get fill picture from interlaced frames, so let's have them 3
#define MFC_CAPTURE_EXTRA_BUFFER_CNT    8       // these are extra buffers, better keep their count as big as going to be simultaneous dequeued buffers number

namespace NExynos
{
using namespace NMMSS::NV4L2;

const uint32_t g_fccDecodingTargetFormat = V4L2_PIX_FMT_NV12M;

VideoDecoder::VideoDecoder()
    : m_decoderSink()
    , m_decoderSource()
    , m_converterSink()
    , m_converterSource()
    , m_bDropPictures()
    , m_hasTargetFormatSupport()
    , m_mfcFormatsChecked()
    , m_needConverter()
    , m_freeBuffersCount()
    , m_iDequeuedToPresentBufferNumber(INVALID_BUFFER_INDEX)
{}

VideoDecoder::~VideoDecoder()
{
    Dispose();
}

void VideoDecoder::Dispose()
{
    m_converterDev.Close();
    m_decoderDev.Close();
    m_freeBuffersCount = 0u;
    m_iDequeuedToPresentBufferNumber = INVALID_BUFFER_INDEX;
    m_decoderSink = nullptr;
    m_decoderSource = nullptr;
    m_converterSink = nullptr;
    m_converterSource = nullptr;
}

struct CloseDir
{
    void operator()(DIR* dir) const
    {
        closedir(dir);
    }
};

template <typename FDriverNameMatch, typename FCapabilitiesMatch>
static bool DiscoverV4L2Device(DECLARE_LOGGER_ARG, CV4L2Device& dev, const char* deviceDescription, FDriverNameMatch checkDriverName, FCapabilitiesMatch checkDeviceCapabilities)
{
    std::unique_ptr<DIR, CloseDir> dir { opendir ("/sys/class/video4linux/") };
    if (!dir)
    {
        _err_ << "Failed to open directory /sys/class/video4linux/";
        return false;
    }

    while ( struct dirent *ent = readdir(dir.get()) )
    {
        if (0 != strncmp(ent->d_name, "video", 5))
            continue;

        char *p;
        char name[64];
        char devname[64];
        char sysname[64];
        char drivername[32];
        char target[1024];

        snprintf(sysname, sizeof(sysname), "/sys/class/video4linux/%s", ent->d_name);
        snprintf(name, sizeof(name), "/sys/class/video4linux/%s/name", ent->d_name);

        FILE* fp = fopen(name, "r");
        if (fp == NULL)
            continue;

        if (fgets(drivername, sizeof(drivername), fp) != NULL)
        {
            p = strchr(drivername, '\n');
            if (p != NULL)
                *p = '\0';
        }
        else
        {
            drivername[0] = '\0';
        }
        fclose(fp);
        if (!checkDriverName(drivername))
            continue;

        int ret = readlink(sysname, target, sizeof(target));
        if (ret < 0)
            continue;
        target[ret] = '\0';
        p = strrchr(target, '/');
        if (nullptr == p)
            continue;

        snprintf(devname, sizeof(devname), "/dev/%s", ++p);

        try
        {
            dev.Open(GET_LOGGER_PTR, devname);
            if ( checkDeviceCapabilities(dev.GetDeviceCapabilities()) )
            {
                _log_ << deviceDescription << " device [" << drivername << "] was found : " << devname;
                return true;
            }
        }
        catch (std::exception const& e)
        {
            _err_ << e.what();
        }
        dev.Close();
    }
    _err_ << deviceDescription << " device is not available";
    return false;
}

bool VideoDecoder::OpenMFCDecoder(DECLARE_LOGGER_ARG)
{
    auto is_mfc_dec = [] (const char* drivername) {
        return 0 == strncmp(drivername, "s5p-mfc-dec", 11);
    };
    auto can_stream_m2m_mplane = [] (NMMSS::NV4L2::CV4L2Device::dev_caps_type caps) {
        return ( (caps & V4L2_CAP_VIDEO_M2M_MPLANE) || ( (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) && (caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) ) ) && (caps & V4L2_CAP_STREAMING);
    };
    return DiscoverV4L2Device(GET_LOGGER_PTR, m_decoderDev, "MFC decoder", is_mfc_dec, can_stream_m2m_mplane);
}

bool VideoDecoder::CheckMFCDecoderFormats(DECLARE_LOGGER_ARG)
{
    // we enumerate all the supported formats looking for NV12MT and NV12
    m_hasTargetFormatSupport = false;
    int index = 0;
    while (true)
    {
        struct v4l2_fmtdesc vid_fmtdesc = {};
        vid_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        vid_fmtdesc.index = index++;

        auto ret = m_decoderDev.IOCTL(VIDIOC_ENUM_FMT, &vid_fmtdesc, false);
        if (ret != 0)
            break;
        _log_ << "MFC decoder CAPTURE format : " << MMSS_PARSEFOURCC(vid_fmtdesc.pixelformat) << " (" << vid_fmtdesc.description << ')';
        if (g_fccDecodingTargetFormat == vid_fmtdesc.pixelformat)
        {
#ifdef NGP_HWPLATFORM_ODROIDU3
            /// @note odroid u3 has MFC5 and drivers that support only tiled CAPTURE formats
#else
            m_hasTargetFormatSupport = true;
#endif
        }
    }
    return true;
}

bool VideoDecoder::OpenConverter(DECLARE_LOGGER_ARG)
{
    auto is_fimc = [] (const char* drivername) {
        return nullptr != strstr(drivername, "fimc") && nullptr != strstr(drivername, "m2m");
    };
    auto can_stream_m2m_mplane = [] (NMMSS::NV4L2::CV4L2Device::dev_caps_type caps) {
        return ( (caps & V4L2_CAP_VIDEO_M2M_MPLANE) || ( (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) && (caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) ) ) && (caps & V4L2_CAP_STREAMING);
    };
    return DiscoverV4L2Device(GET_LOGGER_PTR, m_converterDev, "FIMC", is_fimc, can_stream_m2m_mplane);
}

static inline void CreateSourceContext(MMapStreamingContext*& dest, CV4L2Device& dev, BuffersCountRequirement& bufReq, IMMapAllocator& allocator, FormatHints const* format)
{
    dest = & dev.CreateMMapSourceContext(bufReq, &allocator, format);
}

static inline void CreateSourceContext(UserPtrStreamingContext*& dest, CV4L2Device& dev, BuffersCountRequirement& bufReq, IUserPtrAllocator& allocator, FormatHints const* format)
{
    dest = & dev.CreateUserPtrSourceContext(bufReq, allocator, format);
}

template <typename Context, typename Allocator>
static inline void CreateSourceContext(Context*& dest, CV4L2Device& dev, BuffersCountRequirement& bufReq, Allocator& allocator, FormatHints const* format)
{
    throw std::logic_error( str( boost::format("%1% of device %2% is not supposed to be a source context of video decoder") % typeid(Context).name() % dev.GetDeviceName() ) );
}

bool VideoDecoder::Open(DECLARE_LOGGER_ARG, FormatHints const& hints, NMMSS::ISample& firstSampleToLaunchDecoder, VideoDecoder::allocator_type& allocator, bool scalingIsRequired)
{
    Dispose();

    if (!OpenMFCDecoder(GET_LOGGER_PTR))
        return false;

    if (!m_mfcFormatsChecked)
    {
        CheckMFCDecoderFormats(GET_LOGGER_PTR);
        m_mfcFormatsChecked = true;
    }

    m_needConverter = !std::is_same<source_context_type, MMapStreamingContext>::value || !m_hasTargetFormatSupport;
    if (m_needConverter || scalingIsRequired)
    {
        if (!OpenConverter(GET_LOGGER_PTR) && m_needConverter)
            return false;
        m_needConverter = true;
    }
    try
    {
        // Setup MFC OUTPUT
        FormatHints decoderOutputFormat = hints;
        if (STREAM_BUFFER_SIZE > hints.plane0BufferSize)
            decoderOutputFormat.plane0BufferSize = STREAM_BUFFER_SIZE;
        BuffersCountRequirement mfcOutputBuffersReq = MFC_OUTPUT_BUFFERS_CNT;
        m_decoderSink = & m_decoderDev.CreateMMapSinkContext(mfcOutputBuffersReq, decoderOutputFormat);
        _dbg_ << "MFC OUTPUT REQBUF: asked " << MFC_OUTPUT_BUFFERS_CNT << ", got " << mfcOutputBuffersReq.numBuffers;
        m_freeBuffersCount = mfcOutputBuffersReq.numBuffers;

        /// @note we must feed the first data in order to setup the decoding chain properly
        buf_index_type indexOfTheBufferForTheFirstSample = --m_freeBuffersCount;
        m_decoderSink->GrabSample(indexOfTheBufferForTheFirstSample, &firstSampleToLaunchDecoder);
        m_decoderSink->QBUF(indexOfTheBufferForTheFirstSample);

        if (!m_needConverter)
        {
            _dbg_ << "MFC CAPTURE S_FMT to target format " << MMSS_PARSEFOURCC(g_fccDecodingTargetFormat) << ". FIMC is not going to be used";
            FormatHints decoderCaptureFormat = {};
            decoderCaptureFormat.pixelFormat = g_fccDecodingTargetFormat;
            m_decoderDev.SetSourceFormat(decoderCaptureFormat);
        }

        m_decoderSink->StreamON();

        BuffersCountRequirement mfcCaptureBuffersReq( MFC_CAPTURE_EXTRA_BUFFER_CNT, BuffersCountRequirement::EExtra );
        if (m_needConverter)
        {
            m_decoderSource =  & m_decoderDev.CreateMMapSourceContext(mfcCaptureBuffersReq, nullptr);
        }
        else
        {
            CreateSourceContext(m_decoderSource, m_decoderDev, mfcCaptureBuffersReq, allocator, nullptr);
        }
        _dbg_ << "MFC CAPTURE REQBUF: asked extra " << MFC_CAPTURE_EXTRA_BUFFER_CNT << ", got " << mfcCaptureBuffersReq.numBuffers;

        {
            auto const& fmt = m_decoderSource->GetFormat().fmt.pix_mp;
            _dbg_ << "MFC CAPTURE G_FMT: fmt 0x" << std::hex << fmt.pixelformat << std::dec << '(' << MMSS_PARSEFOURCC(fmt.pixelformat) <<
                "), (" << fmt.width << 'x' << fmt.height <<
                "), plane[0]=" << fmt.plane_fmt[0].sizeimage << " plane[1]=" << fmt.plane_fmt[1].sizeimage;

            // Get MFC CAPTURE crop to check and setup Line Size as well as FIMC converter if needed
            auto const& decoderCrop = m_decoderSource->GetCrop();

            // This is the picture boundaries we are interested in, everything outside is alignement because of tiled MFC output
            _dbg_ << "MFC CAPTURE G_CROP: (x, y, w, h) = (" << decoderCrop.left << ", " << decoderCrop.top << ", " << decoderCrop.width << ", " << decoderCrop.height << ')';
        }

        m_decoderSource->StreamON();

        if (m_needConverter)
        {
            SetupConverter(GET_LOGGER_PTR, hints.width, hints.height, allocator);
        }
        _log_ << "Exynos hardware accelerated video decoder setup is successfull, start streaming";
        return true;
    }
    catch(std::exception const& e)
    {
        _err_ << "Exynos hardware accelerated video decoder setup has failed: " << e.what();
    }
    Dispose();
    return false;
}

bool VideoDecoder::ChangeOutputResolution(DECLARE_LOGGER_ARG, unsigned width, unsigned height, VideoDecoder::allocator_type& allocator)
{
    if (!m_needConverter)
        return false;

    try
    {
        m_converterDev.ResetStreamingContext();
        m_converterSource = nullptr;
        m_converterSink = nullptr;
        
        SetupConverter(GET_LOGGER_PTR, width, height, allocator);
        return true;
    }
    catch(std::exception const& e)
    {
        _err_ << "Changing output resolution has failed: " << e.what();
    }
    Dispose();
    return false;
}

void VideoDecoder::SetupConverter(DECLARE_LOGGER_ARG, unsigned width, unsigned height, VideoDecoder::allocator_type& allocator)
{
    assert(nullptr != m_decoderSource);

    // Setup FIMC OUTPUT fmt with data from MFC CAPTURE
    m_converterSink = & m_converterDev.CreateUserPtrSinkContext( *m_decoderSource );
    _dbg_ << "FIMC OUTPUT REQBUF is ready to get buffers from MFC CAPTURE";

    FormatHints converterCaptureFormat = {};
    converterCaptureFormat.pixelFormat = g_fccDecodingTargetFormat;
    converterCaptureFormat.width = width;
    converterCaptureFormat.height = height;

    BuffersCountRequirement fimcCaptureBuffersReq = FIMC_CAPTURE_BUFFERS_CNT;
    CreateSourceContext( m_converterSource, m_converterDev, fimcCaptureBuffersReq, allocator, &converterCaptureFormat );
    _dbg_ << "FIMC CAPTURE REQBUF: asked " << FIMC_CAPTURE_BUFFERS_CNT << ", got " << fimcCaptureBuffersReq.numBuffers;

    {
        auto const& fmt = m_converterSource->GetFormat().fmt.pix_mp;
        _dbg_ << "FIMC CAPTURE G_FMT: fmt 0x" << std::hex << fmt.pixelformat << std::dec << '(' << MMSS_PARSEFOURCC(fmt.pixelformat) <<
            "), (" << fmt.width << 'x' << fmt.height <<
            "), plane[0]=" << fmt.plane_fmt[0].sizeimage << " plane[1]=" << fmt.plane_fmt[1].sizeimage;


        auto const& converterCrop = m_converterSource->GetCrop();
        _dbg_ << "FIMC CAPTURE G_CROP: (x, y, w, h) = (" << converterCrop.left << ", " << converterCrop.top << ", " << converterCrop.width << ", " << converterCrop.height << ')';
    }

    m_converterSink->StreamON();
    m_converterSource->StreamON();
}

VideoDecoder::Status VideoDecoder::Decode(DECLARE_LOGGER_ARG, NMMSS::ISample& sample)
{
    assert(INVALID_BUFFER_INDEX == m_iDequeuedToPresentBufferNumber);
    Status status = EIGNORED;
    try
    {
        auto index = 0 != m_freeBuffersCount ? --m_freeBuffersCount : m_decoderSink->DQBUF();
        const bool dropSample = INVALID_BUFFER_INDEX == index;
        if (dropSample)
        {
            _wrn_ << "All MFC OUTPUT buffers are queued and busy, no space for new frame to decode";
            status |= EDROPPED_INPUT;
        }
        else
        {
            m_decoderSink->GrabSample(index, &sample);
            m_decoderSink->QBUF(index);
        }

        index = m_decoderSource->DQBUF();
        const bool hasDecodedFrames = INVALID_BUFFER_INDEX != index;
        if (hasDecodedFrames)
        {
            if (m_bDropPictures)
            {
                _trc_ << "Dropping frame decoded with buffer index " << index << ". Queue it back to MFC CAPTURE since the picture is dropped anyway";
                m_decoderSource->QBUF(index);
                status |= EDROPPED_OUTPUT;
            }
            else
            {
                if (m_needConverter)
                {
                    m_converterSink->QBUF(index);
                    m_iDequeuedToPresentBufferNumber = m_converterSource->DQBUF();
                }
                else
                {
                    m_iDequeuedToPresentBufferNumber = index;
                }
                if (INVALID_BUFFER_INDEX != m_iDequeuedToPresentBufferNumber)
                    status |= ETRANSFORMED;
            }
        }

        if (m_needConverter)
        {
            index = m_converterSink->DQBUF();
            const bool converterReleasedSomeBufferThatMayBeQueuedBackToMFC = INVALID_BUFFER_INDEX != index;
            if (converterReleasedSomeBufferThatMayBeQueuedBackToMFC)
            {
                m_decoderSource->QBUF(index);
            }
        }
    }
    catch(std::exception const& e)
    {
        _err_ << e.what();
        status |= EFAILED;
    }
    return status;
}

static inline NMMSS::ISample* FillSample(MMapStreamingContext& context, buf_index_type index, IMMapAllocator& allocator)
{
    return context.FillSample(index, allocator);
}

static inline NMMSS::ISample* FillSample(UserPtrStreamingContext& context, buf_index_type index, IUserPtrAllocator& allocator)
{
    return context.FillSample(index, allocator);
}

template <typename Context, typename Allocator>
static inline NMMSS::ISample* FillSample(Context& context, buf_index_type index, Allocator& allocator)
{
    throw std::logic_error( str( boost::format("%1% of device %2% is not supposed to emit samples for video decoder") % typeid(Context).name() % context.GetDevice()->GetDeviceName() ) );
}

template <typename Context>
static inline NMMSS::ISample* CopySample(Context& context, buf_index_type index, NMMSS::IAllocator& allocator)
{
    BOOST_SCOPE_EXIT_TPL(&context, index) {
        context.QBUF(index);
    } BOOST_SCOPE_EXIT_END
    return context.CopySample(index, allocator);
}

NMMSS::ISample* VideoDecoder::GetPicture(VideoDecoder::allocator_type& allocator)
{
    auto index = m_iDequeuedToPresentBufferNumber;
    assert(INVALID_BUFFER_INDEX != index);
    m_iDequeuedToPresentBufferNumber = INVALID_BUFFER_INDEX;
    return m_needConverter
        ? FillSample( *m_converterSource, index, allocator )
        : FillSample( *m_decoderSource, index, allocator );
}

NMMSS::ISample* VideoDecoder::CopyPicture(NMMSS::IAllocator& allocator)
{
    auto index = m_iDequeuedToPresentBufferNumber;
    assert(INVALID_BUFFER_INDEX != index);
    m_iDequeuedToPresentBufferNumber = INVALID_BUFFER_INDEX;
    return m_needConverter
        ? CopySample( *m_converterSource, index, allocator )
        : CopySample( *m_decoderSource, index, allocator );
}

} // namespace NExynos
