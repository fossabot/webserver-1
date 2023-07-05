#include "BaseSampleTransformer.h"
#include "QSDeviceVA.h"
#include "HWAccelerated.h"
#include "HWCodecs/HWUtils.h"

#include "../FilterImpl.h"
#include "../MediaType.h"

#include <CorbaHelpers/Envar.h>

#include <thread>

namespace
{
    VASampleData& sampleData(NMMSS::ISample& sample)
    {
        return *reinterpret_cast<VASampleData*>(sample.GetBody());
    }

    const VASampleData& sampleData(const NMMSS::ISample& sample)
    {
        return sampleData(const_cast<NMMSS::ISample&>(sample));
    }
}

VASample::VASample(QSDeviceVASP device):
    VideoMemorySample(NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::VA),
    m_device(device)
{
}

void VASample::Setup(uint64_t timestamp, QSSampleData* data, NMMSS::CDeferredAllocSampleHolder& holder)
{
    data->Locked = true;
    m_sampleData = data;

    mfxFrameSurface1& surface = data->Surface;
    if (CreateSample(holder, sizeof(VASampleData), false))
    {
        sampleData(*m_sample) = { m_device.get(), *((VASurfaceID*)surface.Data.MemId) };
    }
    VideoMemorySample::Setup({(int)surface.Info.Width, (int)surface.Info.Height}, {}, timestamp);
}

void VASample::Setup(const mfxFrameInfo& info, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    if (info.Width != m_mfxSurface.Info.Width || info.Height != m_mfxSurface.Info.Height)
    {
        m_mfxSurface.Info = info;
        SurfaceSize size = {info.Width, info.Height, Aligned32(info.CropW)};
        SurfaceSize uvSize = {info.Width / 2, info.Height / 2, size.Pitch / 2};
        if(CreateSample(holder, size.MemorySize() + uvSize.MemorySize() * 2, true))
        {
            m_mfxSurface.Data.Y = m_sample->GetBody();
            m_mfxSurface.Data.U = m_mfxSurface.Data.Y + size.MemorySize();
            m_mfxSurface.Data.V = m_mfxSurface.Data.U + uvSize.MemorySize();
            m_mfxSurface.Data.Pitch = size.Pitch;
        }
        VideoMemorySample::Setup(size, uvSize, timestamp, {info.CropW, info.CropH});            
    }
}

void VASample::ReleaseSampleData()
{
    if (m_sampleData)
    {
        m_sampleData->Locked = false;
        m_sampleData = nullptr;
    }
}

mfxFrameSurface1& VASample::MfxSurface()
{
    return m_mfxSurface;
}

QSDeviceVASP  VASample::GetDevice(const NMMSS::ISample& sample)
{
    return std::static_pointer_cast<QSDeviceVA>(sampleData(sample).Device->shared_from_this());
}

VASurfaceID VASample::GetVASurfaceId(const NMMSS::ISample& sample)
{
    return sampleData(sample).SurfaceId;
}


namespace
{

class QSExternalSurfaces : public IExternalSurfaces
{
public:
    ~QSExternalSurfaces()
    {
        if (!VideoSurfaces.empty())
        {
            vaDestroySurfaces(Device->Display(), VideoSurfaces.data(), VideoSurfaces.size());
        }
    }

    int Count() const override
    {
        return (int)VideoSurfaces.size();
    }

