#include <typeinfo>
#include <limits>

#include "../WindowsSpecific.h"
#include "../FilterImpl.h"
#include "../MiscFilters.h"
#include "../Exception.h"
#include "../MediaType.h"
#include "FFMPEGCodec.h"
#include "MPEG4Allocator.h"
#include "HooksPluggable.h"
#include "OVSCodec.h"
#include "LinkFFmpeg.h"
#include "SessionWatcher.h"

#ifdef _WIN32
#include "ITVCodec.h"
#include "WXWLCodec.h"
#include "BIMWCodec.h"
#endif //_WIN32

#include "../Codecs/include/EncoderAPI.h"
#include "../Codecs/include/DecoderAPI.h"
#include "../Codecs/MPEG4/include/mpeg4decoder.h"
#include "../Codecs/MPEG4/include/mpeg4preview.h"

#include "../FrameSizeStats.h"

static const int nMaxCompressedFrameSize=2*704*576;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4505)
#endif // _MSC_VER

namespace
{

class CEncoderContext
{
public:
    CEncoderContext(uint32_t _nMaxFrameSize, NOVSCodec::EQuality _eQuality, int _nKeyFrameEvery, int _nSegmentSize)
    :   nMaxFrameSize(_nMaxFrameSize)
    ,   eQuality(_eQuality)
    ,   nKeyFrameEvery(_nKeyFrameEvery)
    ,   nSegmentSize(_nSegmentSize)
    ,   bKeyFrame(true)
    ,   nFrameNumber(0)
    {
    }
    void CheckYContext(int nWidth, int nHeight, NOVSCodec::IEncoder* pEncoder)
    {
        CheckContext(pCtxY, true, nWidth, nHeight, pEncoder);
    }
    void CheckUVContext(int nWidth, int nHeight, NOVSCodec::IEncoder* pEncoder)
    {
        CheckContext(pCtxUV, false, nWidth, nHeight, pEncoder);
    }
    bool IsKeyFrame() const
    {
        return bKeyFrame;
    }
    void AdvanceFrame()
    {
        if(nKeyFrameEvery)
        {
            bKeyFrame=(0==nFrameNumber);
            if(pCtxY.get())
                pCtxY->WantNextKeyFrame(bKeyFrame);
            if(pCtxUV.get())
                pCtxUV->WantNextKeyFrame(bKeyFrame);
            ++nFrameNumber;
            nFrameNumber%=nKeyFrameEvery;
        }
    }
    void ForceNextKeyFrame()
    {
        bKeyFrame=true;
        nFrameNumber=0;
    }
    NOVSCodec::IEncoderContext* GetContextY() { return pCtxY.get(); }
    NOVSCodec::IEncoderContext* GetContextUV() { return pCtxUV.get(); }
public:
    const int32_t nMaxFrameSize;
    const NOVSCodec::EQuality eQuality;
    const int nKeyFrameEvery;
    const int nSegmentSize;
private:
    bool bKeyFrame;
    int nFrameNumber;
    typedef std::unique_ptr<NOVSCodec::IEncoderContext> PContext;
    PContext pCtxY;
    PContext pCtxUV;
    NOVSCodec::EFrameType eFrameType;

