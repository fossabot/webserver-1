#include "CudaSample.h"
#include "CudaDevice.h"
#include "../MediaType.h"

#include "dynlink_cuda.h"
#include <boost/process/environment.hpp>

namespace
{
    using TProcessId = decltype(boost::this_process::get_id());

    class HandleTableEntry
    {
    public:
        TProcessId ProcessId;
        int32_t Reserved;
        void* SharedHandle;
    };

    class CudaSampleData
    {
    public:
        CudaDevice* Device{};
        CUdeviceptr DevicePtr{};
        int64_t MemoryOffset{};
        int64_t MemorySize{};
        int64_t TotalMemorySize{};
        TProcessId OriginalProcessId{};
        int32_t Padding{};
        void* OriginalSharedHandle{};
        int64_t Reserved0{};
        int64_t Reserved1{};
        int64_t SharedHandleCount{};
    };


    CudaSampleData& sampleData(NMMSS::ISample& sample)
    {
        return *reinterpret_cast<CudaSampleData*>(sample.GetBody());
    }

    const CudaSampleData& sampleData(const NMMSS::ISample& sample)
    {
        return sampleData(const_cast<NMMSS::ISample&>(sample));
    }
}


CudaSample::CudaSample(CudaStreamSP stream, bool inHostMemory, CudaSharedMemorySP sharedMemory):
    VideoMemorySample(NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::CUDA, stream->Device()->GetDeviceIndex()),
    m_stream(stream),
    m_inHostMemory(inHostMemory),
    m_sharedMemory(sharedMemory)
{
}

bool CudaSample::setup(const SurfaceSize& size, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    return setup(size, GetNv12UVSize(size), timestamp, holder);
}

bool CudaSample::setup(const SurfaceSize& size, const SurfaceSize& uvSize, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    int memorySize = size.MemorySize() + uvSize.MemorySize() * (m_inHostMemory ? 2 : 1);
    if (!m_surface || m_surface->Size() < memorySize)
    {
        m_surface.reset();
        int64_t tableSize = m_sharedMemory ? (m_sharedMemory->GetHandleTable().size() * sizeof(HandleTableEntry)) : 0;
        int64_t sampleSize = m_inHostMemory ? memorySize : (sizeof(CudaSampleData) + tableSize);
        if (CreateSample(holder, sampleSize, m_inHostMemory))
        {
            if (m_inHostMemory)
            {
                m_surface = Device()->RegisterHostMemory(m_sample->GetBody(), memorySize);
            }
            else
            {
                m_surface = m_sharedMemory ? m_sharedMemory->AllocateMemory(memorySize) : Device()->AllocateMemory(memorySize, m_inHostMemory);
                if(m_surface)
                {
                    auto& data = sampleData(*m_sample);
                    data = { Device().get(), m_surface->DevicePtr(), m_surface->Offset(), m_surface->Size() };
                    if (m_sharedMemory)
                    {
                        data.TotalMemorySize = m_sharedMemory->Size();
                        data.OriginalProcessId = m_sharedMemory->SharedHandle()->SourceProcessId();
                        data.OriginalSharedHandle = m_sharedMemory->SharedHandle()->Handle();
                        auto& table = m_sharedMemory->GetHandleTable();
                        data.SharedHandleCount = table.size();
                        auto* entries = reinterpret_cast<HandleTableEntry*>(m_sample->GetBody() + sizeof(CudaSampleData));
                        for (const auto& pair : table)
                        {
                            *(entries++) = { pair.first, 0, pair.second->Handle() };
                        }
                    }
                }
            }
        }
    }

    return IsValid() && VideoMemorySample::Setup(size, uvSize, timestamp);
}

void CudaSample::SetupSharedMemory(NMMSS::ISample& sharedSample, CudaMemorySP memory, NMMSS::CDeferredAllocSampleHolder& holder)
{
    m_sharedSample = NCorbaHelpers::ShareRefcounted(&sharedSample);
    m_surface = memory;

    auto size = GetSurface(sharedSample, false).Size;
    if (CreateSample(holder, sizeof(CudaSampleData), false))
    {
        sampleData(*m_sample) = { Device().get(), m_surface->DevicePtr(), m_surface->Offset(), m_surface->Size(), m_sharedMemory ? m_sharedMemory->Size() : 0 };
    }
    VideoMemorySample::Setup(size, GetNv12UVSize(size), sharedSample.Header().dtTimeBegin);
}

void CudaSample::Setup(const CudaSurfaceRegion& src, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    if (src.Ptr && setup(src.Size, timestamp, holder))
    {
        Device()->CheckStatus(
            cuMemcpyDtoDAsync(m_surface->DevicePtr(), src.Ptr, GetNv12MemorySize(src.Size), m_stream->Stream()),
            "cuMemcpyDtoDAsync");
    }
}

