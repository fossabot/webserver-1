#ifndef D3D_WRAPPER_H
#define D3D_WRAPPER_H

#include "../MMCoding/Points.h"

#include <atlbase.h>
#include <memory>

struct ID3D11Device;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11DeviceContext;
struct ID3D11InputLayout;
struct ID3D11SamplerState;

enum DXGI_FORMAT;

class DXProgram
{
public:
    CComPtr<ID3D11VertexShader> VertexShader;
    CComPtr<ID3D11PixelShader> PixelShader;
    DXGI_FORMAT SourceFormat[2];
};

class RenderTarget
{
public:
    CComPtr<ID3D11Texture2D> Target;
    CComPtr<ID3D11RenderTargetView> View;
    Point Size;
};

class D3DContext;

class D3DWrapper
{
public:
    D3DWrapper(CComPtr<ID3D11Device> device);

    CComPtr<ID3D11Buffer> CreateBuffer(const void* data, int size, bool vertices);
    CComPtr<ID3D11Texture2D> CreateTexture(const Point& size, DXGI_FORMAT format, bool cpuRead, bool mipmaps = false, bool notRenderTarget = false);
    CComPtr<ID3D11RenderTargetView> CreateRTView(CComPtr<ID3D11Texture2D> texture, DXGI_FORMAT format);
    RenderTarget CreateRenderTarget(const Point& size, DXGI_FORMAT format, int viewFormat = 0, bool mipmaps = false);
    void Setup(ID3D11DeviceContext& context);

public:
    CComPtr<ID3D11Device> m_device;
    CComPtr<ID3D11DeviceContext> m_immediateContext;
    CComPtr<ID3D11VertexShader> m_vertexShader;
    CComPtr<ID3D11InputLayout> m_layout;
    CComPtr<ID3D11SamplerState> m_sampler;
    CComPtr<ID3D11Buffer> m_vertices;
    DXProgram m_copyY, m_copyNV, m_copyUV, m_copyRGB;
};

class D3DContext
{
public:
    D3DContext(std::shared_ptr<D3DWrapper> wrapper);

    void UpdateConstants(float yFactor, float invWidth, bool force = true);
    void Begin(float yFactor, float invWidth, bool setupContext = true);
    void End();
    void SetTarget(const RenderTarget& dst, const DXProgram& program);
    void Draw(ID3D11Texture2D* src1, ID3D11Texture2D* src2, bool mipmaps, const Box& rect = {});
    void Draw(ID3D11Texture2D* src1, ID3D11Texture2D* src2, const RenderTarget& dst, const DXProgram& program, bool mipmaps);
    void Draw(ID3D11Texture2D& src, const RenderTarget& dst, const DXProgram& program);
    ID3D11DeviceContext& Context();

private:
    std::shared_ptr<D3DWrapper> m_wrapper;
    CComPtr<ID3D11DeviceContext> m_context;
    CComPtr<ID3D11Buffer> m_constants;
    const DXProgram* m_program{};
    const RenderTarget* m_target;

    float m_yFactor{}, m_invWidth{};
};

#endif
