#include "D3DWrapper.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <string>
#include <array>


const std::string VSHADER = R"RawLiteral(
struct VS_INPUT
{
    float4 Pos : POSITION;
    float2 Tex : TEXCOORD;
};

struct VS_OUTPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
};

VS_OUTPUT VS(VS_INPUT input)
{
    return input;
}
)RawLiteral";


const std::string PSHADER_COPY_Y = R"RawLiteral(
struct PS_INPUT
{
	float4 pos         : SV_POSITION;
	float2 texCoord    : TEXCOORD0;
};

Texture2D<float>  luminanceChannel   : t0;
SamplerState      defaultSampler     : s0;

cbuffer PS_CONSTANTS : register(b0)
{
    float invWidth;
    float yFactor;
};

min16float PS(PS_INPUT input) : SV_TARGET
{
	float y = luminanceChannel.Sample(defaultSampler, float2(input.texCoord.x, input.texCoord.y * yFactor));
	return min16float(y);
}
)RawLiteral";

const std::string PSHADER_COPY_NV = R"RawLiteral(
struct PS_INPUT
{
	float4 pos         : SV_POSITION;
	float2 texCoord    : TEXCOORD0;
};

Texture2D<float2> chrominanceChannel : t0;
SamplerState      defaultSampler     : s0;

cbuffer PS_CONSTANTS : register(b0)
{
    float invWidth;
    float yFactor;
};

min16float2 PS(PS_INPUT input) : SV_TARGET
{
	float2 uv = chrominanceChannel.Sample(defaultSampler, float2(input.texCoord.x, input.texCoord.y * yFactor));
	return min16float2(uv);
}
)RawLiteral";

const std::string PSHADER_COPY_UV = R"RawLiteral(
struct PS_INPUT
{
	float4 pos         : SV_POSITION;
	float2 texCoord    : TEXCOORD0;
};

Texture2D<float2> nvChannel : t0;
SamplerState      defaultSampler     : s0;

cbuffer PS_CONSTANTS : register(b0)
{
    float invWidth;
    float yFactor;
};

min16float PS(PS_INPUT input) : SV_TARGET
{
    bool isV = input.texCoord.y >= 0.5;
    float y = (input.texCoord.y - 0.5 * isV) * 2;
    float2 uv = nvChannel.Sample(defaultSampler, float2(input.texCoord.x, y * yFactor));
    return min16float(isV ? uv.y : uv.x);
}
)RawLiteral";

const std::string PSHADER_COPY_RGB = R"RawLiteral(
struct PS_INPUT
{
	float4 pos         : SV_POSITION;
	float2 texCoord    : TEXCOORD0;
};

Texture2D<float>  yChannel          : t0;
Texture2D<float2> nvChannel         : t1;
SamplerState      defaultSampler    : s0;

cbuffer PS_CONSTANTS : register(b0)
{
    float invWidth;
    float yFactor;
};

static const float3x3 YUVtoRGBCoeffMatrix =
{
	1.164383f,  1.164383f, 1.164383f,
	0.000000f, -0.391762f, 2.017232f,
	1.596027f, -0.812968f, 0.000000f
};

float3 ConvertYUVtoRGB(float3 yuv)
{
	yuv -= float3(0.062745f, 0.501960f, 0.501960f);
	yuv = mul(yuv, YUVtoRGBCoeffMatrix);
	return saturate(yuv);
}

min16float4 PS(PS_INPUT input) : SV_TARGET
{
    float2 texCoords = float2(input.texCoord.x, input.texCoord.y * yFactor);
	float y = yChannel.Sample(defaultSampler, texCoords);
    float2 nv = nvChannel.Sample(defaultSampler, texCoords);
	return min16float4(ConvertYUVtoRGB(float3(y, nv)), 1.f);
}
)RawLiteral";


struct VERTEX
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT2 TexCoord;
};

struct PS_CONSTANTS
{
    float invWidth;
    float yFactor;
    float reserved0;
    float reserved1;
};


static CComPtr<ID3DBlob> CompileShader(const std::string& shaderCode, bool pixel)
{
    CComPtr<ID3DBlob> code, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS;
    HRESULT hr = D3DCompile(shaderCode.data(), shaderCode.size(), nullptr, nullptr, nullptr, pixel ? "PS" : "VS", pixel ? "ps_5_0" : "vs_5_0", flags, 0, &code, &errors);
    hr;
    std::string sErrors;
    if (errors)
    {
        sErrors = std::string((const char*)errors->GetBufferPointer(), errors->GetBufferSize());
    }
    return code;
}

