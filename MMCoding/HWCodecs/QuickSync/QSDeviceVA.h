#pragma once

#include "QSDevice.h"
#include "../MMCoding/HWCodecs/VideoMemorySample.h"

#include "mfxstructures.h"

#include <va/va_glx.h>
#include <va/va_drm.h>

class QSDeviceVA : public QSDevice
{
public:
    ~QSDeviceVA();

    void Init(int adapterNum) override;
    bool IsValid() const override;
    VADisplay Display() const;

private:
    VADisplay getDisplayGLX();
    VADisplay getDisplayDRM();
    bool init(VADisplay display, bool primary);

private:
    int m_adapter {};
    VADisplay m_display {};
    bool m_initialized {};
};

using QSDeviceVASP = std::shared_ptr<QSDeviceVA>;

class QSSampleData;

class VASampleData
{
public:
    QSDeviceVA* Device{};
    VASurfaceID SurfaceId{};
    int64_t Reserved0{};
    int64_t Reserved1{};
    int64_t Reserved2{};
    int64_t Reserved3{};

};

class VASample : public VideoMemorySample
{
public:
    VASample(QSDeviceVASP device);

    void Setup(uint64_t timestamp, QSSampleData* data, NMMSS::CDeferredAllocSampleHolder& holder);

    void Setup(const mfxFrameInfo& info, uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder);

    void ReleaseSampleData();

    mfxFrameSurface1& MfxSurface();

    static QSDeviceVASP GetDevice(const NMMSS::ISample& sample);
    static VASurfaceID GetVASurfaceId(const NMMSS::ISample& sample);

private:
    QSDeviceVASP m_device;
    QSSampleData* m_sampleData{};
    mfxFrameSurface1 m_mfxSurface{};
};