    void CheckContext(PContext& pCtx, bool bCheckFrameType, int nWidth, int nHeight, NOVSCodec::IEncoder* pEncoder)
    {
        NOVSCodec::EFrameType ftNew=NOVSCodec::CIF1;
        if(bCheckFrameType)
        {
            if(nWidth>=512)
            {
                ftNew=NOVSCodec::CIF2;
                if(nHeight>=400)
                    ftNew=NOVSCodec::CIF4;
            }
        }
        NOVSCodec::EStatus res;
        bool bNewContext=false;
        if(!pCtx.get())
        {
            pCtx.reset(pEncoder->CreateEncoderContext());
            if(!pCtx.get())
                MMSS_THROW_LOGIC("CreateEncoderContext() on OVS encoder failed");
            bNewContext=true;
        }
        if(bNewContext || (bCheckFrameType && (eFrameType!=ftNew)))
        {
            if(bCheckFrameType)
                eFrameType=ftNew;
            res=pCtx->SetParameters(eQuality, eFrameType, !!nKeyFrameEvery, nSegmentSize);
            if(NOVSCodec::SUCCESS!=res)
                MMSS_THROW_LOGIC("SetParameters() on OVS encoder context failed");
        }
        res=pCtx->SetGeometry(nWidth, nHeight);
        if(NOVSCodec::SUCCESS!=res)
            MMSS_THROW_LOGIC("SetGeometry() on OVS encoder context failed");
    }
};

class CEncoderHelper
{
    DECLARE_LOGGER_HOLDER;
public:
    CEncoderHelper(DECLARE_LOGGER_ARG, NOVSCodec::IEncoder* _pEncoder, CEncoderContext& _ec,
        NMMSS::CDeferredAllocSampleHolder& holder)
    :   pEncoder(_pEncoder)
    ,   ec(_ec)
    ,   m_holder(holder)
    ,   m_result(NMMSS::ETHROUGH)
    {
        INIT_LOGGER_HOLDER;
    }
    template<typename TMediaTypeHeader>
    void operator()(TMediaTypeHeader* header, uint8_t* dataPtr)
    {
    }
    void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* dataPtr)
    {
        ec.CheckYContext(header->nPitch, header->nHeight, pEncoder);
        ec.AdvanceFrame();

        if(!m_holder.Alloc(ec.nMaxFrameSize))
        {
            _log_ << "Encoder memory allocation failed" << std::endl;
            m_result = NMMSS::EFAILED;
            return;
        }


        NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader* pOutHeader = 0;
        NMMSS::NMediaType::MakeMediaTypeStruct< NMMSS::NMediaType::Video::fccOVS2>(
            m_holder->GetHeader(), &pOutHeader);
        memset(static_cast<void*>(pOutHeader), 0, sizeof(NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader));

        uint32_t nSize = ec.nMaxFrameSize;
        NOVSCodec::EStatus res = pEncoder->Encode(dataPtr + header->nOffset,
                                                  m_holder->GetBody(), ec.GetContextY(), nSize);
        if(NOVSCodec::SUCCESS!=res)
            MMSS_THROW_LOGIC("Encode() on OVS encoder failed");
        m_holder->Header().nBodySize = nSize;
        pOutHeader->nSegmentSize=ec.nSegmentSize;
        pOutHeader->nColour=0;
        pOutHeader->nFragmentsPresence=NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_Y_All;
        m_result=NMMSS::ETRANSFORMED;
    }
    void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* dataPtr)
    {
        EncodeYUV(header, dataPtr, 1);
        m_result=NMMSS::ETRANSFORMED;
    }
    void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* dataPtr)
    {
        EncodeYUV(header, dataPtr, 2);
        m_result=NMMSS::ETRANSFORMED;
    }
    NMMSS::ETransformResult GetResult() const
    {
        return m_result;
    }
    bool IsKeyFrame() const
    {
        return ec.IsKeyFrame();
    }
