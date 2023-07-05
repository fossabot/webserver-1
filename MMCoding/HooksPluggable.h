#ifndef MMSS_MMCODING_HOOKS_PLUGGABLE_H
#define MMSS_MMCODING_HOOKS_PLUGGABLE_H

#include <Logging/log2.h>
#include "../MMSS.h"
#include "Transforms.h"

namespace NMMSS
{

enum EPluggableFilterPriority
{
    PFP_AboveStandard,
    PFP_BelowStandard
};

NMMSS::IFilter* CreateStandardVideoDecoderPullFilter(DECLARE_LOGGER_ARG, 
                                                                   NMMSS::IFrameGeometryAdvisor*, bool multithreaded);
NMMSS::IFilter* CreatePluggableVideoDecoderPullFilter(EPluggableFilterPriority priority, 
                                                                    DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor*);

}

#endif
