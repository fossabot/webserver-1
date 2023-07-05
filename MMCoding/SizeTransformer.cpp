#include "Transforms.h"
#include "FFMPEGCodec.h"
#include "../FilterImpl.h"

extern "C"
{
#include <libswscale/swscale.h>
}

namespace
{
    class CBiTransformer

    {
        DECLARE_LOGGER_HOLDER;
    public:
        CBiTransformer(DECLARE_LOGGER_ARG, uint32_t width = 0, uint32_t height = 0, bool storeAspectRatio = true
                       ,bool crop=false, float crop_x = 0.f, float crop_y = 0.f, float crop_width = 1.f, float crop_height=1.f)
            : m_width(width)
            , m_height(height)
            , m_storeAspectRatio(storeAspectRatio)
            , m_downscaleStepY(1)
            , m_downscaleStepX(1)            
            , m_crop_x(toInterval(crop_x, 0.f, 1.f))
            , m_crop_y(toInterval(crop_y, 0.f, 1.f))
            , m_crop_width (toInterval(crop_width,  0.f, 1.f- m_crop_x))
            , m_crop_height(toInterval(crop_height, 0.f, 1.f- m_crop_y))
            , m_crop(crop && isCrop(m_crop_x, m_crop_x, m_crop_width, m_crop_height))
        {
            INIT_LOGGER_HOLDER;
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            if (!sample)
                return NMMSS::EIGNORED;

            if (0 == m_width && 0 == m_height && !m_crop)
                return NMMSS::ETHROUGH;

            if (holder.GetAllocator() == nullptr)
                return NMMSS::ETHROUGH;

            m_allocator = holder.GetAllocator();

            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_scaleSample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_scaleSample->Header().dtTimeEnd = sample->Header().dtTimeEnd;
                    holder.AddSample(m_scaleSample);
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
            if (SetScalingContext(header))
            {
                // начальное изображение
                const uint32_t yWidth = header->nWidth;
                const uint32_t yHeight = header->nHeight;

                // преобразованное изображение
                // делим и умножаем на два на той случай, если новый размер не кратен 
                // двум и не ясно как быть с размером цветовой плоскости
                const uint32_t scaledYWidth = yWidth / m_downscaleStepX / 2 * 2;
                const uint32_t scaledYHeight = yHeight / m_downscaleStepY / 2 * 2;
                const uint32_t scaledYPitch = GetScaledPitch(scaledYWidth);

                m_scaleSample = allocateSample(AV_PIX_FMT_GRAY8, scaledYPitch, scaledYHeight);
                if (m_scaleSample)
                {
                    NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
                    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>
                        (m_scaleSample->GetHeader(), &subheader);

                    subheader->nOffset = 0;
                    subheader->nPitch = scaledYPitch;
                    subheader->nHeight = scaledYHeight;
                    subheader->nWidth = scaledYWidth;

                    m_scaleSample->Header().nBodySize = scaledYHeight * scaledYPitch;

                    m_result = NMMSS::ETRANSFORMED;
                }
            }
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            if (SetScalingContext(header))
                DoConvert<NMMSS::NMediaType::Video::fccI420>(header, body, false);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            if (SetScalingContext(header))
                DoConvert<NMMSS::NMediaType::Video::fccY42B>(header, body, true);
        }

    private:
        template <typename THeader>
        bool SetScalingContext(const THeader* header)
        {
            if (0 != m_height)
            {
                while (m_height < header->nHeight / m_downscaleStepY)
                    m_downscaleStepY *= 2;
            }
            if (0 != m_width)
            {
                while (m_width < header->nWidth / m_downscaleStepX)
                    m_downscaleStepX *= 2;
            }

            m_downscaleStepY = (0 == m_height) ? m_downscaleStepX : m_downscaleStepY;
            m_downscaleStepX = (0 == m_width) ? m_downscaleStepY : m_downscaleStepX;

            if (m_storeAspectRatio)
                m_downscaleStepY = m_downscaleStepX = (m_downscaleStepY < m_downscaleStepX) ?
            m_downscaleStepX : m_downscaleStepY;

            m_resize = !(1 == m_downscaleStepY && 1 == m_downscaleStepX);
            
            if (m_resize || m_crop)
                return true;

            m_result = NMMSS::ETHROUGH;
            return false;
        }

        template <typename THeader>
        void DoConvert(typename THeader::SubtypeHeader* header, uint8_t* body, bool is422)
        {
            m_result = NMMSS::EFAILED;

            // начальное изображение
            const uint32_t yWidth = header->nWidth;
            const uint32_t yHeight = header->nHeight;

            // преобразованное изображение
            // делим и умножаем на два на той случай, если новый размер не кратен 
            // двум и не ясно как быть с размером цветовой плоскости
            const uint32_t scaledYWidth = yWidth / m_downscaleStepX / 2 * 2;
            const uint32_t scaledYHeight = yHeight / m_downscaleStepY / 2 * 2;

            int sampleYWidth = m_crop ? (int)(scaledYWidth * m_crop_width) : scaledYWidth;
            int sampleYHeight = m_crop ? (int)(scaledYHeight * m_crop_height) : scaledYHeight;

            const uint32_t scaledYPitch = GetScaledPitch(sampleYWidth);
            const uint32_t scaledUVWidth = sampleYWidth / 2;
            const uint32_t scaledUVPitch = GetScaledPitch(scaledUVWidth);

            // вычисления
            m_scaleSample = allocateSample(AV_PIX_FMT_YUV420P, scaledYPitch, sampleYHeight);

            if (m_scaleSample)
            {
                typename THeader::SubtypeHeader *subheader = 0;
                NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_scaleSample->GetHeader(), &subheader);

                subheader->nOffset = 0;
                subheader->nHeight = scaledYHeight;
                subheader->nWidth = scaledYWidth;
                subheader->nPitch = scaledYPitch;
                subheader->nPitchU = scaledUVPitch;
                subheader->nPitchV = scaledUVPitch;

                AVPicture picture;
                picture.data[0] = body + header->nOffset;
                picture.data[1] = body + header->nOffsetV;
                picture.data[2] = body + header->nOffsetU;
                picture.linesize[0] = header->nPitch;
                picture.linesize[1] = header->nPitchV;
                picture.linesize[2] = header->nPitchU;

                AVPicture scalePicture;
                AVPicture cropPicture;   

                BOOST_SCOPE_EXIT_TPL(&scalePicture, m_resize, m_crop)
                {
                    if (m_resize && m_crop)
                        avpicture_free(&scalePicture);
                }
                BOOST_SCOPE_EXIT_END

                if (m_resize)
                {
                    if (m_crop)
                        avpicture_alloc(&scalePicture, AV_PIX_FMT_YUV420P, scaledYWidth, scaledYHeight);
                    else
                        avpicture_fill(&scalePicture, m_scaleSample->GetBody(), AV_PIX_FMT_YUV420P, scaledYWidth, scaledYHeight);

                    SwsContext *resize = sws_getContext(header->nWidth, header->nHeight, AV_PIX_FMT_YUV420P,
                        scaledYWidth, scaledYHeight, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

                    if (!resize)
                        return;

                    sws_scale(resize, picture.data, picture.linesize, 0, header->nHeight, scalePicture.data, scalePicture.linesize);
                    sws_freeContext(resize);
                }
                
                AVPicture &resPictureRef = m_resize ? scalePicture : picture;

                if (m_crop)
                {
                    AVPicture tmpPict;

                    if (0 != av_picture_crop(&tmpPict, &resPictureRef, AV_PIX_FMT_YUV420P, (uint32_t)(scaledYHeight*m_crop_y), (uint32_t)(scaledYWidth*m_crop_x)))
                        return;
                    
                    avpicture_fill(&cropPicture, m_scaleSample->GetBody(), AV_PIX_FMT_YUV420P, sampleYWidth, sampleYHeight);
                    av_picture_copy(&cropPicture, &tmpPict, AV_PIX_FMT_YUV420P, sampleYWidth, sampleYHeight);

                    subheader->nHeight = sampleYHeight;
                    subheader->nWidth = sampleYWidth;
                }

                AVPicture &finalPictureRef = m_crop ? cropPicture : resPictureRef;

                subheader->nOffset = (uint32_t)(finalPictureRef.data[0] - m_scaleSample->GetBody());
                subheader->nOffsetV = (uint32_t)(finalPictureRef.data[1] - m_scaleSample->GetBody());
                subheader->nOffsetU = (uint32_t)(finalPictureRef.data[2] - m_scaleSample->GetBody());
                subheader->nPitch = finalPictureRef.linesize[0];
                subheader->nPitchV = finalPictureRef.linesize[1];
                subheader->nPitchU = finalPictureRef.linesize[2];

                m_result = NMMSS::ETRANSFORMED;
            }
        }

        uint32_t GetScaledPitch(uint32_t width)
        {
            return ((width + 0xf)&~0xf);
        }

        NMMSS::PSample allocateSample(AVPixelFormat pfmt, uint32_t width, uint32_t height)
        {
            int size = avpicture_get_size(pfmt, width, height);
            NMMSS::PSample s(m_allocator->Alloc(size));
            if (s)
                s->Header().nBodySize = size;
            return s;
        }

        static float toInterval(float val, float min, float max)
        {
            return std::min(std::max(val, min), max);
        }

        static bool eq(float a, float b)
        {
            return fabs(a - b) < std::numeric_limits<float>::epsilon();
        }

        static bool isCrop(float x, float y, float crop_width, float crop_height)
        {
            return !eq(x, 0.f) || !eq(y, 0.f) || 
                   !eq(x + crop_width, 1.f) || !eq(y + crop_height, 1.f);
        }

        const uint32_t m_width;
        const uint32_t m_height;

        bool m_storeAspectRatio;

        uint32_t m_downscaleStepY;
        uint32_t m_downscaleStepX;

        NMMSS::PSample m_scaleSample;
        NMMSS::ETransformResult m_result;
        bool m_resize;      
        
        //all-ratio
        const float m_crop_x;
        const float m_crop_y;
        const float m_crop_width;
        const float m_crop_height;
        const bool m_crop;

        std::unique_ptr<NMMSS::CFFmpegAllocator> m_swsAllocator;
        NMMSS::AVFramePtr m_swsFrame;

        NMMSS::PAllocator m_allocator;
    };
}

namespace NMMSS
{
    IFilter* CreateSizeFilter(DECLARE_LOGGER_ARG, uint32_t width, uint32_t height
        , bool crop, float crop_x, float crop_y, float crop_width, float crop_height)
    {
        return new CPullFilterImpl<CBiTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CBiTransformer(GET_LOGGER_PTR, width, height, true, 
                crop, crop_x, crop_y, crop_width, crop_height)
        );
    }
}
