R"RawLiteral(

class SurfaceSize
{
public:
    int Width;
    int Height;
    int Pitch;
};


typedef unsigned long long CUsurfObject;
typedef unsigned long long CUtexObject;



inline __device__ unsigned char Clamp(float v)
{
    return max(min((int)lrintf(v), 255), 0);
}

inline __device__ float TexCoord(int dstCoord, int dstSize)
{
    return ((float)dstCoord + 0.5f) / dstSize;
}

inline __device__ int UntexCoord(float srcCoord, int srcSize, float* factor)
{
    float t = srcCoord * srcSize - 0.5f;
    int tInt = (int)t;
    *factor = t - tInt;
    return tInt;
}




inline __device__ float4 operator+(float4 a, float4 b)
{
    return make_float4(a.x + b.x, a.y + b.y, a.z + b.z,  a.w + b.w);
}

inline __device__ float2 operator+(float2 a, float2 b)
{
    return make_float2(a.x + b.x, a.y + b.y);
}

inline __device__ void operator+=(float2& a, float2 b)
{
    a.x += b.x;
    a.y += b.y;
}




inline __device__ float4 operator*(float4 a, float b)
{
    return make_float4(a.x * b, a.y * b, a.z * b,  a.w * b);
}

inline __device__ float2 operator*(float2 a, float b)
{
    return make_float2(a.x * b, a.y * b);
}





inline __device__ float4 make_float4(float s)
{
    return make_float4(s, s, s, s);
}

inline __device__ float4 make_float4(uchar4 v)
{
    return make_float4(v.x, v.y, v.z, v.w);
}

inline __device__ float2 make_float2(float s)
{
    return make_float2(s, s);
}

inline __device__ float2 make_float2(uchar2 v)
{
    return make_float2(v.x, v.y);
}

inline __device__ uchar4 make_uchar4(float r, float g, float b)
{
    return make_uchar4(Clamp(r), Clamp(g), Clamp(b), 0xFF);
}





)RawLiteral"
