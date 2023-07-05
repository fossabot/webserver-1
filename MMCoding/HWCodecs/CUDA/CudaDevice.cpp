#include "CudaDevice.h"
#include "CudaProcessor.h"
#include "CudaSampleHolder.h"
#include "NVDecoderGL.h"
#include "HWAccelerated.h"
#include "HWCodecs/DecoderPerformance.h"
#include "HWCodecs/HWUtils.h"
#include "../MediaType.h"
#include <CorbaHelpers/Envar.h>

#include "dynlink_nvcuvid.h"

#include <boost/process/environment.hpp>


const Codec2PerformanceMap_t Codec2PerformanceMapPascal =
{
    {cudaVideoCodec::cudaVideoCodec_HEVC, 0.84},
    {cudaVideoCodec::cudaVideoCodec_VP9, 0.80},
};

const Codec2PerformanceMap_t Codec2PerformanceMapTuring =
{
    {cudaVideoCodec::cudaVideoCodec_HEVC, 0.59},
    {cudaVideoCodec::cudaVideoCodec_VP9, 0.80},
};

const std::map<uint32_t, uint32_t> CudaCodecMap = 
{
    {NMMSS::NMediaType::Video::fccH264::ID, cudaVideoCodec_H264},
    {NMMSS::NMediaType::Video::fccH265::ID, cudaVideoCodec_HEVC},
    {NMMSS::NMediaType::Video::fccMPEG2::ID, cudaVideoCodec_MPEG2},
    {NMMSS::NMediaType::Video::fccMPEG4::ID, cudaVideoCodec_MPEG4}
};

uint32_t CudaDevice::CudaCodecToCodecId(uint32_t cudaCodec)
{
    for (const auto& pair : CudaCodecMap)
    {
        if (pair.second == cudaCodec)
        {
            return pair.first;
        }
    }
    return 0;
}

CUDAScopedContext::CUDAScopedContext(CUcontext context, const CudaDevice& device)
{
    if (context)
    {
        device.CheckStatus(cuCtxPushCurrent(context), "CUDAScopedContext ctor: cuCtxPushCurrent");
    }
}

CUDAScopedContext::~CUDAScopedContext()
{
    cuCtxPopCurrent(nullptr);
}

#ifdef _WIN32
    #include <windows.h>
    #include <winternl.h>
    bool IsPrimaryAdapter(const std::string& device, LUID adapterLUID);

    bool CudaDevice::checkIsPrimary() const
    {
        unsigned int mask = 0;
        LUID deviceLuid{};
        CheckStatus(cuDeviceGetLuid(reinterpret_cast<char*>(&deviceLuid), &mask, 0), "cuDeviceGetLuid");
        return IsPrimaryAdapter(m_name, deviceLuid);
    }

    const CUdevice_attribute SHARED_HANDLE_TYPE_SUPPORTED = CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_WIN32_HANDLE_SUPPORTED;
    const CUmemAllocationHandleType SHARED_HANDLE_TYPE = CU_MEM_HANDLE_TYPE_WIN32;

    OBJECT_ATTRIBUTES* GetSecurityAttributes()
    {
        static SECURITY_DESCRIPTOR descr = {};
        InitializeSecurityDescriptor(&descr, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&descr, TRUE, nullptr, FALSE);

        static OBJECT_ATTRIBUTES objAttributes = {};
        InitializeObjectAttributes(&objAttributes, nullptr, 0, nullptr, &descr);
        return &objAttributes;
    }

    void* GetSharedHandleMetadata()
    {
        static OBJECT_ATTRIBUTES* attributes = GetSecurityAttributes();
        return attributes;
    }

    class CudaMemoryHandleImpl : public ICudaMemoryHandle
    {
    public:
        CudaMemoryHandleImpl(void* handle, bool isServer, int32_t sourceProcessId, int32_t targetProcessId):
            m_handle(handle),
            m_isServer(isServer),
            m_sourceProcessId(sourceProcessId),
            m_targetProcessId(targetProcessId)
        {
        }

        ~CudaMemoryHandleImpl()
        {
            if(m_targetProcessId)
            {
                if (HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, m_targetProcessId))
                {
                    DuplicateHandle(hProcess, m_handle, 0, 0, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
                    CloseHandle(hProcess);
                }
            }
            else if (m_isServer)
            {
                CloseHandle(m_handle);
            }
        }

        CudaMemoryHandleSP CreateHandle(int32_t targetProcessId) override
        {
            HANDLE duplicatedHandle = nullptr;
            if (HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, targetProcessId))
            {
                DuplicateHandle(GetCurrentProcess(), m_handle, hProcess, &duplicatedHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
                CloseHandle(hProcess);
            }
            return duplicatedHandle ? std::make_shared<CudaMemoryHandleImpl>(duplicatedHandle, true, 0, targetProcessId) : nullptr;
        }

        void* Handle() const override { return m_handle; }
        int32_t SourceProcessId() const override { return m_sourceProcessId; }

    private:
        void* m_handle;
        bool m_isServer;
        int32_t m_sourceProcessId;
        int32_t m_targetProcessId;
    };
