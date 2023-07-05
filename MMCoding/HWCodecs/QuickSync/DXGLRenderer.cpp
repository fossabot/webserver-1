#include "DXGLRenderer.h"
#define MONITOR_USES_GL
#define MONITOR_USES_UGLY
#include "../Monitor/GlHelper.h"
#include "D3DWrapper.h"
#include "D3DSample.h"
#include "HiddenDXDevice.h"
#include "../MMCoding/HWAccelerated.h"
#include "HWCodecs/CUDA/CudaSurface.h"
#include "../Sample.h"
#include <CorbaHelpers/Envar.h>

#include <d3d11.h>

// - - - - - - - - - - - - - - Definitions from wglext.h - - - - - - - - - - - - - - 
#define WGL_ACCESS_READ_ONLY_NV           0x00000000
#define WGL_ACCESS_READ_WRITE_NV          0x00000001
#define WGL_ACCESS_WRITE_DISCARD_NV       0x00000002
typedef HANDLE(WINAPI * PFNWGLDXOPENDEVICENVPROC) (void *dxDevice);
typedef BOOL(WINAPI * PFNWGLDXCLOSEDEVICENVPROC) (HANDLE hDevice);
typedef HANDLE(WINAPI * PFNWGLDXREGISTEROBJECTNVPROC) (HANDLE hDevice, void *dxObject, GLuint name, GLenum type, GLenum access);
typedef BOOL(WINAPI * PFNWGLDXUNREGISTEROBJECTNVPROC) (HANDLE hDevice, HANDLE hObject);
typedef BOOL(WINAPI * PFNWGLDXOBJECTACCESSNVPROC) (HANDLE hObject, GLenum access);
typedef BOOL(WINAPI * PFNWGLDXLOCKOBJECTSNVPROC) (HANDLE hDevice, GLint count, HANDLE *hObjects);
typedef BOOL(WINAPI * PFNWGLDXUNLOCKOBJECTSNVPROC) (HANDLE hDevice, GLint count, HANDLE *hObjects);


#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_FRAMEBUFFER                    0x8D40
typedef void (APIENTRYP PFNGLBINDFRAMEBUFFERPROC) (GLenum target, GLuint framebuffer);
typedef void (APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef void (APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);


PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV = nullptr;
PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV = nullptr;
PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV = nullptr;
PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV = nullptr;
PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV = nullptr;
PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV = nullptr;

PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;

bool CheckWglProcs()
{
    if (!wglDXOpenDeviceNV)
    {
        wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
        glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
        if (!wglDXOpenDeviceNV || !glGenFramebuffers)
        {
            NCorbaHelpers::CEnvar::NgpDisableHWDecoding(true);
        }
        else
        {
            wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXRegisterObjectNV");

            wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)wglGetProcAddress("wglDXUnregisterObjectNV");
            wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)wglGetProcAddress("wglDXCloseDeviceNV");

            wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXLockObjectsNV");
            wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)wglGetProcAddress("wglDXUnlockObjectsNV");

            glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
            glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
            glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D");
        }
    }
    return wglDXOpenDeviceNV != NULL;
}

inline void CheckGLError()
{
    int err = glGetError();
    if (GL_NO_ERROR != err)
    {
        int n = 0;
        ++n;
    }
}

const int ATLAS_SIZE = 4096;
const int ATLAS_SLICE = 128;
const int ATLAS_BANDS = ATLAS_SIZE / ATLAS_SLICE;
const double SHRINK_THRESHOLD = 0.8;
const int FRAMES_TO_SHRINK = 500;

class TextureAtlas;

inline int SliceSize(int size)
{
    return (size + (ATLAS_SLICE - 1)) / ATLAS_SLICE;
}

inline int PlaneIndex(int sliceY)
{
    return sliceY / ATLAS_BANDS;
}

class AtlasPlane;

class AtlasAllocation
{
public:
    AtlasAllocation(TextureAtlas& atlas, const Point& size, GLuint texture):
        m_atlas(atlas),
        m_rect({}, size),
        m_texture(texture)
    {
    }

    void SetSample(NMMSS::ISample& sample)
    {
        m_sample = NMMSS::PSample(&sample, NCorbaHelpers::ShareOwnership());
    }

    void SetPlane(AtlasPlane* plane)
    {
        m_plane = plane;
    }

    AtlasPlane* Plane() const
    {
        return m_plane;
    }

