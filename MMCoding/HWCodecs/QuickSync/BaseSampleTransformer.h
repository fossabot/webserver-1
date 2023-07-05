#pragma once

#include "FrameGeometryAdvisor.h"
#include "Points.h"
#include "HWCodecs/IHWDecoder.h"

#include "Logging/log2.h"

#include "mfxvideo++.h"

#include <vector>

namespace NMMSS
{
    class CDeferredAllocSampleHolder;
    class HWDecoderRequirements;
};

class QSSampleData
{
public:
    QSSampleData(const mfxFrameInfo& frameInfo)
    {
        Surface.Info = frameInfo;
    }

public:
    mfxFrameSurface1 Surface {};
    bool Locked {};
};

class IExternalSurfaces
{
public:
    virtual ~IExternalSurfaces(){}

    virtual int Count() const = 0;
};
using ExternalSurfacesSP = std::shared_ptr<IExternalSurfaces>;

class BaseSampleTransformer : public NLogging::WithLogger
{
public:
    BaseSampleTransformer(DECLARE_LOGGER_ARG, QSDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor);
    virtual ~BaseSampleTransformer() {}

    virtual void GetSample(QSSampleData* src, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) = 0;
    virtual int GetAdditionalSurfaceCount() const { return 0; }
    virtual bool IsValid() const { return true; }

protected:
    virtual Point GetResultSize(const Point& srcSize, bool* mipmaps = nullptr);

protected:
    QSDeviceSP m_QSDevice;
    NMMSS::PFrameGeometryAdvisor m_advisor;
};

std::shared_ptr<BaseSampleTransformer> CreateQSSampleTransformer(DECLARE_LOGGER_ARG, QSDeviceSP device, MFXVideoSession& session,
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements);

class BaseQSAllocator : public mfxFrameAllocator
{
public:
    BaseQSAllocator();
    virtual ~BaseQSAllocator(){}

    virtual mfxStatus GetFrameHDL(mfxMemId mid, mfxHDL* handle);
    //virtual mfxStatus FreeFrames(mfxFrameAllocResponse* response) = 0;
    virtual void SetupFrameData(mfxFrameData& data, mfxMemId mid) = 0;

    mfxStatus AllocFrames(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response);

protected:
    virtual mfxStatus AllocFramesInternal(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response) = 0;

private:
    static mfxStatus MFX_CDECL alloc(mfxHDL pthis, mfxFrameAllocRequest* request, mfxFrameAllocResponse* response);
    static mfxStatus MFX_CDECL lock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
    static mfxStatus MFX_CDECL unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
    static mfxStatus MFX_CDECL gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL* handle);
    static mfxStatus MFX_CDECL free(mfxHDL pthis, mfxFrameAllocResponse* response);

protected:
    mfxFrameAllocResponse m_savedAllocResponse {};
    std::vector<mfxMemId> m_mids;
    mfxStatus m_status{};
};

std::shared_ptr<BaseQSAllocator> CreateQSAllocator(QSDeviceSP device, MFXVideoSession& session, ExternalSurfacesSP surfaces);