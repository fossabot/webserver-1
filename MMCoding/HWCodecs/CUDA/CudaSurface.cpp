#include "CudaSurface.h"
#include "CudaDevice.h"
#include "dynlink_cuda.h"


CudaTexture::CudaTexture(const CudaDevice& device, const CudaSurfaceRegion& surface, CUarray_format format, int channels) :
    CudaTexture(device, surface.Ptr, surface.Size, format, channels)
{
}

CudaTexture::CudaTexture(const CudaDevice& device, const CudaSurfaceRegion& surface, bool color):
    CudaTexture(device, surface, CUarray_format::CU_AD_FORMAT_UNSIGNED_INT8, color ? 2 : 1)
{

}

CudaTexture::CudaTexture(const CudaDevice& device, CUdeviceptr ptr, const SurfaceSize& size, CUarray_format format, int channels)
{
    Size = size;

    CUDA_RESOURCE_DESC res = {};
    res.resType = CU_RESOURCE_TYPE_PITCH2D;
    res.res.pitch2D.devPtr = ptr;
    res.res.pitch2D.width = Size.Width;
    res.res.pitch2D.height = Size.Height;
    res.res.pitch2D.pitchInBytes = Size.Pitch;
    res.res.pitch2D.numChannels = channels;
    res.res.pitch2D.format = format;

    CUDA_TEXTURE_DESC tex = {};
    tex.addressMode[0] = tex.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    tex.filterMode = CU_TR_FILTER_MODE_LINEAR;
    tex.flags = CU_TRSF_NORMALIZED_COORDINATES;
    if (format != CUarray_format::CU_AD_FORMAT_FLOAT && format != CUarray_format::CU_AD_FORMAT_HALF)
    {
        tex.flags |= CU_TRSF_READ_AS_INTEGER;
    }

    device.CheckStatus(cuTexObjectCreate(&Tex, &res, &tex, nullptr), "cuTexObjectCreate");
}

CudaTexture::~CudaTexture()
{
    if (Tex)
    {
        cuTexObjectDestroy(Tex);
    }
}

int RoundUpTo2(int size)
{
    return (size + 1) & ~1;
}

CudaSurfaceRegion GetNVRegion(const CudaSurfaceRegion& yRegion)
{
    SurfaceSize size = { RoundUpTo2(yRegion.Size.Width) / 2, RoundUpTo2(yRegion.Size.Height) / 2, yRegion.Size.Pitch };
    return { size, yRegion.Ptr + yRegion.Size.Height * yRegion.Size.Pitch};
}

SurfaceSize GetNv12UVSize(const SurfaceSize& size)
{
    return { RoundUpTo2(size.Width) / 2,  RoundUpTo2(size.Height) / 2, size.Pitch };
}

int GetNv12MemorySize(const SurfaceSize& size)
{
    return size.MemorySize() + GetNv12UVSize(size).MemorySize();
}