static CComPtr<ID3D11PixelShader> CreatePixelShader(CComPtr<ID3D11Device> device, const std::string& shaderCode)
{
    auto pixelShaderCode = CompileShader(shaderCode, true);
    CComPtr<ID3D11PixelShader> result;
    HRESULT hr = device->CreatePixelShader(pixelShaderCode->GetBufferPointer(), pixelShaderCode->GetBufferSize(), nullptr, &result);
    hr;
    return result;
}


D3DWrapper::D3DWrapper(CComPtr<ID3D11Device> device) :
    m_device(device)
{
    m_device->GetImmediateContext(&m_immediateContext);

    auto vertexShaderCode = CompileShader(VSHADER, false);
    HRESULT hr = m_device->CreateVertexShader(vertexShaderCode->GetBufferPointer(), vertexShaderCode->GetBufferSize(), nullptr, &m_vertexShader);

    constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> Layout =
    { {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    } };
    hr = m_device->CreateInputLayout(Layout.data(), Layout.size(), vertexShaderCode->GetBufferPointer(), vertexShaderCode->GetBufferSize(), &m_layout);

    D3D11_SAMPLER_DESC samplerDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
    hr = m_device->CreateSamplerState(&samplerDesc, &m_sampler);

    VERTEX Vertices[4] =
    {
        {DirectX::XMFLOAT3(-1.0f, -1.0f, 0), DirectX::XMFLOAT2(0.0f, 1.0f)},
        {DirectX::XMFLOAT3(-1.0f, 1.0f, 0), DirectX::XMFLOAT2(0.0f, 0.0f)},
        {DirectX::XMFLOAT3(1.0f, -1.0f, 0), DirectX::XMFLOAT2(1.0f, 1.0f)},
        {DirectX::XMFLOAT3(1.0f, 1.0f, 0), DirectX::XMFLOAT2(1.0f, 0.0f)},
    };

    m_vertices = CreateBuffer(Vertices, sizeof(VERTEX) * 4, true);

    m_copyY = { m_vertexShader, CreatePixelShader(m_device, PSHADER_COPY_Y), DXGI_FORMAT_R8_UNORM };
    m_copyNV = { m_vertexShader, CreatePixelShader(m_device, PSHADER_COPY_NV), DXGI_FORMAT_R8G8_UNORM };
    m_copyUV = { m_vertexShader, CreatePixelShader(m_device, PSHADER_COPY_UV), DXGI_FORMAT_R8G8_UNORM };
    m_copyRGB = { m_vertexShader, CreatePixelShader(m_device, PSHADER_COPY_RGB), DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM };
}

CComPtr<ID3D11Buffer> D3DWrapper::CreateBuffer(const void* data, int size, bool vertices)
{
    CComPtr<ID3D11Buffer> result;

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = size;
    desc.BindFlags = vertices ? D3D11_BIND_VERTEX_BUFFER : D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA dataDesc = { data };
    HRESULT hr = m_device->CreateBuffer(&desc, data ? &dataDesc : nullptr, &result);
    hr;
    return result;
}

CComPtr<ID3D11Texture2D> D3DWrapper::CreateTexture(const Point& size, DXGI_FORMAT format, bool cpuRead, bool mipmaps, bool notRenderTarget)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size.X;
    desc.Height = size.Y;
    desc.MipLevels = mipmaps ? 0 : 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = cpuRead ? 0 : !notRenderTarget ? D3D11_BIND_RENDER_TARGET : 0;
    if (mipmaps)
    {
        desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    desc.Usage = cpuRead ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = cpuRead ? D3D11_CPU_ACCESS_READ : 0;

    CComPtr<ID3D11Texture2D> result;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &result);
    hr;
    return result;
}

CComPtr<ID3D11RenderTargetView> D3DWrapper::CreateRTView(CComPtr<ID3D11Texture2D> texture, DXGI_FORMAT format)
{
    D3D11_RENDER_TARGET_VIEW_DESC rtDesc = CD3D11_RENDER_TARGET_VIEW_DESC(D3D11_RTV_DIMENSION_TEXTURE2D, format);
    CComPtr<ID3D11RenderTargetView> result;
    HRESULT hr = m_device->CreateRenderTargetView(texture, &rtDesc, &result);
    hr;
    return result;
}

