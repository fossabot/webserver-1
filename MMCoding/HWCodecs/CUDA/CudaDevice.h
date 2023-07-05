#pragma once

#include "../MMCoding/HWCodecs/IHWDevice.h"
#include "CudaSurface.h"
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <map>

typedef int CUdevice;
typedef struct CUctx_st *CUcontext;
enum CUdevice_attribute_enum : int;
typedef CUdevice_attribute_enum CUdevice_attribute;
typedef struct CUstream_st *CUstream;
typedef struct CUgraphicsResource_st *CUgraphicsResource;
typedef unsigned long long CUmemGenericAllocationHandle;

class CUDAScopedContext
{
public:
    CUDAScopedContext(CUcontext ctx, const CudaDevice& device);
    ~CUDAScopedContext();
    CUDAScopedContext(const CUDAScopedContext&) = delete;
    CUDAScopedContext(CUDAScopedContext&&) = default;
    CUDAScopedContext& operator=(const CUDAScopedContext&) = delete;
    CUDAScopedContext& operator=(CUDAScopedContext&&) = default;
};

class CudaSample;
class ErrorCounter;

class MappedSample
{
public:
    const CudaSurfaceRegion Surface;
    SurfaceSize ResultSize;
};

using CudaSharedMemoryWP = std::weak_ptr<CudaSharedMemory>;

class CudaDevice : public IHWDevice, public std::enable_shared_from_this<CudaDevice>
{
public:
    CudaDevice(CUdevice device);
    ~CudaDevice();

    bool IsPrimary() const override;
    int GetPitch(int width) const override;
    NMMSS::EHWDeviceType GetDeviceType() const override;
    int GetDeviceIndex() const override;
    IHWDecoderSP CreateDecoder(NLogging::ILogger* logger, VideoPerformanceInfo info, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) override;
    IHWReceiverSP CreateReceiver(NLogging::ILogger* logger, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) override;
    bool CanProcessOutput() const override;

    CUDAScopedContext SetContext() const;
    CUcontext GetContext() const;
    int GetAttribute(CUdevice_attribute attribute) const;
    CudaSharedMemorySP CreateSharedMemory(int minAllocationsCount);
    CudaSharedMemorySP CreateSharedMemory(int32_t sourceProcessId, void* sharedHandle, int64_t size);
    CudaMemorySP AllocateMemory(int64_t memorySize, bool hostMemory);
    CudaMemorySP RegisterHostMemory(void* ptr, int64_t memorySize);
    CudaMemoryLeaseSP LeaseMemory(int64_t memorySize);
    void ReleaseMemory(int64_t memorySize);
    void ReleaseDecoder();
    int64_t GetAvailableMemory() const;
    CudaProcessor* Processor() const;
    DecoderPerformanceSP AcquireDecoderPerformance(NLogging::ILogger* logger, const VideoPerformanceInfo& info);
    CudaSampleHolderSP CreateSampleHolder(bool inHostMemory);
    CudaStreamSP CreateStream();
    CudaStreamSP DefaultStream();
    void CreateDefaultStream();
    bool AddResourceForDisplay(CUgraphicsResource resource, const MappedSample& sample);
    void CompleteDisplayResources();
    bool SupportsVideoDecoding() const;
    int AvailableDecoders() const;
    const std::string& Name() const;
    bool IsValid() const;
    bool CheckStatus(CUresult code, const char* message, NLogging::ILogger* logger = nullptr) const;

    virtual void PushContext() const;
    virtual void PopContext() const;

    static uint32_t CudaCodecToCodecId(uint32_t cudaCodec);

private:
    bool addDecoder();
    bool checkIsPrimary() const;

private:
    mutable bool m_isValid;
    std::unique_ptr<ErrorCounter> m_errorCounter;
    CUdevice m_device;
    CUcontext m_context;
    std::unique_ptr<CudaProcessor> m_processor;
    DecoderPerformancePoolSP m_decoderPerformance;
    bool m_primary;
    int m_pitchAlignment;
    CudaStreamSP m_defaultStream;
    std::vector<CUgraphicsResource> m_mappedResources;
    std::vector<MappedSample> m_mappedSamples;
    std::string m_name;

