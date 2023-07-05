#ifndef D3D_SAMPLE_H
#define D3D_SAMPLE_H

#include "../MMCoding/HWCodecs/VideoMemorySample.h"
#include "../MMCoding/HWCodecs/QuickSync/D3DWrapper.h"

struct ID3D11Texture2D;

class HiddenDxDevice;
using HiddenDxDeviceSP = std::shared_ptr<HiddenDxDevice>;

class D3DSampleData
{
public:
    HiddenDxDevice* Device{};
    RenderTarget* Target;
    int64_t Reserved0{};
    int64_t Reserved1{};
    int64_t Reserved2{};
    int64_t Reserved3{};

};

class D3DSample : public VideoMemorySample
{
public:
    D3DSample(HiddenDxDeviceSP device);

    void Setup(const Point& size, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);

    static HiddenDxDeviceSP GetDevice(const NMMSS::ISample& sample);
    static RenderTarget& GetRenderTarget(const NMMSS::ISample& sample);

public:
    HiddenDxDeviceSP m_Device;
    RenderTarget m_Target;
};

#endif
