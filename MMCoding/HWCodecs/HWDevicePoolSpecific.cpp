#include "HWDevicePool.h"
#include "IHWDevice.h"
#include "IGLRenderer.h"
#include "../MediaType.h"


void AddCudaDevices(std::vector<HWDeviceSP>& devices);

#if defined(_WIN64) || defined(__x86_64)
void AddQuickSyncDevices(std::vector<HWDeviceSP>& devices);
NMMSS::IGLRenderer* CreateQSGLRenderer(NLogging::ILogger*);
void InitializeQSGLRenderer();
#endif

#ifdef NGP_PRODUCT_FEATURE_HUAWEIASCEND
void AddAscendDevices(std::vector<HWDeviceSP>& devices);
#endif

void HWDevicePool::addDevices(std::vector<HWDeviceSP>& devices)
{
    AddCudaDevices(devices);

#if defined(_WIN64) || defined(__x86_64)
    AddQuickSyncDevices(devices);
#endif

#ifdef NGP_PRODUCT_FEATURE_HUAWEIASCEND
    AddAscendDevices(devices);
#endif
}

NMMSS::IGLRenderer* CreateCudaGLRenderer();

namespace NMMSS
{
    NMMSS::IGLRenderer* CreateGLRenderer(NLogging::ILogger* ngp_Logger_Ptr_, ISample& sample)
    {
        using THeader = NMMSS::NMediaType::Video::VideoMemory;
        const auto& header = sample.SubHeader<THeader>();
        if (header.Type == THeader::EVideoMemoryType::CUDA)
        {
            return CreateCudaGLRenderer();
        }
#if defined(_WIN64) || defined(__x86_64)
        else if (header.Type == THeader::EVideoMemoryType::DX11 || header.Type == THeader::EVideoMemoryType::VA)
        {
            return CreateQSGLRenderer(ngp_Logger_Ptr_);
        }
#endif
        return nullptr;
    }

    void InitializeGLRenderer()
    {
#if defined(_WIN64) || defined(__x86_64)
        InitializeQSGLRenderer();
#endif
    }
}
