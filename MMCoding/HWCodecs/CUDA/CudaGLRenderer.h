#ifndef CUDA_GL_RENDERER_H
#define CUDA_GL_RENDERER_H

#include "../Sample.h"
#include "HWCodecs/IGLRenderer.h"
#include "HWCodecs/HWCodecsDeclarations.h"
#include "../MMCoding/Points.h"

typedef struct CUgraphicsResource_st *CUgraphicsResource;

class CudaSample;

class CudaGLRenderer : public NMMSS::IGLRenderer
{
public:
    ~CudaGLRenderer();

    void Setup(const NMMSS::ISample& sample, GLuint texture, int width, int height, bool mipmaps) override;
    void Render(NMMSS::ISample& sample) override;
    void CompleteRender() override;
    bool IsValid() const override;
    void Destroy() override;
    void GetTextureSize(float& w, float& h) override;

private:
    void setTexture(GLuint texture, const Point& size);
    void setup(const NMMSS::ISample& sample, GLuint texture, const Point& size);

private:
    GLuint m_texture{};
    GLuint m_bufferId{};
    CUgraphicsResource m_surface{};
    CudaDeviceSP m_device;
    NMMSS::PSample m_sample;
    SurfaceSize m_size{};
};

#endif