#include "HWAccelerated.h"
#include <Logging/log2.h>
#include <CorbaHelpers/Envar.h>

#include "CoordinateTransform.h"
#include "HWCodecs/HWDevicePool.h"


namespace NMMSS
{
    class IFilter;
    class IFrameGeometryAdvisor;

    bool HasHardwareAccelerationForVideoDecoding(const HWDecoderRequirements& requirements)
    {
#ifdef NGP_HWPLATFORM_ODROIDU3
        return true;
#endif
        return (requirements.Destination != EMemoryDestination::ToPrimaryVideoMemory || !NCorbaHelpers::CEnvar::NgpDisableHWDecoding()) &&
            !!HWDevicePool::Instance()->NextDevice(nullptr, requirements);
    }

    void DeinitHardwareAccelerationForVideoDecoding()
    {
        HWDevicePool::Deinitialize();
    }

    IFilter* CreateHardwareAcceleratedVideoDecoderPullFilter(DECLARE_LOGGER_ARG, const DecoderAdvisors& advisors, const HWDecoderOptionalSettings& settings)
    {
#ifdef NGP_HWPLATFORM_ODROIDU3
        IFilter* CreateExynosVideoDecoderPullFilter(DECLARE_LOGGER_ARG, IFrameGeometryAdvisor*);
        return CreateExynosVideoDecoderPullFilter(GET_LOGGER_PTR, advisor);
#endif
        extern IFilter* CreateHWVideoDecoderPullFilter(DECLARE_LOGGER_ARG, const DecoderAdvisors& advisors, const HWDecoderOptionalSettings&);
        return CreateHWVideoDecoderPullFilter(GET_LOGGER_PTR, advisors, settings);
    }

#ifndef _WIN32
    IFilter* CreateCudaDewarpFilter(DECLARE_LOGGER_ARG, DewarpCircle circles[2], ICoordinateTransform** transform)
    {
        return nullptr;
    }
#endif
}
