#include "Transforms.h"
#include "../FilterImpl.h"

namespace
{
    class CMaskTransformer
    {
        const float EPSILON  = 0.00001F;

        DECLARE_LOGGER_HOLDER;

    public:
        CMaskTransformer(DECLARE_LOGGER_ARG, float x1 = 0.0F, float y1 = 0.0F, float x2 = 0.0F, float y2 = 0.0F)
            : m_xFactor(x1)
            , m_yFactor(y1)
            , m_widthFactor(x2 - x1)
            , m_heightFactor(y2 - y1)
            , m_initialized(false)
        {
            INIT_LOGGER_HOLDER;
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            if (EPSILON > m_widthFactor || EPSILON > m_heightFactor || 1.0F <= m_xFactor || 1.0F <= m_yFactor)
                return NMMSS::ETHROUGH;

            m_maskSample = holder.GetAllocator()->Alloc(sample->Header().nBodySize);
            m_maskSample->Header().nBodySize = sample->Header().nBodySize;
            
            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_maskSample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_maskSample->Header().dtTimeEnd = sample->Header().dtTimeEnd;
                    
                    holder.AddSample(m_maskSample);
                }
            }
            catch (std::exception& e)
            {
                _log_ << e.what() << std::endl;
                return NMMSS::EFAILED;
            }
            return m_result;
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* subtypeHeader, uint8_t* dataPtr)
        {
            m_result = NMMSS::ETHROUGH;
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            InitMaskArea(header->nWidth, header->nHeight);

            uint8_t* maskBody = m_maskSample->GetBody();
            memcpy(maskBody, body, m_maskSample->Header().nBodySize);
            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(m_maskSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader));

            uint32_t shiftX = m_wishedPosX & -2;
            uint32_t shiftY = m_wishedPosY & -2;

            uint32_t maskWidth = subheader->nWidth - shiftX;
            uint32_t maskHeight = subheader->nHeight - shiftY;

            maskWidth = maskWidth > m_wishedWidth ? (m_wishedWidth & -2) : (maskWidth & -2);
            maskHeight = maskHeight > m_wishedHeight ? (m_wishedHeight & -2) : (maskHeight & -2);
            uint32_t maskPitch = maskWidth;

            DoMaskArea(maskBody + subheader->nOffset, subheader->nPitch, shiftX, shiftY, maskPitch, maskHeight, 0);

            m_result = NMMSS::ETRANSFORMED;
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            DoConvert<NMMSS::NMediaType::Video::fccI420>(header, body, 2, 2);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            DoConvert<NMMSS::NMediaType::Video::fccY42B>(header, body, 2, 1);
        }

    private:

        template <typename THeader>
        void DoConvert(typename THeader::SubtypeHeader* header, uint8_t* body, uint8_t uvWidthDiminish, uint8_t uvHeightDiminish)
        {
            InitMaskArea(header->nWidth, header->nHeight);

            uint8_t* maskBody = m_maskSample->GetBody();
            memcpy(maskBody, body, m_maskSample->Header().nBodySize);
            typename THeader::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_maskSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(typename THeader::SubtypeHeader));

            uint32_t shiftX = m_wishedPosX & -2;
            uint32_t shiftY = m_wishedPosY & -2;

            uint32_t maskYWidth = subheader->nWidth - shiftX;
            uint32_t maskYHeight = subheader->nHeight - shiftY;

            maskYWidth = maskYWidth > m_wishedWidth ? (m_wishedWidth & -2) : (maskYWidth & -2);
            maskYHeight = maskYHeight > m_wishedHeight ? (m_wishedHeight & -2) : (maskYHeight & -2);
            uint32_t maskYPitch = maskYWidth;
            
            uint32_t maskUVWidth = maskYWidth / uvWidthDiminish;
            uint32_t maskUVHeight = maskYHeight / uvHeightDiminish;
            uint32_t maskUVPitch = maskUVWidth;

            DoMaskArea(maskBody + subheader->nOffset, subheader->nPitch, shiftX, shiftY, maskYPitch, maskYHeight, 0);
            DoMaskArea(maskBody + subheader->nOffsetU, subheader->nPitchU, shiftX / uvWidthDiminish, shiftY / uvHeightDiminish, maskUVPitch, maskUVHeight, 128);
            DoMaskArea(maskBody + subheader->nOffsetV, subheader->nPitchV, shiftX / uvWidthDiminish, shiftY / uvHeightDiminish, maskUVPitch, maskUVHeight, 128);

            m_result = NMMSS::ETRANSFORMED;
        }

        void DoMaskArea(uint8_t* pSrc, uint32_t srcPitch, uint32_t maskShiftX, uint32_t maskShiftY, uint32_t maskPitch, uint32_t maskHeight, unsigned char maskValue)
        {
            uint8_t* pSource = pSrc;
            for (uint32_t y = maskShiftY; y < maskShiftY + maskHeight; ++y)
            {
                pSource = pSrc + y * srcPitch + maskShiftX;
                memset(pSource, maskValue, maskPitch);
            }
        }

        void InitMaskArea(uint32_t srcWidth, uint32_t srcHeight)
        {
            if (m_initialized)
                return;

            m_wishedPosX = uint32_t(m_xFactor * srcWidth);
            m_wishedPosY = uint32_t(m_yFactor * srcHeight);
            m_wishedWidth = uint32_t(m_widthFactor * srcWidth);
            m_wishedHeight = uint32_t(m_heightFactor * srcHeight);

            m_initialized = true;
        }

        float m_xFactor;
        float m_yFactor;
        float m_widthFactor;
        float m_heightFactor;

        uint32_t m_wishedPosX;
        uint32_t m_wishedPosY;
        uint32_t m_wishedWidth;
        uint32_t m_wishedHeight;

        bool m_initialized;

        NMMSS::PSample m_maskSample;
        NMMSS::ETransformResult m_result;
    };
}

namespace NMMSS
{
    IFilter* CreateMaskFilter(DECLARE_LOGGER_ARG, float x1, float y1, float x2, float y2)
    {
        return new CPullFilterImpl<CMaskTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CMaskTransformer(GET_LOGGER_PTR, x1, y1, x2, y2)
            );
    }
}
