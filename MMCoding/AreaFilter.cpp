#include "Transforms.h"
#include "../FilterImpl.h"

namespace
{
    class CAreaTransformer
    {
        const float EPSILON = 0.00001F;

        DECLARE_LOGGER_HOLDER;

    public:
        CAreaTransformer(DECLARE_LOGGER_ARG, float x1 = 0.0F, float y1 = 0.0F, float x2 = 0.0F, float y2 = 0.0F)
            : m_xFactor(x1)
            , m_yFactor(y1)
            , m_widthFactor(x2 - x1)
            , m_heightFactor(y2 - y1)
            , m_initialized(false)
        {
            INIT_LOGGER_HOLDER;
            if (m_xFactor < 0.0F || m_xFactor > 1.0f || m_yFactor < 0.0F || m_yFactor > 1.0f ||
                m_widthFactor < 0.0F || m_widthFactor > 1.0f || m_heightFactor < 0.0F || m_heightFactor > 1.0f)
            {
                throw std::runtime_error("Bad area parameters");
            }
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            if (m_widthFactor >= 1.0f - EPSILON && m_heightFactor >= 1.0f - EPSILON)
                return NMMSS::ETHROUGH;

            m_areaSample = holder.GetAllocator()->Alloc(sample->Header().nBodySize);

            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_areaSample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_areaSample->Header().dtTimeEnd = sample->Header().dtTimeEnd;
                    holder.AddSample(m_areaSample);
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
            InitCropArea(header->nWidth, header->nHeight);

            uint8_t* areaBody = m_areaSample->GetBody();

            DoCopyPlane(body + header->nOffset, header->nPitch, m_shiftX, m_shiftY, areaBody, m_areaPitch, m_areaHeight);

            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(m_areaSample->GetHeader(), &subheader);

            subheader->nOffset = 0;
            subheader->nPitch = m_areaPitch;
            subheader->nHeight = m_areaHeight;
            subheader->nWidth = m_areaWidth;

            m_areaSample->Header().nBodySize = m_areaHeight * m_areaPitch;

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
            InitCropArea(header->nWidth, header->nHeight);
            
            uint32_t areaUVHeight = m_areaHeight / uvHeightDiminish;
            uint32_t areaUVPitch = m_areaPitch / uvWidthDiminish;

            uint8_t* areaBody = m_areaSample->GetBody();

            typename THeader::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_areaSample->GetHeader(), &subheader);

            subheader->nOffset = 0;
            subheader->nHeight = m_areaHeight;
            subheader->nWidth = m_areaWidth;
            subheader->nPitch = m_areaPitch;
            subheader->nPitchU = areaUVPitch;
            subheader->nPitchV = areaUVPitch;

            DoCopyPlane(body + header->nOffset, header->nPitch, m_shiftX, m_shiftY, areaBody, m_areaPitch, m_areaHeight);

            subheader->nOffsetU = (uint32_t)(areaBody - m_areaSample->GetBody());
            DoCopyPlane(body + header->nOffsetU, header->nPitchU, m_shiftX / uvWidthDiminish, m_shiftY / uvHeightDiminish, areaBody, areaUVPitch, areaUVHeight);

            subheader->nOffsetV = (uint32_t)(areaBody - m_areaSample->GetBody());
            DoCopyPlane(body + header->nOffsetV, header->nPitchV, m_shiftX / uvWidthDiminish, m_shiftY / uvHeightDiminish, areaBody, areaUVPitch, areaUVHeight);

            m_areaSample->Header().nBodySize = m_areaHeight * m_areaPitch + 2 * (areaUVHeight * areaUVPitch);

            m_result = NMMSS::ETRANSFORMED;
        }

        void DoCopyPlane(uint8_t* pSrc, uint32_t srcPitch, uint32_t shiftX, uint32_t shiftY, uint8_t*& pDst, uint32_t dstPitch, uint32_t dstHeight)
        {
            uint8_t* pSource = pSrc;
            uint32_t copyLen = std::min(srcPitch - shiftX, dstPitch);
            for (uint32_t y = shiftY; y < shiftY + dstHeight; ++y)
            {
                pSource = pSrc + y * srcPitch + shiftX;
                memcpy(pDst, pSource, copyLen);

                if (copyLen < dstPitch)
                    memset(pDst + copyLen, pDst[copyLen - 1], dstPitch - copyLen); 

                pDst += dstPitch;
            }
        }

        void InitCropArea(uint32_t srcWidth, uint32_t srcHeight)
        {
            if (m_initialized)
                return;

            m_shiftX = uint32_t(m_xFactor * srcWidth) & -2;
            m_shiftY = uint32_t(m_yFactor * srcHeight) & -2;

            uint32_t wishedWidth = uint32_t(m_widthFactor * srcWidth);
            uint32_t wishedHeight = uint32_t(m_heightFactor * srcHeight);

            uint32_t maxWidth = srcWidth - m_shiftX;
            uint32_t maxHeight = srcHeight - m_shiftY;

            m_areaWidth = maxWidth > wishedWidth ? ((wishedWidth + 7) & -8) : (maxWidth & -8);
            m_areaHeight = maxHeight > wishedHeight ? ((wishedHeight + 7) & -8) : (maxHeight & -8);
            m_areaPitch = (m_areaWidth + 15) & -16;

            m_initialized = true;
        }
      
        float m_xFactor;
        float m_yFactor;
        float m_widthFactor;
        float m_heightFactor;

        uint32_t m_shiftX;
        uint32_t m_shiftY;
        uint32_t m_areaWidth;
        uint32_t m_areaHeight;
        uint32_t m_areaPitch;

        bool m_initialized;

        NMMSS::PSample m_areaSample;
        NMMSS::ETransformResult m_result;
    };
}

namespace NMMSS
{
    IFilter* CreateAreaFilter(DECLARE_LOGGER_ARG, float lPos, float tPos, float rPos, float bPos)
    {
        return new CPullFilterImpl<CAreaTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CAreaTransformer(GET_LOGGER_PTR, lPos, tPos, rPos, bPos)
            );
    }
}
