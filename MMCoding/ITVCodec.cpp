#include "ITVCodec.h"
#include <WaveLib3dll.h>

NMMSS::CITVDecoder::CITVDecoder() :
         mpContext(0)
    ,    m_width(0)
    ,    m_height(0)
    ,    m_frameFormat(-1)
{
}

NMMSS::CITVDecoder::~CITVDecoder()
{
    if(mpContext) DLL_VideoClose(mpContext);
}

bool NMMSS::CITVDecoder::InitDecoder(int width, int height, int frameFormat)
{
    m_width = width; m_height = height; m_frameFormat = frameFormat;

    TStreamHead head;
    memset(&head,0,sizeof(head));
    head.Width  = m_width;
    head.Height = m_height;
    head.Stride = m_width;

    m_levels = 4;
    if( (m_width >= 800) && (m_height >= 600) )
    {
        m_levels = 6;
    } else if( (m_width >= 640) && (m_height >= 320) )
    {
        m_levels = 5;
    }

    head.Flags = m_levels;
    head.Sign = SET_VERS(0x20);

    if(F_YUV420 == m_frameFormat)
    {
        head.Flags |= F_YUV420;
    }else{
        head.Flags |= F_YUV422;
    }

    if(height <= width/2) head.Flags |= F_SPLIT2;

    mpContext = DLL_VideoOpen(&head);

    if(!mpContext) return false;

    return true;
}

bool NMMSS::CITVDecoder::Decode(
        NMMSS::CDeferredAllocSampleHolder& holder,
        NMMSS::NMediaType::Video::fccITV::SubtypeHeader* pHeader,
        uint8_t* pData,
        int scalePowerOfTwo)
{
    int width  = pHeader->nCodedWidth;
    int height = pHeader->nCodedHeight;

    TFrameHead* frameHeader = reinterpret_cast<TFrameHead*>(pData);

    int frameFormat = -1;
    if(frameHeader->Flags & F_MONO)
    {
        frameFormat = F_MONO; //GREY
    }else{
        int version = pData[frameHeader->Size + 3] - '0';
        switch(version)
        {
            case 110:
                frameFormat = F_YUV420; //YUV420
            break;
            case 111:
                frameFormat = F_YUV422; //YUV422
            break;
            default: return false;
        }
    }

    if( !mpContext || (width != m_width) || (height != m_height ) ||
        (frameFormat != m_frameFormat) )
    {
        if(mpContext) DLL_VideoClose(mpContext);
        mpContext = 0;

        if(frameHeader->Flags & F_TYPE_P) return false;

        if( !InitDecoder(width, height, frameFormat) ) return false;
    }

    int need_levels = m_levels + 1 - scalePowerOfTwo;
    if(height <= width/2) need_levels += 1;

    width  /= (1 << scalePowerOfTwo);
    height /= (1 << scalePowerOfTwo);

    uint32_t maxBodySize = 2*width*height;
    if(!holder.Alloc(maxBodySize))
    {
        throw std::runtime_error("Allocation failed");
    }

    unsigned char* pPlanes[3];
    pPlanes[0] = holder->GetBody();
    pPlanes[1] = pPlanes[0] + width*height;
    if(m_frameFormat == F_YUV420)
    {
        pPlanes[2] = pPlanes[1] + (width*height)/4;
    }else{
        pPlanes[2] = pPlanes[1] + (width*height)/2;
    }

    int res = DLL_VideoDecode(mpContext, (UInt32**)pPlanes,
        (UInt32*)(pData), 0, need_levels);
    if(res <= 0) return false;

    NMMSS::SMediaSampleHeader& header = holder->Header();
    header.nBodySize = width*height;

    switch(m_frameFormat)
    {
        case F_MONO:
        {
            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(
                holder->GetHeader(), &subheader);
            subheader->nWidth   = width;
            subheader->nHeight  = height;
            subheader->nOffset  = 0;
            subheader->nPitch   = width;
        }
        break;

        case F_YUV420:
        {
            NMMSS::NMediaType::Video::fccI420::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(
                holder->GetHeader(), &subheader);
            subheader->nWidth   = width;
            subheader->nHeight  = height;
            subheader->nOffset  = 0;
            subheader->nPitch   = width;
            subheader->nOffsetU = width*height;
            subheader->nPitchU  = width/2;
            subheader->nOffsetV = subheader->nOffsetU + width*height/4;
            subheader->nPitchV  = width/2;

            header.nBodySize += width*height/2;
        }
        break;

        case F_YUV422:
        {
            NMMSS::NMediaType::Video::fccY42B::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccY42B>(
                holder->GetHeader(), &subheader);
            subheader->nWidth   = width;
            subheader->nHeight  = height;
            subheader->nOffset  = 0;
            subheader->nPitch   = width;
            subheader->nOffsetU = width*height;
            subheader->nPitchU  = width/2;
            subheader->nOffsetV = subheader->nOffsetU + width*height/2;
            subheader->nPitchV  = width/2;

            header.nBodySize += width*height;
        }
        break;

        default: return false;
    }

    return true;
}
