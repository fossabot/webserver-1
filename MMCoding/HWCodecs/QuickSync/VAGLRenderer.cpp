#define MONITOR_USES_GL
#define MONITOR_USES_UGLY
#include "../Monitor/GlHelper.h"
#include "QSDeviceVA.h"

#include "../MMCoding/HWCodecs/IGLRenderer.h"
#include <Logging/log2.h>


namespace
{

class VAGLRenderer : public NLogging::WithLogger, public NMMSS::IGLRenderer
{
public:
    VAGLRenderer(DECLARE_LOGGER_ARG) :
        NLogging::WithLogger(GET_LOGGER_PTR)
    {
    }

    ~VAGLRenderer()
    {
        clearSurface();
    }

    void Setup(const NMMSS::ISample& sample, GLuint texture, int width, int height, bool mipmaps) override
    {
        m_device = VASample::GetDevice(sample);
        clearSurface();
        VAStatus status = vaCreateSurfaceGLX(m_device->Display(), GL_TEXTURE_2D, texture, &m_glxSurface);
        (void)status;
    }

    void Render(NMMSS::ISample& sample) override
    {
        VAStatus status = vaCopySurfaceGLX(m_device->Display(), m_glxSurface, VASample::GetVASurfaceId(sample), 0);
        (void)status;
    }

    void CompleteRender() override
    {

    }

    bool IsValid() const override
    {
        return !!m_glxSurface;
    }

    void Destroy() override
    {
        delete this;
    }

    void GetTextureSize(float& w, float& h) override
    {
        w = 0.f;
        h = 0.f;
    }

private:
    void clearSurface()
    {
        if (m_glxSurface)
        {
            vaDestroySurfaceGLX(m_device->Display(), m_glxSurface);
            m_glxSurface = nullptr;
        }
    }

private:
    QSDeviceVASP m_device;
    GLuint m_texture;
    void* m_glxSurface{};
};

}

NMMSS::IGLRenderer* CreateQSGLRenderer(DECLARE_LOGGER_ARG)
{
    return new VAGLRenderer(GET_LOGGER_PTR);
}