private:
    void EncodeYUV(NMMSS::NMediaType::Video::SYUVPlanarVideoHeader* header,
                   uint8_t* dataPtr, uint8_t nColour)
    {
        ec.CheckYContext(header->nPitch, header->nHeight, pEncoder);
        if(header->nPitchU != header->nPitchV)
            MMSS_THROW_LOGIC("Encoding YUV planar with different U and V pitches is not supported");

        int pad = header->nOffsetV - ( header->nOffsetU +
                                        nColour * header->nPitchV *
                                        header->nHeight / 2);

        if(pad < 0)
        {
            MMSS_THROW_LOGIC("Encoding YUV planar with negative padding between U and V planes is not supported");
        }


        uint8_t* dataPtrY = dataPtr + header->nOffset;
        uint8_t* dataPtrUV = dataPtr + header->nOffsetU;

        if(pad > 0)
        {
            int sizeUV = nColour * header->nPitchV * header->nHeight;

            m_pSampleUV = m_holder.AllocUnmanaged(sizeUV);
            if (!m_pSampleUV)
            {
                _log_ << "UV buffer memory allocation failed" << std::endl;
                m_result = NMMSS::EFAILED;
                return;
            }

            memcpy(m_pSampleUV->GetBody(),
                   dataPtr + header->nOffsetU, sizeUV/2);

            memcpy(m_pSampleUV->GetBody() + sizeUV/2,
                   dataPtr + header->nOffsetV, sizeUV/2);

            dataPtrUV = m_pSampleUV->GetBody();
        }



        ec.CheckUVContext(header->nPitchU, header->nHeight * nColour, pEncoder);
        ec.AdvanceFrame();

        if(!m_holder.Alloc(ec.nMaxFrameSize))
        {
            _log_ << "Encoder memory allocation failed" << std::endl;
            m_result = NMMSS::EFAILED;
            return;
        }



        NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader* pOutHeader = 0;
        NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccOVS2>(m_holder->GetHeader(), &pOutHeader);
        memset(static_cast<void*>(pOutHeader), 0, sizeof(NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader));

        NOVSCodec::EStatus res;
        uint32_t nSizeY = ec.nMaxFrameSize;
        res = pEncoder->Encode(dataPtrY, m_holder->GetBody(), ec.GetContextY(), nSizeY);
        if(NOVSCodec::SUCCESS != res)
            MMSS_THROW_LOGIC("Encode() on OVS encoder failed");

        uint32_t nSizeUV = ec.nMaxFrameSize - nSizeY;
        res = pEncoder->Encode(dataPtrUV, m_holder->GetBody() + nSizeY,
                               ec.GetContextUV(), nSizeUV);

        if(NOVSCodec::SUCCESS!=res)
            MMSS_THROW_LOGIC("Encode() on OVS encoder failed");

        m_holder->Header().nBodySize = nSizeY + nSizeUV;

        pOutHeader->nSegmentSize = ec.nSegmentSize;
        pOutHeader->nColour = nColour;
        pOutHeader->nFragmentsPresence =
                NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_Y_All
            |   NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_UV_All;

        pOutHeader->nUVOffset = nSizeY;

        pOutHeader->nBorderPixSize = header->nPitch - header->nWidth;
    }
private:
    NOVSCodec::IEncoder* const pEncoder;
    CEncoderContext& ec;
    NMMSS::CDeferredAllocSampleHolder& m_holder;
    NMMSS::ETransformResult m_result;
    NMMSS::PSample m_pSampleUV;
};

class CEncoder
{
    DECLARE_LOGGER_HOLDER;
public:
    NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        CEncoderHelper eh(GET_LOGGER_PTR, m_encoder.get(), m_context, holder);

        NMMSS::NMediaType::ApplyMediaTypeVisitor(in, eh);

        NMMSS::ETransformResult res = eh.GetResult();
        if(res == NMMSS::ETRANSFORMED)
        {
            holder->Header().dtTimeBegin=in->Header().dtTimeBegin;
            holder->Header().dtTimeEnd=in->Header().dtTimeEnd;

            if(eh.IsKeyFrame())
            {
                holder->Header().eFlags = 0;
                m_lastKeyFrameSize=holder->Header().nBodySize;
            }
            else
            {
                holder->Header().eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame;
                if(holder->Header().nBodySize>((m_lastKeyFrameSize*3)/4))
                    m_context.ForceNextKeyFrame();
            }

            m_frameSizeStats.ReportFrame(holder->Header().nBodySize,
                0!=(holder->Header().eFlags & NMMSS::SMediaSampleHeader::EFNeedKeyFrame));
            if(m_frameSizeStats.MaxCountReached())
            {
                _log_ << "Encoder statistics: " << m_frameSizeStats << std::endl;
                m_frameSizeStats.Reset();
            }
        }
        return res;
    }
    CEncoder(DECLARE_LOGGER_ARG, int nQualityPreset=3, int nKeyFrameEvery=100, int nSegmentSize=0)
    :   m_context(nMaxCompressedFrameSize,
            NOVSCodec::EQuality(int(NOVSCodec::PRESET_0+nQualityPreset)),
            nKeyFrameEvery, nSegmentSize)
    ,   m_lastKeyFrameSize(0)
    {
        INIT_LOGGER_HOLDER;
        m_encoder.reset(NOVSCodec::CreateOVSEncoder());
    }
