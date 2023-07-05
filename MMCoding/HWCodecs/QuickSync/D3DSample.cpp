#include "D3DSample.h"
#include "../MediaType.h"
#include "D3DWrapper.h"
#include "HiddenDXDevice.h"
#include <d3d11.h>

namespace
{
    D3DSampleData& sampleData(NMMSS::ISample& sample)
    {
        return *reinterpret_cast<D3DSampleData*>(sample.GetBody());
    }

    const D3DSampleData& sampleData(const NMMSS::ISample& sample)
    {
        return sampleData(const_cast<NMMSS::ISample&>(sample));
    }
}

D3DSample::D3DSample(HiddenDxDeviceSP device) :
    VideoMemorySample(NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::DX11),
    m_Device(device)
{
}

void D3DSample::Setup(const Point& size, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    if (m_Target.Size != size)
    {
        if (CreateSample(holder, sizeof(D3DSampleData), false))
        {
            m_Target = m_Device->GetWrapper()->CreateRenderTarget(size, DXGI_FORMAT_R8G8B8A8_UNORM);
            sampleData(*m_sample) = { m_Device.get(), &m_Target };
        }
    }

    VideoMemorySample::Setup({size.X, size.Y}, {}, timestamp);
}

HiddenDxDeviceSP D3DSample::GetDevice(const NMMSS::ISample& sample)
{
    return std::static_pointer_cast<HiddenDxDevice>(sampleData(sample).Device->shared_from_this());
}

RenderTarget& D3DSample::GetRenderTarget(const NMMSS::ISample& sample)
{
    return *sampleData(sample).Target;
}
