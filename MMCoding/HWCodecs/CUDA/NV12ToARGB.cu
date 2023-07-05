#include <cuda.h>
#include <helper_math.h>
#include <stdint.h>

inline static __device__ float4 make_float4(uchar4 v)
{
    return make_float4(v.x, v.y, v.z, v.w);
}

inline static __device__ float2 make_float2(uchar2 v)
{
    return make_float2(v.x, v.y);
}

inline static __device__ uint8_t Clamp(float v)
{
    return max(min(__float2int_rn(v), 255), 0);
}

inline static __device__ uchar4 make_uchar4(float r, float g, float b)
{
    return make_uchar4(Clamp(r), Clamp(g), Clamp(b), 0xFF);
}

__device__ const float Y_OFFSET = -16.0f;
__device__ const float UV_OFFSET = -128.0f;
__device__ const float Y_SCALE = 1.1644f;
__device__ const float V_SCALE_R = 1.5960f;
__device__ const float V_SCALE_G = -0.8130f;
__device__ const float U_SCALE_G = -0.3918f;
__device__ const float U_SCALE_B = 2.0172f;

static __global__ void Nv12ToArgbKernel(const uint8_t* src, int srcPitch, CUsurfObject dst, int dstWidth, int dstHeight)
{
    int x = (blockIdx.x * blockDim.x + threadIdx.x) << 2;
    int y = (blockIdx.y * blockDim.y + threadIdx.y) << 1;
    if (x < dstWidth && y < dstHeight)
    {
        int surfaceX = x << 2;

        float4 nv = make_float4(*(uchar4*)(src + (dstHeight + (y >> 1)) * srcPitch + x)) + make_float4(UV_OFFSET);
        float4 yOffset = make_float4(Y_OFFSET);
        float4 u = make_float4(nv.x, nv.x, nv.z, nv.z);
        float4 v = make_float4(nv.y, nv.y, nv.w, nv.w);

        for (int row = 0; row < 2; ++row)
        {
            float4 luma = (make_float4(*(uchar4*)(src + (y + row) * srcPitch + x)) + yOffset) * Y_SCALE;

            float4 r = luma + v * V_SCALE_R;
            float4 g = luma + u * U_SCALE_G + v * V_SCALE_G;
            float4 b = luma + u * U_SCALE_B;

            surf2Dwrite(make_uchar4(r.x, g.x, b.x), dst, surfaceX,      y + row, cudaBoundaryModeZero);
            surf2Dwrite(make_uchar4(r.y, g.y, b.y), dst, surfaceX + 4,  y + row, cudaBoundaryModeZero);
            surf2Dwrite(make_uchar4(r.z, g.z, b.z), dst, surfaceX + 8,  y + row, cudaBoundaryModeZero);
            surf2Dwrite(make_uchar4(r.w, g.w, b.w), dst, surfaceX + 12, y + row, cudaBoundaryModeZero);
        }
    }
}

static __global__ void Nv12ToArgbScaleKernel(const uint8_t* src, int srcPitch, int srcHeight, CUsurfObject dst, int dstWidth, int dstHeight, int scale)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstWidth && dstY < dstHeight)
    {
        float y = ((*(src + (dstY * srcPitch + dstX) * scale)) + Y_OFFSET) * Y_SCALE;
        float2 nv = make_float2(*(uchar2*)(src + (srcHeight + ((dstY * scale) >> 1)) * srcPitch + (((dstX * scale) >> 1) << 1))) + make_float2(UV_OFFSET);
        float u = nv.x;
        float v = nv.y;

        float r = y + v * V_SCALE_R;
        float g = y + u * U_SCALE_G + v * V_SCALE_G;
        float b = y + u * U_SCALE_B;
        surf2Dwrite(make_uchar4(r, g, b), dst, dstX << 2, dstY, cudaBoundaryModeZero);
    }
}

const int BLOCK_SIZE = 32;

dim3 DefaultBlock()
{
    return dim3(BLOCK_SIZE, BLOCK_SIZE);
}

dim3 GetGrid(int countX, int countY)
{
    return dim3((countX + BLOCK_SIZE - 1) / BLOCK_SIZE, (countY + BLOCK_SIZE - 1) / BLOCK_SIZE);
}

extern "C" void Nv12ToArgb(CUdeviceptr src, int srcWidth, int srcHeight, int srcPitch, CUarray dstArray)
{
    CUDA_ARRAY_DESCRIPTOR dst = {};
    cuArrayGetDescriptor(&dst, dstArray);

    CUDA_RESOURCE_DESC resourceDescriptor = {};
    resourceDescriptor.resType = CUresourcetype::CU_RESOURCE_TYPE_ARRAY;
    resourceDescriptor.res.array.hArray = dstArray;

    CUsurfObject surface = {};
    cuSurfObjectCreate(&surface, &resourceDescriptor);

    int scale = srcWidth / (int)dst.Width;
    if (scale == 1)
    {
        dim3 grid = GetGrid((int)dst.Width / 4, (int)dst.Height / 2);
        Nv12ToArgbKernel<<<grid, DefaultBlock()>>>((const uint8_t*)src, srcPitch, surface, (int)dst.Width, (int)dst.Height);
    }
    else
    {
        dim3 grid = GetGrid((int)dst.Width, (int)dst.Height);
        Nv12ToArgbScaleKernel<<<grid, DefaultBlock()>>>((const uint8_t*)src, srcPitch, srcHeight, surface, (int)dst.Width, (int)dst.Height, scale);
    }

    cuSurfObjectDestroy(surface);
}