private:
    std::auto_ptr<NOVSCodec::IEncoder> m_encoder;
    CEncoderContext m_context;
    size_t m_lastKeyFrameSize;
    CFrameSizeStats m_frameSizeStats;
};


class CDecoder
{
    DECLARE_LOGGER_HOLDER;

    const int DEFAULT_VIDEO_FRAMERATE;
public:
    CDecoder(
        DECLARE_LOGGER_ARG,
        NMMSS::IFrameGeometryAdvisor* advisor, bool multithreaded)
        : DEFAULT_VIDEO_FRAMERATE(50)
        , m_advisor(advisor, NCorbaHelpers::ShareOwnership())
        , m_allocator(0)
        , m_sample(nullptr)
        , m_result(NMMSS::ETHROUGH)
        , m_lastType(0)
        , m_lastCodecId(AV_CODEC_ID_NONE)
        , m_decoderFlushed(true)
        , m_multithreaded(multithreaded)
        , m_setDiscontinuity(false)
    {
        INIT_LOGGER_HOLDER;

        if (m_multithreaded)
        {
            boost::mutex::scoped_lock lock(m_concurrentMutex);
            ++m_concurrentCount;
        }
    }

    ~CDecoder()
    {
        if (m_multithreaded)
        {
            boost::mutex::scoped_lock lock(m_concurrentMutex);
            --m_concurrentCount;
        }
    }

    NMMSS::ETransformResult operator()(
        NMMSS::ISample* in,
        NMMSS::CDeferredAllocSampleHolder& holder)
    {
        m_holder = &holder;
        m_sample = in;
        m_sampleHeader = &(in->Header());

        NMMSS::IAllocator* allocator = m_holder->GetAllocator().Get();
        if (!allocator)
        {
            _err_ << "Video decoder receive NULL allocator" << std::endl;
            return NMMSS::EFAILED;
        }

        if(m_sampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFDiscontinuity ||
            !m_allocator || m_allocator != allocator)
        {
            m_allocator = allocator;
            //порождаем сбрасывание контекста декодера
            m_lastType = 0;
            m_sessionWatcher.RegisterStartSample(in);
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

        if (m_result == NMMSS::ETRANSFORMED && m_sampleHeader->HasFlag(NMMSS::SMediaSampleHeader::EFPreroll))
            m_result = NMMSS::EIGNORED;

        return m_result;
    }

    template<typename TMediaTypeHeader>
    void operator()(TMediaTypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        m_result = NMMSS::ETHROUGH;
    }

    void operator()(
        NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader* subtypeHeader,
        uint8_t* dataPtr)
    {
        const ::uint32_t type=NMMSS::NMediaType::Video::fccOVS2::ID;
        if(!m_codecOVS.get() || (m_lastType != type))
        {
            m_codecOVS.reset(new NMMSS::COVSDecoder());
            m_lastType = type;
        }

        if(m_codecOVS->Decode(
               subtypeHeader, dataPtr,
               m_sampleHeader->nBodySize,
               *m_holder, m_advisor.Get()))
        {
            (*m_holder)->Header().dtTimeBegin = m_sampleHeader->dtTimeBegin;
            (*m_holder)->Header().dtTimeEnd   = m_sampleHeader->dtTimeEnd;

            m_result = NMMSS::ETRANSFORMED;
        }
        else
        {
            _err_ << "OVS decoder failed" << std::endl;
            m_result = NMMSS::EFAILED;
        }
    }

    void operator()(NMMSS::NMediaType::Video::fccMPEG4::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccMPEG4::ID, AV_CODEC_ID_MPEG4, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccMSMPEG4V3::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccMSMPEG4V3::ID, AV_CODEC_ID_MSMPEG4V3, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccMPEG1::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccMPEG1::ID, AV_CODEC_ID_MPEG2VIDEO, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccMPEG2::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccMPEG2::ID, AV_CODEC_ID_MPEG2VIDEO, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccH263::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccH263::ID, AV_CODEC_ID_H263, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccH264::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccH264::ID, AV_CODEC_ID_H264, subtypeHeader, dataPtr, false);
    }