    void SetPosition(const Point& pos)
    {
        m_rect.Position = pos;
    }

    const Box& Rect() const
    {
        return m_rect;
    }

    void Draw(ID3D11Texture2D* dst);

    void Copy();

    Point GetSliceSize() const
    {
        return { SliceSize(m_rect.Size.X), SliceSize(m_rect.Size.Y) };
    }

private:
    TextureAtlas& m_atlas;
    Box m_rect;
    GLuint m_texture;
    NMMSS::PSample m_sample{};
    AtlasPlane* m_plane{};
};

inline Point MaxPoint(const Point& p1, const Point& p2)
{
    return { std::max(p1.X, p2.X), std::max(p1.Y, p2.Y) };
}


class AtlasPlane : public NLogging::WithLogger
{
public:
    AtlasPlane(TextureAtlas& atlas);

    ~AtlasPlane()
    {
        removeTexture();
        if (m_glFrameBuffer)
        {
            glDeleteFramebuffers(1, &m_glFrameBuffer);
        }
    }

    void ClearLayout()
    {
        m_allocations.clear();
        m_invalidatedAllocations.clear();
        m_requiredSize = {};
        m_bands.clear();
        m_bands.push_back(Box({}, { ATLAS_BANDS, ATLAS_BANDS }));
    }

    bool Place(AtlasAllocationSP allocation)
    {
        auto sliceSize = allocation->GetSliceSize();
        auto pos = allocateSliceSize(sliceSize);
        if (pos.X >= 0)
        {
            allocation->SetPosition(pos * ATLAS_SLICE);
            m_requiredSize = MaxPoint(m_requiredSize, (pos + sliceSize) * ATLAS_SLICE);
            m_allocations.push_back(allocation);
            m_invalidatedAllocations.push_back(allocation);
            allocation->SetPlane(this);
            return true;
        }
        return false;
    };

    void InvalidateAllocation(AtlasAllocationSP allocation)
    {
        m_invalidatedAllocations.push_back(allocation);
    }

    void CheckTextureSize(bool shrink)
    {
        bool needResize = m_requiredSize.X > m_size.X || m_requiredSize.Y > m_size.Y ||
            (shrink && (m_requiredSize.X * m_requiredSize.Y < (int)(SHRINK_THRESHOLD * m_size.X * m_size.Y)));
        if (needResize)
        {
            removeTexture();
            createTexture();
        }
    }

    void Render();

    bool IsEmpty()
    {
        return m_requiredSize == Point();
    }

private:
    Point allocateSliceSize(int bandIndex, const Point& sliceSize)
    {
        auto& band = m_bands[bandIndex];
        Point bandSize = band.Size;
        if (sliceSize.Y <= bandSize.Y && sliceSize.X <= bandSize.X)
        {
            Point pos = band.Position;
            if (band.Size.X > sliceSize.X)
            {
                band.Position.X += sliceSize.X;
                band.Size = { band.Size.X - sliceSize.X, sliceSize.Y };
                ++bandIndex;
            }
            else
            {
                m_bands.erase(m_bands.begin() + bandIndex);
            }
            if (sliceSize.Y < bandSize.Y)
            {
                Box bandRect({ pos.X, pos.Y + sliceSize.Y }, { bandSize.X, bandSize.Y - sliceSize.Y });
                m_bands.insert(m_bands.begin() + bandIndex, bandRect);
            }
            return pos;
        }
        return { -1, -1 };
    }

    Point allocateSliceSize(const Point& sliceSize)
    {
        for (int bandIndex = 0; bandIndex < (int)m_bands.size(); ++bandIndex)
        {
            auto pos = allocateSliceSize(bandIndex, sliceSize);
            if (pos.X >= 0)
            {
                return pos;
            }
        }
        return { -1, -1 };
    }

    void createTexture();

    void removeTexture();

private:
    TextureAtlas& m_atlas;
    Point m_size, m_requiredSize;
    GLuint m_glTexture{};
    GLuint m_glFrameBuffer{};
    HANDLE m_sharedTexture{};
    CComPtr<ID3D11Texture2D> m_texture;
    std::vector<Box> m_bands;
    std::vector<AtlasAllocationSP> m_allocations, m_invalidatedAllocations;
};

using AtlasPlaneSP = std::shared_ptr<AtlasPlane>;

