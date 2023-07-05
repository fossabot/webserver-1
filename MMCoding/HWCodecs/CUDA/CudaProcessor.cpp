#include "CudaProcessor.h"
#include "CudaDevice.h"
#include "CudaSample.h"

#include "dynlink_nvrtc.h"
#include <string>

std::string HelperCode =
#include "Helper.inl"
;

std::string Nv12ToArgbCode =
#include "NV12ToARGB.inl"
;

std::string MapCode =
#include "Map.inl"
;


CudaProcessor::CudaProcessor(const CudaDevice& device, int major, int minor):
    m_device(device)
{
    nvrtcProgram program;
    nvrtcCreateProgram(&program, (HelperCode + Nv12ToArgbCode + MapCode).c_str(), "Processor", 0, nullptr, nullptr);

    std::stringstream ss;
    ss << "-arch=compute_" << major << minor;
    std::string option = ss.str();
    const char* options[] = { option.c_str() };

    if(nvrtcCompileProgram(program, 1, options) == nvrtcResult::NVRTC_SUCCESS)
    {
        size_t ptxSize;
        nvrtcResult res = nvrtcGetPTXSize(program, &ptxSize);
        std::string ptx(ptxSize, ' ');
        res = nvrtcGetPTX(program, &ptx[0]);
        res = nvrtcDestroyProgram(&program);
        (void)res;

        if (m_device.CheckStatus(cuModuleLoadData(&m_module, ptx.c_str()), "cuModuleLoadData"))
        {
            loadFunction(&m_nv12ToArgb,         "Nv12ToArgbKernel");
            loadFunction(&m_nv12ToArgbScale,    "Nv12ToArgbScaleKernel");
            loadFunction(&m_map,                "MapKernel");
            loadFunction(&m_mapColor,           "MapColorKernel");
            loadFunction(&m_scale,              "ScaleKernel");
            loadFunction(&m_scaleColor,         "ScaleColorKernel");
            loadFunction(&m_scale2,             "Scale2Kernel");
            loadFunction(&m_scale2Color,        "Scale2ColorKernel");
            loadFunction(&m_extractUV,          "ExtractUVKernel");
            loadFunction(&m_createMip,          "CreateMipKernel");
            loadFunction(&m_createMipColor,     "CreateMipColorKernel");
            loadFunction(&m_mipFilter,          "MipFilterKernel");
            loadFunction(&m_mipFilterColor,     "MipFilterColorKernel");
            m_isValid = true;
            return;
        }
    }

    size_t logSize;
    nvrtcGetProgramLogSize(program, &logSize);
    std::string log(logSize, ' ');
    nvrtcGetProgramLog(program, &log[0]);
}

CudaProcessor::~CudaProcessor()
{
    if (m_module)
    {
        cuModuleUnload(m_module);
    }
}

void CudaProcessor::loadFunction(CUfunction* hfunc, const char* name)
{
    m_device.CheckStatus(cuModuleGetFunction(hfunc, m_module, name), "cuModuleGetFunction");
}

bool CudaProcessor::IsValid() const
{
    return m_isValid;
}

const int BLOCK_SIZE = 32;