    void operator()(NMMSS::NMediaType::Video::fccH265::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccH265::ID, AV_CODEC_ID_H265, subtypeHeader, dataPtr, false);
    }

    void operator()(NMMSS::NMediaType::Video::fccVP8::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccVP8::ID, AV_CODEC_ID_VP8, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccVP9::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccVP9::ID, AV_CODEC_ID_VP9, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccJPEG::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccJPEG::ID, AV_CODEC_ID_MJPEG, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccJPEG2000::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccJPEG2000::ID, AV_CODEC_ID_JPEG2000, subtypeHeader, dataPtr);
    }

    void operator()(NMMSS::NMediaType::Video::fccMXPEG::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        DecodeWithFFMPEG(NMMSS::NMediaType::Video::fccMXPEG::ID, AV_CODEC_ID_MXPEG, subtypeHeader, dataPtr);
    }

#ifdef _WIN32
    void operator()(
        NMMSS::NMediaType::Video::fccITV::SubtypeHeader* subtypeHeader,
        uint8_t* dataPtr)
    {
        const ::uint32_t type=NMMSS::NMediaType::Video::fccITV::ID;
        if(!m_codecITV.get() || (m_lastType != type))
        {
            m_codecITV.reset(new NMMSS::CITVDecoder());
            m_lastType = type;
        }

        int lowres = GetWaveletlowres(subtypeHeader->nCodedWidth,
                                      subtypeHeader->nCodedHeight);


        if( m_codecITV->Decode(*m_holder, subtypeHeader, dataPtr, lowres) )
        {
            (*m_holder)->Header().dtTimeBegin = m_sampleHeader->dtTimeBegin;
            (*m_holder)->Header().dtTimeEnd   = m_sampleHeader->dtTimeEnd;

            m_result = NMMSS::ETRANSFORMED;
        }
        else
        {
            m_result = NMMSS::EFAILED;
            _err_ << "Couldn't decode MW-frame" << std::endl;
        }
    }

    void operator()(
        NMMSS::NMediaType::Video::fccWXWL::SubtypeHeader* subtypeHeader,
        uint8_t* dataPtr)
    {
        const ::uint32_t type=NMMSS::NMediaType::Video::fccWXWL::ID;
        if(!m_codecWXWL.get() || (m_lastType != type))
        {
            m_codecWXWL.reset(new NMMSS::CWXWLDecoder());
            m_lastType = type;
        }

        int lowres = GetWaveletlowres(NMMSS::CWXWLDecoder::GetWidth(dataPtr),
                                      NMMSS::CWXWLDecoder::GetHeight(dataPtr));

        if( m_codecWXWL->Decode(*m_holder, dataPtr, lowres) )
        {
            (*m_holder)->Header().dtTimeBegin = m_sampleHeader->dtTimeBegin;
            (*m_holder)->Header().dtTimeEnd   = m_sampleHeader->dtTimeEnd;

            m_result = NMMSS::ETRANSFORMED;
        }
        else
        {
            m_result = NMMSS::EFAILED;
            _err_ << "Couldn't decode WXWL-frame" << std::endl;
        }
    }

    void operator()(
        NMMSS::NMediaType::Video::fccBIMW::SubtypeHeader* subtypeHeader,
        uint8_t* dataPtr)
    {
        const ::uint32_t type=NMMSS::NMediaType::Video::fccBIMW::ID;
        if(!m_codecBIMW.get() || (m_lastType != type))
        {
            m_codecBIMW.reset(new NMMSS::CBIMWDecoder(m_advisor.Dup()));
            m_lastType = type;
        }
        auto result = m_codecBIMW->Decode( subtypeHeader, dataPtr, *m_holder );
        if( result )
        {
            (*m_holder)->Header().dtTimeBegin = m_sampleHeader->dtTimeBegin;
            (*m_holder)->Header().dtTimeEnd   = m_sampleHeader->dtTimeEnd;
            m_result = NMMSS::ETRANSFORMED;
        }
        else
        {
            m_result = NMMSS::EFAILED;
            _err_ << "Couldn't decode BIMW-frame" << std::endl;
        }
    }
