#include "DXAllocator.h"
#include "HiddenDXDevice.h"
#include <initguid.h>
#include <d3d11.h>


DXAllocator::DXAllocator(CComPtr<ID3D11Device> device, MFXVideoSession& session, ExternalSurfacesSP surfaces)
{
    m_device = device;
    m_surfaces = surfaces ? std::static_pointer_cast<QSExternalSurfaces>(surfaces) : std::make_shared<QSExternalSurfaces>();

    m_status = session.SetHandle(MFX_HANDLE_D3D11_DEVICE, m_device.p);
    if (m_status >= 0)
    {
        m_status = session.SetFrameAllocator(this);
    }
}

DXAllocator::~DXAllocator()
{
}

mfxStatus DXAllocator::AllocFramesInternal(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response)
{
    if(m_status >= 0)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = request->Info.Width;
        desc.Height = request->Info.Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
        for (int i = 0; i < (int)request->NumFrameSuggested; ++i)
        {
            if (m_surfaces->Count() <= i)
            {
                CComPtr<ID3D11Texture2D> surface;
                HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &surface);
                if (FAILED(hr))
                {
                    return MFX_ERR_MEMORY_ALLOC;
                }
                m_surfaces->Surfaces.push_back(surface);
            }
            m_mids.push_back(m_surfaces->Surfaces[i].p);
        }

        response->mids = m_mids.data();
        response->NumFrameActual = request->NumFrameSuggested;
    }
    return m_status;
}

mfxStatus DXAllocator::GetFrameHDL(mfxMemId mid, mfxHDL* handle)
{
    if (!handle)
    {
        return MFX_ERR_INVALID_HANDLE;
    } 

    mfxHDLPair* pair = (mfxHDLPair*)handle;
    pair->first = mid;
    pair->second = 0;
    return MFX_ERR_NONE;
}

void DXAllocator::SetupFrameData(mfxFrameData& data, mfxMemId mid)
{
    data.MemId = mid;
}

ExternalSurfacesSP CreateQSExternalSurfaces()
{
    return std::make_shared<QSExternalSurfaces>();
}

std::shared_ptr<BaseQSAllocator> CreateQSAllocator(QSDeviceSP device, MFXVideoSession& session, ExternalSurfacesSP surfaces)
{
    return std::make_shared<DXAllocator>(std::static_pointer_cast<HiddenDxDevice>(device)->GetDevice(), session, surfaces);
}