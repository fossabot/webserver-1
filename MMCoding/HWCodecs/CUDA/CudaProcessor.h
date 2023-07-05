#pragma once

#include "dynlink_cuda.h"
#include "HWCodecs/HWCodecsDeclarations.h"

class CudaTexture;
class CudaSurfaceRegion;
class SurfaceSize;

class CudaProcessor
{
public:
    CudaProcessor(const CudaDevice& device, int major, int minor);
    ~CudaProcessor();

public:
    void Nv12ToArgb(const CudaSurfaceRegion& src, CUgraphicsResource dstSurface, const SurfaceSize& dstSize, CudaStreamSP stream) const;
    void Map(CudaTexture& src, CudaSurfaceRegion& dst, CudaTexture& map, bool color, CudaStreamSP stream) const;
    void Scale(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const;
    void Scale2(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const;
    void ExtractUV(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const;
    void CreateMip(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const;
    void MipFilter(const CudaSurfaceRegion& src1, const CudaSurfaceRegion& src2, const CudaSurfaceRegion& dst, CudaStreamSP stream) const;
    bool IsValid() const;

private:
    void launchKernel(CUfunction f, int countX, int countY, std::initializer_list<const void*> args, CudaStreamSP stream = nullptr) const;
    void launchKernel(CUfunction f, const SurfaceSize& size, std::initializer_list<const void*> args, CudaStreamSP stream = nullptr) const;
    void loadFunction(CUfunction *hfunc, const char *name);

private:
    const CudaDevice& m_device;
    CUmodule m_module{};
    CUfunction m_nv12ToArgb, m_nv12ToArgbScale, m_map, m_mapColor, m_scale, m_scaleColor, m_extractUV,
        m_createMip, m_createMipColor, m_mipFilter, m_mipFilterColor,
        m_scale2, m_scale2Color;
    bool m_isValid{};
};
