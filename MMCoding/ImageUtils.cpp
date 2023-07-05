#include "Transforms.h"

#include <CorbaHelpers/GccUtils.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#endif

GCC_SUPPRESS_WARNING_BEGIN((-Wunused-local-typedefs)(-Wparentheses))

#include <boost/gil/image.hpp>
#include <boost/gil/gil_all.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <boost/gil/extension/io/png.hpp>

GCC_SUPPRESS_WARNING_END();

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace NMMSS
{
    void CutOutImageArea(const std::string& src, const std::string& dst, float x1, float y1, float x2, float y2)
    {
        if (x1 > x2) std::swap(x1, x2);
        if (y1 > y2) std::swap(y1, y2);

        x1 = std::max(std::min(x1, 1.0f), 0.0f);
        x2 = std::max(std::min(x2, 1.0f), 0.0f);
        y1 = std::max(std::min(y1, 1.0f), 0.0f);
        y2 = std::max(std::min(y2, 1.0f), 0.0f);

        boost::filesystem::path path(src, boost::filesystem::detail::utf8_codecvt_facet());
        std::string ext = path.extension().string();
        boost::algorithm::to_lower(ext);

        boost::gil::rgb8_image_t in_image;

        if (ext == ".png")
        {
            boost::gil::read_and_convert_image(path.generic_string(), in_image, boost::gil::png_tag());
        }
        else
        {
            boost::gil::read_and_convert_image(path.generic_string(), in_image, boost::gil::jpeg_tag());
        }

        int x = int(in_image.width() * x1);
        int y = int(in_image.height() * y1);
        int w = int(in_image.width() * (x2 - x1));
        int h = int(in_image.height() * (y2 - y1));

        boost::gil::rgb8_image_t out_image(w, h);

        boost::gil::copy_pixels(boost::gil::subimage_view(boost::gil::view(in_image), x, y, w, h), boost::gil::view(out_image));

        path = boost::filesystem::path(dst, boost::filesystem::detail::utf8_codecvt_facet());
        ext = path.extension().string();
        boost::algorithm::to_lower(ext);

        if (ext == ".png")
        {
            boost::gil::write_view(path.generic_string(), boost::gil::const_view(out_image), boost::gil::png_tag());
        }
        else
        {
            boost::gil::write_view(path.generic_string(), boost::gil::const_view(out_image), boost::gil::image_write_info<boost::gil::jpeg_tag>(boost::gil::jpeg_quality::default_value));
        }
    }
}