#else
    #include <CorbaHelpers/fd_passing_server.hpp>
    #include <CorbaHelpers/WithReactor.h>

    bool CudaDevice::checkIsPrimary() const
    {
        return true;
    }

    const CUdevice_attribute SHARED_HANDLE_TYPE_SUPPORTED = CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_SUPPORTED;
    const CUmemAllocationHandleType SHARED_HANDLE_TYPE = CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR;

    void* GetSharedHandleMetadata()
    {
        return nullptr;
    }

    class SharedHandleServer : private NCorbaHelpers::WithReactor, 
        public NCorbaHelpers::FDPassingServer<SharedHandleServer>, 
        public std::enable_shared_from_this<SharedHandleServer>
    {
        int m_handle;

        server_data_snapshot_ptr_type prepareDataSnapshot(int fd_to_pass)
        {
            auto snapshot = std::make_shared<server_data_snapshot_type>();
            std::ostream os( snapshot->stream_buffer() );
            os << MMSS_PARSEFOURCC(handshake_phrase);
            snapshot->pass_fd( fd_to_pass );
            return snapshot;
        }

        static void onUnusedChannelFound(const std::string& channelName) {}

    public:
        static const std::uint32_t handshake_phrase = MMSS_MAKEFOURCC('S', 'H', 'N', 'V');
        static std::string GetChannelName(int32_t processId, int32_t handle)
        {
            return "SharedHandle_" + std::to_string(processId) + "_" + std::to_string(handle);
        }

        SharedHandleServer(int32_t sourceProcessId, int32_t handle):
            NCorbaHelpers::FDPassingServer<SharedHandleServer>(GetIO(), GetChannelName(sourceProcessId, handle), onUnusedChannelFound),
            m_handle(handle)
        {
        }

        void Run()
        {
            publishServerData(prepareDataSnapshot(m_handle));
        }

        auto onAcceptorError(boost::system::error_code const& ec)
        {
            DECLARE_LOGGER_HOLDER = NCorbaHelpers::ShareRefcounted( NLogging::GetDefaultLogger() );
            _err_ << "Unexpected error observed by SharedHandleServer acceptor " << ec.message();
            return this->defaultOnAcceptorError(ec);
        }
    };

    class SharedHandleClient : public NCorbaHelpers::FDReceivingClient<SharedHandleClient>
    {
    public:
        static const std::uint32_t handshake_phrase = SharedHandleServer::handshake_phrase;

        SharedHandleClient(boost::asio::io_service& service, int32_t sourceProcessId, int32_t externalHandle):
            NCorbaHelpers::FDReceivingClient<SharedHandleClient>(service, SharedHandleServer::GetChannelName(sourceProcessId, externalHandle))
        {
            auto data = this->initializeServerDataBuffer();
            data->pass_fd(m_handle);
            this->readServerData(*data);
        }

        int GetHandle() const { return m_handle; }
        
    private:
        int m_handle{};
    };

    class CudaMemoryHandleImpl : public ICudaMemoryHandle
    {
    public:
        CudaMemoryHandleImpl(void* handle, bool isServer, int32_t sourceProcessId, int32_t targetProcessId):
            m_handle(reinterpret_cast<intptr_t>(handle)),
            m_isServer(isServer),
            m_sourceProcessId(sourceProcessId),
            m_targetProcessId(targetProcessId)
        {
            if(!m_targetProcessId)
            {
                if(m_isServer)
                {
                    m_server = std::make_shared<SharedHandleServer>(sourceProcessId, m_handle);
                    m_server->Run();
                }
                else
                {
                    boost::asio::io_service io_service;
                    SharedHandleClient client(io_service, sourceProcessId, m_handle);
                    io_service.run();
                    m_handle = client.GetHandle();
                }
            }
        }

        ~CudaMemoryHandleImpl()
        {
            if(!m_targetProcessId)
            {
                close(m_handle);
            }
        }

        CudaMemoryHandleSP CreateHandle(int32_t targetProcessId) override
        {
            return std::make_shared<CudaMemoryHandleImpl>(Handle(), true, 0, targetProcessId);
        }

        void* Handle() const override { return reinterpret_cast<void*>(m_handle); }
        int32_t SourceProcessId() const override { return m_sourceProcessId; }

    private:
        int m_handle;
        bool m_isServer;
        int32_t m_sourceProcessId{};
        int32_t m_targetProcessId{};
        std::shared_ptr<SharedHandleServer> m_server;
        std::shared_ptr<SharedHandleClient> m_client;
    };
