#include <algorithm>
#include "Transforms.h"
#include "UtcTimeToLocal.h"
#include "../FilterImpl.h"
#include "../PtimeFromQword.h"
#include <ItvFramework/Library.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif // _MSC_VER

namespace
{
    class CImageTransformerBase
    {
    protected:

        DECLARE_LOGGER_HOLDER;

        NMMSS::PSample          m_sample;
        NMMSS::ETransformResult m_result;

    public:

        CImageTransformerBase(DECLARE_LOGGER_ARG)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual ~CImageTransformerBase() {}

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            return Transform(sample, holder);
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* subtypeHeader, uint8_t* dataPtr)
        {
            m_result = NMMSS::ETHROUGH;
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            AVPicture picture;    
            picture.data[0] = body + header->nOffset;
            picture.linesize[0] = header->nPitch;

            DoTransform(picture, AV_PIX_FMT_GRAY8, header->nWidth, header->nHeight);

            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(m_sample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader));

            subheader->nOffset = picture.data[0] - m_sample->GetBody();
            subheader->nPitch = picture.linesize[0];

            m_result = NMMSS::ETRANSFORMED;
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            AVPicture picture;    
            picture.data[0] = body + header->nOffset;
            picture.data[1] = body + header->nOffsetV;
            picture.data[2] = body + header->nOffsetU;
            picture.linesize[0] = header->nPitch;
            picture.linesize[1] = header->nPitchV;
            picture.linesize[2] = header->nPitchU;

            DoTransform(picture, AV_PIX_FMT_YUV420P, header->nWidth, header->nHeight);

            NMMSS::NMediaType::Video::fccI420::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(m_sample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccI420::SubtypeHeader));

            subheader->nOffset = picture.data[0] - m_sample->GetBody();
            subheader->nOffsetV = picture.data[1] - m_sample->GetBody();
            subheader->nOffsetU = picture.data[2] - m_sample->GetBody();
            subheader->nPitch = picture.linesize[0];
            subheader->nPitchV = picture.linesize[1];
            subheader->nPitchU = picture.linesize[2];

            m_result = NMMSS::ETRANSFORMED;
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            AVPicture picture;    
            picture.data[0] = body + header->nOffset;
            picture.data[1] = body + header->nOffsetV;
            picture.data[2] = body + header->nOffsetU;
            picture.linesize[0] = header->nPitch;
            picture.linesize[1] = header->nPitchV;
            picture.linesize[2] = header->nPitchU;

            DoTransform(picture, AV_PIX_FMT_YUV422P, header->nWidth, header->nHeight);

            NMMSS::NMediaType::Video::fccY42B::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccY42B>(m_sample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader));

            subheader->nOffset = picture.data[0] - m_sample->GetBody();
            subheader->nOffsetV = picture.data[1] - m_sample->GetBody();
            subheader->nOffsetU = picture.data[2] - m_sample->GetBody();
            subheader->nPitch = picture.linesize[0];
            subheader->nPitchV = picture.linesize[1];
            subheader->nPitchU = picture.linesize[2];

            m_result = NMMSS::ETRANSFORMED;
        }

        void DoTransform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                throw std::runtime_error("wrong frame size " + std::to_string(width) + "/" + std::to_string(height));

            Transform(picture, format, width, height);
        }

    protected:

        virtual NMMSS::ETransformResult Transform(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            m_sample = holder.GetAllocator()->Alloc(sample->Header().nBodySize);
            m_sample->Header().nBodySize = sample->Header().nBodySize;
            m_sample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
            m_sample->Header().dtTimeEnd = sample->Header().dtTimeEnd;

            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);
                if (NMMSS::ETRANSFORMED == m_result)
                    holder.AddSample(m_sample);
            }
            catch (std::exception& e)
            {
                _log_ << e.what() << std::endl;
                return NMMSS::EFAILED;
            }
            return m_result;
        }

        virtual void Transform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height) = 0;
    };
}

// code goes here
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER
