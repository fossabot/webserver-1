R"RawLiteral(

__device__ const float Y_OFFSET = -16.0f;
__device__ const float UV_OFFSET = -128.0f;
__device__ const float Y_SCALE = 1.1644f;
__device__ const float V_SCALE_R = 1.5960f;
__device__ const float V_SCALE_G = -0.8130f;
__device__ const float U_SCALE_G = -0.3918f;
__device__ const float U_SCALE_B = 2.0172f;

extern "C" __global__ void Nv12ToArgbKernel(const unsigned char* src, int srcPitch, unsigned char* dst, SurfaceSize dstSize)
{
    int x = (blockIdx.x * blockDim.x + threadIdx.x) << 2;
    int y = (blockIdx.y * blockDim.y + threadIdx.y) << 1;
    if (x < dstSize.Width && y < dstSize.Height)
    {
        float4 nv = make_float4(*(uchar4*)(src + (dstSize.Height + (y >> 1)) * srcPitch + x)) + make_float4(UV_OFFSET);
        float4 yOffset = make_float4(Y_OFFSET);
        float4 u = make_float4(nv.x, nv.x, nv.z, nv.z);
        float4 v = make_float4(nv.y, nv.y, nv.w, nv.w);

        for (int row = 0; row < 2; ++row)
        {
            float4 luma = (make_float4(*(uchar4*)(src + (y + row) * srcPitch + x)) + yOffset) * Y_SCALE;

            float4 r = luma + v * V_SCALE_R;
            float4 g = luma + u * U_SCALE_G + v * V_SCALE_G;
            float4 b = luma + u * U_SCALE_B;

            unsigned char* dst1 = dst + (y + row) * dstSize.Pitch + (x << 2);
            *(uchar4*)(dst1)      = make_uchar4(b.x, g.x, r.x);
            *(uchar4*)(dst1 + 4)  = make_uchar4(b.y, g.y, r.y);
            *(uchar4*)(dst1 + 8)  = make_uchar4(b.z, g.z, r.z);
            *(uchar4*)(dst1 + 12) = make_uchar4(b.w, g.w, r.w);
        }
    }
}

extern "C" __global__ void Nv12ToArgbScaleKernel(CUtexObject srcY, CUtexObject srcNV, unsigned char* dst, SurfaceSize dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float y = (Y_OFFSET + tex2D<uchar1>(srcY, TexCoord(dstX, dstSize.Width), TexCoord(dstY, dstSize.Height)).x) * Y_SCALE;
        float2 nv = make_float2(tex2D<uchar2>(srcNV, TexCoord(dstX, dstSize.Width), TexCoord(dstY, dstSize.Height))) + make_float2(UV_OFFSET);
        float u = nv.x;
        float v = nv.y;

        float r = y + v * V_SCALE_R;
        float g = y + u * U_SCALE_G + v * V_SCALE_G;
        float b = y + u * U_SCALE_B;
        *(uchar4*)(dst + dstY * dstSize.Pitch + (dstX << 2)) = make_uchar4(b, g, r);
    }
}

extern "C" __global__ void ExtractUVKernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < dstSize.Width && y < dstSize.Height)
    {
        uchar2 nv = *(uchar2*)(src + (srcSize.Height + y) * srcSize.Pitch + x * 2);
        unsigned char* pDst = dst + y * dstSize.Pitch + x;
        *pDst = nv.x;
        *(pDst + dstSize.Height * dstSize.Pitch) = nv.y;
    }
}

)RawLiteral"