#endif //_WIN32

CudaDevice::CudaDevice(CUdevice device):
    m_isValid(false),
    m_device(device),
    m_primary(false),
    m_leasedMemory(0)
{
    m_errorCounter = std::make_unique<ErrorCounter>();

    char deviceName[256];
    if (CheckStatus(cuCtxCreate(&m_context, CU_CTX_SCHED_YIELD, m_device), "cuCtxCreate") &&
        CheckStatus(cuDeviceGetName(deviceName, sizeof(deviceName), m_device), "cuDeviceGetName"))
    {
        m_isValid = true;
        m_name = deviceName;

        int major = GetAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
        int minor = GetAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);

        if (cuRtcLoaded())
        {
            m_processor = std::make_unique<CudaProcessor>(*this, major, minor);
            if (!m_processor->IsValid())
            {
                m_processor.reset();
            }
        }

        if (!GetAttribute(CU_DEVICE_ATTRIBUTE_TCC_DRIVER))
        {
            m_primary = checkIsPrimary();
        }

        m_pitchAlignment = GetAttribute(CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT);

        const int TURING_CAPABILITY = 7;
        const int UNLIMITED_DECODERS_CAPABILITY = 5;

        bool cutExcessive = NCorbaHelpers::CEnvar::NgpMixedDecoding();
        const double PRIMARY_MAX_USAGE = 0.85;
        const double SECONDARY_MAX_USAGE = 0.9;
        m_decoderPerformance = std::make_shared<DecoderPerformancePool>((major < TURING_CAPABILITY) ? Codec2PerformanceMapPascal : Codec2PerformanceMapTuring,
            cutExcessive, m_primary ? PRIMARY_MAX_USAGE : SECONDARY_MAX_USAGE);

        const int UNLIMITED_DECODERS = 1000000;
        const int OLD_DEVICE_DECODER_LIMIT = 5; //GTX 720, GTX 770
        m_availableDecoders = (major < UNLIMITED_DECODERS_CAPABILITY) ? OLD_DEVICE_DECODER_LIMIT : UNLIMITED_DECODERS;
    }
}

CudaDevice::~CudaDevice()
{
    if (m_context)
    {
        {
            auto context = SetContext();
            m_processor.reset();
        }
        cuCtxDestroy(m_context);
    }
}

CUDAScopedContext CudaDevice::SetContext() const
{
    return CUDAScopedContext(m_context, *this);
}

void CudaDevice::PushContext() const
{
    CheckStatus(cuCtxPushCurrent(m_context), "cuCtxPushCurrent");
}

void CudaDevice::PopContext() const
{
    cuCtxPopCurrent(nullptr);
}

int CudaDevice::GetAttribute(CUdevice_attribute attribute) const
{
    int value = 0;
    CheckStatus(cuDeviceGetAttribute(&value, attribute, m_device), "cuDeviceGetAttribute");
    return value;
}

CUcontext CudaDevice::GetContext() const
{
    return m_context;
}

CudaSharedMemorySP CudaDevice::CreateSharedMemory(int minAllocationsCount)
{
    int virualAddressSupported = GetAttribute(CU_DEVICE_ATTRIBUTE_VIRTUAL_ADDRESS_MANAGEMENT_SUPPORTED);
    int handleSupported = GetAttribute(SHARED_HANDLE_TYPE_SUPPORTED);
    if (CheckStatus((virualAddressSupported && handleSupported) ? CUDA_SUCCESS : CUDA_ERROR_NOT_SUPPORTED, "CreateSharedMemory"))
    {
        return std::make_shared<CudaSharedMemory>(shared_from_this(), minAllocationsCount);
    }
    return nullptr;
}

CudaSharedMemorySP CudaDevice::CreateSharedMemory(int32_t sourceProcessId, void* sharedHandle, int64_t size)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto& weak = m_sharedMemoryCache[std::make_pair(sourceProcessId, sharedHandle)];
    auto memory = weak.lock();
    if (!memory)
    {
        memory = std::make_shared<CudaSharedMemory>(shared_from_this(), sourceProcessId, sharedHandle, size);
        weak = memory;
        const auto CACHE_CLEAR_LIMIT = 1000;
        if (m_sharedMemoryCache.size() > CACHE_CLEAR_LIMIT)
        {
            for (auto it = m_sharedMemoryCache.begin(); it != m_sharedMemoryCache.end();)
            {
                it = (it->second.lock()) ? std::next(it) : m_sharedMemoryCache.erase(it);
            }
        }
    }
    return memory;
}

