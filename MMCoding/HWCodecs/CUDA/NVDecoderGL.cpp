#include "NVDecoderGL.h"
#include "MemorySampleTransformer.h"
#include "../FilterImpl.h"
#include "../MakeFourCC.h"
#include "../MediaType.h"
#include "../PtimeFromQword.h"
#include "HWCodecs/DecoderPerformance.h"
#include "HWCodecs/HWDevicePool.h"
#include "HWCodecs/HWUtils.h"
#include "HWCodecs/CUDA/CudaDevice.h"
#include "HWCodecs/CUDA/CudaSampleHolder.h"


// Forward declaration for callback setup.
static int CUDAAPI handleVideoSequence(void *pUserData, CUVIDEOFORMAT *pVideoFormat) { return ((NVDecoderGL *)pUserData)->HandleVideoSequence(pVideoFormat); }
static int CUDAAPI handlePictureDecode(void *pUserData, CUVIDPICPARAMS *pPicParams) { return ((NVDecoderGL *)pUserData)->HandlePictureDecode(pPicParams); }
static int CUDAAPI handlePictureDisplay(void *pUserData, CUVIDPARSERDISPINFO *pDispInfo) { return ((NVDecoderGL *)pUserData)->HandlePictureDisplay(pDispInfo); }


std::atomic<int> NVDecoderGLCounter(0);

NVDecoderGL::NVDecoderGL(DECLARE_LOGGER_ARG, CudaDeviceSP device, DecoderPerformanceSP performance, const VideoPerformanceInfo& info, 
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements):
    NLogging::WithLogger(GET_LOGGER_PTR),
    m_device(device),
    m_performance(performance),
    m_performanceInfo(info),
    m_stopRequested(false)
{
    init();

    if (IsValid())
    {
        auto primaryDevice = HWDevicePool::Instance()->GetPrimaryDevice();
        bool toPrimaryMemory = (requirements.Destination == NMMSS::EMemoryDestination::ToPrimaryVideoMemory);
        bool decodeToSystemMemory = (requirements.Destination == NMMSS::EMemoryDestination::ToSystemMemory) ||
            (toPrimaryMemory && (!primaryDevice || primaryDevice->GetDeviceType() != NMMSS::EHWDeviceType::NvidiaCUDA));

        m_sampleHolder = m_device->CreateSampleHolder(decodeToSystemMemory);
        if (decodeToSystemMemory)
        {
            m_sampleTransformer = std::make_unique<MemorySampleTransformer>(m_sampleHolder, advisor);
        }
    }
}

NVDecoderGL::~NVDecoderGL()
{
    auto context = m_device->SetContext();
    stop();
    if (m_parser)
    {
        check(cuvidDestroyVideoParser(m_parser), "cuvidDestroyVideoParser");
        m_parser = nullptr;
    }
    m_device->ReleaseDecoder();
}

void NVDecoderGL::stopDecoder(bool reset)
{
    if (m_decoder)
    {
        unmapFrame();
        check(cuvidDestroyDecoder(m_decoder), "cuvidDestroyDecoder");
        m_decoder = nullptr;
        --NVDecoderGLCounter;
        _log_ << "Destroy decoder, count = " << NVDecoderGLCounter << ", reset = " << reset;
        if (reset)
        {
            m_frameQueue.clear();
            m_lagHandler.Clear();
        }
        else
        {
            m_lagHandler.SkipAll();
        }
    }
    m_performance.reset();
}

void NVDecoderGL::stop()
{
    stopDecoder(true);
    if (m_cuCtxLock)
    {
        check(cuvidCtxLockDestroy(m_cuCtxLock), "cuvidCtxLockDestroy");
        m_cuCtxLock = nullptr;
    }
}

void NVDecoderGL::init()
{
    _log_ << "Init NVDecoder. Decoder limit : " << m_device->AvailableDecoders();

    auto context = m_device->SetContext();

    if (!m_performanceInfo.Width || !m_performanceInfo.Height || 
        checkDecoderCaps((cudaVideoCodec)m_performanceInfo.Codec, m_performanceInfo.Width, m_performanceInfo.Height, cudaVideoChromaFormat_420))
    {
        // bind the context lock to the CUDA context
        if (check(cuvidCtxLockCreate(&m_cuCtxLock, m_device->GetContext()), "cuvidCtxLockCreate"))
        {
            initCudaVideo();
        }
    }
}

void NVDecoderGL::initCudaVideo()
{
    // Create video parser
    CUVIDPARSERPARAMS videoParserParameters{};
    videoParserParameters.CodecType = (cudaVideoCodec)m_performanceInfo.Codec;
    videoParserParameters.ulErrorThreshold = 100;
    videoParserParameters.ulMaxNumDecodeSurfaces = 1;
    videoParserParameters.ulMaxDisplayDelay = 4;

    videoParserParameters.pUserData = this; // opaque data
    videoParserParameters.pfnSequenceCallback = handleVideoSequence;    // Called before decoding frames and/or whenever there is a format change
    videoParserParameters.pfnDecodePicture = handlePictureDecode;    // Called when a picture is ready to be decoded (decode order)
    videoParserParameters.pfnDisplayPicture = handlePictureDisplay;   // Called whenever a picture is ready to be displayed (display order)
    
    check(cuvidCreateVideoParser(&m_parser, &videoParserParameters), "cuvidCreateVideoParser");
}

void NVDecoderGL::Decode(const CompressedData& data, bool preroll)
{
    CUVIDSOURCEDATAPACKET cupkt{};
    if (data.Ptr && data.Size > 0)
    {
        cupkt.payload = data.Ptr;
        cupkt.payload_size = data.Size;

        if (data.Timestamp > 0)
        {
            cupkt.flags |= CUVID_PKT_TIMESTAMP | CUVID_PKT_ENDOFPICTURE;
            cupkt.timestamp = data.Timestamp;
        }
        m_prerollFlag = preroll;
        m_timestamp = data.Timestamp;
    }
    else
    {
        cupkt.flags = CUVID_PKT_ENDOFSTREAM;
    }

    check(cuvidParseVideoData(m_parser, &cupkt), "cuvidParseVideoData");

    if ((m_performance && !m_performance->IsLegal()) || m_stopRequested || !m_device->IsValid())
    {
        stop();
    }
}


bool NVDecoderGL::GetDecodedSamples(NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample)
{
    const int MAX_FRAME_QUEUE_SIZE = 3;
    int counter = 0;
    bool hasResult = !m_frameQueue.empty() || m_sample;
    while (!m_frameQueue.empty() && (!counter++ || m_frameQueue.size() > MAX_FRAME_QUEUE_SIZE))
    {
        sendSample(*holder);
        CUVIDPARSERDISPINFO frame = m_frameQueue.front();
        m_frameQueue.pop_front();
        if (m_lagHandler.RegisterOutputFrame())
        {
            static ErrorCounter counter;
            auto timestamp = m_lagHandler.LastTimestamp();
            if (((::uint64_t)frame.timestamp != timestamp) && counter.ShowError())
            {
                _err_ << "Output frame timestamp drift. Expected: " << NMMSS::PtimeFromQword(timestamp) << ", actual: " << NMMSS::PtimeFromQword(frame.timestamp);
            }
            output(frame, *holder, m_lagHandler.LastTimestamp());
        }
    }

    if (waitForSample)
    {
        sendSample(*holder);
    }
    return hasResult;
}

void NVDecoderGL::DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll)
{
    auto context = m_device->SetContext();
    HWDecoderUtils::DecodeAndGetResult(*this, data, holder, preroll);
}

void NVDecoderGL::output(const CUVIDPARSERDISPINFO& frame, NMMSS::CDeferredAllocSampleHolder& holder, uint64_t timestamp)
{
    unmapFrame();

    CUVIDPROCPARAMS params{};
    params.progressive_frame = frame.progressive_frame;
    params.second_field = 0;
    params.top_field_first = frame.top_field_first;
    params.unpaired_field = (frame.repeat_first_field < 0);
    params.output_stream = m_sampleHolder->Stream()->Stream();

    uint32_t pitch = 0;
    if (check(cuvidMapVideoFrame(m_decoder, frame.picture_index, &m_mappedFrame, &pitch, &params), "cuvidMapVideoFrame") && m_mappedFrame && pitch)
    {
        CudaSurfaceRegion src = { { m_format.display_area.right, m_format.display_area.bottom, (int)pitch }, m_mappedFrame};
        if (m_sampleTransformer)
        {
            m_sample = m_sampleTransformer->Transform(src, timestamp, holder);
        }
        else
        {
            m_sample = m_sampleHolder->GetFreeSample();
            if (m_sample)
            {
                m_sample->Setup(src, timestamp, holder);
            }
            else
            {
                _dbg_ << "Out of free samples, frame skipped";
            }
        }
        if(m_sample && !check(m_sample->Status(), "failed to fill sample"))
        {
            m_stopRequested = true;
        }
    }
}

void NVDecoderGL::sendSample(NMMSS::CDeferredAllocSampleHolder& holder)
{
    if (m_sample && m_sample->IsValid())
    {
        m_sampleHolder->Stream()->Synchronize();
        holder.AddSample(m_sample->Sample());
        m_sample.reset();
    }
}

void NVDecoderGL::unmapFrame()
{
    if (m_mappedFrame)
    {
        cuvidUnmapVideoFrame(m_decoder, m_mappedFrame);
        m_mappedFrame = 0;
    }
}

int NVDecoderGL::HandlePictureDisplay(CUVIDPARSERDISPINFO* dispinfo)
{
    m_frameQueue.emplace_back(*dispinfo);
    return 1;
}

int NVDecoderGL::HandlePictureDecode(CUVIDPICPARAMS* picparams)
{
    if (m_performance)
    {
        DecodeWrapper w(*m_performance);
        if (m_decoder && check(cuvidDecodePicture(m_decoder, picparams), "cuvidDecodePicture"))
        {
            m_lagHandler.RegisterInputFrame(m_timestamp, m_prerollFlag);
            return 1;
        }
    }
    return 0;
}

const int ADDITIONAL_DECODE_SURFACES = 3;

bool FormatChanged(CUVIDEOFORMAT f1, CUVIDEOFORMAT f2)
{
    f2.bitrate = f1.bitrate;
    f2.frame_rate = f1.frame_rate;
    f2.min_num_decode_surfaces = std::max(f1.min_num_decode_surfaces, f2.min_num_decode_surfaces);
    return !!std::memcmp(&f1, &f2, sizeof(CUVIDEOFORMAT));
}

// Decoder creation if reset stream format or codec changed.
int NVDecoderGL::HandleVideoSequence(CUVIDEOFORMAT* format)
{
    if (!FormatChanged(m_format, *format))
    {
        return m_format.min_num_decode_surfaces + ADDITIONAL_DECODE_SURFACES;
    }

    m_format = *format;

    stopDecoder(false);

    CUVIDDECODECREATEINFO cuinfo{};
    cuinfo.CodecType = format->codec;
    cuinfo.ChromaFormat = format->chroma_format;
    cuinfo.OutputFormat = cudaVideoSurfaceFormat_NV12;

    cuinfo.ulWidth = format->coded_width;
    cuinfo.ulHeight = format->coded_height;
    cuinfo.ulTargetWidth = format->display_area.right;
    cuinfo.ulTargetHeight = format->display_area.bottom;

    _log_ << "Handling video sequence w:" << m_format.coded_width << "x" << m_format.coded_height;

    cuinfo.display_area.right = static_cast<short>(cuinfo.ulTargetWidth);
    cuinfo.display_area.bottom = static_cast<short>(cuinfo.ulTargetHeight);

    cuinfo.ulNumDecodeSurfaces = format->min_num_decode_surfaces + ADDITIONAL_DECODE_SURFACES;
    cuinfo.ulNumOutputSurfaces = 1;
    cuinfo.ulCreationFlags = cudaVideoCreate_PreferCUVID;
        
    cuinfo.bitDepthMinus8 = format->bit_depth_luma_minus8;
    //cuinfo.vidLock = m_cuCtxLock;

    if (format->progressive_sequence)
    {
        cuinfo.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    }

    m_performanceInfo.Width = (int)cuinfo.ulWidth;
    m_performanceInfo.Height = (int)cuinfo.ulHeight;
    m_performanceInfo.Fps = GetFps(format->frame_rate.numerator, format->frame_rate.denominator);

    if (checkDecoderCaps(format->codec, format->coded_width, format->coded_height, format->chroma_format))
    {
        if (!m_performance)
        {
            m_performance = m_device->AcquireDecoderPerformance(GET_LOGGER_PTR, m_performanceInfo);
        }
        if (m_performance && check(cuvidCreateDecoder(&m_decoder, &cuinfo), "cuvidCreateDecoder"))
        {
            ++NVDecoderGLCounter;
            m_memoryLease.reset();
            _log_ << "Decoder created, count = " << NVDecoderGLCounter << ", available GPU memory: " << m_device->GetAvailableMemory() / 1024 / 1024 << "MB";
            return cuinfo.ulNumDecodeSurfaces;
        }
    }

    stop();
    return 0;
}

bool NVDecoderGL::checkDecoderCaps(cudaVideoCodec codec, uint32_t width, uint32_t height, cudaVideoChromaFormat chromaFormat)
{
    if (!m_memoryLease)
    {
        const int FACTOR = 35;
        m_memoryLease = m_device->LeaseMemory(FACTOR * (int)width * (int)height);
        if (!m_memoryLease)
        {
            _log_ << CANNOT_CREATE_DECODER << "Low available GPU memory: " << m_device->GetAvailableMemory() / 1024 / 1024 << "MB";
            return false;
        }
    }

    CUVIDDECODECAPS caps{};
    caps.eCodecType = codec;
    caps.eChromaFormat = chromaFormat;
    if (!check(cuvidGetDecoderCaps(&caps), "cuvidGetDecoderCaps") || !caps.bIsSupported)
    {
        _log_ << CANNOT_CREATE_DECODER << "Codec or chroma format not supported (video should be in yuv420)";
        return false;
    }

    if (width > caps.nMaxWidth || height > caps.nMaxHeight)
    {
        auto ngpCodecId = CudaDevice::CudaCodecToCodecId(codec);
        _log_ << CANNOT_CREATE_DECODER << "Maximum allowed size = " << caps.nMaxWidth << "x" << caps.nMaxHeight << " for " << MMSS_PARSEFOURCC(ngpCodecId);
        return false;
    }

    return true;
}

bool NVDecoderGL::IsValid() const
{
    return m_parser && m_cuCtxLock;
}

void NVDecoderGL::ReleaseSamples()
{
}

bool NVDecoderGL::check(CUresult code, const char* message) const
{
    return m_device->CheckStatus(code, message, GET_LOGGER_PTR);
}

const VideoPerformanceInfo& NVDecoderGL::GetPerformanceInfo(std::chrono::milliseconds /*recalc_for_period*/) const
{
    return m_performanceInfo;
}

HWDeviceSP NVDecoderGL::Device() const
{
    return m_device;
}

void NVDecoderGL::SetAdvisor(NMMSS::IHWDecoderAdvisor* advisor)
{
    m_sampleHolder->SetDecoderAdvisor(advisor);
}


NVReceiver::NVReceiver(DECLARE_LOGGER_ARG, CudaDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements):
    NLogging::WithLogger(GET_LOGGER_PTR),
    m_device(device)
{
    bool decodeToSystemMemory = (requirements.Destination == NMMSS::EMemoryDestination::ToSystemMemory);
    m_sampleHolder = m_device->CreateSampleHolder(decodeToSystemMemory);
    if (decodeToSystemMemory)
    {
        m_sampleTransformer = std::make_unique<MemorySampleTransformer>(m_sampleHolder, advisor);
    }
}

NVReceiver::~NVReceiver()
{
}

void NVReceiver::ProcessSample(NMMSS::ISample& sample, NMMSS::CDeferredAllocSampleHolder& holder)
{
    auto context = m_device->SetContext();
    sendSample(holder);
    m_sampleHolder->ReleaseSharedSamples();
    if (auto memory = CudaSample::GetSharedMemory(m_device, sample, m_sharedMemory, m_originalHandle))
    {
        if (m_sampleTransformer)
        {
            CudaSurfaceRegion src = CudaSample::GetSurface(sample, memory->DevicePtr(), false);
            m_sample = m_sampleTransformer->Transform(src, sample.Header().dtTimeBegin, holder);
        }
        else
        {
            m_sample = m_sampleHolder->GetFreeSample();
            if (m_sample)
            {
                m_sample->SetupSharedMemory(sample, memory, holder);
            }
        }
    }
    else
    {
        _err_ << "Cannot access shared video memory";
    }
}

void NVReceiver::sendSample(NMMSS::CDeferredAllocSampleHolder& holder)
{
    if (m_sample && m_sample->IsValid())
    {
        m_sampleHolder->Stream()->Synchronize();
        holder.AddSample(m_sample->Sample());
        m_sample.reset();
    }
}