RenderTarget D3DWrapper::CreateRenderTarget(const Point& size, DXGI_FORMAT format, int viewFormat, bool mipmaps)
{
    RenderTarget result;
    result.Size = size;
    result.Target = CreateTexture(size, format, false, mipmaps);
    result.View = CreateRTView(result.Target, viewFormat ? (DXGI_FORMAT)viewFormat : format);
    return result;
}

void D3DWrapper::Setup(ID3D11DeviceContext& context)
{
    context.IASetInputLayout(m_layout);
    context.OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context.PSSetSamplers(0, 1, &m_sampler.p);
    context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    UINT Stride = sizeof(VERTEX);
    UINT Offset = 0;
    context.IASetVertexBuffers(0, 1, &m_vertices.p, &Stride, &Offset);
}



D3DContext::D3DContext(std::shared_ptr<D3DWrapper> wrapper):
    m_wrapper(wrapper)
{
    HRESULT hr = m_wrapper->m_device->CreateDeferredContext(0, &m_context);
    hr;
}

void D3DContext::Begin(float yFactor, float invWidth, bool setupContext)
{
    UpdateConstants(yFactor, invWidth, setupContext);
    if (setupContext)
    {
        m_wrapper->Setup(*m_context);
    }
}

void D3DContext::UpdateConstants(float yFactor, float invWidth, bool force)
{
    if (!m_constants)
    {
        m_constants = m_wrapper->CreateBuffer(nullptr, sizeof(PS_CONSTANTS), false);
    }
    bool changed = yFactor != m_yFactor || invWidth != m_invWidth;
    if (changed)
    {
        m_yFactor = yFactor;
        m_invWidth = invWidth;
        PS_CONSTANTS constants{ m_invWidth, m_yFactor };
        m_context->UpdateSubresource(m_constants, 0, nullptr, &constants, 0, 0);
    }
    if (changed || force)
    {
        m_context->PSSetConstantBuffers(0, 1, &m_constants.p);
    }
}

void D3DContext::End()
{
    CComPtr<ID3D11CommandList> list;
    HRESULT hr = m_context->FinishCommandList(FALSE, &list);
    hr;
    m_wrapper->m_immediateContext->ExecuteCommandList(list, FALSE);
}

void D3DContext::SetTarget(const RenderTarget& target, const DXProgram& program)
{
    m_program = &program;
    m_target = &target;
    m_context->OMSetRenderTargets(1, &target.View.p, nullptr);
    m_context->VSSetShader(program.VertexShader, nullptr, 0);
    m_context->PSSetShader(program.PixelShader, nullptr, 0);
}

void D3DContext::Draw(ID3D11Texture2D* src1, ID3D11Texture2D* src2, bool mipmaps, const Box& rect)
{
    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = (float)rect.Position.X;
    viewport.TopLeftY = (float)rect.Position.Y;
    viewport.Width = (float)(rect.Size.X ? rect.Size.X : m_target->Size.X);
    viewport.Height = (float)(rect.Size.Y ? rect.Size.Y : m_target->Size.Y);
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    CComPtr<ID3D11ShaderResourceView> shaderView[2];
    ID3D11ShaderResourceView* shaderViewPtrs[2]{};
    int viewCount = 1 + (m_program->SourceFormat[1] != DXGI_FORMAT_UNKNOWN);
    for (int i = 0; i < viewCount; ++i)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = CD3D11_SHADER_RESOURCE_VIEW_DESC(D3D11_SRV_DIMENSION_TEXTURE2D, m_program->SourceFormat[i]);
        HRESULT hr = m_wrapper->m_device->CreateShaderResourceView(i ? src2 : src1, &viewDesc, &shaderView[i]);
        hr;
        if (mipmaps)
        {
            m_context->GenerateMips(shaderView[i]);
        }
        shaderViewPtrs[i] = shaderView[i];
    }
    m_context->PSSetShaderResources(0, viewCount, shaderViewPtrs);
    m_context->Draw(4, 0);
}

void D3DContext::Draw(ID3D11Texture2D* src1, ID3D11Texture2D* src2, const RenderTarget& dst, const DXProgram& program, bool mipmaps)
{
    SetTarget(dst, program);
    Draw(src1, src2, mipmaps);
}

void D3DContext::Draw(ID3D11Texture2D& src, const RenderTarget& dst, const DXProgram& program)
{
    Draw( &src, &src, dst, program, false);
}

ID3D11DeviceContext& D3DContext::Context()
{
    return *m_context;
}
