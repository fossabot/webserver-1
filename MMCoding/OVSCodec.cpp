#include "OVSCodec.h"

using namespace NMMSS;

bool COVSDecoder::Decode(
    NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader* pHeader,
    uint8_t* pData,
    uint32_t nDataSize,
    NMMSS::CDeferredAllocSampleHolder& holder,
    NMMSS::IFrameGeometryAdvisor* pAdvisor)
{
    int nWidth = 0, nHeight = 0;

    NOVSCodec::EStatus res;
    NOVSCodec::SPackedFragment pf;

    pf.pComprSrc = pData;
    pf.nErrors = 0;
    pf.nSegmentSize = pHeader->nSegmentSize;
    pf.pErrorMask = 0;

    if(pHeader->nColour)
    {
        pf.nFragmentSize = pHeader->nUVOffset;
    }
    else
    {
        pf.nFragmentSize = nDataSize;
    }

    NOVSCodec::IPlaneParamsParser* const parser = NOVSCodec::GetOVSPlaneParamsParser();
    NOVSCodec::SPlaneParams pp;
    res = parser->GetPlaneParams(&pf, 1, pp);
    if(NOVSCodec::SUCCESS != res)
    {
        return false;
    }

    if(!m_bHaveSeenKeyFrame && pp.bIsUseDelta && !pp.bIsKeyFrame)
    {
        return false;
    }
    else if(!m_bHaveSeenKeyFrame && pp.bIsUseDelta && pp.bIsKeyFrame)
    {
        m_bHaveSeenKeyFrame = true;
    }

    NOVSCodec::SFrameGeometry theGeomArray[4];
    uint32_t nGeomArraySize = 0;
    res = parser->GetFrameGeometry(pp.MaxFrameGeometry, theGeomArray, nGeomArraySize);
    if(NOVSCodec::SUCCESS != res)
    {
        return false;
    }

    uint32_t nGeomArrayIndex = 0;
    if(pAdvisor)
    {
        uint32_t w = 0, h = 0;
        NMMSS::IFrameGeometryAdvisor::EAdviceType at(pAdvisor->GetAdvice(w, h));
        if(NMMSS::IFrameGeometryAdvisor::ATLargest == at)
        {
            nGeomArrayIndex = 0;
        }
        else if(NMMSS::IFrameGeometryAdvisor::ATSmallest == at)
        {
            nGeomArrayIndex = nGeomArraySize - 1;
        }
        else if(NMMSS::IFrameGeometryAdvisor::ATSpecific == at)
        {
            int nBestDiff = 1024*1024;
            for(int k = 0; k < int(nGeomArraySize); ++k)
            {
                int diff = (int(theGeomArray[k].nWidth) - int(w));
                if( (diff >= 0) && (diff < nBestDiff) )
                {
                    nBestDiff = diff;
                    nGeomArrayIndex = k;
                }
            }
        }
    }

    nGeomArrayIndex = std::min(nGeomArrayIndex, nGeomArraySize - 1);

    nWidth  = theGeomArray[nGeomArrayIndex].nWidth;
    nHeight = theGeomArray[nGeomArrayIndex].nHeight;

    m_pContextY->SetParameters(theGeomArray[nGeomArrayIndex].eType, false);
    if(NOVSCodec::SUCCESS != res)
    {
        return false;
    }


    uint32_t bodySizeY  = nWidth * nHeight;
    uint32_t bodySizeUV = pHeader->nColour * (bodySizeY >> 1);

    if( !holder.Alloc(bodySizeY + bodySizeUV) )
    {
        return false;
    }

    holder->Header().nBodySize = bodySizeY + bodySizeUV;

    res = m_pDecoder->Decode(&pf, 1, holder->GetBody(), bodySizeY,
                             m_pContextY.get());
    if(NOVSCodec::SUCCESS != res)
    {
        return false;
    }

    if(pHeader->nColour)
    {
        pf.pComprSrc = pData + pHeader->nUVOffset;
        pf.nErrors = 0;
        pf.nSegmentSize = pHeader->nSegmentSize;
        pf.pErrorMask = 0;
        pf.nFragmentSize = nDataSize - pHeader->nUVOffset;

        m_pContextUV->SetParameters(theGeomArray[nGeomArrayIndex].eType, false);
        res = m_pDecoder->Decode(&pf, 1, holder->GetBody() + bodySizeY,
                                 bodySizeUV, m_pContextUV.get());
        if(NOVSCodec::SUCCESS != res)
        {
            return false;
        }
    }


    switch(pHeader->nColour)
    {
        case 0:
        {
            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* pOutHeader =0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(
                holder->GetHeader(), &pOutHeader);
            pOutHeader->nOffset = 0;
            pOutHeader->nWidth  = nWidth -
                (pHeader->nBorderPixSize >> nGeomArrayIndex);
            pOutHeader->nPitch  = nWidth;
            pOutHeader->nHeight = nHeight;
        }
        break;

        case 1:
        {
            NMMSS::NMediaType::Video::fccI420::SubtypeHeader* pOutHeader=0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(
                holder->GetHeader(), &pOutHeader);
            pOutHeader->nOffset  = 0;
            pOutHeader->nWidth   = nWidth -
                (pHeader->nBorderPixSize >> nGeomArrayIndex);
            pOutHeader->nPitch   = nWidth;
            pOutHeader->nHeight  = nHeight;
            pOutHeader->nOffsetU = bodySizeY;
            pOutHeader->nOffsetV = pOutHeader->nOffsetU + bodySizeUV/2;
            pOutHeader->nPitchU  = pOutHeader->nPitchV = nWidth/2;
        }
        break;

        case 2:
        {
            NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* pOutHeader=0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccY42B>(
                holder->GetHeader(), &pOutHeader);
            pOutHeader->nOffset  = 0;
            pOutHeader->nWidth   = nWidth -
                (pHeader->nBorderPixSize >> nGeomArrayIndex);
            pOutHeader->nPitch   = nWidth;
            pOutHeader->nHeight  = nHeight;
            pOutHeader->nOffsetU = bodySizeY;
            pOutHeader->nOffsetV = pOutHeader->nOffsetU + bodySizeUV/2;
            pOutHeader->nPitchU  = pOutHeader->nPitchV = nWidth/2;
        }
        break;

    default:;
    }

    return true;
}

class COVCodecQualityDecimationTransform
{
    class Helper
    {
        COVCodecQualityDecimationTransform * m_transform;
        NMMSS::ISample * m_inSample;
        NMMSS::CDeferredAllocSampleHolder & m_holder;
        NMMSS::ETransformResult m_result;
    public:
        Helper(
            COVCodecQualityDecimationTransform * transform,
            NMMSS::ISample * sample,
            NMMSS::CDeferredAllocSampleHolder & holder )
        : m_transform( transform )
        , m_inSample( sample )
        , m_holder( holder )
        {
        }
        template< class TMediaTypeHeader >
        void operator()( TMediaTypeHeader * header, uint8_t * dataPtr )
        {
            m_result = NMMSS::EIGNORED;
        }
        void operator()( NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader * inHeader, uint8_t * dataPtr )
        {
            m_result = NMMSS::EFAILED;

            ::uint32_t const bodySize = m_inSample->Header().nBodySize;
            unsigned const fragmentIndex = m_transform->m_fragmentIndex;
            size_t offsetY = 0, offsetUV = 0;
            uint32_t sizeY = 0, sizeUV = 0;

            NOVSCodec::SPackedFragment pf;
            pf.nSegmentSize = inHeader->nSegmentSize;
            pf.nErrors = 0;
            pf.pErrorMask = 0;

            pf.pComprSrc = dataPtr;
            pf.nFragmentSize = inHeader->nColour ? inHeader->nUVOffset : bodySize;
            if( !GetFragmentOffsetAndSize( offsetY, sizeY, pf, fragmentIndex ) )
                return;

            if( inHeader->nColour )
            {
                pf.pComprSrc = dataPtr + inHeader->nUVOffset;
                pf.nFragmentSize = bodySize - inHeader->nUVOffset;
                if( !GetFragmentOffsetAndSize( offsetUV, sizeUV, pf, fragmentIndex ) )
                    return;
            }

            if( !m_holder.Alloc( sizeY + sizeUV ) )
                return;

            NMMSS::PSample outSample = m_holder.GetSample();

            memcpy( outSample->GetHeader(), m_inSample->GetHeader(),
                sizeof(NMMSS::SMediaSampleHeader) + sizeof(NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader) );

            outSample->Header().nBodySize = sizeY + sizeUV;

            NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader & outHeader = outSample->SubHeader<NMMSS::NMediaType::Video::fccOVS2>();
            outHeader.nFragmentsPresence = m_transform->m_fragmentPresenceY | m_transform->m_fragmentPresenceUV;
            outHeader.nUVOffset = sizeY;

            memcpy( outSample->GetBody(),         dataPtr + offsetY,                        sizeY  );
            memcpy( outSample->GetBody() + sizeY, dataPtr + inHeader->nUVOffset + offsetUV, sizeUV );

            m_result = NMMSS::ETRANSFORMED;
        }
        bool GetFragmentOffsetAndSize( size_t & off, uint32_t & sz, NOVSCodec::SPackedFragment const& pf, unsigned fragmentIndex )
        {
            size_t offset[3];
            uint32_t size[3];

            if( m_transform->m_parser->GetPlaneStructure( pf, offset, size ) != NOVSCodec::SUCCESS )
                return false;
            
            off = offset[fragmentIndex];
            sz = size[fragmentIndex];
            return true;
        }
        NMMSS::ETransformResult GetResult() const
        {
            return m_result;
        }
    };

public:
    COVCodecQualityDecimationTransform( NOVSCodec::EFrameType format )
    : m_parser( NOVSCodec::GetOVSPlaneParamsParser() )
    {
        switch( format )
        {
        case NOVSCodec::QCIF:
            m_fragmentIndex = 0;
            m_fragmentPresenceY  = NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_Y_QCIF;
            m_fragmentPresenceUV = NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_UV_QCIF;
            break;
        case NOVSCodec::CIF1:
            m_fragmentIndex = 1;
            m_fragmentPresenceY  = NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_Y_CIF1;
            m_fragmentPresenceUV = NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_UV_CIF1;
            break;
        case NOVSCodec::CIF2:
        case NOVSCodec::CIF4:
            m_fragmentIndex = 2;
            m_fragmentPresenceY  = NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_Y_CIF42;
            m_fragmentPresenceUV = NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader::FP_UV_CIF42;
            break;
        default:
            throw std::logic_error("Unsupported frame format");
        }
    }
    NMMSS::ETransformResult operator()( NMMSS::ISample * sample, NMMSS::CDeferredAllocSampleHolder & holder )
    {
        Helper helper( this, sample, holder );
        NMMSS::NMediaType::ApplyMediaTypeVisitor( sample, helper );
        return helper.GetResult();
    }
private:
    NOVSCodec::IPlaneParamsParser * const m_parser;
    unsigned m_fragmentIndex;
    ::uint8_t m_fragmentPresenceY;
    ::uint8_t m_fragmentPresenceUV;
};
