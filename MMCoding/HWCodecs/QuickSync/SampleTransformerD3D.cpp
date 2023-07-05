#include "BaseSampleTransformer.h"

#include "D3DSample.h"
#include "D3DWrapper.h"
#include "HiddenDXDevice.h"

#include "HWCodecs/HWDevicePool.h"
#include "HWCodecs/CUDA/CudaDevice.h"
#include "HWCodecs/CUDA/CudaSampleHolder.h"

#include "../FilterImpl.h"
#include "../MediaType.h"

#include <d3d11.h>

namespace
{

class BaseSampleTransformerD3D : public BaseSampleTransformer
{
public:
    BaseSampleTransformerD3D(DECLARE_LOGGER_ARG, HiddenDxDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor):
        BaseSampleTransformer(GET_LOGGER_PTR, device, advisor),
        m_device(device),
        m_d3dWrapper(m_device->GetWrapper())
    {
        m_deviceContext = std::make_shared<D3DContext>(m_d3dWrapper);
    }

protected:
    HiddenDxDeviceSP m_device;
    std::shared_ptr<D3DContext> m_deviceContext;
    std::shared_ptr<D3DWrapper> m_d3dWrapper;
};

class CudaSampleTransformer : public BaseSampleTransformerD3D
{
public:
    CudaSampleTransformer(DECLARE_LOGGER_ARG, HiddenDxDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor, bool toSystemMemory, bool delayGetSample):
        BaseSampleTransformerD3D(GET_LOGGER_PTR, device, advisor),
        m_delayGetSample(delayGetSample),
        m_toSystemMemory(toSystemMemory)
    {
        if (m_toSystemMemory)
        {
            m_sampleHolder = std::make_shared<BaseVideoMemorySampleHolder>();
        }
        else
        {
            CudaDeviceSP cudaDevice = std::static_pointer_cast<CudaDevice>(HWDevicePool::Instance()->GetPrimaryDevice());
            m_sampleHolder = cudaDevice->CreateSampleHolder(false);
        }
    }

    int GetAdditionalSurfaceCount() const override
    {
        return !!m_delayGetSample;
    }

    void GetSample(QSSampleData* src, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) override
    {
        getSample(*holder);

        m_src = src;
        m_src->Locked = true;
        m_timestamp = timestamp;

        ID3D11Texture2D* srcSurface = reinterpret_cast<ID3D11Texture2D*>(m_src->Surface.Data.MemId);
        Point size = {(int)m_src->Surface.Info.CropW, (int)m_src->Surface.Info.CropH};
        int codedHeight = (int)m_src->Surface.Info.Height;

        m_resultSize = GetResultSize(size);
        if (m_resultSize != size || m_toSystemMemory)
        {
            checkRenderTarget(m_resultSize);
            m_deviceContext->Begin((float)size.Y / codedHeight, 2.0f / m_renderTarget.Size.X);
            m_deviceContext->Draw(*srcSurface, m_renderTarget, m_d3dWrapper->m_copyY);
            m_deviceContext->Draw(*srcSurface, m_renderTargetNV, m_toSystemMemory ? m_d3dWrapper->m_copyUV : m_d3dWrapper->m_copyNV);
            srcSurface = m_renderTarget.Target;
            codedHeight = m_resultSize.Y;
        }

        if (m_toSystemMemory)
        {
            m_deviceContext->Context().CopyResource(m_cpuRead, m_renderTarget.Target);
            m_deviceContext->Context().CopyResource(m_cpuReadNV, m_renderTargetNV.Target);
        }
        else
        {
            checkCpuRead({ m_resultSize.X, codedHeight });
            m_deviceContext->Context().CopyResource(m_cpuRead, srcSurface);
        }
        m_deviceContext->End();
        if (waitForSample)
        {
            getSample(*holder);
        }
    }

private:
    Point GetResultSize(const Point& srcSize, bool* mipmaps = nullptr) override
    {
        int resultWidth = 0, resultHeight = 0;
        auto adviceType = m_advisor ? m_advisor->GetAdvice((uint32_t&)resultWidth, (uint32_t&)resultHeight)
                                    : NMMSS::IFrameGeometryAdvisor::ATLargest;
        if (resultWidth && resultWidth < srcSize.X)
        {
            if (adviceType == NMMSS::IFrameGeometryAdvisor::ATLargest)
            {
                int scale = 1;
                for (; resultWidth * scale * 4 < srcSize.X; scale *= 2);
                return {RoundUpTo2(srcSize.X / scale), RoundUpTo2(srcSize.Y / scale)};
            }
            else if ((adviceType == NMMSS::IFrameGeometryAdvisor::ATLimitSizeAndKeepAspect) || (srcSize.X >= resultWidth * 3 / 2))
            {
                return {RoundUpTo2(resultWidth), RoundUpTo2(resultHeight)};
            }
        }
        return srcSize;
    }