void CudaSample::SetupSystemMem(const uint8_t* src, const SurfaceSize& size, int codedHeight, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    auto context = Device()->SetContext();
    SurfaceSize memorySize = {size.Width, size.Height, Device()->GetPitch(size.Width) };
    if (src && setup(memorySize, timestamp, holder))
    {
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_HOST;
        copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.srcHost = src;
        copy.dstDevice = m_surface->DevicePtr();
        copy.srcPitch = size.Pitch;
        copy.dstPitch = memorySize.Pitch;
        copy.WidthInBytes = size.Width;
        copy.Height = size.Height;
        Device()->CheckStatus(cuMemcpy2DAsync(&copy, m_stream->Stream()), "CudaSample::SetupSystemMem cuMemcpy2DAsync");

        copy.srcHost = src + size.Pitch * codedHeight;
        copy.dstDevice = copy.dstDevice + memorySize.Pitch * size.Height;
        copy.Height /= 2;
        Device()->CheckStatus(cuMemcpy2DAsync(&copy, m_stream->Stream()), "CudaSample::SetupSystemMem cuMemcpy2DAsync for UV");
        m_stream->Synchronize();
    }
}

void CudaSample::Setup(int width, int height, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    setup({ width, height, Device()->GetPitch(width) }, timestamp, holder);
}

void CudaSample::SetupToHostMem(const CudaSurfaceRegion& srcY, const CudaSurfaceRegion& srcUV, ::uint64_t timestamp, NMMSS::CDeferredAllocSampleHolder& holder)
{
    if (srcY.Ptr && srcUV.Ptr && setup(srcY.Size, srcUV.Size, timestamp, holder))
    {
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.dstMemoryType = CU_MEMORYTYPE_HOST;
        copy.srcDevice = srcY.Ptr;
        copy.dstHost = m_surface->HostPtr();
        copy.srcPitch = copy.dstPitch = srcY.Size.Pitch;
        copy.WidthInBytes = srcY.Size.Width;
        copy.Height = srcY.Size.Height;
        Device()->CheckStatus(cuMemcpy2DAsync(&copy, m_stream->Stream()), "CudaSample::SetupToHostMem cuMemcpy2DAsync");

        copy.srcDevice = srcUV.Ptr;
        copy.dstHost = m_surface->HostPtr() + srcY.Size.MemorySize();
        copy.srcPitch = copy.dstPitch = srcUV.Size.Pitch;
        copy.WidthInBytes = srcUV.Size.Width;
        copy.Height = srcUV.Size.Height * 2;
        Device()->CheckStatus(cuMemcpy2DAsync(&copy, m_stream->Stream()), "CudaSample::SetupToHostMem cuMemcpy2DAsync for UV");
    }
}

CudaStreamSP CudaSample::Stream() const
{
    return m_stream;
}

CudaDeviceSP CudaSample::Device() const
{
    return m_stream->Device();
}

bool CudaSample::IsValid() const
{
    return !!m_surface;
}

CUresult CudaSample::Status() const
{
    return IsValid() ? CUDA_SUCCESS : CUDA_ERROR_UNKNOWN;
}

CudaDeviceSP CudaSample::GetDevice(const NMMSS::ISample& sample)
{
    return IsValid(sample) ? sampleData(sample).Device->shared_from_this() : nullptr;
}

CudaSurfaceRegion CudaSample::GetSurface(const NMMSS::ISample& sample, bool color)
{
    return GetSurface(sample, sampleData(sample).DevicePtr, color);
}

bool CudaSample::IsValid(const NMMSS::ISample& sample)
{
    static const auto currentProcessId = boost::this_process::get_id();
    auto sourceProcessId = sampleData(sample).OriginalProcessId;
    return !sourceProcessId || (currentProcessId == sourceProcessId);
}

CudaSurfaceRegion CudaSample::GetSurface(const NMMSS::ISample& sample, CUdeviceptr ptr, bool color)
{
    const auto& header = sample.SubHeader<NMMSS::NMediaType::Video::VideoMemory::SubtypeHeader>();
    CudaSurfaceRegion yRegion = { {(int)header.nWidth, (int)header.nHeight, (int)header.nPitch}, ptr };
    return color ? GetNVRegion(yRegion) : yRegion;
}

CudaMemorySP CudaSample::GetSharedMemory(CudaDeviceSP device, const NMMSS::ISample& sample, CudaSharedMemorySP& sharedMemory, void*& originalHandle)
{
    const auto& data = sampleData(sample);
    if (!sharedMemory || data.OriginalSharedHandle != originalHandle || data.OriginalProcessId != sharedMemory->SharedHandle()->SourceProcessId())
    {
        sharedMemory.reset();
        originalHandle = data.OriginalSharedHandle;

        auto processId = boost::this_process::get_id();
        const auto* entries = reinterpret_cast<const HandleTableEntry*>(sample.GetBody() + sizeof(CudaSampleData));
        for (int i = 0; i < data.SharedHandleCount; ++i)
        {
            if (entries[i].ProcessId == processId)
            {
                sharedMemory = device->CreateSharedMemory(data.OriginalProcessId, entries[i].SharedHandle, data.TotalMemorySize);
                break;
            }
        }
    }
    return sharedMemory ? sharedMemory->AllocateMemory(data.MemoryOffset, data.MemorySize) : nullptr;
}

void CudaSample::ReleaseSharedSample()
{
    m_sharedSample.Reset();
}
