#ifndef CUDA_SURFACE_H
#define CUDA_SURFACE_H

#include "../MMCoding/Points.h"

#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64) || defined(__aarch64__)
typedef unsigned long long CUdeviceptr;
#else
typedef unsigned int CUdeviceptr;
#endif

enum CUarray_format_enum : int;
typedef CUarray_format_enum CUarray_format;
typedef unsigned long long CUtexObject;
enum cudaError_enum : int;
typedef cudaError_enum CUresult;

class CudaDevice;

class CudaSurfaceRegion
{
public:
    SurfaceSize Size;
    CUdeviceptr Ptr{};
};

CudaSurfaceRegion GetNVRegion(const CudaSurfaceRegion& yRegion);
int RoundUpTo2(int size);
SurfaceSize GetNv12UVSize(const SurfaceSize& size);
int GetNv12MemorySize(const SurfaceSize& size);

class CudaTexture
{
public:
    CudaTexture(const CudaDevice& device, const CudaSurfaceRegion& surface, CUarray_format format, int channels);
    CudaTexture(const CudaDevice& device, const CudaSurfaceRegion& surface, bool color);
    CudaTexture(const CudaDevice& device, CUdeviceptr ptr, const SurfaceSize& size, CUarray_format format, int channels);
    ~CudaTexture();

public:
    CUtexObject Tex = {};
    SurfaceSize Size;
};

#endif
