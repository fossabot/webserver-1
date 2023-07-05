#include <SDL.h>
#include "Transforms.h"
#include "../FilterImpl.h"
#include "../PtimeFromQword.h"
#include "ImageTransformerBase.h"


namespace
{
    class COverlayTransformer : public CImageTransformerBase
    {
        NMMSS::POverlayProvider m_provider;
        unsigned char           m_color[4];
        NMMSS::TBoxList         m_frame;

    public:

        COverlayTransformer(DECLARE_LOGGER_ARG, NMMSS::POverlayProvider provider, unsigned char color[4])
            : CImageTransformerBase(GET_LOGGER_PTR)
            , m_provider(provider)
        {
            memcpy(m_color, color, sizeof(m_color));
        }

    protected:

        NMMSS::ETransformResult Transform(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder) override
        {
            m_result = NMMSS::ETHROUGH;
            m_frame.clear();

            if (sample)
            {
                m_frame = m_provider->Advance(NMMSS::PtimeFromQword(sample->Header().dtTimeBegin));
                if (!m_frame.empty())
                    m_result = CImageTransformerBase::Transform(sample, holder);
            }
            return m_result;
        }

        void Transform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height) override
        {
            // make source picture
            AVPicture rgbPicture1, rgbPicture2;
			avpicture_alloc(&rgbPicture1, AV_PIX_FMT_RGBA, width, height);

			BOOST_SCOPE_EXIT(&rgbPicture1)
			{
				avpicture_free(&rgbPicture1);
			} 
			BOOST_SCOPE_EXIT_END

			std::shared_ptr<SwsContext> rgbContext(sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL), sws_freeContext);
            sws_scale(rgbContext.get(), picture.data, picture.linesize, 0, height, rgbPicture1.data, rgbPicture1.linesize);
            
            // create frame surface
			std::shared_ptr<SDL_Surface> surface(SDL_CreateRGBSurfaceFrom(rgbPicture1.data[0], width, height, 32, rgbPicture1.linesize[0], 0, 0, 0, 0), SDL_FreeSurface);
            
            // make overlays
            for (auto& box : m_frame)
            {
                int x = static_cast<int>(box.min_corner().x() * width);
                int y = static_cast<int>(box.min_corner().y() * height);
                int w = static_cast<int>(box.max_corner().x() * width - x);
                int h = static_cast<int>(box.max_corner().y() * height - y);

                // create frame surface
                std::shared_ptr<SDL_Surface> overlay(SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, surface->format->Rmask, surface->format->Gmask, surface->format->Bmask, surface->format->Amask), SDL_FreeSurface);

                if (overlay)
                {
                    // draw overlay
                    SDL_Rect rect {x, y, w, h};
                    if (SDL_FillRect(overlay.get(), NULL, SDL_MapRGB(overlay->format, m_color[0], m_color[1], m_color[2])))
                    {
                        _err_ << "SDL_FillRect failed: " << SDL_GetError();
                    }
                    else if (SDL_SetSurfaceAlphaMod(overlay.get(), m_color[3]))
                    {
                        _err_ << "SDL_SetSurfaceAlphaMod failed: " << SDL_GetError();
                    }
                    else if (SDL_SetSurfaceBlendMode(overlay.get(), SDL_BLENDMODE_BLEND))
                    {
                        _err_ << "SDL_SetSurfaceBlendMode failed: " << SDL_GetError();
                    }
                    else if (SDL_BlitSurface(overlay.get(), NULL, surface.get(), &rect))
                    {
                        _err_ << "SDL_BlitSurface failed: " << SDL_GetError();
                    }
                }
                else
                {
                    _err_ << "SDL_CreateRGBSurface failed: " << SDL_GetError();
                }
            }

            // make result picture
			avpicture_fill(&rgbPicture2, (uint8_t*)surface->pixels, AV_PIX_FMT_RGBA, width, height);
            avpicture_fill(&picture, m_sample->GetBody(), format, width, height);

            std::shared_ptr<SwsContext> fmtContext(sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height, format, SWS_BICUBIC, NULL, NULL, NULL), sws_freeContext);
            sws_scale(fmtContext.get(), rgbPicture2.data, rgbPicture2.linesize, 0, height, picture.data, picture.linesize);
        }
    };
}

namespace NMMSS
{
    IFilter* CreateOverlayFilter(DECLARE_LOGGER_ARG, POverlayProvider provider, unsigned char color[4])
    {
        return new CPullFilterImpl<COverlayTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new COverlayTransformer(GET_LOGGER_PTR, provider, color)
            );
    }
}
