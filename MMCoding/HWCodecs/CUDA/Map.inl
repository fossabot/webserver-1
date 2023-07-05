R"RawLiteral(

inline __device__ float MapCoord(int dstCoord, int dstSize, int mapSize)
{
    return (TexCoord(dstCoord, dstSize) * (mapSize - 1) + 0.5f) / mapSize;
}

inline __device__ unsigned char Mid(int c1, int c2, int c3, int c4)
{
    return (unsigned char)((c1 + c2 + c3 + c4 + 2) / 4);
}

inline __device__ unsigned char Interpolatef(float c1, float c2, float factor)
{
    return c1 * (1.0 - factor) + c2 * factor;
}

inline __device__ unsigned char Interpolate(float c1, float c2, float factor)
{
    return (unsigned char)(Interpolatef(c1, c2, factor) + 0.5f);
}

extern "C" __global__ void MapKernel(CUtexObject src, CUtexObject map, SurfaceSize mapSize, unsigned char* dst, SurfaceSize dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float2 xy = tex2D<float2>(map, MapCoord(dstX, dstSize.Width, mapSize.Width), MapCoord(dstY, dstSize.Height, mapSize.Height));
        *(dst + dstY * dstSize.Pitch + dstX) = tex2D<uchar1>(src, xy.x, xy.y).x;
    }
}

extern "C" __global__ void MapColorKernel(CUtexObject src, CUtexObject map, SurfaceSize mapSize, unsigned char* dst, SurfaceSize dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float2 xy = tex2D<float2>(map, MapCoord(dstX, dstSize.Width, mapSize.Width), MapCoord(dstY, dstSize.Height, mapSize.Height));
        *(uchar2*)(dst + dstY * dstSize.Pitch + dstX * 2) = tex2D<uchar2>(src, xy.x, xy.y);
    }
}

extern "C" __global__ void ScaleKernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float fx, fy;
        int srcX = UntexCoord(TexCoord(dstX, dstSize.Width), srcSize.Width, &fx);
        int srcY = UntexCoord(TexCoord(dstY, dstSize.Height), srcSize.Height, &fy);

        const unsigned char* pSrc = src + srcSize.Pitch * srcY + srcX;
        uchar2 src1 = make_uchar2(*pSrc, *(pSrc + 1));
        pSrc += srcSize.Pitch;
        uchar2 src2 = make_uchar2(*pSrc, *(pSrc + 1));

        *(dst + dstY * dstSize.Pitch + dstX) = Interpolate(Interpolatef(src1.x, src1.y, fx), Interpolatef(src2.x, src2.y, fx), fy);
    }
}

extern "C" __global__ void ScaleColorKernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize, float xFactor, float yFactor)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float fx, fy;
        int srcX = UntexCoord(TexCoord(dstX, dstSize.Width) * xFactor, srcSize.Width, &fx);
        int srcY = UntexCoord(TexCoord(dstY, dstSize.Height) * yFactor, srcSize.Height, &fy);
        int deltaX = min(1, srcSize.Width - srcX - 1) * 2;
        int deltaY = min(1, srcSize.Height - srcY - 1) * srcSize.Pitch;

        const unsigned char* pSrc = src + srcSize.Pitch * srcY + srcX * 2;
        uchar2 src11 = *(uchar2*)pSrc;
        uchar2 src12 = *(uchar2*)(pSrc + deltaX);
        pSrc += deltaY;
        uchar2 src21 = *(uchar2*)pSrc;
        uchar2 src22 = *(uchar2*)(pSrc + deltaX);

        *(uchar2*)(dst + dstY * dstSize.Pitch + dstX * 2) = make_uchar2(
            Interpolate(Interpolatef(src11.x, src12.x, fx), Interpolatef(src21.x, src22.x, fx), fy),
            Interpolate(Interpolatef(src11.y, src12.y, fx), Interpolatef(src21.y, src22.y, fx), fy));
    }
}

extern "C" __global__ void CreateMipKernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        const unsigned char* pSrc = src + srcSize.Pitch * dstY * 2 + dstX * 2;
        uchar2 src1 = *(uchar2*)pSrc;
        uchar2 src2 = *(uchar2*)(pSrc + srcSize.Pitch);
        *(dst + dstY * dstSize.Pitch + dstX) = Mid(src1.x, src1.y, src2.x, src2.y);
    }
}

extern "C" __global__ void CreateMipColorKernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        const unsigned char* pSrc = src + srcSize.Pitch * dstY * 2 + dstX * 4;
        uchar4 src1 = *(uchar4*)pSrc;
        uchar4 src2 = *(uchar4*)(pSrc + srcSize.Pitch);
        *(uchar2*)(dst + dstY * dstSize.Pitch + dstX * 2) = 
            make_uchar2(Mid(src1.x, src1.z, src2.x, src2.z), Mid(src1.y, src1.w, src2.y, src2.w));
    }
}

extern "C" __global__ void MipFilterKernel(CUtexObject src1, CUtexObject src2, unsigned char* dst, SurfaceSize dstSize, float mipFactor)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float tx = TexCoord(dstX, dstSize.Width);
        float ty = TexCoord(dstY, dstSize.Height);
        *(dst + dstY * dstSize.Pitch + dstX) = Interpolate(tex2D<uchar1>(src1, tx, ty).x, tex2D<uchar1>(src2, tx, ty).x, mipFactor);
    }
}