    void checkRenderTarget(const Point& size)
    {
        if (m_renderTarget.Size != size)
        {
            if (m_toSystemMemory)
            {
                Point nvSize = { size.X / 2, size.Y };
                m_renderTarget = m_d3dWrapper->CreateRenderTarget(size, DXGI_FORMAT_R8_UNORM);
                m_renderTargetNV = m_d3dWrapper->CreateRenderTarget(nvSize, DXGI_FORMAT_R8_UNORM);
                m_cpuRead = m_d3dWrapper->CreateTexture(size, DXGI_FORMAT_R8_UNORM, true);
                m_cpuReadNV = m_d3dWrapper->CreateTexture(nvSize, DXGI_FORMAT_R8_UNORM, true);
            }
            else
            {
                m_renderTarget = m_d3dWrapper->CreateRenderTarget(size, DXGI_FORMAT_NV12, DXGI_FORMAT_R8_UNORM);
                m_renderTargetNV = { m_renderTarget.Target, m_d3dWrapper->CreateRTView(m_renderTarget.Target, DXGI_FORMAT_R8G8_UNORM), size / 2 };
            }
        }
    }

    void checkCpuRead(const Point& size)
    {
        if (m_cpuReadSize != size)
        {
            m_cpuReadSize = size;
            m_cpuRead = m_d3dWrapper->CreateTexture(size, DXGI_FORMAT_NV12, true);
        }
    }

    void getSample(NMMSS::CDeferredAllocSampleHolder& holder)
    {
        if (m_src)
        {
            D3D11_MAPPED_SUBRESOURCE mapped {}, mappedNV {};
            m_d3dWrapper->m_immediateContext->Map(m_cpuRead, 0, D3D11_MAP_READ, 0, &mapped);
            auto result = m_sampleHolder->GetFreeSampleBase();
            SurfaceSize size = { m_resultSize.X, m_resultSize.Y, (int)mapped.RowPitch };
            if (m_toSystemMemory)
            {
                m_d3dWrapper->m_immediateContext->Map(m_cpuReadNV, 0, D3D11_MAP_READ, 0, &mappedNV);
                SurfaceSize uvSize = { size.Width / 2, size.Height / 2, (int)mappedNV.RowPitch };
                result->SetupSystemMemory(size, (const uint8_t*)mapped.pData, uvSize, (const uint8_t*)mappedNV.pData, m_timestamp, holder);
                m_d3dWrapper->m_immediateContext->Unmap(m_cpuReadNV, 0);
            }
            else
            {
                std::static_pointer_cast<CudaSample>(result)->SetupSystemMem((const uint8_t*)mapped.pData, size, m_cpuReadSize.Y, m_timestamp, holder);
            }
            m_d3dWrapper->m_immediateContext->Unmap(m_cpuRead, 0);
            holder.AddSample(result->Sample());
            m_src->Locked = false;
            m_src = nullptr;
        }
    }

private:
    std::shared_ptr<BaseVideoMemorySampleHolder> m_sampleHolder;
    CComPtr<ID3D11Texture2D> m_cpuRead, m_cpuReadNV;
    RenderTarget m_renderTarget, m_renderTargetNV;