inline unsigned int GridSize(unsigned int count)
{
    return (count + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

void CudaProcessor::launchKernel(CUfunction f, int countX, int countY, std::initializer_list<const void*> args, CudaStreamSP stream) const
{
    m_device.CheckStatus(cuLaunchKernel(f,
        GridSize(countX), GridSize(countY), 1,   // grid dim
        BLOCK_SIZE, BLOCK_SIZE, 1,               // block dim
        0, stream ? stream->Stream() : nullptr,  // shared mem and stream
        const_cast<void**>(args.begin()),        // arguments
        0), "cuLaunchKernel");
    if (!stream)
    {
        m_device.CheckStatus(cuCtxSynchronize(), "cuCtxSynchronize");
    }
}

void CudaProcessor::launchKernel(CUfunction f, const SurfaceSize& size, std::initializer_list<const void*> args, CudaStreamSP stream) const
{
    launchKernel(f, size.Width, size.Height, args, stream);
}

void CudaProcessor::Nv12ToArgb(const CudaSurfaceRegion& src, CUgraphicsResource dstResource, const SurfaceSize& dstSize, CudaStreamSP stream) const
{
    CUdeviceptr ptr;
    size_t size;
    m_device.CheckStatus(cuGraphicsResourceGetMappedPointer(&ptr, &size, dstResource), "cuGraphicsResourceGetMappedPointer");

    if (src.Size.Width == dstSize.Width && src.Size.Height == dstSize.Height)
    {
        launchKernel(m_nv12ToArgb, dstSize.Width / 4, dstSize.Height / 2, 
            { &src.Ptr, &src.Size.Pitch, &ptr, &dstSize }, stream);
    }
    else
    {
        CudaTexture srcY(m_device, src, false);
        CudaTexture srcNV(m_device, GetNVRegion(src), true);
        launchKernel(m_nv12ToArgbScale, dstSize, 
            { &srcY.Tex, &srcNV.Tex, &ptr, &dstSize}, stream);
    }
}

void CudaProcessor::Map(CudaTexture& src, CudaSurfaceRegion& dst, CudaTexture& map, bool color, CudaStreamSP stream) const
{
    launchKernel(color ? m_mapColor : m_map, dst.Size,
        { &src.Tex, &map.Tex, &map.Size, &dst.Ptr, &dst.Size }, stream);
}

void CudaProcessor::Scale(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const
{
    CudaSurfaceRegion srcNV = GetNVRegion(src);
    CudaSurfaceRegion dstNV = GetNVRegion(dst);
    launchKernel(m_scale, dst.Size, { &src.Ptr, &src.Size, &dst.Ptr, &dst.Size }, stream);

    float xFactor = (float)RoundUpTo2(dst.Size.Width) / dst.Size.Width;
    float yFactor = (float)RoundUpTo2(dst.Size.Height) / dst.Size.Height;
    launchKernel(m_scaleColor, dstNV.Size, { &srcNV.Ptr, &srcNV.Size, &dstNV.Ptr, &dstNV.Size, &xFactor, &yFactor }, stream);
}

void CudaProcessor::Scale2(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const
{
    CudaSurfaceRegion srcNV = GetNVRegion(src);
    CudaSurfaceRegion dstNV = GetNVRegion(dst);
    launchKernel(m_scale2, dst.Size, { &src.Ptr, &src.Size, &dst.Ptr, &dst.Size }, stream);
    launchKernel(m_scale2Color, dstNV.Size, { &srcNV.Ptr, &srcNV.Size, &dstNV.Ptr, &dstNV.Size }, stream);
}

void CudaProcessor::ExtractUV(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const
{
    launchKernel(m_extractUV, src.Size,
        {&src.Ptr, &src.Size, &dst.Ptr, &dst.Size}, stream);
}

void CudaProcessor::CreateMip(const CudaSurfaceRegion& src, const CudaSurfaceRegion& dst, CudaStreamSP stream) const
{
    CudaSurfaceRegion srcNV = GetNVRegion(src);
    CudaSurfaceRegion dstNV = GetNVRegion(dst);
    launchKernel(m_createMip, dst.Size, {&src.Ptr, &src.Size, &dst.Ptr, &dst.Size}, stream);
    launchKernel(m_createMipColor, dstNV.Size, { &srcNV.Ptr, &srcNV.Size, &dstNV.Ptr, &dstNV.Size }, stream);
}

CudaSurfaceRegion CorrectHiRegion(CudaSurfaceRegion hiRegion, const CudaSurfaceRegion& loRegion)
{
    hiRegion.Size.Width = loRegion.Size.Width * 2;
    hiRegion.Size.Height = loRegion.Size.Height * 2;
    return hiRegion;
}

void CudaProcessor::MipFilter(const CudaSurfaceRegion& src1, const CudaSurfaceRegion& src2, const CudaSurfaceRegion& dst, CudaStreamSP stream) const
{
    CudaSurfaceRegion srcYLo = src2;
    CudaSurfaceRegion srcNVLo = GetNVRegion(src2);

    CudaSurfaceRegion srcYHi = CorrectHiRegion(src1, srcYLo);
    CudaSurfaceRegion srcNVHi = CorrectHiRegion(GetNVRegion(src1), srcNVLo);

    CudaTexture textureYHi(m_device, srcYHi, false);
    CudaTexture textureNVHi(m_device, srcNVHi, true);
    CudaTexture textureYLo(m_device, srcYLo, false);
    CudaTexture textureNVLo(m_device, srcNVLo, true);

    CudaSurfaceRegion dstNV = GetNVRegion(dst);

    float mipFactor = ((float)(dst.Size.Width - srcYHi.Size.Width)) / (srcYLo.Size.Width - srcYHi.Size.Width);
    launchKernel(m_mipFilter, dst.Size, {&textureYHi.Tex, &textureYLo.Tex, &dst.Ptr, &dst.Size, &mipFactor }, stream);

    float xFactor = (float)RoundUpTo2(dst.Size.Width) / dst.Size.Width;
    float yFactor = (float)RoundUpTo2(dst.Size.Height) / dst.Size.Height;
    launchKernel(m_mipFilterColor, dstNV.Size, { &textureNVHi.Tex, &textureNVLo.Tex, &dstNV.Ptr, &dstNV.Size, &mipFactor, &xFactor, &yFactor }, stream);
}