CudaMemorySP CudaDevice::AllocateMemory(int64_t memorySize, bool hostMemory)
{
    auto result = std::make_shared<CudaMemory>(shared_from_this(), memorySize, hostMemory);
    return result->MemoryAcquired() ? result : nullptr;
}

CudaMemorySP CudaDevice::RegisterHostMemory(void* ptr, int64_t memorySize)
{
    auto result = std::make_shared<CudaMemory>(shared_from_this(), ptr, memorySize);
    return result->MemoryAcquired() ? result : nullptr;
}

int64_t CudaDevice::GetAvailableMemory() const
{
    auto context = SetContext();
    size_t availableMemory, totalMemory;
    if (!CheckStatus(cuMemGetInfo(&availableMemory, &totalMemory), "cuMemGetInfo"))
    {
        availableMemory = 0;
    }
    return (int64_t)availableMemory;
}

CudaMemoryLeaseSP CudaDevice::LeaseMemory(int64_t memorySize)
{
    const int64_t MIN_AVAILABLE_MEMORY = 400 * 1024 * 1024;

    std::lock_guard<std::mutex> lock(m_lock);
    int64_t availableMemory = GetAvailableMemory() - m_leasedMemory;
    if (availableMemory - memorySize >= MIN_AVAILABLE_MEMORY)
    {
        m_leasedMemory += memorySize;
        return std::make_shared<CudaMemoryLease>(shared_from_this(), memorySize);
    }
    return nullptr;
}

void CudaDevice::ReleaseMemory(int64_t memorySize)
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_leasedMemory -= memorySize;
}

bool CudaDevice::addDecoder()
{
    std::lock_guard<std::mutex> lock(m_lock);
    if(m_availableDecoders > 0)
    {
        --m_availableDecoders;
        return true;
    }
    return false;
}

void CudaDevice::ReleaseDecoder()
{
    std::lock_guard<std::mutex> lock(m_lock);
    ++m_availableDecoders;
}

CudaProcessor* CudaDevice::Processor() const
{
    return m_processor.get();
}

bool CudaDevice::CanProcessOutput() const
{
    return !!m_processor && m_isValid;
}

DecoderPerformanceSP CudaDevice::AcquireDecoderPerformance(DECLARE_LOGGER_ARG, const VideoPerformanceInfo& info)
{
    if (auto result = m_decoderPerformance->Acquire(info))
    {
        return result;
    }
    _log_ << CANNOT_CREATE_DECODER << "Device performance limit exceeded.";
    return nullptr;
}

bool CudaDevice::IsPrimary() const
{
    return m_primary;
}

int CudaDevice::GetPitch(int width) const
{
    return RoundUpToPow2(width, m_pitchAlignment);
}

NMMSS::EHWDeviceType CudaDevice::GetDeviceType() const
{
    return NMMSS::EHWDeviceType::NvidiaCUDA;
}

int CudaDevice::GetDeviceIndex() const
{
    return m_device;
}

CudaSampleHolderSP CudaDevice::CreateSampleHolder(bool inHostMemory)
{
    return std::make_shared<CudaSampleHolder>(shared_from_this(), inHostMemory);
}

IHWDecoderSP CudaDevice::CreateDecoder(DECLARE_LOGGER_ARG, VideoPerformanceInfo info, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    CLONE_AND_REPLACE_LOGGER(("[CUDA] device " + std::to_string(m_device)).c_str());
    auto it = CudaCodecMap.find(info.Codec);
    if (it == CudaCodecMap.end())
    {
        LogUnsupportedCodec(GET_LOGGER_PTR, info.Codec, CudaCodecMap);
        return nullptr;
    }
    if (!m_isValid)
    {
        _err_ << CANNOT_CREATE_DECODER << "Device is in invalid state and must be restarted.";
        return nullptr;
    }

    info.Codec = it->second;
    DecoderPerformanceSP performance = !info.IsEmpty() ? AcquireDecoderPerformance(GET_LOGGER_PTR, info) : nullptr;
    if (!info.IsEmpty() && !performance)
    {
        return nullptr;
    }
    if (!addDecoder())
    {
        _err_ << CANNOT_CREATE_DECODER << "Decoders limit exceeded";
        return nullptr;
    }
    auto result = std::make_shared<NVDecoderGL>(GET_LOGGER_PTR, shared_from_this(), performance, info, advisor, requirements);
    if (!result->IsValid())
    {
        return nullptr;
    }
    return result;
}