extern "C" __global__ void MipFilterColorKernel(CUtexObject src1, CUtexObject src2, unsigned char* dst, SurfaceSize dstSize, float mipFactor, float xFactor, float yFactor)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float tx = TexCoord(dstX, dstSize.Width) * xFactor;
        float ty = TexCoord(dstY, dstSize.Height) * yFactor;
        uchar2 v1 = tex2D<uchar2>(src1, tx, ty);
        uchar2 v2 = tex2D<uchar2>(src2, tx, ty);
        *(uchar2*)(dst + dstY * dstSize.Pitch + dstX * 2) = make_uchar2(Interpolate(v1.x, v2.x, mipFactor), Interpolate(v1.y, v2.y, mipFactor));
    }
}



inline __device__ float GetPixelFactor(float startCoordinate, float endCoordinate, int coordinate)
{
    ++coordinate;
    return min(coordinate - startCoordinate, 1.0f) - max(coordinate - endCoordinate, 0.0f);
}

template<typename TPixel>
inline __device__ TPixel GetPixel(const unsigned char* data);

template<> inline __device__ float GetPixel<float>(const unsigned char* data)
{
    return *data;
}

template<> inline __device__ float2 GetPixel<float2>(const unsigned char* data)
{
    return make_float2(*(uchar2*)data);
}


template<typename TPixel>
inline __device__ void SetPixel(unsigned char* dst, int dstX, int dstY, int dstPitch, TPixel value);

template<> inline __device__ void SetPixel<float>(unsigned char* dst, int dstX, int dstY, int dstPitch, float value)
{
    *(dst + dstY * dstPitch + dstX) = lrintf(value);
}

template<> inline __device__ void SetPixel<float2>(unsigned char* dst, int dstX, int dstY, int dstPitch, float2 value)
{
    *(uchar2*)(dst + dstY * dstPitch + dstX * 2) = make_uchar2(lrintf(value.x), lrintf(value.y));
}


template<typename TPixel, int PixelSize>
inline __device__ TPixel GetLineResult(const unsigned char* src, int pixels, float xFirstFactor, float xLastFactor)
{
    TPixel result = GetPixel<TPixel>(src) * xFirstFactor;
    src += PixelSize;
    for(int x = 1; x < pixels; ++x, src += PixelSize)
    {
        result += GetPixel<TPixel>(src);
    }
    return result + (GetPixel<TPixel>(src) * xLastFactor);
}

template<typename TPixel, int PixelSize>
inline __device__ void DoScale2(const unsigned char* src, const SurfaceSize& srcSize, unsigned char* dst, const SurfaceSize& dstSize)
{
    int dstX = blockIdx.x * blockDim.x + threadIdx.x;
    int dstY = blockIdx.y * blockDim.y + threadIdx.y;
    if (dstX < dstSize.Width && dstY < dstSize.Height)
    {
        float xScale = (float)srcSize.Width / dstSize.Width;
        float srcX0 = xScale * dstX;
        float srcX1 = srcX0 + xScale;
        int xFirst = (int)(srcX0 + 0.001);
        int xLast = (int)(srcX1 - 0.001);
        int pixelsX = xLast - xFirst;
        float xFirstFactor = GetPixelFactor(srcX0, srcX1, xFirst);
        float xLastFactor = GetPixelFactor(srcX0, srcX1, xLast);

        float yScale = (float)srcSize.Height / dstSize.Height;
        float srcY0 = yScale * dstY;
        float srcY1 = srcY0 + yScale;
        int yFirst = (int)(srcY0 + 0.001);
        int yLast = (int)(srcY1 - 0.001);
        int pixelsY = yLast - yFirst;
        float yFirstFactor = GetPixelFactor(srcY0, srcY1, yFirst);
        float yLastFactor = GetPixelFactor(srcY0, srcY1, yLast);

        src += yFirst * srcSize.Pitch + xFirst * PixelSize;
        float factor = 1.0f / (xScale * yScale);

        TPixel result = GetLineResult<TPixel, PixelSize>(src, pixelsX, xFirstFactor, xLastFactor) * yFirstFactor;
        src += srcSize.Pitch;
        for(int y = 1; y < pixelsY; ++y, src += srcSize.Pitch)
        {
            result += GetLineResult<TPixel, PixelSize>(src, pixelsX, xFirstFactor, xLastFactor);
        }
        result += GetLineResult<TPixel, PixelSize>(src, pixelsX, xFirstFactor, xLastFactor) * yLastFactor;

        SetPixel<TPixel>(dst, dstX, dstY, dstSize.Pitch, result * factor);
    }
}

extern "C" __global__ void Scale2Kernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize)
{
    DoScale2<float, 1>(src, srcSize, dst, dstSize);
}

extern "C" __global__ void Scale2ColorKernel(const unsigned char* src, SurfaceSize srcSize, unsigned char* dst, SurfaceSize dstSize)
{
    DoScale2<float2, 2>(src, srcSize, dst, dstSize);
}


)RawLiteral"
