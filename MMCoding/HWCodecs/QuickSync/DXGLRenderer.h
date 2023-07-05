#ifndef DX_GL_RENDERER_H
#define DX_GL_RENDERER_H

#include "../MMCoding/HWCodecs/IGLRenderer.h"
#include <Logging/log2.h>
#include <memory>
#include <windows.h>

class HiddenDxDevice;
class D3DContext;
class RenderTarget;

using HiddenDxDeviceSP = std::shared_ptr<HiddenDxDevice>;

class AtlasAllocation;
using AtlasAllocationSP = std::shared_ptr<AtlasAllocation>;

class TextureAtlas;
using TextureAtlasSP = std::unique_ptr<TextureAtlas>;

typedef unsigned int GLuint;

class DXGLRenderer : public NLogging::WithLogger, public NMMSS::IGLRenderer
{
public:
    DXGLRenderer(DECLARE_LOGGER_ARG);
    ~DXGLRenderer();

    void Setup(const NMMSS::ISample& sample, GLuint texture, int width, int height, bool mipmaps) override;
    void Render(NMMSS::ISample& sample) override;
    void CompleteRender() override;
    bool IsValid() const override;
    void Destroy() override;
    void GetTextureSize(float& w, float& h) override;

private:
    bool bind(int tex, HiddenDxDeviceSP device);
    void unbind();

private:
    int m_width;
    int m_height;

    AtlasAllocationSP m_atlasAllocation;

public:
    static TextureAtlasSP m_atlas;
};

#endif