IHWReceiverSP CudaDevice::CreateReceiver(DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    return std::make_shared<NVReceiver>(GET_LOGGER_PTR, shared_from_this(), advisor, requirements);
}

CudaStreamSP CudaDevice::CreateStream()
{
    return std::make_shared<CudaStream>(shared_from_this());
}

CudaStreamSP CudaDevice::DefaultStream()
{
    return m_defaultStream;
}

void CudaDevice::CreateDefaultStream()
{
    m_defaultStream = CreateStream();
}

bool CudaDevice::AddResourceForDisplay(CUgraphicsResource resource, const MappedSample& sample)
{
    m_mappedResources.push_back(resource);
    m_mappedSamples.push_back(sample);
    return true;
}

void CudaDevice::CompleteDisplayResources()
{
    if (!m_mappedResources.empty())
    {
        auto context = SetContext();
        if (CheckStatus(cuGraphicsMapResources(m_mappedResources.size(), m_mappedResources.data(), m_defaultStream->Stream()), "cuGraphicsMapResources"))
        {
            for (int i = 0; i < (int)m_mappedResources.size(); ++i)
            {
                m_processor->Nv12ToArgb(m_mappedSamples[i].Surface, m_mappedResources[i], m_mappedSamples[i].ResultSize, m_defaultStream);
            }
            CheckStatus(cuGraphicsUnmapResources(m_mappedResources.size(), m_mappedResources.data(), m_defaultStream->Stream()), "cuGraphicsUnmapResources");
        }
        m_mappedResources.clear();
        m_mappedSamples.clear();
    }
}

bool CudaDevice::SupportsVideoDecoding() const
{
    int major = GetAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
    int computeMode = GetAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_MODE);
    if (computeMode != CU_COMPUTEMODE_PROHIBITED && major > 2 && major < 9999)
    {
        CUVIDDECODECAPS caps{};
        caps.eCodecType = cudaVideoCodec_H264;
        caps.eChromaFormat = cudaVideoChromaFormat_420;
        if (CheckStatus(cuvidGetDecoderCaps(&caps), "cuvidGetDecoderCaps") && caps.bIsSupported && caps.nMaxWidth)
        {
            return true;
        }
    }
    return false;
}

int CudaDevice::AvailableDecoders() const
{
    return m_availableDecoders;
}

const std::string& CudaDevice::Name() const
{
    return m_name;
}

bool CudaDevice::IsValid() const
{
    return m_isValid;
}

const std::vector<CUresult> IRRECOVERABLE_CUDA_ERRORS =
{
    CUDA_ERROR_ILLEGAL_ADDRESS,
    CUDA_ERROR_LAUNCH_TIMEOUT,
    CUDA_ERROR_HARDWARE_STACK_ERROR,
    CUDA_ERROR_ILLEGAL_INSTRUCTION,
    CUDA_ERROR_MISALIGNED_ADDRESS,
    CUDA_ERROR_INVALID_ADDRESS_SPACE,
    CUDA_ERROR_INVALID_PC,
    CUDA_ERROR_LAUNCH_FAILED
};

bool IsIrrecoverable(CUresult code)
{
    return std::find(IRRECOVERABLE_CUDA_ERRORS.begin(), IRRECOVERABLE_CUDA_ERRORS.end(), code) != IRRECOVERABLE_CUDA_ERRORS.end();
}

bool CudaDevice::CheckStatus(CUresult code, const char* message, NLogging::ILogger* logger) const
{
    if (code != CUDA_SUCCESS)
    {
        bool irrecoverable = IsIrrecoverable(code);
        if (m_errorCounter->ShowError(irrecoverable))
        {
            const char* DEFAULT = "Unknown";
            const char* pName = nullptr;
            const char* pStr = nullptr;

            cuGetErrorName(code, &pName);
            cuGetErrorString(code, &pStr);

            DECLARE_LOGGER_HOLDER;
            if (logger)
            {
                SHARE_LOGGER_HOLDER(logger);
            }
            else
            {
                SHARE_LOGGER_HOLDER(NLogging::GetDefaultLogger());
            }

            _err_ << message << ", CUDA error " << code << ", " << (pName ? pName : DEFAULT) << ", " << (pStr ? pStr : DEFAULT);
            if (irrecoverable)
            {
                m_isValid = false;
                _err_ << "irrecoverable CUDA error, process must be restarted to restore NVDEC hardware decompression";
            }
        }
    }
    return code == CUDA_SUCCESS;
}



CudaMemory::CudaMemory(CudaDeviceSP device, int64_t memorySize, bool hostMemory) :
    m_device(device),
    m_memorySize(memorySize),
    m_memory(0),
    m_memoryType(hostMemory ? EMemoryType::HOST : EMemoryType::DEVICE)
{
    AcquireMemory();
}

CudaMemory::CudaMemory(CudaDeviceSP device, void* ptr, int64_t memorySize) :
    m_device(device),
    m_memorySize(memorySize),
    m_memory(reinterpret_cast<CUdeviceptr>(ptr)),
    m_memoryType(EMemoryType::HOST_EXTERNAL)
{
    m_device->CheckStatus(cuMemHostRegister(ptr, memorySize, CU_MEMHOSTREGISTER_PORTABLE), "cuMemHostRegister");
}

CudaMemory::CudaMemory(CudaSharedMemorySP sharedMemory, CUdeviceptr ptr, int64_t offset, int64_t memorySize) :
    m_device(sharedMemory->Device()),
    m_sharedMemory(sharedMemory),
    m_offset(offset),
    m_memorySize(memorySize),
    m_memory(ptr),
    m_memoryType(EMemoryType::DEVICE_SHARED)
{
}

CudaMemory::~CudaMemory()
{
    ReleaseMemory();
}

void CudaMemory::AcquireMemory()
{
    if (m_memorySize && !m_memory)
    {
        auto context = m_device->SetContext();
        auto status = (m_memoryType == EMemoryType::HOST) ? cuMemAllocHost((void**)&m_memory, (size_t)m_memorySize) : cuMemAlloc(&m_memory, (size_t)m_memorySize);
        m_device->CheckStatus(status, "CudaMemory::AcquireMemory");
    }
}

void CudaMemory::ReleaseMemory()
{
    if (m_memory)
    {
        auto context = m_device->SetContext();
        m_device->CheckStatus(releaseMemoryInternal(), "CudaMemory::ReleaseMemory");
        m_memory = 0;
    }
}

CUresult CudaMemory::releaseMemoryInternal()
{
    switch (m_memoryType)
    {
    case EMemoryType::DEVICE:
        return cuMemFree(m_memory);
    case EMemoryType::HOST:
        return cuMemFreeHost(HostPtr());
    case EMemoryType::HOST_EXTERNAL:
        return cuMemHostUnregister(HostPtr());
    case EMemoryType::DEVICE_SHARED:
        return CUDA_SUCCESS;
    default:
        return CUDA_ERROR_INVALID_VALUE;
    }
}

CudaDeviceSP CudaMemory::Device() const
{
    return m_device;
}

CudaSharedMemorySP CudaMemory::SharedMemory() const
{
    return m_sharedMemory;
}

bool CudaMemory::MemoryAcquired() const
{
    return m_memory;
}


bool SetDeviceMemoryAccess(const CudaDevice& device, CUdeviceptr ptr, int64_t size)
{
    CUmemAccessDesc accessDesc = {};
    accessDesc.location = { CU_MEM_LOCATION_TYPE_DEVICE, device.GetDeviceIndex() };
    accessDesc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    return device.CheckStatus(cuMemSetAccess(ptr, size, &accessDesc, 1), "cuMemSetAccess");
}

CudaSharedMemory::CudaSharedMemory(CudaDeviceSP device, int minAllocationsCount):
    m_device(device),
    m_minAllocationsCount(minAllocationsCount)
{
}

CudaSharedMemory::CudaSharedMemory(CudaDeviceSP device, int32_t sourceProcessId, void* sharedHandle, int64_t size):
    m_device(device)
{
    m_sharedHandle = std::make_shared<CudaMemoryHandleImpl>(sharedHandle, false, sourceProcessId, 0);
    CUdeviceptr devicePtr = {};
    if (m_device->CheckStatus(cuMemImportFromShareableHandle(&m_cudaMemoryHandle, m_sharedHandle->Handle(), SHARED_HANDLE_TYPE), "cuMemImportFromShareableHandle") &&
        m_device->CheckStatus(cuMemAddressReserve(&devicePtr, size, 0, 0, 0), "cuMemAddressReserve") &&
        m_device->CheckStatus(cuMemMap(devicePtr, size, 0, m_cudaMemoryHandle, 0), "cuMemMap") &&
        SetDeviceMemoryAccess(*m_device, devicePtr, size))
    {
        m_ptr = devicePtr;
        m_usedSize = m_allocatedSize = size;
    }
}

