#include "CudaGLRenderer.h"

#include "CudaSample.h"
#include "CudaDevice.h"
#include "CudaProcessor.h"

#ifndef _WIN32
#include <GL/glx.h>
#endif //_WIN32

#include "dynlink_cudaGL.h"
#include "../../ugly/sgi_glext.h"

namespace
{
#ifdef _WIN32
    void* GlGetProcAddress(const char *name)
    {
        return wglGetProcAddress(name);
    }
#else
    void* GlGetProcAddress(const char *name)
    {
        void(*pf)() = glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name));
        void **pp = reinterpret_cast<void**>(&pf);
        return *pp;
    }
#endif //_WIN32

    PFNGLGENBUFFERSPROC glGenBuffersPtr = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffersPtr = nullptr;
    PFNGLBINDBUFFERPROC glBindBufferPtr = nullptr;
    PFNGLBUFFERDATAPROC glBufferDataPtr = nullptr;

    bool CheckWglProcs()
    {
        if (!glGenBuffersPtr)
        {
            glGenBuffersPtr = (PFNGLGENBUFFERSPROC)GlGetProcAddress("glGenBuffers");
            glDeleteBuffersPtr = (PFNGLDELETEBUFFERSPROC)GlGetProcAddress("glDeleteBuffers");
            glBindBufferPtr = (PFNGLBINDBUFFERPROC)GlGetProcAddress("glBindBuffer");
            glBufferDataPtr = (PFNGLBUFFERDATAPROC)GlGetProcAddress("glBufferData");
        }
        return glGenBuffersPtr != nullptr;
    }
}

CudaGLRenderer::~CudaGLRenderer()
{
    setTexture(0, {});
}

void CudaGLRenderer::Setup(const NMMSS::ISample& sample, GLuint texture, int width, int height, bool mipmaps)
{
    setup(sample, texture, { width, height });
}

void CudaGLRenderer::setup(const NMMSS::ISample& sample, GLuint texture, const Point& size)
{
    setTexture(0, {});
    m_device = CudaSample::GetDevice(sample);
    setTexture(texture, size);
}

void CudaGLRenderer::setTexture(GLuint texture, const Point& size)
{
    if (m_device)
    {
        auto context = m_device->SetContext();
        if (m_surface)
        {
            m_device->CheckStatus(cuGraphicsUnregisterResource(m_surface), "cuGraphicsUnregisterResource");
            glDeleteBuffersPtr(1, &m_bufferId);
            m_bufferId = 0;
            m_surface = 0;
            m_size = {};
        }
        m_texture = texture;
        if (m_texture && CheckWglProcs())
        {
            m_size = { size.X, size.Y, m_device->GetPitch(size.X * 4) };
            glGenBuffersPtr(1, &m_bufferId);
            glBindBufferPtr(GL_PIXEL_UNPACK_BUFFER_EXT, m_bufferId);
            glBufferDataPtr(GL_PIXEL_UNPACK_BUFFER_EXT, m_size.Pitch * m_size.Height, nullptr, GL_DYNAMIC_COPY);
            glBindBufferPtr(GL_PIXEL_UNPACK_BUFFER_EXT, 0);
            m_device->CheckStatus(cuGraphicsGLRegisterBuffer(&m_surface, m_bufferId, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD), "cuGraphicsGLRegisterBuffer");
            m_device->CheckStatus(cuGraphicsResourceSetMapFlags(m_surface, CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD), "cuGraphicsResourceSetMapFlags");
        }
    }
}

void CudaGLRenderer::Render(NMMSS::ISample& sample)
{
    m_sample = NMMSS::PSample(&sample, NCorbaHelpers::ShareOwnership());

    if (m_device != CudaSample::GetDevice(sample))
    {
        setup(sample, m_texture, m_size);
    }

    if (IsValid())
    {
        m_device->AddResourceForDisplay(m_surface, { CudaSample::GetSurface(sample, false), m_size });
    }
}

void CudaGLRenderer::CompleteRender()
{
    if(IsValid())
    {
        m_device->CompleteDisplayResources();

        glBindBufferPtr(GL_PIXEL_UNPACK_BUFFER_EXT, m_bufferId);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m_size.Pitch / 4);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_size.Width, m_size.Height, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBufferPtr(GL_PIXEL_UNPACK_BUFFER_EXT, 0);
    }

    m_sample.Reset();
}

bool CudaGLRenderer::IsValid() const
{
    return m_surface && m_device->CanProcessOutput();
}

void CudaGLRenderer::GetTextureSize(float& w, float& h)
{
    w = 0.f;
    h = 0.f;
}

void CudaGLRenderer::Destroy()
{
    delete this;
}

NMMSS::IGLRenderer* CreateCudaGLRenderer()
{
    return new CudaGLRenderer();
}
