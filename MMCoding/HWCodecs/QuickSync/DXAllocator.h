#ifndef DX_ALLOCATOR_H
#define DX_ALLOCATOR_H

#include <windows.h>
#include <atlbase.h>
#include "BaseSampleTransformer.h"

struct ID3D11Device;
struct ID3D11Texture2D;

class QSExternalSurfaces : public IExternalSurfaces
{
public:
    int Count() const override
    {
        return (int)Surfaces.size();
    }

    std::vector<CComPtr<ID3D11Texture2D>> Surfaces;
};

class DXAllocator  : public BaseQSAllocator
{
public:
    DXAllocator(CComPtr<ID3D11Device> device, MFXVideoSession& session, ExternalSurfacesSP externalSurfaces);
    ~DXAllocator();
    
public:
    mfxStatus GetFrameHDL(mfxMemId mid, mfxHDL* handle) override;
    void SetupFrameData(mfxFrameData& data, mfxMemId mid) override;

protected:
    mfxStatus AllocFramesInternal(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response) override;

private:
    CComPtr<ID3D11Device> m_device;
    std::shared_ptr<QSExternalSurfaces> m_surfaces;
};

#endif
