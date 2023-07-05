#include "WXWLCodec.h"
#include "../MediaType.h"
#include <WaveLib6dll.h>
#include <string.h>

using namespace WaveLib_6;

NMMSS::CWXWLDecoder::CWXWLDecoder() : m_pCodecCtx(0)
{
}

NMMSS::CWXWLDecoder::~CWXWLDecoder()
{
    if(m_pCodecCtx)
    {
        DLL_WXWLCodecClose(m_pCodecCtx);
    }
}

bool NMMSS::CWXWLDecoder::Decode(
    NMMSS::CDeferredAllocSampleHolder& holder,
    uint8_t* pData,
    int scalePowerOfTwo)
{
    const int F_GREY = 0x04000000;

    TFrame *pFrameHeader = (TFrame*)pData;

    if( !m_pCodecCtx )
    {
        Int32 err;

        m_pCodecCtx =
            DLL_WXWLCodecOpenEx(pFrameHeader, pFrameHeader->Width, &err);

        if(!m_pCodecCtx)
        {
            auto msg = "Couldn't init decoder. Error code: " + std::to_string(err);
            
            if (err == 0)
                msg += " (probably there was disabled support for current format "
                    + std::to_string(pFrameHeader->Flags & F_FORMAT) + ")";

            throw std::runtime_error(msg.c_str());
        }
    }
    
    int isGrey = pFrameHeader->Flags & F_GREY;


    int width  = pFrameHeader->Width,
        height = pFrameHeader->Height,
        pitch  = pFrameHeader->Width;

    width  /= (1 << scalePowerOfTwo);
    height /= (1 << scalePowerOfTwo);


    int decodedSize = 0;
    uint32_t *pYUV[3];

    decodedSize = (pitch*height*3)/2;
    if(!holder.Alloc(decodedSize))
    {
        throw std::runtime_error("Couldn't alloc frame");
    }
    pYUV[0] = (uint32_t*)((uint8_t*)holder->GetBody());
    pYUV[1] = (uint32_t*)((uint8_t*)pYUV[0] + pitch*height);
    pYUV[2] = (uint32_t*)((uint8_t*)pYUV[1] + pitch*height/4);


    holder->Header().eFlags = 0;

    TDecPar   decoderParams;
    DLL_WXWLSetDecDefault(&decoderParams);
    decoderParams.nScale = scalePowerOfTwo;
    long res = DLL_WXWLCodecDecode(m_pCodecCtx, (UInt32**)pYUV,
                                   (UInt32*)pData, &decoderParams);

    if(!res) return false;

    /* Defragment picture (remove pitches) */
    if(scalePowerOfTwo > 0)
    {
        uint8_t *pTarget = (uint8_t*)holder->GetBody(),
                *pSource = pTarget;

        /* Defragment Y-plane */
        for(int i = 0; i < height; ++i)
        {
            memcpy(pTarget, pSource, width);

            pTarget += width;
            pSource += pitch;
        }

        if (!isGrey)
        {
            /* Defragment UV-plane */
            for(int i = 0; i < height; ++i)
            {
                memcpy(pTarget, pSource, width/2);

                pTarget += width/2;
                pSource += pitch/2;
            }
        }

    }

    if (!isGrey)
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

        holder->Header().nBodySize = (pitch*height*3)/2;
    }
    else
    {
        NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
        NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(
            holder->GetHeader(), &subheader);
        subheader->nWidth   = width;
        subheader->nHeight  = height;
        subheader->nOffset  = 0;
        subheader->nPitch   = width;

        holder->Header().nBodySize = pitch*height;
    }

    return true;
}

uint16_t NMMSS::CWXWLDecoder::GetWidth(uint8_t* pFrameHeader)
{
    return reinterpret_cast<TFrame*>(pFrameHeader)->Width;
}

uint16_t NMMSS::CWXWLDecoder::GetHeight(uint8_t* pFrameHeader)
{
    return reinterpret_cast<TFrame*>(pFrameHeader)->Height;
}