    Point m_cpuReadSize;
    Point m_resultSize;

    QSSampleData* m_src {};
    uint64_t m_timestamp;
    bool m_delayGetSample;
    bool m_toSystemMemory;
};

class D3DSampleHolder : public VideoMemorySampleHolder<D3DSample>
{
public:
    D3DSampleHolder(HiddenDxDeviceSP device) :
        m_device(device)
    {
    }

protected:
    VideoMemorySampleSP CreateSample() override
    {
        return std::make_shared<D3DSample>(m_device);
    }

private:
    HiddenDxDeviceSP m_device;
};

class D3DSampleTransformer : public BaseSampleTransformerD3D
{
public:
    D3DSampleTransformer(DECLARE_LOGGER_ARG, HiddenDxDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor):
        BaseSampleTransformerD3D(GET_LOGGER_PTR, device, advisor),
        m_sampleHolder(device)
    {
    }

    void GetSample(QSSampleData* src, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) override
    {
        auto sample = m_sampleHolder.GetFreeSample();

        Point srcSize = {(int)src->Surface.Info.CropW, (int)src->Surface.Info.CropH};
        int codedHeight = (int)src->Surface.Info.Height;
        bool mipmaps = false;
        Point resultSize = GetResultSize(srcSize, &mipmaps);

        sample->Setup(resultSize, timestamp, *holder);

        auto* srcSurface = reinterpret_cast<ID3D11Texture2D*>(src->Surface.Data.MemId);
        m_deviceContext->Begin((float)srcSize.Y / codedHeight, 0);
        if (mipmaps)
        {
            if (m_mipsY.Size != srcSize)
            {
                m_mipsY = m_d3dWrapper->CreateRenderTarget(srcSize, DXGI_FORMAT_R8_UNORM, 0, true);
                m_mipsNV = m_d3dWrapper->CreateRenderTarget(srcSize / 2, DXGI_FORMAT_R8G8_UNORM, 0, true);
            }
            m_deviceContext->Draw(*srcSurface, m_mipsY, m_d3dWrapper->m_copyY);
            m_deviceContext->Draw(*srcSurface, m_mipsNV, m_d3dWrapper->m_copyNV);
            m_deviceContext->UpdateConstants(1.0f, 0.0f);
            m_deviceContext->Draw(m_mipsY.Target, m_mipsNV.Target, sample->m_Target, m_d3dWrapper->m_copyRGB, true);
        }
        else
        {
            m_deviceContext->Draw(*srcSurface, sample->m_Target, m_d3dWrapper->m_copyRGB);
        }
        m_deviceContext->End();
        holder->AddSample(sample->Sample());
    }

private:
    D3DSampleHolder m_sampleHolder;
    RenderTarget m_mipsY, m_mipsNV;
};

bool CanDecodeToCuda()
{
    auto primary = HWDevicePool::Instance()->GetPrimaryDevice();
    return primary && (primary->GetDeviceType() == NMMSS::EHWDeviceType::NvidiaCUDA) && primary->CanProcessOutput();
}

}

std::shared_ptr<BaseSampleTransformer> CreateQSSampleTransformer(DECLARE_LOGGER_ARG, QSDeviceSP device, MFXVideoSession& session,
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    bool toMemory = (requirements.Destination == NMMSS::EMemoryDestination::ToSystemMemory);
    auto dxDevice = std::static_pointer_cast<HiddenDxDevice>(device);
    if (toMemory || !dxDevice->IsPrimary())
    {
        bool decodeToMemorySample = toMemory || !CanDecodeToCuda();
        return std::make_shared<CudaSampleTransformer>(GET_LOGGER_PTR, dxDevice, advisor, decodeToMemorySample, !requirements.KeySamplesOnly);
    }
    else
    {
        return std::make_shared<D3DSampleTransformer>(GET_LOGGER_PTR, dxDevice, advisor);
    }
}