class TextureAtlas : public NLogging::WithLogger
{
public:
    TextureAtlas(DECLARE_LOGGER_ARG, HiddenDxDeviceSP device):
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_device(device)
    {
        m_sharedDevice = wglDXOpenDeviceNV(m_device->GetDevice());
        m_deviceContext = std::make_shared<D3DContext>(m_device->GetWrapper());
    }

    ~TextureAtlas()
    {
        m_planes.clear();
        if (m_sharedDevice)
        {
            wglDXCloseDeviceNV(m_sharedDevice);
        }
    }

    HiddenDxDeviceSP Device() const
    {
        return m_device;
    }

    HANDLE SharedDevice() const
    {
        return m_sharedDevice;
    }

    D3DContext& Context()
    {
        return *m_deviceContext;
    }

    AtlasAllocationSP Allocate(const Point& size, GLuint texture)
    {
        if (size.X <= ATLAS_SIZE && size.Y <= ATLAS_SIZE)
        {
            auto allocation = std::make_shared<AtlasAllocation>(*this, size, texture);
            m_allocations.push_back(allocation);
            m_invalidLayout = true;
            return allocation;
        }
        return nullptr;
    }

    void Deallocate(AtlasAllocationSP allocation)
    {
        auto it = std::find(m_allocations.begin(), m_allocations.end(), allocation);
        m_allocations.erase(it);
        m_invalidLayout = true;
    }

    void InvalidateAllocation(AtlasAllocationSP allocation, NMMSS::ISample& sample)
    {
        if (auto* plane = allocation->Plane())
        {
            plane->InvalidateAllocation(allocation);
        }
        allocation->SetSample(sample);
        m_hasInvalidAllocations = true;
    }

    void Render()
    {
        if (m_hasInvalidAllocations)
        {
            validateLayout();
            if (m_framesSinceLayoutChange == FRAMES_TO_SHRINK)
            {
                for (int planeIndex = (int)m_planes.size() - 1; planeIndex >= 0; --planeIndex)
                {
                    if (m_planes[planeIndex]->IsEmpty())
                    {
                        m_planes.erase(m_planes.begin() + planeIndex);
                    }
                    else
                    {
                        m_planes[planeIndex]->CheckTextureSize(true);
                    }
                }
                _log_ << "!!!!TextureAtlas layout reevaluated, plane count: " << m_planes.size();
            }
            for (auto& plane : m_planes)
            {
                plane->Render();
            }
            ++m_framesSinceLayoutChange;
            m_hasInvalidAllocations = false;
        }
    }

    void Unregister(HANDLE& sharedTexture) const
    {
        if (sharedTexture)
        {
            BOOL success = FALSE;
            while (!success)
            {
                success = wglDXUnregisterObjectNV(m_sharedDevice, sharedTexture);
            }
            sharedTexture = nullptr;
        }
    }

private:
    void validateLayout()
    {
        if (m_invalidLayout)
        {
            for (auto plane : m_planes)
            {
                plane->ClearLayout();
            }

            std::sort(m_allocations.begin(), m_allocations.end(), [](const auto& a1, const auto& a2) { return a1->GetSliceSize().Y > a2->GetSliceSize().Y; });
            for (auto allocation : m_allocations)
            {
                Place(allocation);
            }

            for (auto plane : m_planes)
            {
                plane->CheckTextureSize(false);
            }

            m_invalidLayout = false;
            m_framesSinceLayoutChange = 0;
            m_hasInvalidAllocations = true;
            _log_ << "!!!!TextureAtlas layout validated, plane count: " << m_planes.size();
        }
    }

    void Place(AtlasAllocationSP allocation)
    {
        for (auto plane : m_planes)
        {
            if (plane->Place(allocation))
            {
                return;
            }
        }
        m_planes.push_back(std::make_shared<AtlasPlane>(*this));
        if (!m_planes.back()->Place(allocation))
        {
            throw std::exception("Cannot allocate texture space!");
        }
    }

private:
    HiddenDxDeviceSP m_device;
    std::shared_ptr<D3DContext> m_deviceContext;
    HANDLE m_sharedDevice{};

    std::vector<AtlasAllocationSP> m_allocations;
    std::vector<AtlasPlaneSP> m_planes;
    bool m_invalidLayout = true;
    bool m_hasInvalidAllocations = true;
    int m_framesSinceLayoutChange{};
};

void AtlasAllocation::Draw(ID3D11Texture2D* dst)
{
    m_atlas.Context().Context().CopySubresourceRegion(dst, 0, m_rect.Position.X, m_rect.Position.Y, 0, D3DSample::GetRenderTarget(*m_sample).Target, 0, nullptr);
}

