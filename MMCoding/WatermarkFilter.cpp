#include "ImageTransformerBase.h"
#include <SDL.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>

#include <CorbaHelpers/GccUtils.h>

GCC_SUPPRESS_WARNING_BEGIN((-Wunused-local-typedefs)(-Wparentheses));
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/io/png.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
GCC_SUPPRESS_WARNING_END();

#include <boost/algorithm/string.hpp>


#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)
#endif // _MSC_VER

namespace
{
    class CWatermarkTransformer : public CImageTransformerBase
    {
        SDL_Surface*              m_watermarkSurface;
        SDL_Rect                  m_watermarkPlace;
        float                     m_x1;
        float                     m_y1;
        float                     m_x2;
        float                     m_y2;
        bool                      m_initialized;
        boost::gil::rgba8_image_t m_image;

    public:

        CWatermarkTransformer(DECLARE_LOGGER_ARG, const std::wstring& watermark, float opacity = 1.0F, float x1 = 0.0F, float y1 = 0.0F, float x2 = 1.0F, float y2 = 1.0F)
            : CImageTransformerBase(GET_LOGGER_PTR)
            , m_watermarkSurface(0)
            , m_x1(x1)
            , m_y1(y1)
            , m_x2(x2)
            , m_y2(y2)
            , m_initialized(false)
        {
            if (m_x1 > m_x2) std::swap(m_x1, m_x2);
            if (m_y1 > m_y2) std::swap(m_y1, m_y2);

            m_x1 = std::max(std::min(m_x1, 1.0f), 0.0f);
            m_x2 = std::max(std::min(m_x2, 1.0f), 0.0f);
            m_y1 = std::max(std::min(m_y1, 1.0f), 0.0f);
            m_y2 = std::max(std::min(m_y2, 1.0f), 0.0f);

            if(m_x1 == m_x2 || m_y1 == m_y2)
                throw std::runtime_error("wrong place for watermark");

            // make watermark surface
            boost::filesystem::path path(NCorbaHelpers::ToUtf8(watermark), boost::filesystem::detail::utf8_codecvt_facet());
            if (boost::filesystem::exists(path))
            {
                std::string ext = path.extension().string();
                boost::algorithm::to_lower(ext);
                if(ext == ".bmp")
                {
                    m_watermarkSurface = SDL_LoadBMP(NCorbaHelpers::ToUtf8(watermark).c_str());
                }
                else
                {
                    if (ext == ".png")
                    {
                        boost::gil::read_and_convert_image(path.generic_string(), m_image, boost::gil::png_tag());
                    }
                    else
                    {
                        // jpeg_read_and_convert_image must throw std::ios_base::failure
                        // on wrong jpeg file, but really it throws unhandled exception.
                        // It looks like the library bug, so we must check jpeg signature.
                        static const unsigned char JPEG_MAGIC[3] = { 0xFF, 0xD8, 0xFF };

                        std::ifstream jpg(path.c_str(), std::ios_base::binary | std::ios_base::in);
                        jpg.seekg(0, std::ios::beg);
                        char bytes[3];
                        jpg.read(bytes, 3);

                        if (std::memcmp(bytes, JPEG_MAGIC, sizeof(JPEG_MAGIC)) != 0)
                            throw std::runtime_error("wrong watermark image file");
                        
                        boost::gil::read_and_convert_image(path.generic_string(), m_image, boost::gil::jpeg_tag());
                    }

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                    uint32_t rmask = 0xff000000;
                    uint32_t gmask = 0x00ff0000;
                    uint32_t bmask = 0x0000ff00;
                    uint32_t amask = 0x000000ff;
#else
                    uint32_t rmask = 0x000000ff;
                    uint32_t gmask = 0x0000ff00;
                    uint32_t bmask = 0x00ff0000;
                    uint32_t amask = 0xff000000;
#endif
                    m_watermarkSurface = SDL_CreateRGBSurfaceFrom(m_image._view.begin().x(), m_image.width(), m_image.height(), 32, m_image.width() * 4, rmask, gmask, bmask, amask);
                }
            }

            if (!m_watermarkSurface)
            {
                _err_ << __FUNCTION__ << ". " << SDL_GetError();
                throw std::runtime_error("Unable to load watermark image");
            }

            SDL_SetSurfaceBlendMode(m_watermarkSurface, SDL_BLENDMODE_BLEND);
            SDL_SetSurfaceAlphaMod(m_watermarkSurface, opacity * 255);
        }

        ~CWatermarkTransformer()
        {
            SDL_FreeSurface(m_watermarkSurface);
        }

    private:

        void Transform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height)
        {
            // make source picture
            AVPicture rgbPicture1, rgbPicture2;
            avpicture_alloc(&rgbPicture1, AV_PIX_FMT_RGB24, width, height);

            SwsContext* rgbContext = sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(rgbContext, picture.data, picture.linesize, 0, height, rgbPicture1.data, rgbPicture1.linesize);
            
            // create frame surface
            SDL_Surface *frameSurface = SDL_CreateRGBSurfaceFrom(rgbPicture1.data[0], width, height, 24, rgbPicture1.linesize[0], 0, 0, 0, 0);

            InitWatermarkMapping(width, height);
            SDL_BlitScaled(m_watermarkSurface, NULL, frameSurface, &m_watermarkPlace);

            // make result picture
            avpicture_fill(&rgbPicture2, (uint8_t*)frameSurface->pixels, AV_PIX_FMT_RGB24, width, height);
            avpicture_fill(&picture, m_sample->GetBody(), format, width, height);

            SwsContext* fmtContext = sws_getContext(width, height, AV_PIX_FMT_RGB24, width, height, format, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(fmtContext, rgbPicture2.data, rgbPicture2.linesize, 0, height, picture.data, picture.linesize);

            SDL_FreeSurface(frameSurface);
            sws_freeContext(rgbContext);
            sws_freeContext(fmtContext);
            avpicture_free(&rgbPicture1);
        }

        void InitWatermarkMapping(uint32_t width, uint32_t height)
        {
            if (m_initialized)
                return;

            m_initialized = true;

            int16_t x = width * m_x1;
            int16_t y = height * m_y1;
            uint16_t _w = width * (m_x2 - m_x1);
            uint16_t _h = height * (m_y2 - m_y1);

            // scaling watermark
            float factor = std::min(std::min(float(_w) / m_watermarkSurface->w, float(_h) / m_watermarkSurface->h), 1.0f);

            uint16_t w = factor * m_watermarkSurface->w;
            uint16_t h = factor * m_watermarkSurface->h;

            // centering watermark
            if(_w > w)
                x += (_w - w) / 2;

            if(_h > h)
                y += (_h - h) / 2;

            m_watermarkPlace = { x, y, w, h };
        }
    };
}

namespace NMMSS
{
    IFilter* CreateWatermarkFilter(DECLARE_LOGGER_ARG, const std::wstring& watermark, float opacity, float x1, float y1, float x2, float y2)
    {
        return new CPullFilterImpl<CWatermarkTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CWatermarkTransformer(GET_LOGGER_PTR, watermark, opacity, x1, y1, x2, y2)
            );
    }
}

// code goes here
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER
