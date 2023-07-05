#include "QSDevice.h"
#include "../MediaType.h"
#include "HWAccelerated.h"
#include "HWCodecs/HWUtils.h"
#include "QSDecoderD3D.h"
#include "QSSharedDecoder.h"

#include <vector>
#include <map>

QSDevice::QSDevice():
    m_primary(false),
    m_supportsHEVC(false),
    m_availableMemory(0)
{
}

QSDevice::~QSDevice() {}

bool QSDevice::IsPrimary() const
{
    return m_primary;
}

int QSDevice::GetPitch(int width) const
{
    return width;
}

NMMSS::EHWDeviceType QSDevice::GetDeviceType() const
{
    return NMMSS::EHWDeviceType::IntelQuickSync;
}

int QSDevice::GetDeviceIndex() const
{
    return 0;
}

void QSDevice::Init(int adapterNum)
{
    m_supportsHEVC = true;

    // Empirical value, needs additional investigation
    const ::int64_t AVAILABLE_MEMORY = 1920 * 1080 * 68;
    m_availableMemory = AVAILABLE_MEMORY;

    m_sharedDecoderHolder = CreateQSSharedDecoderHolder(*this);
}

bool QSDevice::SupportsHEVC() const
{
    return m_supportsHEVC;
}

QSDecoderLease::QSDecoderLease(QSDeviceSP device, ::int64_t size):
    m_device(device),
    m_size(size)
{
}

QSDecoderLease::~QSDecoderLease()
{
    m_device->relaseMemory(m_size);
}

QSDeviceSP QSDecoderLease::Device() const
{
    return m_device;
}


void QSDevice::relaseMemory(::int64_t size)
{
    std::lock_guard<std::mutex> lock(m_memoryLock);
    m_availableMemory += size;
}


const std::map<uint32_t, uint32_t> QSCodecMap =
{
    {NMMSS::NMediaType::Video::fccH264::ID, MFX_CODEC_AVC},
    {NMMSS::NMediaType::Video::fccH265::ID, MFX_CODEC_HEVC}
};

IHWDecoderSP QSDevice::CreateDecoder(DECLARE_LOGGER_ARG, VideoPerformanceInfo info,
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    return (requirements.KeySamplesOnly && m_sharedDecoderHolder) ?
        m_sharedDecoderHolder->CreateDecoder(GET_LOGGER_PTR, info, advisor, requirements) :
        CreateDecoderInternal(GET_LOGGER_PTR, info, advisor, requirements);
}

IHWDecoderSP QSDevice::CreateDecoderInternal(DECLARE_LOGGER_ARG, VideoPerformanceInfo info, 
    NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    CLONE_AND_REPLACE_LOGGER("[QuickSync] device");
    auto it = QSCodecMap.find(info.Codec);
    if (it == QSCodecMap.end())
    {
        LogUnsupportedCodec(GET_LOGGER_PTR, info.Codec, QSCodecMap);
        return nullptr;
    }

    QSDecoderLeaseSP lease;
    {
        std::lock_guard<std::mutex> lock(m_memoryLock);
        ::int64_t size = info.Width * info.Height;
        if (requirements.KeySamplesOnly && m_sharedDecoderHolder)
        {
            size /= DECODER_SHARING_FACTOR;
        }
        else if (it->second == MFX_CODEC_HEVC)
        {
            size *= 2;
        }

        if (size > 0 && m_availableMemory >= size)
        {
            m_availableMemory -= size;
            lease = std::make_shared<QSDecoderLease>(shared_from_this(), size);
        }
        else
        {
            _log_ << CANNOT_CREATE_DECODER << "low available GPU memory: " << m_availableMemory;
            return nullptr;
        }
    }
    info.Codec = it->second;
    return std::make_shared<QSDecoderD3D>(GET_LOGGER_PTR, lease, info, advisor, requirements);
}

bool QSDevice::CanProcessOutput() const
{
    return true;
}

std::string MfxStatusToString(mfxStatus status)
{
    static const char* UNKNOWN_ERROR = "Unknown error value ";
    static const std::map<mfxStatus, std::string> map = 
    {
        {MFX_ERR_NONE,                      "No error"},
        {MFX_ERR_UNKNOWN,                   "Unknown error"},
        {MFX_ERR_NULL_PTR,                  "Null pointer"},
        {MFX_ERR_UNSUPPORTED,               "Undeveloped feature"},
        {MFX_ERR_MEMORY_ALLOC,              "Failed to allocate memory"},
        {MFX_ERR_NOT_ENOUGH_BUFFER,         "Insufficient buffer at input/output"},
        {MFX_ERR_INVALID_HANDLE,            "Invalid handle"},
        {MFX_ERR_LOCK_MEMORY,               "Failed to lock memory block"},
        {MFX_ERR_NOT_INITIALIZED,           "Member function called before initialization"},
        {MFX_ERR_NOT_FOUND,                 "The specified object is not found"},
        {MFX_ERR_MORE_DATA,                 "Expect more data at input"},
        {MFX_ERR_MORE_SURFACE,              "Expect more surface at output"},
        {MFX_ERR_ABORTED,                   "Operation aborted"},
        {MFX_ERR_DEVICE_LOST,               "HW acceleration device lost"},
        {MFX_ERR_INCOMPATIBLE_VIDEO_PARAM,  "Incompatible video parameters"},
        {MFX_ERR_INVALID_VIDEO_PARAM,       "Invalid video parameters"},
        {MFX_ERR_UNDEFINED_BEHAVIOR,        "Undefined behaviour"},
        {MFX_ERR_DEVICE_FAILED,             "Device operation failure"},
        {MFX_ERR_MORE_BITSTREAM,            "Expect more bitstream buffers at output"},
        {MFX_ERR_INCOMPATIBLE_AUDIO_PARAM,  "Incompatible audio parameters"},
        {MFX_ERR_INVALID_AUDIO_PARAM,       "Invalid audio parameters"},
        {MFX_ERR_GPU_HANG,                  "Device operation failure caused by GPU hang"},
        {MFX_ERR_REALLOC_SURFACE,           "Bigger output surface required"}
    };
    auto it = map.find(status);
    if(it != map.end())
    {
        return it->second;
    }
    return UNKNOWN_ERROR + std::to_string(status);
}

int GetIntelDeviceAdapterNum();

QSDeviceSP CreateQSDevice();

void AddQuickSyncDevices(std::vector<HWDeviceSP>& devices)
{
    int adapter = GetIntelDeviceAdapterNum();
    if (adapter >= 0)
    {
        auto device = CreateQSDevice();
        device->Init(adapter);
        if (device->IsValid())
        {
            devices.emplace_back(device);
        }
    }
}