    int64_t m_leasedMemory;
    int m_availableDecoders;
    std::mutex m_lock;
    using SharedHandleKey = std::pair<int32_t, void*>;
    std::map<SharedHandleKey, CudaSharedMemoryWP> m_sharedMemoryCache;
};

class CudaMemory
{
public:
    CudaMemory(CudaDeviceSP device, int64_t memorySize, bool hostMemory);
    CudaMemory(CudaDeviceSP device, void* ptr, int64_t memorySize);
    CudaMemory(CudaSharedMemorySP sharedMemory, CUdeviceptr ptr, int64_t offset, int64_t memorySize);
    ~CudaMemory();

    CudaDeviceSP Device() const;
    CudaSharedMemorySP SharedMemory() const;
    bool MemoryAcquired() const;
    void AcquireMemory();
    void ReleaseMemory();
    int64_t Size() const
    {
        return m_memorySize;
    }
    int64_t Offset() const
    {
        return m_offset;
    }
    CUdeviceptr DevicePtr() const
    {
        return m_memory + m_offset;
    }
    ::uint8_t* HostPtr() const
    {
        return reinterpret_cast<::uint8_t*>(m_memory);
    }

private:
    CUresult releaseMemoryInternal();

private:
    enum EMemoryType
    {
        DEVICE,
        DEVICE_SHARED,
        HOST,
        HOST_EXTERNAL       
    };
    CudaDeviceSP m_device;
    CudaSharedMemorySP m_sharedMemory;
    int64_t m_offset{};
    int64_t m_memorySize;
    CUdeviceptr m_memory;
    EMemoryType m_memoryType;
};

class ICudaMemoryHandle;
using CudaMemoryHandleSP = std::shared_ptr<ICudaMemoryHandle>;

class ICudaMemoryHandle
{
public:
    virtual ~ICudaMemoryHandle(){}
    virtual CudaMemoryHandleSP CreateHandle(int32_t targetProcessId) = 0;
    virtual void* Handle() const = 0;
    virtual int32_t SourceProcessId() const = 0;
};

class CudaSharedMemory : public std::enable_shared_from_this<CudaSharedMemory>
{
public:
    using HandleTable = std::unordered_map<int32_t, CudaMemoryHandleSP>;

    CudaSharedMemory(CudaDeviceSP device, int minAllocationsCount = 1);
    CudaSharedMemory(CudaDeviceSP device, int32_t sourceProcessId, void* sharedHandle, int64_t size);
    ~CudaSharedMemory();

    CudaDeviceSP Device();
    CudaMemorySP AllocateMemory(int64_t memorySize);
    CudaMemorySP AllocateMemory(int64_t offset, int64_t memorySize);
    void UpdateTargetProcessIds(std::unordered_set<int32_t>& ids);
    const HandleTable& GetHandleTable() const;
    int64_t Size() const
    {
        return m_allocatedSize;
    }
    CudaMemoryHandleSP SharedHandle() const
    {
        return m_sharedHandle;
    }

private:
    void updateTargetProcessIds();

private:
    CudaDeviceSP m_device;
    int m_minAllocationsCount{};
    int64_t m_allocatedSize{};
    int64_t m_usedSize{};
    CUdeviceptr m_ptr{};
    CudaMemoryHandleSP m_sharedHandle;
    CUmemGenericAllocationHandle m_cudaMemoryHandle{};
    std::unordered_set<int32_t> m_targetProcessIds;
    HandleTable m_sharedHandles;
};

class CudaMemoryLease
{
public:
    CudaMemoryLease(CudaDeviceSP device, int64_t memorySize);
    ~CudaMemoryLease();

private:
    CudaDeviceSP m_device;
    int64_t m_memorySize;
};

class CudaStream
{
public:
    CudaStream(CudaDeviceSP device);
    ~CudaStream();

    CUstream Stream() const;
    CudaDeviceSP Device() const;
    void Synchronize();

private:
    CudaDeviceSP m_device;
    CUstream m_stream;
};
