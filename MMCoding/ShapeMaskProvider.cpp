#include <CorbaHelpers/RefcountedImpl.h>
#include "Transforms.h"

namespace
{
    const uint32_t MASK_WIDTH = 240;
    const uint32_t MASK_HEIGHT = 180;

    class CShapeMaskProvider : public virtual NMMSS::IPixelMaskProvider, public virtual NCorbaHelpers::CRefcountedImpl
    {
        DECLARE_LOGGER_HOLDER;
        
        NMMSS::TTimeMaskList               m_timeMask;
        boost::shared_array<unsigned char> m_stencil;

    public:

        CShapeMaskProvider(DECLARE_LOGGER_ARG, const NMMSS::TPolygonList& staticMask, const NMMSS::TTimeMaskList& timeMask)
            : m_timeMask(timeMask)
        {
            INIT_LOGGER_HOLDER;

            if (!staticMask.empty())
            {
                m_stencil.reset(new unsigned char[MASK_WIDTH * MASK_HEIGHT]);
                std::memset(m_stencil.get(), 0, MASK_WIDTH * MASK_HEIGHT);
                std::for_each(staticMask.begin(), staticMask.end(), [&](const NMMSS::Polygon& mask) { ApplyMask(mask, m_stencil.get()); });
            }
        }

        NMMSS::TPixelMask Advance(const boost::posix_time::ptime& time) override
        {
            NMMSS::TTimeMaskList timeMask;
            std::copy_if(m_timeMask.begin(), m_timeMask.end(), std::back_inserter(timeMask), [&](const NMMSS::TTimeMask& rep) {
                return rep.first.contains(time);
            });

            if (m_stencil.get() || !timeMask.empty())
            {
                boost::shared_array<unsigned char> stencil(new unsigned char[MASK_WIDTH * MASK_HEIGHT]);
                if (m_stencil.get())
                    std::memcpy(stencil.get(), m_stencil.get(), MASK_WIDTH * MASK_HEIGHT);
                else
                    std::memset(stencil.get(), 0, MASK_WIDTH * MASK_HEIGHT);

                std::for_each(timeMask.begin(), timeMask.end(), [&](const NMMSS::TTimeMask& rep) {
                    if (rep.first.contains(time))
                        ApplyMask(rep.second, stencil.get());
                });
                return std::make_tuple(stencil, MASK_WIDTH, MASK_HEIGHT);
            }

            return std::make_tuple(boost::shared_array<unsigned char>(), 0u, 0u);
        }

    protected:

        void ApplyMask(const NMMSS::Polygon& space, unsigned char* stencil)
        {
            NMMSS::Polygon polygon;
            boost::geometry::strategy::transform::scale_transformer<double, 2, 2> scale(MASK_WIDTH, MASK_HEIGHT);
            boost::geometry::transform(space, polygon, scale);

            NMMSS::Box box;
            boost::geometry::envelope(polygon, box);

            int32_t minX = std::max(static_cast<int32_t>(std::min(box.min_corner().x(), box.max_corner().x())), 0);
            int32_t maxX = std::min(static_cast<int32_t>(std::max(box.min_corner().x(), box.max_corner().x())), static_cast<int32_t>(MASK_WIDTH));
            int32_t minY = std::max(static_cast<int32_t>(std::min(box.min_corner().y(), box.max_corner().y())), 0);
            int32_t maxY = std::min(static_cast<int32_t>(std::max(box.min_corner().y(), box.max_corner().y())), static_cast<int32_t>(MASK_HEIGHT));

            for (int32_t y = minY; y < maxY; ++y)
            {
                for (int32_t x = minX; x < maxX; ++x)
                {
                    if (boost::geometry::within(NMMSS::Point(x, y), polygon))
                        stencil[y * MASK_WIDTH + x] = true;
                }
            }
        }
    };
}

namespace NMMSS
{
    IPixelMaskProvider* CreateShapeMaskProvider(DECLARE_LOGGER_ARG, const TPolygonList& staticMask, const TTimeMaskList& timeMask)
    {
        return new CShapeMaskProvider(GET_LOGGER_PTR, staticMask, timeMask);
    }
}
