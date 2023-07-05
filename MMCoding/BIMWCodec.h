#ifndef MMCODING_BUILTINMOTIONWAVELET_H
#define MMCODING_BUILTINMOTIONWAVELET_H

// CRAP: Built-in motion wavelet codec uses WaveLib3 just as CITVDecoder.
//       Their code should be united and moved to CodecPack (IPINT-19094).

#include "Transforms.h"
#include "../MediaType.h"
#include "../FilterImpl.h"
#include <WaveLib3dll.h>

namespace NMMSS {

const int WaveLibVersion = 0x30;
const int WaveLibLevels = 4;
const int WaveLibSmooth = 0;
const int WaveLibTresh = 6;

class CWaveLibCodec
{
public:
    CWaveLibCodec(CWaveLibCodec const&) =delete;
    CWaveLibCodec& operator=(CWaveLibCodec const&) =delete;

    CWaveLibCodec()
        : m_codec( nullptr )
    {}
    CWaveLibCodec(CWaveLibCodec && other)
        : m_codec( other.m_codec )
    {
        other.m_codec = nullptr;
    }
    ~CWaveLibCodec()
    {
        Close();
    }
    CWaveLibCodec& operator=(CWaveLibCodec && other)
    {
        Close();
        std::swap( m_codec, other.m_codec );
    }
    void Open(TStreamHead* StreamH)
    {
        Close();
        m_codec = DLL_VideoOpen(StreamH);
    }
    void Close()
    {
        if( m_codec )
        {
            DLL_VideoClose(m_codec);
            m_codec = nullptr;
        }
    }
    long Encode(UInt32** pYUV, UInt32* pZip, Int Bpp32, Int Tresh)
    {
        return DLL_VideoEncode(m_codec, pYUV, pZip, Bpp32, Tresh);
    }
    long Decode(UInt32** pYUV, UInt32* pZip, UInt nSmooth, UInt nMulti)
    {
        return DLL_VideoDecode(m_codec, pYUV, pZip, nSmooth, nMulti);
    }
    long Truncate(UInt32* pZip, UInt nMulti, UInt32* pKs, UInt32* Err)
    {
        // Cake is a lie: pZip will contain result, so it isn't const.
        return DLL_VideoTruncate(m_codec, pZip, nMulti, pKs, Err);
    }
    explicit operator bool() const
    {
        return (m_codec != nullptr);
    }
    bool operator!() const
    {
        return !operator bool();
    }
    static UInt GetLevels(UInt flags)
    {
        auto levels = flags & F_LEVELS;
        if (levels < 2)
            levels = 2;
        return levels + (flags&F_SPLIT2?1:0);
    }
private:
    void* m_codec;
};

class CBIMWDecoder
{
public:
    CBIMWDecoder( NMMSS::IFrameGeometryAdvisor * advisor )
    : m_advisor( advisor )
    {
        memset(&m_streamHead, 0, sizeof(m_streamHead));
        m_streamHead.Sign = SET_VERS(WaveLibVersion);
    }
    NMMSS::ETransformResult operator()( NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder )
    {
        if( !NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Video::fccBIMW>(&in->Header()) )
            return NMMSS::EFAILED;

        auto& subHeader = in->SubHeader<NMMSS::NMediaType::Video::fccBIMW>();
        if( !Decode(&subHeader, in->GetBody(), holder) )
            return NMMSS::EFAILED;

        holder->Header().dtTimeBegin = in->Header().dtTimeBegin;
        holder->Header().dtTimeEnd = in->Header().dtTimeEnd;

        return NMMSS::ETRANSFORMED;
    }
    bool Decode( NMMSS::NMediaType::Video::fccBIMW::SubtypeHeader* head,
        uint8_t* body, NMMSS::CDeferredAllocSampleHolder& holder )
    {
        auto const flags = head->eStreamFlags;
        auto const stride = head->nOriginalStride;
        auto const width = head->nCodedWidth;
        auto const height = head->nCodedHeight;

        if( m_streamHead.Flags != flags ||
            m_streamHead.Stride != stride ||
            m_streamHead.Width != width ||
            m_streamHead.Height != height )
        {
            m_streamHead.Flags = flags;
            m_streamHead.Stride = stride;
            m_streamHead.Width = width;
            m_streamHead.Height = height;
            m_codec.Close();
        }
        if( !m_codec )
        {
            m_codec.Open(&m_streamHead);
            if( !m_codec )
                return false;
        }

        UInt32* yuv[3] = {};

        auto const levels = CWaveLibCodec::GetLevels(flags);
        auto const scale = (flags&F_SPLIT2) ? GetScale(width,height) : 0;
        auto const scaledWidth = width >> scale;
        auto const scaledHeight = height >> scale;

        switch( flags & F_FORMAT )
        {
        case F_YUV422:
        {
            auto size = stride * height * 2;
            if( !Alloc<NMMSS::NMediaType::Video::fccY42B>(holder,size) )
                return false;
            auto & subHeader = holder->SubHeader<NMMSS::NMediaType::Video::fccY42B>();
            subHeader.nWidth = scaledWidth;
            subHeader.nHeight = scaledHeight;
            subHeader.nOffset = 0;
            subHeader.nOffsetU = stride*height;
            subHeader.nOffsetV = stride*height * 3/2;
            subHeader.nPitch = stride;
            subHeader.nPitchU = stride/2;
            subHeader.nPitchV = stride/2;
            yuv[0] = reinterpret_cast<UInt32*>( holder->GetBody() );
            yuv[1] = reinterpret_cast<UInt32*>( holder->GetBody() + subHeader.nOffsetU );
            yuv[2] = reinterpret_cast<UInt32*>( holder->GetBody() + subHeader.nOffsetV );
        }   break;
        case F_YUV420:
        case F_YUV42X:
        case F_CCD:
        {
            auto size = stride * height * 3/2;
            if( !Alloc<NMMSS::NMediaType::Video::fccI420>(holder,size) )
                return false;
            auto & subHeader = holder->SubHeader<NMMSS::NMediaType::Video::fccI420>();
            subHeader.nWidth = scaledWidth;
            subHeader.nHeight = scaledHeight;
            subHeader.nOffset = 0;
            subHeader.nOffsetU = stride*height;
            subHeader.nOffsetV = stride*height * 5/4;
            subHeader.nPitch = stride;
            subHeader.nPitchU = stride/2;
            subHeader.nPitchV = stride/2;
            yuv[0] = reinterpret_cast<UInt32*>( holder->GetBody() );
            yuv[1] = reinterpret_cast<UInt32*>( holder->GetBody() + subHeader.nOffsetU );
            yuv[2] = reinterpret_cast<UInt32*>( holder->GetBody() + subHeader.nOffsetV );
        }   break;
        case F_YUV400:
        {
            auto size = stride * height;
            if( !Alloc<NMMSS::NMediaType::Video::fccGREY>(holder,size) )
                return false;
            auto & subHeader = holder->SubHeader<NMMSS::NMediaType::Video::fccGREY>();
            subHeader.nWidth = scaledWidth;
            subHeader.nHeight = scaledHeight;
            subHeader.nOffset = 0;
            subHeader.nPitch = stride;
            yuv[0] = reinterpret_cast<UInt32*>( holder->GetBody() );
        }   break;
        default:
            return false;
        }

        auto zip = reinterpret_cast<UInt32*>( body );
        auto synt = (scale == 0) ? 0 : levels - scale + 1;
        auto result = m_codec.Decode(yuv, zip, WaveLibSmooth, synt);

        return (result != 0);
    }
private:
    UInt GetScale( uint32_t width, uint32_t height )
    {
        // WaveLib 3.5 partial decode doesn't scale proportional
        // If F_SPLIT2 flag specified it's possible to take a qurter
        // of image with right ratio, so scale should be either '0' or '1'
        if( !m_advisor )
            return 0;
        uint32_t w=0, h=0;
        auto advice = m_advisor->GetAdvice(w,h);
        switch( advice )
        {
        default:
        case NMMSS::IFrameGeometryAdvisor::ATDontCare:
        case NMMSS::IFrameGeometryAdvisor::ATLargest:
            return 0;
        case NMMSS::IFrameGeometryAdvisor::ATSmallest:
            return 1;
        case NMMSS::IFrameGeometryAdvisor::ATSpecific:
            auto ratio = std::max( (double)w/width, (double)h/height );
            return (ratio < 3./8) ? 1 : 0;
        }
    }
    template< class TMediaType >
    bool Alloc( NMMSS::CDeferredAllocSampleHolder & holder, uint32_t size )
    {
        if( !holder.Alloc(size) )
            return false;
        NMMSS::NMediaType::MakeMediaTypeStruct<TMediaType>(holder->GetHeader());
        holder->Header().nBodySize = size;
        return true;
    }
private:
    NMMSS::PFrameGeometryAdvisor m_advisor;
    TStreamHead m_streamHead;
    CWaveLibCodec m_codec;
    NMMSS::ETransformResult m_result;
};


class CBIWMDownscale
{
public:
    CBIWMDownscale( UInt16 w, UInt16 h )
        : m_width( w )
        , m_height( h )
    {
        memset(&m_streamHead, 0, sizeof(m_streamHead));
        m_streamHead.Sign = SET_VERS(WaveLibVersion);
    }
    CBIWMDownscale( CBIWMDownscale const& other )
        : CBIWMDownscale( other.m_width, other.m_height )
    {}
    CBIWMDownscale& operator=( CBIWMDownscale const& other )
    {
        Tweak( other.m_width, other.m_height );
    }
    void Tweak( UInt16 w, UInt16 h )
    {
        m_width = w;
        m_height = h;
    }
    void GetTweak( UInt16 * w, UInt16 * h )
    {
        *w = m_width;
        *h = m_height;
    }
    NMMSS::ETransformResult operator()( NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder )
    {
        if( !NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Video::fccBIMW>(&in->Header()) )
            return NMMSS::EFAILED;

        auto& subHeader = in->SubHeader<NMMSS::NMediaType::Video::fccBIMW>();

        if( m_streamHead.Flags != subHeader.eStreamFlags ||
            m_streamHead.Stride != subHeader.nOriginalStride ||
            m_streamHead.Width != subHeader.nCodedWidth ||
            m_streamHead.Height != subHeader.nCodedHeight )
        {
            m_streamHead.Flags = subHeader.eStreamFlags;
            m_streamHead.Stride = subHeader.nOriginalStride;
            m_streamHead.Width = subHeader.nCodedWidth;
            m_streamHead.Height = subHeader.nCodedHeight;
            m_codec.Close();
        }
        if( !m_codec )
        {
            m_codec.Open(&m_streamHead);
            if( !m_codec )
                return NMMSS::EFAILED;
        }

        auto const levels = CWaveLibCodec::GetLevels(m_streamHead.Flags);
        auto const scale = CalculateScale(subHeader.nCodedWidth, subHeader.nCodedHeight);
        auto const trunc = (scale == 0) ? 0 : levels - std::min(levels, scale) + 1;
        
        if( trunc == 0 )
            return NMMSS::ETHROUGH;

        if( !holder.Alloc(in->Header().nBodySize) )
            return NMMSS::EFAILED;

        memcpy( holder->GetHeader(), in->GetHeader(), in->Header().nHeaderSize );
        memcpy( holder->GetBody(), in->GetBody(), in->Header().nBodySize );

        UInt32 * zip = reinterpret_cast<UInt32*>(holder->GetBody());
        UInt32 err = 0;
        auto resultingSize = m_codec.Truncate( zip, trunc, nullptr, &err );
        if( resultingSize == 0 || err != 0 )
            return NMMSS::EFAILED;

        holder->Header().nBodySize = resultingSize;
        // TODO: set new levels in sub header?

        return NMMSS::ETRANSFORMED;
    }
private:
    UInt CalculateScale( UInt16 w, UInt16 h ) const
    {
        UInt scale = 0;
        while ((m_width != 0 && m_width < w/2)
            || (m_height != 0 && m_height < h/2))
        {
            ++scale;
            w /= 2;
            h /= 2;
        }
        return scale;
    }
private:
    TStreamHead m_streamHead;
    CWaveLibCodec m_codec;
    UInt16 m_width;
    UInt16 m_height;
};

} // namespace NMMSS

#endif // MMCODING_BUILTINMOTIONWAVELET_H