#endif //_WIN32

    void operator()(NMMSS::NMediaType::Auxiliary::EndOfStream::SubtypeHeader* subtypeHeader, uint8_t* dataPtr)
    {
        m_result = NMMSS::ETHROUGH;
        if (flushFFMPEGDecoder() && m_result == NMMSS::ETRANSFORMED)
        {
            m_sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFTransformedFromKeySample;
            m_holder->AddSample(NMMSS::PSample(m_sample, NCorbaHelpers::ShareOwnership()));
        }
    }

private:
    bool flushFFMPEGDecoder()
    {
        if (!m_decoderFlushed && m_codecFFMPEG)
        {
            if (m_lastType || !m_sessionWatcher.SessionChanged())
            {
                uint8_t* dataPtr = nullptr;
                uint32_t dataSize = 0;
                while (decodeFrameWithFFMPEG(false, m_lastCodecId, 0, 0, 0, dataPtr, dataSize, 0, true, false));
            }
            m_decoderFlushed = true;
            return true;
        }
        return false;
    }

    void checkFFMPEGDecoder(::uint32_t type, AVCodecID codecId)
    {
        if(!m_codecFFMPEG.get() || (m_lastType != type))
        {
            flushFFMPEGDecoder();

            m_codecFFMPEG.reset(new NMMSS::CFFMPEGVideoDecoder(GET_LOGGER_PTR, m_allocator, m_multithreaded ? m_concurrentCount : 0));
            m_lastType = type;
            m_lastCodecId = codecId;
            m_setDiscontinuity = true;
        }
    }

    bool decodeFrameWithFFMPEG(bool isKeyFrame, AVCodecID codecId, int width, int height, int lowRes, uint8_t*& dataPtr, uint32_t& dataSize, uint64_t pts, bool traceError, bool preroll)
    {
        try
        {
            AVFrame *picture = m_codecFFMPEG->Decode(isKeyFrame, codecId, width, height, lowRes, dataPtr, dataSize, pts, preroll);
            if(picture)
            {
                if (!preroll && CreateSampleFromPicture(picture, isKeyFrame))
                {
                    m_result = NMMSS::ETRANSFORMED;
                }
                return true;
            }
        }
        catch(std::exception& e)
        {
            if (traceError)
            {
                _err_ << "FFMPEG decoder get error: " << e.what() << std::endl;
            }
        }
        return false;
    }

    void DecodeWithFFMPEG(::uint32_t type, AVCodecID codecID, NMMSS::NMediaType::Video::SCodedHeader* subtypeHeader, uint8_t* dataPtr, bool calcLowres = true)
    {
        m_result = NMMSS::EIGNORED;
        checkFFMPEGDecoder(type, codecID);
        m_decoderFlushed = false;
        uint32_t dataSize = m_sampleHeader->nBodySize;

        if (m_sampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFInitData)
        {
            m_codecFFMPEG->SetExtradata(dataPtr, dataSize);
            return;
        }

        bool preroll = m_sampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFPreroll;
        bool isKeyFrame = !(m_sampleHeader->eFlags & (
                NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                NMMSS::SMediaSampleHeader::EFNeedPreviousFrame));

        int lowres = calcLowres ? GetMPEGlowres(subtypeHeader->nCodedWidth, subtypeHeader->nCodedHeight) : 0;
        uint64_t dts = m_sampleHeader->dtTimeBegin;
        for(int frame_num = 0; dataSize > 0; ++frame_num)
        {
            if(frame_num)
            {
                AVRational time_base = m_codecFFMPEG->Context()->time_base;
                if( (0 == time_base.den) || (0 == time_base.num) )
                {
                    time_base.num  = 1;
                    time_base.den = DEFAULT_VIDEO_FRAMERATE;
                }

                ///FIXME: �������� ����� ������ ���� ������ ������ � ������,
                ///����� ������� ������ ����� �������� ������� ������������.
                dts += (time_base.num*1000 + (time_base.den >> 1) ) / time_base.den;
            }
            if (!decodeFrameWithFFMPEG(isKeyFrame, codecID, subtypeHeader->nCodedWidth, subtypeHeader->nCodedHeight, lowres, dataPtr, dataSize, dts, !frame_num, preroll))
            {
                return;
            }
        }
    }

    bool CreateSampleFromPicture(AVFrame* picture, bool decodedFromKeySample)
    {
        NMMSS::PSample sample(NMMSS::CFFmpegAllocator::ExtractSampleFromFrame(picture));
        if(!sample)
        {
            return false;
        }

        if ((AV_PIX_FMT_YUVJ420P == picture->format) ||
            (AV_PIX_FMT_YUV420P == picture->format))
        {
            CreateFrameHeader<NMMSS::NMediaType::Video::fccI420>(picture, sample.Get());
            FillPlanarVideoHeader(picture, sample.Get(), &sample->SubHeader<NMMSS::NMediaType::Video::fccI420>());
        }
        else if ((AV_PIX_FMT_YUVJ422P == picture->format) ||
            (AV_PIX_FMT_YUV422P == picture->format))
        {
            CreateFrameHeader<NMMSS::NMediaType::Video::fccY42B>(picture, sample.Get());
            FillPlanarVideoHeader(picture, sample.Get(), &sample->SubHeader<NMMSS::NMediaType::Video::fccY42B>());
        }
        else if (AV_PIX_FMT_GRAY8 == picture->format)
        {
            CreateFrameHeader<NMMSS::NMediaType::Video::fccGREY>(picture, sample.Get());
        }
        else
        {
            NMMSS::AVFramePtr f = m_codecFFMPEG->convertPixelFormat(AV_PIX_FMT_YUV422P);
            if (!f)
            {
                return false;
            }

            auto s = NMMSS::CFFmpegAllocator::ExtractSampleFromFrame(f.get());
            CreateFrameHeader<NMMSS::NMediaType::Video::fccY42B>(f.get(), s.Get());
            FillPlanarVideoHeader(f.get(), s.Get(), &s->SubHeader<NMMSS::NMediaType::Video::fccY42B>());
            sample = s;
        }

        const uint64_t pts = picture->reordered_opaque;
        sample->Header().dtTimeBegin = pts;
        sample->Header().dtTimeEnd = pts + 20;
        if (m_setDiscontinuity)
        {
            sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
            m_setDiscontinuity = false;
        }
        if (decodedFromKeySample)
        {
            sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFTransformedFromKeySample;
        }

        m_holder->AddSample(sample);

        return true;
    }

    template <typename TMediaType>
    void CreateFrameHeader(const AVFrame* pFrame,
                           NMMSS::ISample* pSample)
    {
        typename TMediaType::SubtypeHeader* h;
        NMMSS::NMediaType::MakeMediaTypeStruct<TMediaType>(
            pSample->GetHeader(), &h);

        NMMSS::NMediaType::Video::SVideoHeader* videoheader = h;
        AVPicture* picture=(AVPicture*)(pFrame);

        videoheader->nWidth  = pFrame->width;
        videoheader->nHeight = pFrame->height;
        videoheader->nOffset = picture->data[0] - pSample->GetBody();
        videoheader->nPitch  = picture->linesize[0];
    }

    void FillPlanarVideoHeader(AVFrame const* pFrame, NMMSS::ISample* pSample,
            NMMSS::NMediaType::Video::SYUVPlanarVideoHeader* pPlanarVideoHeader)
    {
        AVPicture* picture=(AVPicture*)(pFrame);

        pPlanarVideoHeader->nOffsetU = picture->data[1] - pSample->GetBody();
        pPlanarVideoHeader->nPitchU  = picture->linesize[1];
        pPlanarVideoHeader->nOffsetV = picture->data[2] - pSample->GetBody();
        pPlanarVideoHeader->nPitchV  = picture->linesize[2];
    }

    int GetMPEGlowres(int codedWidth, int codedHeight)
    {
        return GetLowRes(codedWidth, codedHeight, 1./4);
    }
    int GetWaveletlowres(int codedWidth, int codedHeight)
    {
        return GetLowRes(codedWidth, codedHeight, 3./8);
    }
    int GetLowRes(int codedWidth, int codedHeight, double ratio)
    {
        int lowres = 0;
        if (0 != m_advisor)
        {
            uint32_t width = 0, height = 0;
            NMMSS::IFrameGeometryAdvisor::EAdviceType adviceType =
                m_advisor->GetAdvice(width, height);
            switch( adviceType )
            {
            case NMMSS::IFrameGeometryAdvisor::ATDontCare:
            case NMMSS::IFrameGeometryAdvisor::ATLargest:
                lowres = 0;
                break;
            case NMMSS::IFrameGeometryAdvisor::ATSmallest:
                lowres = 3;
                break;
            case NMMSS::IFrameGeometryAdvisor::ATSpecific:
                if (NMMSS::CheckCodedDimensionsSanity(codedWidth, codedHeight))
                {
                    double widthResult = (double)width / codedWidth;
                    double heightResult = (double)height / codedHeight;
                    while( widthResult < ratio && heightResult < ratio )
                    {
                        if( ++lowres == 3 )
                            break;
                        ratio /= 2;
                    }
                }
                break;
            default:
                break;
            }
        }
        return lowres;
    }