CudaSharedMemory::~CudaSharedMemory()
{
    auto context = m_device->SetContext();
    if (m_allocatedSize)
    {
        m_device->CheckStatus(cuMemRelease(m_cudaMemoryHandle), "cuMemRelease");
        m_device->CheckStatus(cuMemUnmap(m_ptr, m_allocatedSize), "cuMemUnmap");
        m_device->CheckStatus(cuMemAddressFree(m_ptr, m_allocatedSize), "cuMemAddressFree");
    }
    m_sharedHandles.clear();
}

CudaDeviceSP CudaSharedMemory::Device()
{
    return m_device;
}

CudaMemorySP CudaSharedMemory::AllocateMemory(int64_t memorySize)
{
    memorySize = m_device->GetPitch((int)memorySize);
    if (!m_allocatedSize)
    {
        CUmemAllocationProp prop = {};
        prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
        prop.location = { CU_MEM_LOCATION_TYPE_DEVICE, m_device->GetDeviceIndex() };
        prop.requestedHandleTypes = SHARED_HANDLE_TYPE;
        prop.win32HandleMetaData = GetSharedHandleMetadata();

        size_t granularity{};
        m_device->CheckStatus(cuMemGetAllocationGranularity(&granularity, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM), "cuMemGetAllocationGranularity");

        int64_t sizeToAllocate = RoundUpToPow2((int)memorySize * m_minAllocationsCount, granularity);
        CUdeviceptr devicePtr = {};
        void* sharedHandle{};
        if (m_device->CheckStatus(cuMemCreate(&m_cudaMemoryHandle, sizeToAllocate, &prop, 0), "cuMemCreate") &&
            m_device->CheckStatus(cuMemExportToShareableHandle((void*)&sharedHandle, m_cudaMemoryHandle, SHARED_HANDLE_TYPE, 0), "cuMemExportToShareableHandle") &&
            m_device->CheckStatus(cuMemAddressReserve(&devicePtr, sizeToAllocate, 0, 0, 0), "cuMemAddressReserve") &&
            m_device->CheckStatus(cuMemMap(devicePtr, sizeToAllocate, 0, m_cudaMemoryHandle, 0), "cuMemMap") &&
            SetDeviceMemoryAccess(*m_device, devicePtr, sizeToAllocate))
        {
            m_ptr = devicePtr;
            m_allocatedSize = sizeToAllocate;
            m_sharedHandle = std::make_shared<CudaMemoryHandleImpl>(sharedHandle, true, boost::this_process::get_id(), 0);
            updateTargetProcessIds();
        }
    }

    if (m_allocatedSize - m_usedSize >= memorySize)
    {
        auto memory = std::make_shared<CudaMemory>(shared_from_this(), m_ptr, m_usedSize, memorySize);
        m_usedSize += memorySize;
        return memory;
    }
    return nullptr;
}

CudaMemorySP CudaSharedMemory::AllocateMemory(int64_t offset, int64_t memorySize)
{
    return (offset + memorySize <= m_allocatedSize) ? std::make_shared<CudaMemory>(shared_from_this(), m_ptr, offset, memorySize) : nullptr;
}

void CudaSharedMemory::UpdateTargetProcessIds(std::unordered_set<int32_t>& ids)
{
    m_targetProcessIds = ids;
    updateTargetProcessIds();
}

void CudaSharedMemory::updateTargetProcessIds()
{
    if (m_allocatedSize)
    {
        for (const auto processId : m_targetProcessIds)
        {
            auto it = m_sharedHandles.find(processId);
            if (it == m_sharedHandles.end())
            {
                if (auto handleForProcess = m_sharedHandle->CreateHandle(processId))
                {
                    m_sharedHandles.emplace(processId, handleForProcess);
                }
                else
                {
                    m_device->CheckStatus(CUDA_ERROR_UNKNOWN, "GetSharedHandleForProcess");
                }
            }
        }
    }
}

const CudaSharedMemory::HandleTable& CudaSharedMemory::GetHandleTable() const
{
    return m_sharedHandles;
}



CudaMemoryLease::CudaMemoryLease(CudaDeviceSP device, int64_t memorySize):
    m_device(device),
    m_memorySize(memorySize)
{
}

CudaMemoryLease::~CudaMemoryLease()
{
    m_device->ReleaseMemory(m_memorySize);
}



