#include "TweakableFilter.h"
#include <boost/type_erasure/any_cast.hpp>

namespace NMMSS {

    ITweakableFilter* CreateDecimationFilter(DECLARE_LOGGER_ARG, NAugment::Decimation const&);
    ITweakableFilter* CreateBufferFilter(DECLARE_LOGGER_ARG, NAugment::Buffer const&);
    ITweakableFilter* CreateBIMWDownscaleFilter(DECLARE_LOGGER_ARG, NAugment::BIMWDownscale const&);

    ITweakableFilter* CreateTweakableFilter(DECLARE_LOGGER_ARG, CAugment const& augmentation)
    {
        if (auto aug = boost::type_erasure::any_cast<NAugment::Decimation const*>(&augmentation))
        {
            return CreateDecimationFilter(GET_LOGGER_PTR, *aug);
        }
        if (auto aug = boost::type_erasure::any_cast<NAugment::Buffer const*>(&augmentation))
        {
            return CreateBufferFilter(GET_LOGGER_PTR, *aug);
        }
        if (auto aug = boost::type_erasure::any_cast<NAugment::BIMWDownscale const*>(&augmentation))
        {
            return CreateBIMWDownscaleFilter(GET_LOGGER_PTR, *aug);
        }
        return nullptr;
    }

} // namespace NMMSS
