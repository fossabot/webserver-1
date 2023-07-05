#include <unordered_map>
#include <boost/algorithm/string.hpp>
#include <CorbaHelpers/Envar.h>
#include "ImageTransformerBase.h"

#ifdef _MSC_VER

#include "SDLttfLib.h"
#pragma warning(push)
#pragma warning(disable : 4244)

#else

#include "FreetypeLib.h"
#include <cairo.h>
#include <cairo-ft.h>

#endif

namespace
{
    class CBurnSubtitleTransformerBase : public CImageTransformerBase
    {
        std::wstring               m_timeFormat;
        boost::posix_time::minutes m_timezone;
        std::string                m_comment;
        NMMSS::TTimeCommentList    m_timeComments;
        const std::string          m_preferredLocale;

    protected:
        std::string getFontPath() const noexcept
        {           
            static constexpr char DEFAULT_FONT[] = "PTC55F.ttf";
            std::string selectedFont;
            if (!m_preferredLocale.empty())
            {
                static const std::unordered_map<std::string, std::vector<std::string> > FONTS_MAP =
                { { "NotoSansSC-VF.ttf", {"zh","cn"} } };

                for (const auto& font : FONTS_MAP)
                {
                    for (const auto& locale : font.second)
                        if (boost::ifind_first(m_preferredLocale, locale))
                        {
                            selectedFont = font.first;
                            break;
                        }

                    if (!selectedFont.empty())
                        break;
                }
            }

            if (selectedFont.empty())
                selectedFont = DEFAULT_FONT;

            std::string fPath = ITV8::Library::GetCurrentExecPath().generic_string() + "/" + selectedFont;

            if (!boost::filesystem::exists(fPath))
                fPath = NCorbaHelpers::CEnvar::NgpBinDir() + "/" + DEFAULT_FONT;

            return fPath;
        }

    public:

        CBurnSubtitleTransformerBase(DECLARE_LOGGER_ARG,
                                 const std::wstring& timeFormat = std::wstring(),
                                 const boost::posix_time::minutes& timezone = boost::posix_time::minutes(0),
                                 const std::wstring& comment = std::wstring(),
                                 const NMMSS::TTimeCommentList& timeComments = NMMSS::TTimeCommentList(),
                                 const std::string& preferredLocale = "")
            : CImageTransformerBase(GET_LOGGER_PTR)
            , m_timeFormat(timeFormat)
            , m_timezone(timezone)
            , m_comment(NCorbaHelpers::ToUtf8(comment))
            , m_timeComments(timeComments)
            , m_preferredLocale(preferredLocale)
        {
        }

        void MakeSubtitles(uint64_t timestamp, std::vector<std::string>& lines)
        {
            boost::posix_time::ptime time = NMMSS::PtimeFromQword(timestamp);

            lines.push_back(NCorbaHelpers::ToUtf8(NMMSS::UtcTimeToSpecifiedLocalAsString(time, m_timezone, m_timeFormat, m_preferredLocale)));

            for (const auto& node : m_timeComments)
            {
                if (node.first.contains(time))
                {
                    std::vector<std::string> temp;
                    boost::split(temp, node.second, boost::is_any_of("\n"));
                    std::copy(temp.begin(), temp.end(), std::back_inserter(lines));
                }
            }

            std::vector<std::string> temp;
            boost::split(temp, m_comment, boost::is_any_of("\n"));
            std::copy(temp.begin(), temp.end(), std::back_inserter(lines));
        }
    };

#ifdef _MSC_VER

    class CBurnSubtitleTransformer : public CBurnSubtitleTransformerBase
    {
        TTF_Font* m_font;
        SDL_Color m_color;

    public:

        CBurnSubtitleTransformer(DECLARE_LOGGER_ARG,
                                 const std::wstring& timeFormat = std::wstring(),
                                 const boost::posix_time::minutes& timezone = boost::posix_time::minutes(0),
                                 const std::wstring& comment = std::wstring(),
                                 const NMMSS::TTimeCommentList& timeComments = NMMSS::TTimeCommentList(),
                                 const std::wstring& fontPath = std::wstring(),
                                 int fontIndex = 0,
                                 int fontSize = 0,
                                 int fontStyle = 0,
                                 unsigned char color[3] = 0,
                                 const std::string& preferredLocale = "")
            : CBurnSubtitleTransformerBase(GET_LOGGER_PTR, timeFormat, timezone, comment, timeComments, preferredLocale)
            , m_font(0)
        {
            boost::mutex::scoped_lock lock = NMMSS::CSDLttfLib::Lock();

            if (!fontPath.empty())
                m_font = TTF_OpenFontIndex(NCorbaHelpers::ToUtf8(fontPath).c_str(), fontSize > 0 ? fontSize : 20, fontIndex);
            
            if (!m_font)
            {
                const auto fPath = getFontPath();
                m_font = TTF_OpenFontIndex(fPath.c_str(), fontSize > 0 ? fontSize : 20, fontIndex);
                if (!m_font)
                    throw std::runtime_error("Unable to load font");
            }

            TTF_SetFontStyle(m_font, fontStyle);

            if (color)
                m_color = {color[0], color[1], color[2], 0};
            else
                m_color = {255, 255, 255, 0};
        }

        ~CBurnSubtitleTransformer()
        {
            boost::mutex::scoped_lock lock = NMMSS::CSDLttfLib::Lock();
            TTF_CloseFont(m_font);
        }

    private:

        void Transform(AVPicture& picture, AVPixelFormat format, uint32_t width, uint32_t height) override
        {
            // make source picture
            AVPicture rgbPicture1, rgbPicture2;
			avpicture_alloc(&rgbPicture1, AV_PIX_FMT_RGB24, width, height);

			SwsContext* rgbContext = sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(rgbContext, picture.data, picture.linesize, 0, height, rgbPicture1.data, rgbPicture1.linesize);

            // create main surface
            SDL_Surface *frameSurface = SDL_CreateRGBSurfaceFrom(rgbPicture1.data[0], width, height, 24, rgbPicture1.linesize[0], 0, 0, 0, 0);

            // burn footer
            std::vector<std::string> lines;
            MakeSubtitles(m_sample->Header().dtTimeBegin, lines);

            int lineHeight = TTF_FontHeight(m_font);
            int linePos = height;
            for (auto& line : lines)
            {
                linePos -= lineHeight;

                if (linePos >= 0)
                {
                    if (!line.empty())
                    {
                        boost::mutex::scoped_lock lock = NMMSS::CSDLttfLib::Lock();
                        SDL_Surface* lineSurface = TTF_RenderUTF8_Blended(m_font, line.c_str(), m_color);
                        lock.unlock();

                        int W = static_cast<int>(width);
                        SDL_Rect place =
                        {
                            W > lineSurface->w ? (W - lineSurface->w) / 2 : 0,
                            linePos,
                            W > lineSurface->w ? lineSurface->w : W,
                            lineHeight
                        };

                        SDL_Rect area =
                        {
                            W > lineSurface->w ? 0 : (lineSurface->w - W) / 2,
                            0,
                            W > lineSurface->w ? lineSurface->w : W,
                            lineHeight
                        };

                        SDL_BlitSurface(lineSurface, &area, frameSurface, &place);
                        SDL_FreeSurface(lineSurface);
                    }
                }
                else
                {
                    break;
                }
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

    class CBurnSubtitleTransformer : public CBurnSubtitleTransformerBase
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
        double m_fontColor[3];

    public:

        CBurnSubtitleTransformer(DECLARE_LOGGER_ARG,
                                 const std::wstring& timeFormat = std::wstring(),
                                 const boost::posix_time::minutes& timezone = boost::posix_time::minutes(0),
                                 const std::wstring& comment = std::wstring(),
                                 const NMMSS::TTimeCommentList& timeComments = NMMSS::TTimeCommentList(),
                                 const std::wstring& fontPath = std::wstring(),
                                 int fontIndex = 0,
                                 int fontSize = 0,
                                 int fontStyle = 0,
                                 unsigned char fontColor[3] = 0,
                                 const std::string& preferredLocale = "")
            : CBurnSubtitleTransformerBase(GET_LOGGER_PTR, timeFormat, timezone, comment, timeComments, preferredLocale)
            , m_fontSize(fontSize > 0 ? fontSize : 20)
            , m_fontStyle(fontStyle)
        {
            m_fontColor[0] = double(fontColor[0]) / 255.0;
            m_fontColor[1] = double(fontColor[1]) / 255.0;
            m_fontColor[2] = double(fontColor[2]) / 255.0;

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
                const auto fPath = getFontPath();

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
            AVPicture rgba;
            avpicture_alloc(&rgba, AV_PIX_FMT_RGBA, width, height);

            SwsContext* toRGBA = sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGBA, SWS_BICUBIC, NULL, NULL, NULL);
            sws_scale(toRGBA, picture.data, picture.linesize, 0, height, rgba.data, rgba.linesize);

            cairo_surface_t* surface = cairo_image_surface_create_for_data(rgba.data[0], CAIRO_FORMAT_ARGB32, width, height, rgba.linesize[0]);
            cairo_t* cairo = cairo_create(surface);
            cairo_set_font_face(cairo, m_font.get());
            cairo_set_font_size(cairo, m_fontSize);
            cairo_set_source_rgb(cairo, m_fontColor[0] , m_fontColor[1], m_fontColor[2]);

            std::vector<std::string> lines;
            MakeSubtitles(m_sample->Header().dtTimeBegin, lines);

            cairo_font_extents_t fe;
            cairo_font_extents(cairo, &fe);

            double yPos = height - fe.descent;
            for (auto& line : lines)
            {
                if (yPos >= fe.height + fe.ascent)
                {
                    if (!line.empty())
                    {
                        cairo_text_extents_t te;
                        cairo_text_extents(cairo, line.c_str(), &te);

                        double xPos = (double(width) - te.width) / 2;
                        cairo_move_to(cairo, xPos, yPos);
                        cairo_show_text(cairo, line.c_str());

                        if (m_fontStyle & FS_Underline)
                        {
                            cairo_move_to(cairo, xPos, yPos);
                            cairo_line_to(cairo, xPos + te.width, yPos);
                            cairo_stroke(cairo);
                        }
                        if (m_fontStyle & FS_Strikeout)
                        {
                            cairo_move_to(cairo, xPos, yPos - te.height / 2);
                            cairo_line_to(cairo, xPos + te.width, yPos - te.height / 2);
                            cairo_stroke(cairo);
                        }
                    }
                    yPos -= fe.height;
                }
                else
                {
                    break;
                }
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
    IFilter* CreateBurnSubtitleFilter(DECLARE_LOGGER_ARG,
                                      const std::wstring& timeFormat,
                                      const boost::posix_time::minutes& timezone,
                                      const std::wstring& comment,
                                      const TTimeCommentList& timeComments,
                                      const std::wstring& fontPath,
                                      int fontIndex,
                                      int fontSize,
                                      int fontStyle,
                                      unsigned char color[3],
                                      const std::string& preferredLocale)
    {
        return new CPullFilterImpl<CBurnSubtitleTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CBurnSubtitleTransformer(GET_LOGGER_PTR, timeFormat, timezone, comment, timeComments, fontPath, fontIndex, fontSize, fontStyle, color, preferredLocale)
            );
    }
}


#ifdef _MSC_VER
#pragma warning(pop)
#endif
