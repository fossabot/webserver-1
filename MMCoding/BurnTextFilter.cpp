#include <deque>
#include <boost/make_shared.hpp>
#include <boost/thread/reverse_lock.hpp>
#include <CorbaHelpers/Envar.h>
#include "../ConnectionResource.h"
#include "ImageTransformerBase.h"

#ifdef _MSC_VER

#include "SDLttfLib.h"

#else

#include "FreetypeLib.h"
#include <cairo.h>
#include <cairo-ft.h>

#endif

namespace
{
    namespace bpt = boost::posix_time;

    class CBurnTextTransformerBase : public CImageTransformerBase
    {
    protected:

        typedef NCorbaHelpers::CAutoPtr<NMMSS::ISampleReader> PSampleReader;

        NMMSS::EBurnMode         m_mode;
        bpt::time_period         m_range;
        float                    m_sx;
        float                    m_sy;
        float                    m_sw;
        float                    m_sh;
        std::vector<std::string> m_lines;
        bpt::time_period         m_textScope;
        PSampleReader            m_source;

    public:

        CBurnTextTransformerBase(DECLARE_LOGGER_ARG,
                                 NMMSS::ISampleReader* textSource,
                                 float x1, float y1, float x2, float y2,
                                 NMMSS::EBurnMode mode,
                                 uint64_t beginTime, uint64_t endTime)
            : CImageTransformerBase(GET_LOGGER_PTR)
            , m_mode(mode)
            , m_range(NMMSS::PtimeFromQword(beginTime)
            , NMMSS::PtimeFromQword(endTime))
            , m_sx(std::min(std::max(0.0f, std::min(x1, x2)), 1.0f))
            , m_sy(std::min(std::max(0.0f, std::min(y1, y2)), 1.0f))
            , m_sw(std::min(std::max(0.0f, std::max(x1, x2)), 1.0f) - m_sx)
            , m_sh(std::min(std::max(0.0f, std::max(y1, y2)), 1.0f) - m_sy)
            , m_textScope(bpt::ptime(bpt::min_date_time), bpt::ptime(bpt::min_date_time))
            , m_source(textSource, NCorbaHelpers::ShareOwnership())
        {
        }

    private:

        NMMSS::ETransformResult Transform(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder) override
        {
            if (!m_source)
                return NMMSS::ETHROUGH;

            try
            {
                bpt::time_period period(
                    NMMSS::PtimeFromQword(sample->Header().dtTimeBegin),
                    NMMSS::PtimeFromQword(sample->Header().dtTimeEnd)
                    );

                if (!GetTextForPeriod(period))
                    return NMMSS::ETHROUGH;

                m_sample = holder.GetAllocator()->Alloc(sample->Header().nBodySize);
                m_sample->Header().nBodySize = sample->Header().nBodySize;
                m_sample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                m_sample->Header().dtTimeEnd = sample->Header().dtTimeEnd;

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

        bool GetTextForPeriod(const bpt::time_period& period)
        {
            if ((m_range.is_null() && period.begin() >= m_range.begin()) || m_range.intersects(period))
            {
                switch (m_mode)
                {
                    case NMMSS::ARCHIVE_STREAM:
                    case NMMSS::ARCHIVE_SNAPSHOT:
                        return WaitMatchedText(period);
                    case NMMSS::LIVE_SNAPSHOT:
                        return WaitAnyText(period);
                    case NMMSS::LIVE_STREAM:
                        return TakeAnyText(period);
                    default: break;
                }
            }
            return false;
        }

        bool WaitMatchedText(const bpt::time_period& period)
        {
            if (m_source && m_textScope.end() <= period.begin())
            {
                m_lines.clear();
                m_textScope = bpt::time_period(bpt::ptime(bpt::min_date_time), bpt::ptime(bpt::min_date_time));

                try
                {
                    NMMSS::PSample sample;
                    do
                    {
                        sample = m_source->PullSample(true);

                        m_textScope = bpt::time_period(
                            NMMSS::PtimeFromQword(sample->Header().dtTimeBegin),
                            NMMSS::PtimeFromQword(sample->Header().dtTimeEnd)
                            );
                    }
                    while (m_textScope.end() <= period.begin());

                    ExtractText(sample);
                }
                catch (const std::exception& ex)
                {
                    _wrn_ << __FUNCTION__ << ". " << ex.what();
                    m_source.Reset();
                }
            }

            return !m_lines.empty() && m_textScope.intersects(period);
        }

        bool TakeAnyText(const bpt::time_period& period)
        {
            if (m_source)
            {
                try
                {
                    NMMSS::PSample last;
                    NMMSS::PSample curr;
                    do
                    {
                        curr = m_source->PullSample(false);
                        if (curr)
                            last = curr;
                    }
                    while (curr);

                    if (last)
                    {
                        auto beg = NMMSS::PtimeFromQword(last->Header().dtTimeBegin);
                        auto end = NMMSS::PtimeFromQword(last->Header().dtTimeEnd);
                        m_textScope = bpt::time_period(beg, std::max(end, beg + boost::posix_time::seconds(5)));
                    }

                    ExtractText(last);
                }
                catch (const std::exception& ex)
                {
                    _wrn_ << __FUNCTION__ << ". " << ex.what();
                    m_source.Reset();
                }
            }

            return !m_lines.empty() && m_textScope.end() >= period.begin();
        }

        bool WaitAnyText(const bpt::time_period& period)
        {
            if (m_source)
            {
                try
                {
                    NMMSS::PSample last;
                    NMMSS::PSample curr = m_source->PullSample(true);
                    while (curr)
                    {
                        last = curr;
                        curr = m_source->PullSample(false);
                    }

                    if (last)
                    {
                        auto beg = NMMSS::PtimeFromQword(last->Header().dtTimeBegin);
                        auto end = NMMSS::PtimeFromQword(last->Header().dtTimeEnd);
                        m_textScope = bpt::time_period(beg, std::max(end, beg + boost::posix_time::seconds(5)));
                    }

                    ExtractText(last);
                }
                catch (const std::exception& ex)
                {
                    _wrn_ << __FUNCTION__ << ". " << ex.what();
                    m_source.Reset();
                }
            }

            return !m_lines.empty() && m_textScope.end() >= period.begin();
        }

        void ExtractText(NMMSS::PSample sample)
        {
            if (sample && NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Application::TypedOctetStream>(&sample->Header()))
            {
                m_lines.clear();

                uint8_t* data = sample->GetBody();

                uint32_t contentSize = 0;
                memcpy(&contentSize, data, sizeof(uint32_t));
                data += sizeof(uint32_t);

                std::string text;
                std::copy((char*)data, (char*)data + contentSize, std::back_inserter(text));
                boost::replace_all(text, "\\n", "\n");
                boost::split(m_lines, text, boost::is_any_of("\n"));
            }
        }
    };

#ifdef _MSC_VER

    class CBurnTextTransformer : public CBurnTextTransformerBase
    {
        TTF_Font* m_font;
        SDL_Color m_color;

    public:
        CBurnTextTransformer(DECLARE_LOGGER_ARG,
                             NMMSS::ISampleReader* textSource,
                             const std::wstring& fontPath,
                             int fontIndex,
                             int fontSize,
                             int fontStyle,
                             unsigned char color[4],
                             float x1, float y1, float x2, float y2,
                             NMMSS::EBurnMode mode,
                             uint64_t beginTime, uint64_t endTime)
            : CBurnTextTransformerBase(GET_LOGGER_PTR, textSource, x1, y1, x2, y2, mode, beginTime, endTime)
            , m_font(0)
        {
            boost::mutex::scoped_lock lock = NMMSS::CSDLttfLib::Lock();

            if (!fontPath.empty())
                m_font = TTF_OpenFontIndex(NCorbaHelpers::ToUtf8(fontPath).c_str(), fontSize > 0 ? fontSize : 20, fontIndex);

            if (!m_font)
            {
                std::string fPath = ITV8::Library::GetCurrentExecPath().generic_string() + "/PTC55F.ttf";

                if (!boost::filesystem::exists(fPath))
                    fPath = NCorbaHelpers::CEnvar::NgpBinDir() + "/PTC55F.ttf";

                m_font = TTF_OpenFontIndex(fPath.c_str(), fontSize > 0 ? fontSize : 20, fontIndex);
                if (!m_font)
                    throw std::runtime_error("Unable to load font");
            }

            TTF_SetFontStyle(m_font, fontStyle);

            if (color)
                m_color = { color[0], color[1], color[2], color[3] };
            else
                m_color = { 255, 255, 255, 0 };
        }

        ~CBurnTextTransformer()
        {
            boost::mutex::scoped_lock lock = NMMSS::CSDLttfLib::Lock();
            TTF_CloseFont(m_font);
        }

    private:

        void Transform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height) override
        {
            static const float EPSILON = 0.00001F;

            // make source picture
            AVPicture rgbPicture1, rgbPicture2;
            avpicture_alloc(&rgbPicture1, AV_PIX_FMT_RGB24, width, height);

            SwsContext* rgbContext = sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(rgbContext, picture.data, picture.linesize, 0, height, rgbPicture1.data, rgbPicture1.linesize);

            // create main surface
            SDL_Surface *frameSurface = SDL_CreateRGBSurfaceFrom(rgbPicture1.data[0], width, height, 24, rgbPicture1.linesize[0], 0, 0, 0, 0);

            // create text lines
            uint32_t lh = TTF_FontHeight(m_font);
            uint32_t lw = m_sw > EPSILON ? uint32_t(m_sw * width) : (width > lh * 2 ? width - lh * 2 : width);

            uint32_t lx = m_sw > EPSILON ? uint32_t(m_sx * width) : lh;
            uint32_t ly = m_sh > EPSILON ? uint32_t(m_sy * height) : lh;

            uint32_t th = m_sh > EPSILON ? uint32_t(m_sh * height) : (height > lh * 5 ? height - lh * 5 : height);
            if (ly + th > height - lh * 4)
                th = height - ly - lh * 4;

            size_t lc = th / lh;
            size_t i = m_lines.size() > lc ? m_lines.size() - lc : 0;

            for (; i < m_lines.size(); ++i)
            {
                if (!m_lines[i].empty())
                {
                    boost::mutex::scoped_lock lock = NMMSS::CSDLttfLib::Lock();
                    SDL_Surface* lineSurface = TTF_RenderUTF8_Blended(m_font, m_lines[i].c_str(), m_color);
                    lock.unlock();

                    SDL_Rect place = {
                        static_cast<int>(lx),
                        static_cast<int>(ly),
                        static_cast<int>(lw),
                        static_cast<int>(lh)
                    };
                    SDL_Rect area = {
                        0, 0,
                        static_cast<int>(lw),
                        static_cast<int>(lh)
                    };

                    SDL_BlitSurface(lineSurface, &area, frameSurface, &place);
                    SDL_FreeSurface(lineSurface);
                }

                ly += lh;
            }

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
    };

#else

    class CBurnTextTransformer : public CBurnTextTransformerBase
    {
        enum EFontStyle
        {
            FS_Underline = 4,
            FS_Strikeout = 8
        };

        typedef boost::shared_ptr<cairo_font_face_t> PFont;

        PFont  m_font;
        double m_fontSize;
        int    m_fontStyle;
        double m_fontColor[4];

    public:

        CBurnTextTransformer(DECLARE_LOGGER_ARG,
                             NMMSS::ISampleReader* textSource,
                             const std::wstring& fontPath,
                             int fontIndex,
                             int fontSize,
                             int fontStyle,
                             unsigned char fontColor[4],
                             float x1, float y1, float x2, float y2,
                             NMMSS::EBurnMode mode,
                             uint64_t beginTime, uint64_t endTime)
            : CBurnTextTransformerBase(GET_LOGGER_PTR, textSource, x1, y1, x2, y2, mode, beginTime, endTime)
            , m_fontSize(fontSize > 0 ? fontSize : 20)
            , m_fontStyle(fontStyle)
        {
            m_fontColor[0] = double(fontColor[0]) / 255.0;
            m_fontColor[1] = double(fontColor[1]) / 255.0;
            m_fontColor[2] = double(fontColor[2]) / 255.0;
            m_fontColor[3] = double(fontColor[3]) / 255.0;

            NMMSS::PFreetype freetype = NMMSS::GetFreetype();
            boost::mutex::scoped_lock lock = freetype->Lock();

            FT_Face face = 0;
            FT_Error error = 0;
            if (!fontPath.empty())
            {
                error = FT_New_Face(freetype->Library(), NCorbaHelpers::ToUtf8(fontPath).c_str(), fontIndex, &face);
                if (error)
                    _err_ << "Error " << error << " on loading font " << NCorbaHelpers::ToUtf8(fontPath);
            }

            if (error)
            {
                std::string fPath = ITV8::Library::GetCurrentExecPath().generic_string() + "/PTC55F.ttf";

                if (!boost::filesystem::exists(fPath))
                    fPath = NCorbaHelpers::CEnvar::NgpBinDir() + "/PTC55F.ttf";

                error = FT_New_Face(freetype->Library(), fPath.c_str(), 0, &face);
                if (error)
                    _err_ << "Error " << error << " on loading font " << fPath;
            }

            if (face)
            {
                m_font.reset(cairo_ft_font_face_create_for_ft_face(face, 0), [freetype, face](cairo_font_face_t* font)
                {
                    boost::mutex::scoped_lock lock = freetype->Lock();
                    cairo_font_face_destroy(font);
                    FT_Done_Face(face);
                });
            }
        }

    private:

        void Transform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height) override
        {
            static const float EPSILON = 0.00001F;

            AVPicture rgba;
			avpicture_alloc(&rgba, AV_PIX_FMT_RGBA, width, height);

			SwsContext* toRGBA = sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(toRGBA, picture.data, picture.linesize, 0, height, rgba.data, rgba.linesize);

            cairo_surface_t* surface = cairo_image_surface_create_for_data(rgba.data[0], CAIRO_FORMAT_ARGB32, width, height, rgba.linesize[0]);
            cairo_t* cairo = cairo_create(surface);
            cairo_set_font_face(cairo, m_font.get());
            cairo_set_font_size(cairo, m_fontSize);
            cairo_set_source_rgba(cairo, m_fontColor[0], m_fontColor[1], m_fontColor[2], m_fontColor[3]);

            cairo_font_extents_t fe;
            cairo_font_extents(cairo, &fe);

            // create text lines
            double lx = m_sw > EPSILON ? double(m_sx * width) : fe.height;
            double ly = m_sh > EPSILON ? double(m_sy * height) + fe.height : fe.height * 2;

            double tw = m_sw > EPSILON ? double(m_sw * width) : (width > fe.height * 2 ? width - fe.height * 2 : width);
            double th = m_sh > EPSILON ? double(m_sh * height) : (height > fe.height * 5 ? height - fe.height * 5 : height);
            if (ly + th > height - fe.height * 4)
                th = height - ly - fe.height * 4;

            cairo_rectangle(cairo, lx, ly, tw, th);

            size_t lc = th / fe.height;
            size_t i = m_lines.size() > lc ? m_lines.size() - lc : 0;

            for (; i < m_lines.size(); ++i)
            {
                if (!m_lines[i].empty())
                {
                    cairo_move_to(cairo, lx, ly);
                    cairo_show_text(cairo, m_lines[i].c_str());

                    cairo_text_extents_t te;
                    cairo_text_extents(cairo, m_lines[i].c_str(), &te);

                    if (m_fontStyle & FS_Underline)
                    {
                        cairo_move_to(cairo, lx, ly);
                        cairo_line_to(cairo, lx + te.width, ly);
                        cairo_stroke(cairo);
                    }
                    if (m_fontStyle & FS_Strikeout)
                    {
                        cairo_move_to(cairo, lx, ly - te.height / 2);
                        cairo_line_to(cairo, lx + te.width, ly - te.height / 2);
                        cairo_stroke(cairo);
                    }
                }

                ly += fe.height;
            }

            avpicture_fill(&picture, m_sample->GetBody(), format, width, height);

			SwsContext* fromRGBA = sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height, format, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(fromRGBA, rgba.data, rgba.linesize, 0, height, picture.data, picture.linesize);

            cairo_destroy(cairo);
            cairo_surface_destroy(surface);

            sws_freeContext(toRGBA);
            sws_freeContext(fromRGBA);
            avpicture_free(&rgba);
        }
    };

#endif
}

namespace NMMSS
{
    IFilter* CreateBurnTextFilter(DECLARE_LOGGER_ARG,
        ISampleReader* textSource,
        const std::wstring& fontPath,
        int fontIndex,
        int fontSize,
        int fontStyle,
        unsigned char color[4],
        float x1, float y1, float x2, float y2,
        EBurnMode mode, uint64_t beginTime, uint64_t endTime)
    {
        return new CPullFilterImpl<CBurnTextTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CBurnTextTransformer(GET_LOGGER_PTR, textSource, fontPath, fontIndex, fontSize, fontStyle, color, x1, y1, x2, y2, mode, beginTime, endTime)
            );
    }
}