void AtlasAllocation::Copy()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_rect.Position.X, m_rect.Position.Y, m_rect.Size.X, m_rect.Size.Y);
}

AtlasPlane::AtlasPlane(TextureAtlas& atlas) :
    NLogging::WithLogger(atlas.GetLogger()),
    m_atlas(atlas)
{
    ClearLayout();
    glGenFramebuffers(1, &m_glFrameBuffer);
}

void AtlasPlane::Render()
{
    if (!m_invalidatedAllocations.empty())
    {
        for (int i = 0; i < (int)m_invalidatedAllocations.size(); ++i)
        {
            m_invalidatedAllocations[i]->Draw(m_texture);
        }
        m_atlas.Context().End();

        wglDXLockObjectsNV(m_atlas.SharedDevice(), 1, &m_sharedTexture);

        CheckGLError();

        glBindFramebuffer(GL_FRAMEBUFFER, m_glFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_glTexture, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        for (auto allocation : m_invalidatedAllocations)
        {
            allocation->Copy();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        CheckGLError();

        wglDXUnlockObjectsNV(m_atlas.SharedDevice(), 1, &m_sharedTexture);
        m_invalidatedAllocations.clear();
    }
}

void AtlasPlane::createTexture()
{
    glGenTextures(1, &m_glTexture);
    glBindTexture(GL_TEXTURE_2D, m_glTexture);
    CheckGLError();
    m_texture = m_atlas.Device()->GetWrapper()->CreateTexture(m_requiredSize, DXGI_FORMAT_R8G8B8A8_UNORM, false, false, true);
    m_sharedTexture = wglDXRegisterObjectNV(m_atlas.SharedDevice(), m_texture, m_glTexture, GL_TEXTURE_2D, WGL_ACCESS_READ_ONLY_NV);
    m_size = m_requiredSize;
    _log_ << "Atlas plane texture created width = " << m_size.X << ", height = " << m_size.Y;
}

void AtlasPlane::removeTexture()
{
    if (m_sharedTexture)
    {
        m_atlas.Unregister(m_sharedTexture);
        glDeleteTextures(1, &m_glTexture);
        m_sharedTexture = nullptr;
        m_glTexture = 0;
        m_size = {};
    }
}


TextureAtlasSP DXGLRenderer::m_atlas;

DXGLRenderer::DXGLRenderer(DECLARE_LOGGER_ARG) :
    NLogging::WithLogger(GET_LOGGER_PTR),
    m_width(0),
    m_height(0)
{
}

void DXGLRenderer::Setup(const NMMSS::ISample& sample, GLuint texture, int width, int height, bool mipmaps)
{
    m_width = width;
    m_height = height;
    bind(texture, D3DSample::GetDevice(sample));
}

void DXGLRenderer::GetTextureSize(float& w, float& h)
{
    w = 0.f;
    h = 0.f;
}

DXGLRenderer::~DXGLRenderer()
{
    unbind();
}

bool DXGLRenderer::bind(int tex, HiddenDxDeviceSP device)
{
    if (CheckWglProcs())
    {
        if (!m_atlas)
        {
            m_atlas = std::make_unique<TextureAtlas>(GET_LOGGER_PTR, device);
        }
        if (m_atlas->SharedDevice())
        {
            m_atlasAllocation = m_atlas->Allocate({ m_width, m_height }, tex);
        }
    }
    return IsValid();
}

void DXGLRenderer::unbind()
{
    m_atlas->Deallocate(m_atlasAllocation);
    m_atlasAllocation.reset();
}

void DXGLRenderer::Render(NMMSS::ISample& sample)
{
    if (D3DSample::GetRenderTarget(sample).Target && IsValid())
    {
        m_atlas->InvalidateAllocation(m_atlasAllocation, sample);
    }
}

void DXGLRenderer::CompleteRender()
{
    m_atlas->Render();
}

bool DXGLRenderer::IsValid() const
{
    return !!m_atlasAllocation;
}

void DXGLRenderer::Destroy()
{
    delete this;
}

NMMSS::IGLRenderer* CreateQSGLRenderer(DECLARE_LOGGER_ARG)
{
    return new DXGLRenderer(GET_LOGGER_PTR);
}

void InitializeQSGLRenderer()
{
}

namespace NMMSS
{
    void FinalizeDXGLRenderer()
    {
        DXGLRenderer::m_atlas.reset();
    }
}