    QSDeviceVASP Device;
    std::vector<VASurfaceID> VideoSurfaces;
};


class VAAllocator : public BaseQSAllocator
{
public:
    VAAllocator(QSDeviceVASP device, MFXVideoSession& session, ExternalSurfacesSP surfaces) :
        m_device(device)
    {
        m_status = session.SetHandle(MFX_HANDLE_VA_DISPLAY, m_device->Display());
        if (m_status >= 0)
        {
            m_status = session.SetFrameAllocator(this);
        }
        m_surfaces = surfaces ? std::static_pointer_cast<QSExternalSurfaces>(surfaces) : std::make_shared<QSExternalSurfaces>();
        m_surfaces->Device = m_device;
    }

public:
    mfxStatus AllocFramesInternal(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response) override
    {
        if(m_status < 0)
        {
            return m_status;
        }
        int existingCount = m_surfaces->Count();
        if(existingCount < request->NumFrameSuggested)
        {
            VASurfaceAttrib attrib;
            attrib.type = VASurfaceAttribPixelFormat;
            attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
            attrib.value.type = VAGenericValueTypeInteger;
            attrib.value.value.i = VA_FOURCC_NV12;
            m_surfaces->VideoSurfaces.resize(request->NumFrameSuggested);

            auto result = vaCreateSurfaces(m_device->Display(), VA_RT_FORMAT_YUV420, request->Info.Width, request->Info.Height, 
                &m_surfaces->VideoSurfaces[existingCount], request->NumFrameSuggested - existingCount, &attrib, 1);
            if(result != VA_STATUS_SUCCESS)
            {
                m_surfaces->VideoSurfaces.resize(existingCount);
                return MFX_ERR_UNSUPPORTED;
            }
        }
        for (int i = 0; i < (int)request->NumFrameSuggested; ++i)
        {
            m_videoMids.push_back(&m_surfaces->VideoSurfaces[i]);
        }
        response->mids = m_videoMids.data();
        response->NumFrameActual = request->NumFrameSuggested;
        return MFX_ERR_NONE;
    }

    void SetupFrameData(mfxFrameData& data, mfxMemId mid) override
    {
        data.MemId = mid;
    }

    mfxStatus GetFrameHDL(mfxMemId mid, mfxHDL* handle) override
    {
        *handle = mid;
        return MFX_ERR_NONE;
    }

private:
    QSDeviceVASP m_device;
    using Buffer = std::shared_ptr<std::vector<unsigned char>>;
    std::vector<mfxMemId> m_videoMids;
    std::shared_ptr<QSExternalSurfaces> m_surfaces;
};

class VASampleHolder : public VideoMemorySampleHolder<VASample>
{
public:
    VASampleHolder(QSDeviceVASP device) :
        m_device(device)
    {
    }

protected:
    VideoMemorySampleSP CreateSample() override
    {
        return std::make_shared<VASample>(m_device);
    }

private:
    QSDeviceVASP m_device;
};

class SampleTransformerVA : public BaseSampleTransformer
{
public:
    SampleTransformerVA(DECLARE_LOGGER_ARG, QSDeviceVASP device, MFXVideoSession& session, NMMSS::IFrameGeometryAdvisor* advisor, bool toSystemMemory):
        BaseSampleTransformer(GET_LOGGER_PTR, device, advisor),
        m_sampleHolder(device),
        m_session(session),
        m_toSystemMemory(toSystemMemory)
    {
    }

    int GetAdditionalSurfaceCount() const override
    {
        return m_toSystemMemory ? 0 : 6;
    }

    void GetSample(QSSampleData* src, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) override
    {
        auto sample = m_sampleHolder.GetFreeSample();
        sample->ReleaseSampleData();
        if (!m_toSystemMemory)
        {
            sample->Setup(timestamp, src, *holder);
            holder->AddSample(sample->Sample());
        }
        else
        {
            mfxFrameSurface1& surface = src->Surface;
            Point srcSize = {(int)surface.Info.CropW, (int)surface.Info.CropH};
            bool mipmaps = false;
            Point resultSize = GetResultSize(srcSize, &mipmaps);
            if(mipmaps)
            {
                resultSize = srcSize;
            }

            if(checkVPP(src->Surface.Info, resultSize))
            {
                sample->Setup(m_vppVideoParams.vpp.Out, timestamp, *holder);

                mfxStatus sts = MFX_ERR_NONE;
                mfxSyncPoint outputSyncPoint {};
                while (!outputSyncPoint && sts >= 0)
                {
                    sts = m_vpp->RunFrameVPPAsync(&surface, &sample->MfxSurface(), NULL, &outputSyncPoint);
                    if (MFX_WRN_DEVICE_BUSY == sts)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                if (outputSyncPoint)
                {
                    sts = m_session.SyncOperation(outputSyncPoint, 60000);
                    holder->AddSample(sample->Sample());
                }
            }
        }
    }

    bool IsValid() const override
    {
        return m_isValid;
    }

private:
    bool checkVPP(const mfxFrameInfo& frameInfo, const Point& resultSize)
    {
        if (Point(m_vppVideoParams.vpp.Out.CropW, m_vppVideoParams.vpp.Out.CropH) != resultSize)
        {
            // mfxFrameAllocRequest vppRequest[2] = {};
            // sts = m_vpp->QueryIOSurf(&m_mfxVppVideoParams, vppRequest);
            // mfxFrameAllocResponse vppResponse {};
            // if(sts>=0)
            // {
            //     sts = m_allocator->AllocFrames(&vppRequest[1], &vppResponse);
            //     if(sts >= 0)
            //     {
            //         for (int i = 0; i < vppResponse.NumFrameActual; ++i)
            //         {
            //             m_vppSamples.emplace_back(std::make_unique<QSSampleData>(m_mfxVppVideoParams.vpp.Out));
            //             m_allocator->SetupFrameData(m_vppSamples[i]->Surface.Data, vppResponse.mids[i]);
            //         }
            //     }
            // }

            m_vppVideoParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

            m_vppVideoParams.vpp.Out = m_vppVideoParams.vpp.In = frameInfo;

            m_vppVideoParams.vpp.Out.CropW = resultSize.X;
            m_vppVideoParams.vpp.Out.CropH = resultSize.Y;
            m_vppVideoParams.vpp.Out.Width = Aligned16(resultSize.X);
            m_vppVideoParams.vpp.Out.Height = Aligned16(resultSize.Y);

            m_vppVideoParams.vpp.Out.FourCC = MFX_FOURCC_YV12;

            m_vppVideoParams.AsyncDepth = 1;
            auto sts = MFX_ERR_NONE;
            if (!m_vpp)
            {
                m_vpp = std::make_unique<MFXVideoVPP>(m_session);
                sts = m_vpp->Init(&m_vppVideoParams);
            }
            else
            {
                sts = m_vpp->Reset(&m_vppVideoParams);
            }
            if (sts < 0)
            {
                _err_ << "Cannot create image processor: " << MfxStatusToString(sts) << ", make sure intel-media-va-driver-non-free is installed";
                m_isValid = false;
                return false;
            }
        }
        return true;
    }

private:
    VASampleHolder m_sampleHolder;
    MFXVideoSession& m_session;
    std::unique_ptr<MFXVideoVPP> m_vpp;
    mfxVideoParam m_vppVideoParams{};
    bool m_isValid = true;
    bool m_toSystemMemory{};
};

}

ExternalSurfacesSP CreateQSExternalSurfaces()
{
    return std::make_shared<QSExternalSurfaces>();
}

std::shared_ptr<BaseQSAllocator> CreateQSAllocator(QSDeviceSP device, MFXVideoSession& session, ExternalSurfacesSP surfaces)
{
    return std::make_shared<VAAllocator>(std::static_pointer_cast<QSDeviceVA>(device), session, surfaces);
}

std::shared_ptr<BaseSampleTransformer> CreateQSSampleTransformer(DECLARE_LOGGER_ARG, QSDeviceSP device, MFXVideoSession& session,
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    bool toSystemMemory = (requirements.Destination == NMMSS::EMemoryDestination::ToSystemMemory) || 
        !device->IsPrimary() || !NCorbaHelpers::CEnvar::NgpQSUseVideoMemory();
    return std::make_shared<SampleTransformerVA>(GET_LOGGER_PTR, std::static_pointer_cast<QSDeviceVA>(device), session, advisor, toSystemMemory);
}
