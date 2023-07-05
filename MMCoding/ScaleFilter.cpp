#include "Transforms.h"
#include "../ScaleTransformer.h"

namespace NMMSS
{
    uint8_t CalculateScalingShift(uint32_t requested, uint32_t actual)
    {
        /// @note scale actual while it exceeds (requested * 1.25)
        uint8_t scalingShift = 0;
        actual <<= 2;
        requested *= 5;
        while (actual > requested)
        {
            actual >>= 1;
            ++scalingShift;
        }
        return scalingShift;
    }

    IFilter* CreateScaleFilter(
        DECLARE_LOGGER_ARG,
        uint32_t width, uint32_t height,
        bool storeAspectRatio
    )
    {
        return new CPullFilterImpl<CScaleTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CScaleTransformer(GET_LOGGER_PTR, width, height, storeAspectRatio)
        );
    }
}