private:
    NCorbaHelpers::CAutoPtr<NMMSS::IFrameGeometryAdvisor> m_advisor;
    NMMSS::CDeferredAllocSampleHolder* m_holder;
    NMMSS::IAllocator* m_allocator;
    const NMMSS::SMediaSampleHeader* m_sampleHeader;
    NMMSS::ISample* m_sample;
    NMMSS::ETransformResult m_result;
    ::uint32_t m_lastType;
    AVCodecID m_lastCodecId;
    bool m_decoderFlushed;
    bool m_multithreaded;
    bool m_setDiscontinuity;
    NMMSS::SessionWatcher m_sessionWatcher;

    //��������
    std::unique_ptr<NMMSS::COVSDecoder> m_codecOVS;
    std::unique_ptr<NMMSS::CFFMPEGVideoDecoder> m_codecFFMPEG;
#ifdef _WIN32
    std::unique_ptr<NMMSS::CITVDecoder> m_codecITV;
    std::unique_ptr<NMMSS::CWXWLDecoder> m_codecWXWL;
    std::unique_ptr<NMMSS::CBIMWDecoder> m_codecBIMW;
#endif //_WIN32

private:
    static boost::mutex m_concurrentMutex;
    static int m_concurrentCount;
};

boost::mutex CDecoder::m_concurrentMutex;
int CDecoder::m_concurrentCount = 0;

} // anonymous namespace

namespace NMMSS
{
IFilter* CreateStandardVideoDecoderPullFilter(DECLARE_LOGGER_ARG, 
    IFrameGeometryAdvisor* pAdvisor, bool multithreaded)
{
    return new CPullFilterImpl<CDecoder, true>(
        GET_LOGGER_PTR,
        SAllocatorRequirements(0, 0, 16),
        SAllocatorRequirements(0, 0, 16),
        new CDecoder(GET_LOGGER_PTR, pAdvisor, multithreaded));
}

}

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER
