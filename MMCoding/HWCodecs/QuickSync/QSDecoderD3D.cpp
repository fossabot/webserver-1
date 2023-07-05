#include "QSDecoderD3D.h"
#include "HWAccelerated.h"
#include "QSDevice.h"
#include "BaseSampleTransformer.h"
#include "mfxplugin.h"

#include "HWCodecs/HWUtils.h"

#include "../FilterImpl.h"

#include <boost/thread/shared_mutex.hpp>
#include <chrono>
#include <thread>

const std::map<mfxIMPL, int> ADAPTERS_MAP = 
{
    {MFX_IMPL_HARDWARE, 0},
    {MFX_IMPL_HARDWARE2, 1},
    {MFX_IMPL_HARDWARE3, 2},
    {MFX_IMPL_HARDWARE4, 3}
};

mfxIMPL GetQuickSyncImpl();

mfxStatus InitSession(MFXVideoSession& session)
{
    mfxInitParam init{};
    init.Implementation = MFX_IMPL_HARDWARE_ANY | GetQuickSyncImpl();
    init.Version = { 3, 1 };
    init.GPUCopy = MFX_GPUCOPY_ON;
    return session.InitEx(init);
}

int GetIntelDeviceAdapterNum()
{
    MFXVideoSession session;
    mfxStatus sts = InitSession(session);
    if (sts == MFX_ERR_NONE)
    {
        mfxIMPL impl;
        MFXQueryIMPL(session, &impl);
        mfxIMPL baseImpl = MFX_IMPL_BASETYPE(impl); // Extract Media SDK base implementation type
        auto it = ADAPTERS_MAP.find(baseImpl);
        if (it != ADAPTERS_MAP.end())
        {
            return it->second;
        }
    }
    return -1;
}

const unsigned int DEFAULT_STREAM_SIZE = 2 * 1024 * 1024;

std::atomic<int> QSDecoderD3DCounter(0);
boost::shared_mutex QSDecoderD3DMutex;

QSDecoderD3D::QSDecoderD3D(DECLARE_LOGGER_ARG, QSDecoderLeaseSP memoryLease, const VideoPerformanceInfo& info, 
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) :
    NLogging::WithLogger(GET_LOGGER_PTR),
	m_allocator(nullptr),
    m_performanceInfo(info),
    m_memoryLease(memoryLease),
    m_device(memoryLease->Device()),
    m_outputSurfaceIndex(-1),
    m_advisor(advisor, NCorbaHelpers::ShareOwnership()),
    m_requirements(requirements)
{
    resizeStreamData(DEFAULT_STREAM_SIZE);
}

QSDecoderD3D::~QSDecoderD3D()
{
    close();
    m_sampleTransformer.reset();
    {
        boost::unique_lock<boost::shared_mutex> lock(QSDecoderD3DMutex);
        m_mfxSession.reset();
    }
    m_allocator.reset();
}

void QSDecoderD3D::close()
{
    if (m_decoder)
    {
        m_decoder.reset();
        --QSDecoderD3DCounter;
        _log_ << "Destroy decoder, count = " << QSDecoderD3DCounter;
    }
    MFXVideoUSER_UnLoad(*m_mfxSession, &MFX_PLUGINID_HEVCD_HW);
}

void QSDecoderD3D::ReleaseSamples()
{
}

int QSDecoderD3D::getFreeSurface(int startIndex)
{
    for(int i = startIndex; i < (int)m_samples.size(); ++i)
    {
        auto& sample = *m_samples[i];
        if (!sample.Locked && !sample.Surface.Data.Locked)
        {
            return i;
        }
    }
    return -1;
}

mfxStatus QSDecoderD3D::InitMfxSession()
{
    m_mfxSession = std::make_unique<MFXVideoSession>();
    mfxStatus sts = InitSession(*m_mfxSession);
    if (sts >= 0 && m_performanceInfo.Codec == MFX_CODEC_HEVC)
    {
        sts = m_device->SupportsHEVC() ? MFXVideoUSER_Load(*m_mfxSession, &MFX_PLUGINID_HEVCD_HW, 1) : MFX_ERR_UNSUPPORTED;
    }
    return sts;
}

mfxStatus QSDecoderD3D::decodeStreamHeader(mfxBitstream *pBS)
{
    if (!m_decoder)
    {
        m_decoder = std::make_unique<MFXVideoDECODE>(*m_mfxSession);
        ++QSDecoderD3DCounter;
        _log_ << "Create decoder, count = " << QSDecoderD3DCounter;
    }
    m_mfxVideoParams = {};
    m_mfxVideoParams.mfx.CodecId = m_performanceInfo.Codec;
    m_mfxVideoParams.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;

    mfxStatus sts = m_decoder->DecodeHeader(pBS, &m_mfxVideoParams);
    if (sts >= 0)
    {
        m_mfxVideoParams.AsyncDepth = 1;
    }
    return sts;
}

void QSDecoderD3D::resizeStreamData(unsigned int length)
{
    m_streamData.resize(length);
    m_mfxBS.MaxLength = length;
    m_mfxBS.Data = m_streamData.data();
}

mfxStatus QSDecoderD3D::Init(mfxBitstream *pBS)
{
    mfxStatus sts = MFX_ERR_NONE;
    if (!m_mfxSession)
    {
        boost::shared_lock<boost::shared_mutex> lock(QSDecoderD3DMutex);
        sts = InitMfxSession();
        if(sts < 0)
        {
            _err_ << "Failed to initialize MFX session: " << MfxStatusToString(sts);
        }
    }
    if (sts >= 0)
    {
        m_sampleTransformer = CreateQSSampleTransformer(GET_LOGGER_PTR, m_device, *m_mfxSession, m_advisor.Get(), m_requirements);
        sts = decodeStreamHeader(pBS);
        if (sts >= 0)
        {
            m_allocator = CreateQSAllocator(m_device, *m_mfxSession, m_externalSurfaces);
            sts = allocateSurfaces();
        }
        if (sts >= 0)
        {
            sts = m_decoder->Init(&m_mfxVideoParams);
            if (sts < 0)
            {
                _err_ << CANNOT_CREATE_DECODER << MfxStatusToString(sts);
            }
        }
    }
    if (sts < 0 && sts != MFX_ERR_MORE_DATA)
    {
        close();
    }
    return sts;
}

mfxStatus QSDecoderD3D::allocateSurfaces()
{
    // Query number of required surfaces for decoder
    mfxFrameAllocRequest request{};
    mfxStatus sts = m_decoder->QueryIOSurf(&m_mfxVideoParams, &request);
    mfxFrameAllocResponse mfxResponse{};
    if (sts >= 0)
    {
        int additionalSurfaces = m_sampleTransformer->GetAdditionalSurfaceCount();
        request.NumFrameMin += additionalSurfaces;
        request.NumFrameSuggested += additionalSurfaces;
        // Allocate required surfaces
        sts = m_allocator->AllocFrames(&request, &mfxResponse);
    }
    if (sts >= 0)
    {
        for (int i = 0; i < mfxResponse.NumFrameActual; ++i)
        {
            m_samples.emplace_back(std::make_unique<QSSampleData>(m_mfxVideoParams.mfx.FrameInfo));
            m_allocator->SetupFrameData(m_samples[i]->Surface.Data, mfxResponse.mids[i]);
        }
    }
    return sts;
}

void QSDecoderD3D::moveBitStream(const unsigned char * pData, unsigned int nLen)
{
    if (m_mfxBS.DataOffset + m_mfxBS.DataLength + nLen > m_mfxBS.MaxLength)
    {
        // Move remaining BS chunk to beginning of buffer
        memmove(m_mfxBS.Data, m_mfxBS.Data + m_mfxBS.DataOffset, m_mfxBS.DataLength);
        m_mfxBS.DataOffset = 0;
        while (m_mfxBS.DataLength + nLen > m_mfxBS.MaxLength)
        {
            resizeStreamData(m_mfxBS.MaxLength + DEFAULT_STREAM_SIZE);
            m_mfxBS.DataLength = 0;
        }
    }
    // Copy bit stream from sample to Media SDK bit stream
    memcpy(m_mfxBS.Data + m_mfxBS.DataLength + m_mfxBS.DataOffset, pData, nLen);
    m_mfxBS.DataLength += nLen;
    m_mfxBS.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
}

bool QSDecoderD3D::IsValid() const
{
    return !m_bInitialized || (m_decoder && m_sampleTransformer->IsValid());
}

void QSDecoderD3D::DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll)
{
    HWDecoderUtils::DecodeAndGetResult(*this, data, holder, preroll, m_requirements.KeySamplesOnly);
}

void QSDecoderD3D::Decode(const CompressedData& data, bool preroll)
{
    if (data.Ptr || m_bInitialized)
    {
        mfxStatus sts = MFX_ERR_NONE;
        if (data.Ptr)
        {
            moveBitStream(data.Ptr, data.Size);
            if (!m_bInitialized)
            {
                sts = Init(&m_mfxBS);
                if (sts == MFX_ERR_MORE_DATA)
                {
                    return;
                }
                m_bInitialized = true;
            }
            if (m_mfxBS.DataLength == 0)
            {
                sts = MFX_ERR_MORE_DATA;
            }
        }
        if (sts == MFX_ERR_NONE)
        {
            decodeBitStream(data.Ptr ? &m_mfxBS : nullptr, preroll, data.Timestamp);
        }
    }
}


int QSDecoderD3D::findSurface(mfxFrameSurface1* surface)
{
    for (size_t i = 0; i < m_samples.size(); ++i)
    {
        if (&m_samples[i]->Surface == surface)
        {
            return i;
        }
    }
    throw std::runtime_error("Surface not found");
}

bool QSDecoderD3D::GetDecodedSamples(NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample)
{
    bool hasResult = m_outputSurfaceIndex >= 0;
    if (hasResult)
    {
        int outputIndex = m_outputSurfaceIndex;
        m_outputSurfaceIndex = -1;
        m_mfxSession->SyncOperation(m_outputSyncPoint, 60000);
        if (m_lagHandler.RegisterOutputFrame())
        {
            m_sampleTransformer->GetSample(m_samples[outputIndex].get(), m_lagHandler.LastTimestamp(), holder, waitForSample);
        }
    }
    return hasResult;
}

void QSDecoderD3D::decodeBitStream(mfxBitstream* bs, bool preroll, ::uint64_t timestamp)
{
    mfxStatus sts = MFX_ERR_NONE;
    m_outputSyncPoint = nullptr;
    mfxFrameSurface1* outSurface;
    int inSurfaceIndex = getFreeSurface();
    while (!m_outputSyncPoint && sts != MFX_ERR_MORE_DATA)
    {
        sts = (inSurfaceIndex >= 0) ? m_decoder->DecodeFrameAsync(bs, &m_samples[inSurfaceIndex]->Surface, &outSurface, &m_outputSyncPoint) : MFX_ERR_ABORTED;

        if (MFX_WRN_DEVICE_BUSY == sts)
        {
            // Wait if device is busy, then repeat the same call to DecodeFrameAsync
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        else if (sts == MFX_ERR_MORE_SURFACE)
        {
            inSurfaceIndex = getFreeSurface();
        }
        else if (sts != MFX_ERR_NONE && sts != MFX_WRN_VIDEO_PARAM_CHANGED && sts != MFX_ERR_MORE_DATA)
        {
            close();
            return;
        }
    }

    if (bs)
    {
        m_lagHandler.RegisterInputFrame(timestamp, preroll);
    }

    if (sts >= MFX_ERR_NONE)
    {
        m_outputSurfaceIndex = findSurface(outSurface);
    }
}

HWDeviceSP QSDecoderD3D::Device() const
{
    return m_device;
}

const VideoPerformanceInfo& QSDecoderD3D::GetPerformanceInfo(std::chrono::milliseconds /*recalc_for_period*/) const
{
    return m_performanceInfo;
}

void QSDecoderD3D::SetExternalSurfaces(ExternalSurfacesSP externalSurfaces)
{
    m_externalSurfaces = externalSurfaces;
}