CudaStream::CudaStream(CudaDeviceSP device):
    m_device(device)
{
    auto context = m_device->SetContext();
    m_device->CheckStatus(cuStreamCreate(&m_stream, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
}

CudaStream::~CudaStream()
{
    auto context = m_device->SetContext();
    m_device->CheckStatus(cuStreamDestroy(m_stream), "cuStreamDestroy");
}

CUstream CudaStream::Stream() const
{
    return m_stream;
}

CudaDeviceSP CudaStream::Device() const
{
    return m_device;
}

void CudaStream::Synchronize()
{
    m_device->CheckStatus(cuStreamSynchronize(m_stream), "cuStreamSynchronize");
}


int ConvertSMVer2CoresDRV(int major, int minor)
{
    static const std::map<int, int> capability2Cores =
    {
        {10, 8},   // Tesla Generation (SM 1.0) G80 class
        {11, 8},   // Tesla Generation (SM 1.1) G8x class
        {12, 8},   // Tesla Generation (SM 1.2) G9x class
        {13, 8},   // Tesla Generation (SM 1.3) GT200 class
        {20, 32},  // Fermi Generation (SM 2.0) GF100 class
        {21, 48},  // Fermi Generation (SM 2.1) GF10x class
        {30, 192}, // Kepler Generation (SM 3.0) GK10x class
        {32, 192}, // Kepler Generation (SM 3.2) GK10x class
        {35, 192}, // Kepler Generation (SM 3.5) GK11x class
        {37, 192}, // Kepler Generation (SM 3.7) GK21x class
        {50, 128}, // Maxwell Generation (SM 5.0) GM10x class
        {52, 128}, // Maxwell Generation (SM 5.2) GM20x class
        {60, 64},  // Pascal
        {61, 128},
        {62, 128},
        {70, 64},  //Volta
        {72, 64},
        {75, 64}   //Turing
    };

    auto it = capability2Cores.find(major * 10 + minor);
    return (it != capability2Cores.end()) ? it->second : 64;
}

#if defined(_WIN64) || defined(__unix__)

void AddCudaDevices(std::vector<HWDeviceSP>& devices)
{
    if(!NCorbaHelpers::CEnvar::NgpDisableCUDADecoding())
    {
        DECLARE_LOGGER_HOLDER;
        SHARE_LOGGER_HOLDER(NLogging::GetDefaultLogger());

        int foundCudaVersion = 0;
        CUresult result = cuInit(0, __CUDA_API_VERSION, foundCudaVersion);
        if (result == CUDA_SUCCESS)
        {
            result = cuvidInit(0);
            if (result == CUDA_SUCCESS)
            {
                int deviceCount = 0;
                result = cuDeviceGetCount(&deviceCount);
                if (result == CUDA_SUCCESS && deviceCount)
                {
                    for (int deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex)
                    {
                        auto device = std::make_shared<CudaDevice>(deviceIndex);
                        bool ok = device->SupportsVideoDecoding();
                        _log_ << "CUDA device " << device->Name() << " supports video decoding: " << ok;
                        if (ok)
                        {
                            if (device->AvailableDecoders() < 100)
                            {
                                _log_ << "CUDA device " << device->Name() << " has limited video decoders count: " << device->AvailableDecoders();
                            }

                            device->CreateDefaultStream();
                            devices.emplace_back(device);
                        }
                    }
                }
            }
        }
        else
        {
            _log_ << "Failed to initialize CUDA";
            if(result == CUDA_ERROR_NOT_FOUND)
            {
                _log_ << "Required CUDA version: " << __CUDA_API_VERSION << ", found version: " << foundCudaVersion;
            }
        }
    }
}

#else

void AddCudaDevices(std::vector<HWDeviceSP>& devices) {}

#endif

//CudaMemorySP CudaDevicePool::LeaseBestDeviceMemory(size_t memorySize)
//{
//    std::lock_guard<std::mutex> lock(m_lock);
//    checkDevices();
//
//    CudaDeviceSP bestDevice;
//    double maxPerf = 0;
//    for (auto device : m_devices)
//    {
//        int major = device->GetAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR);
//        int minor = device->GetAttribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
//
//        size_t availableMemory = device->GetAvailableMemory();
//        if (availableMemory > memorySize)
//        {
//            auto context = device->SetContext();
//            int multiProcessorCount = device->GetAttribute(CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT);
//            int smPerMultiproc = ConvertSMVer2CoresDRV(major, minor);
//
//            const auto perf = static_cast<double>(multiProcessorCount * smPerMultiproc);
//            const auto memPerf = static_cast<double>(availableMemory) / (1024. * 1024.);
//            const double computePerf = perf * memPerf;
//            if (computePerf > maxPerf)
//            {
//                maxPerf = computePerf;
//                bestDevice = device;
//            }
//        }
//    }
//    if (bestDevice)
//    {
//        std::lock_guard<std::mutex> lock(bestDevice->MemoryLock());
//        return bestDevice->AllocateMemory(memorySize);
//    }
//    return nullptr;
//}
