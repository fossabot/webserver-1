#pragma once

#include "MMCodingExports.h"
#include <CorbaHelpers/Refcounted.h>
#include <string>
#include <unordered_set>

namespace NLogging
{
    class ILogger;
};

namespace NStatisticsAggregator
{
    class IStatisticsAggregatorImpl;
}

namespace NMMSS
{
    class IFilter;
    class IFrameGeometryAdvisor;
    class ICallback;

    const uint32_t MASK_ANY = 0xFFFFFFFF;

    enum EHWDeviceType : uint32_t
    {
        NoDevice = 0,
        IntelQuickSync = 0x01,
        NvidiaCUDA = 0x02,
        HuaweiAscend = 0x04,
        CPU = 0x08,
        Any = MASK_ANY
    };

    enum EMemoryDestination : uint32_t
    {
        Auto = 0,
        ToSystemMemory = 0x01,
        ToPrimaryVideoMemory = 0x02
    };

    class HWDecoderRequirements
    {
    public:
        EHWDeviceType DeviceType{ EHWDeviceType::Any };
        EMemoryDestination Destination{ EMemoryDestination::ToPrimaryVideoMemory };
        uint32_t GpuIdMask{ MASK_ANY };
        bool KeySamplesOnly{};

        bool operator==(const HWDecoderRequirements& r) const
        {
            return DeviceType == r.DeviceType &&
                Destination == r.Destination &&
                GpuIdMask == r.GpuIdMask &&
                KeySamplesOnly == r.KeySamplesOnly;
        }

        bool operator!=(const HWDecoderRequirements& r) const
        {
            return !(*this == r);
        }
    };

    class IHWDecoderAdvisor : public virtual NCorbaHelpers::IRefcounted
    {
    public:
        virtual bool GetRequirementsIfChanged(NMMSS::HWDecoderRequirements& requirements, int64_t& requirementsRevision) const = 0;
        virtual bool GetTargetProcessIdsIfChanged(std::unordered_set<int32_t>& processIds, int64_t& processIdsRevision) const = 0;
    };

    using PHWDecoderAdvisor = NCorbaHelpers::CAutoPtr<IHWDecoderAdvisor>;

    class DecoderAdvisors
    {
    public:
        IFrameGeometryAdvisor* GeometryAdvisor{};
        IHWDecoderAdvisor* DecoderAdvisor{};
    };

    class HWDecoderOptionalSettings
    {
    public:
        std::string OwnerEndpoint;
        NStatisticsAggregator::IStatisticsAggregatorImpl* StatSink{};
        NMMSS::ICallback* OnCannotFindDecoder{};
    };

    MMCODING_DECLSPEC bool HasHardwareAccelerationForVideoDecoding(const HWDecoderRequirements& requirements = {});

    MMCODING_DECLSPEC IFilter* CreateHardwareAcceleratedVideoDecoderPullFilter(NLogging::ILogger* ngp_Logger_Ptr_,
        const DecoderAdvisors& advisors, const HWDecoderOptionalSettings& settings = {});

    MMCODING_DECLSPEC IFilter* CreateHWDecoderSharedReceiver(NLogging::ILogger* ngp_Logger_Ptr_, IFrameGeometryAdvisor* advisor, 
        const NMMSS::HWDecoderRequirements& requirements);

    MMCODING_DECLSPEC void DeinitHardwareAccelerationForVideoDecoding();
}
