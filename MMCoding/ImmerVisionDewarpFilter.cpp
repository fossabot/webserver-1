extern "C"
{
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <boost/make_shared.hpp>
#include <IMV1.h>
#include "Transforms.h"
#include "../FilterImpl.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif // _MSC_VER

namespace
{
    float rad2deg(float rad)
    {
        return rad / 3.14159265f * 180.0f;
    }

    class CImmerVisionTransformer
    {
        DECLARE_LOGGER_HOLDER;

    public:

        CImmerVisionTransformer(DECLARE_LOGGER_ARG, float pan, float tilt, float zoom, const char* lens, NMMSS::EDewarpMode mode, NMMSS::ECameraPlace place, uint32_t destWidth, uint32_t destHeight)
            : m_pan(rad2deg(pan))
            , m_tilt(rad2deg(tilt))
            , m_zoom(rad2deg(zoom))
            , m_lens(lens)
            , m_mode(mode)
            , m_place(place)
            , m_destWidth((destWidth + 7) & -8)
            , m_destHeight((destHeight + 7) & -8)
            , m_inputBuf(new IMV_Buffer())
            , m_outputBuf(new IMV_Buffer())
            , m_camera(new IMV_CameraInterface())
            , m_initialized(false)
        {
            INIT_LOGGER_HOLDER;

            _log_ << "ImmerVision dewarp: pan=" << pan << " tilt=" << tilt << " zoom=" << zoom << " lens=" << lens << " mode=" << mode << " place=" << place << " width=" << destWidth << " height=" << destHeight;

            if (m_zoom <= 0.0f || m_zoom > 180.0f)
                throw std::runtime_error("bad 'zoom' parameter");

            if (m_destWidth == 0 || m_destHeight == 0)
                throw std::runtime_error("bad 'destWidth' or 'destHeight' parameter");

            m_data.resize(m_destWidth * m_destHeight * 3);
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            m_allocator = holder.GetAllocator();

            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_sample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_sample->Header().dtTimeEnd = sample->Header().dtTimeEnd;
                    holder.AddSample(m_sample);
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
        void operator()(TMediaTypeHeader* header, uint8_t* body)
        {
            m_result = NMMSS::ETHROUGH;
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            size_t bodySize = m_destWidth * m_destHeight;
            m_sample = m_allocator->Alloc(bodySize);
            m_sample->Header().nBodySize = bodySize;

            AVPicture picture;
            picture.data[0] = body + header->nOffset;
            picture.linesize[0] = header->nPitch;

            DoTransform<NMMSS::NMediaType::Video::fccGREY>(picture, AVPixelFormat::AV_PIX_FMT_GRAY8, header->nWidth, header->nHeight);

            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(m_sample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader));

            subheader->nWidth = m_destWidth;
            subheader->nHeight = m_destHeight;
            subheader->nOffset = picture.data[0] - m_sample->GetBody();
            subheader->nPitch = picture.linesize[0];
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            size_t bodySize = m_destWidth * m_destHeight + m_destWidth * m_destHeight / 2;
            m_sample = m_allocator->Alloc(bodySize);
            m_sample->Header().nBodySize = bodySize;

            AVPicture picture;
            picture.data[0] = body + header->nOffset;
            picture.data[1] = body + header->nOffsetV;
            picture.data[2] = body + header->nOffsetU;
            picture.linesize[0] = header->nPitch;
            picture.linesize[1] = header->nPitchV;
            picture.linesize[2] = header->nPitchU;

            DoTransform<NMMSS::NMediaType::Video::fccI420>(picture, AVPixelFormat::AV_PIX_FMT_YUV420P, header->nWidth, header->nHeight);

            NMMSS::NMediaType::Video::fccI420::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccI420>(m_sample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccI420::SubtypeHeader));

            subheader->nWidth = m_destWidth;
            subheader->nHeight = m_destHeight;
            subheader->nOffset = picture.data[0] - m_sample->GetBody();
            subheader->nOffsetV = picture.data[1] - m_sample->GetBody();
            subheader->nOffsetU = picture.data[2] - m_sample->GetBody();
            subheader->nPitch = picture.linesize[0];
            subheader->nPitchV = picture.linesize[1];
            subheader->nPitchU = picture.linesize[2];
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            size_t bodySize = m_destWidth * m_destHeight * 2;
            m_sample = m_allocator->Alloc(bodySize);
            m_sample->Header().nBodySize = bodySize;

            AVPicture picture;
            picture.data[0] = body + header->nOffset;
            picture.data[1] = body + header->nOffsetV;
            picture.data[2] = body + header->nOffsetU;
            picture.linesize[0] = header->nPitch;
            picture.linesize[1] = header->nPitchV;
            picture.linesize[2] = header->nPitchU;

            DoTransform<NMMSS::NMediaType::Video::fccY42B>(picture, AVPixelFormat::AV_PIX_FMT_YUV422P, header->nWidth, header->nHeight);

            NMMSS::NMediaType::Video::fccY42B::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccY42B>(m_sample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader));

            subheader->nWidth = m_destWidth;
            subheader->nHeight = m_destHeight;
            subheader->nOffset = picture.data[0] - m_sample->GetBody();
            subheader->nOffsetV = picture.data[1] - m_sample->GetBody();
            subheader->nOffsetU = picture.data[2] - m_sample->GetBody();
            subheader->nPitch = picture.linesize[0];
            subheader->nPitchV = picture.linesize[1];
            subheader->nPitchU = picture.linesize[2];
        }

    protected:

        template <typename THeader>
        void DoTransform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height)
        {
            SwsContext *srcContext = 0;
            SwsContext *dstContext = 0;
            AVPicture rgbFullPicture, rgbDewarpPicture;
            memset(&rgbFullPicture, 0, sizeof(AVPicture));

            try
            {
                avpicture_alloc(&rgbFullPicture, AV_PIX_FMT_RGB24, width, height);
                srcContext = sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
                sws_scale(srcContext, picture.data, picture.linesize, 0, height, rgbFullPicture.data, rgbFullPicture.linesize);

                MakeDewarp(rgbFullPicture.data[0], width, height);

                avpicture_fill(&rgbDewarpPicture, m_data.data(), AV_PIX_FMT_RGB24, m_destWidth, m_destHeight);
                avpicture_fill(&picture, m_sample->GetBody(), format, m_destWidth, m_destHeight);
                dstContext = sws_getContext(m_destWidth, m_destHeight, AV_PIX_FMT_RGB24, m_destWidth, m_destHeight, format, SWS_BICUBIC, NULL, NULL, NULL);
                sws_scale(dstContext, rgbDewarpPicture.data, rgbDewarpPicture.linesize, 0, m_destHeight, picture.data, picture.linesize);

                m_result = NMMSS::ETRANSFORMED;
            }
            catch (const std::exception& ex)
            {
                m_result = NMMSS::EFAILED;
                _err_ << __FUNCTION__ << ". " << ex.what();
            }

            sws_freeContext(srcContext);
            sws_freeContext(dstContext);
            avpicture_free(&rgbFullPicture);
        }

        void MakeDewarp(uint8_t* pSrc, uint32_t srcWidth, uint32_t srcHeight)
        {
            if (!m_initialized)
            {
                m_inputBuf->data = pSrc;
                m_inputBuf->frameX = 0;
                m_inputBuf->frameY = 0;
                m_inputBuf->width = srcWidth;
                m_inputBuf->height = srcHeight;
                m_inputBuf->frameWidth = srcWidth;
                m_inputBuf->frameHeight = srcHeight;

                m_outputBuf->data = m_data.data();
                m_outputBuf->frameX = 0;
                m_outputBuf->frameY = 0;
                m_outputBuf->width = m_destWidth;
                m_outputBuf->height = m_destHeight;
                m_outputBuf->frameWidth = m_destWidth;
                m_outputBuf->frameHeight = m_destHeight;

                unsigned long result = IMV_Defs::E_ERR_OK;

                if (!m_lens.empty())
                    result = m_camera->SetLens(const_cast<char*>(m_lens.c_str()));

                unsigned long mode = m_mode == NMMSS::EDewarpMode::PTZ ? IMV_Defs::E_VTYPE_PTZ : IMV_Defs::E_VTYPE_PERI;
                unsigned long place = m_place == NMMSS::ECameraPlace::CEILING ? IMV_Defs::E_CPOS_CEILING : m_place == NMMSS::ECameraPlace::WALL ? IMV_Defs::E_CPOS_WALL : IMV_Defs::E_CPOS_GROUND;

                result = result || m_camera->SetVideoParams(m_inputBuf.get(), m_outputBuf.get(), IMV_Defs::E_RGB_24_STD, mode, place);
                result = result || m_camera->SetFiltering(IMV_Defs::E_FILTER_BILINEAR);
                result = result || m_camera->SetPosition(&m_pan, &m_tilt, &m_zoom);
                result = result || m_camera->SetZoomLimits(0, 180);

                if (result != IMV_Defs::E_ERR_OK)
                {
                    _err_ << "IMV init camera error: " << result;
                    throw std::runtime_error("IMV init error");
                }

                m_initialized = true;
            }
            else
            {
                m_inputBuf->data = pSrc;
            }

            unsigned long result = m_camera->Update();
            if (result != IMV_Defs::E_ERR_OK)
            {
                _err_ << "IMV dewarp error: " << result;
                throw std::runtime_error("IMV dewarp error");
            }
        }

    private:

        float m_pan;
        float m_tilt;
        float m_zoom;
        std::string m_lens;
        NMMSS::EDewarpMode m_mode;
        NMMSS::ECameraPlace m_place;
        uint32_t m_destWidth;
        uint32_t m_destHeight;

        std::vector<uint8_t> m_data;
        boost::shared_ptr<IMV_Buffer> m_inputBuf;
        boost::shared_ptr<IMV_Buffer> m_outputBuf;
        boost::shared_ptr<IMV_CameraInterface> m_camera;

        NMMSS::PAllocator m_allocator;
        NMMSS::PSample m_sample;
        NMMSS::ETransformResult m_result;
        bool m_initialized;
    };
}

namespace NMMSS
{
    IFilter* CreateImmerVisionDewarpFilter(DECLARE_LOGGER_ARG, float pan, float tilt, float zoom, const char* lens, ECameraPlace place, EDewarpMode mode, uint32_t destWidth, uint32_t destHeight)
    {
        return new CPullFilterImpl<CImmerVisionTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CImmerVisionTransformer(GET_LOGGER_PTR, pan, tilt, zoom, lens, mode, place, destWidth, destHeight)
            );
    }
}
