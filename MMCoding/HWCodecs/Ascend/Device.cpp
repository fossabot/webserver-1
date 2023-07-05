#include "Device.h"
#include "Decoder.h"

#include <HuaweiAscend/LibraryConvenience.h>

#include "HWCodecs/DecoderPerformance.h"
#include "HWCodecs/HWDevicePool.h"
#include "../MediaType.h"
#include <ItvFramework/AddonPathHelper.h>

#include <string>

#include <Logging/log2.h>
#include <CorbaHelpers/Envar.h>

namespace {

std::once_flag g_createAscendDecoderOnce;

const std::map<uint32_t, std::string> AscendCodecMap =
{
    {uint32_t(NMMSS::NMediaType::Video::fccH264::ID), std::string("H264")},
    {uint32_t(NMMSS::NMediaType::Video::fccH265::ID), std::string("H265")},
};

std::once_flag g_loadHuaweiAscendOnce;
boost::dll::shared_library g_libHuaweiAscend;
NAscend::APIv2 g_ascendAPI;

void LoadHuaweiAscend(DECLARE_LOGGER_ARG) noexcept
{
    try
    {
        TRY_LOAD_HUAWEI_ASCEND_ONLY_FROM(g_libHuaweiAscend, g_ascendAPI, CAddonsPath().GetPaths(), CreateAscendDecoder);
    }
    catch (const std::exception & ex)
    {
        _err_ << "Failed to load HuaweiAscend library: " << ex.what();
    }
    catch (...)
    {
        _err_ << "Failed to load HuaweiAscend library: unexpected exception";
    }
}

} // anonymous namespace

AscendDevice::AscendDevice()
{
    m_ascendDecoder = g_ascendAPI.fCreateAscendDecoder();
}

AscendDevice::~AscendDevice()
{
}

std::shared_ptr<AscendDecoder::StreamContext> AscendDevice::CreateStreamContext(const VideoPerformanceInfo& info)
{
    return m_ascendDecoder->CreateStreamContext(AscendDecoder::StreamContextParams{ AscendCodecMap.find(info.Codec)->second.c_str(), unsigned(info.Width), unsigned(info.Height) });
}

bool AscendDevice::IsPrimary() const
{
    return false;
}


int AscendDevice::GetPitchH(int height) const
{
    auto stride_h = m_ascendDecoder->PitchAlignmentH();
    return (height + stride_h - 1) / stride_h  * stride_h;
}

int AscendDevice::GetPitch(int width) const
{
    auto stride_w = m_ascendDecoder->PitchAlignmentW();
    return (width + stride_w - 1) / stride_w * stride_w;
}

NMMSS::EHWDeviceType AscendDevice::GetDeviceType() const
{
    return NMMSS::EHWDeviceType::HuaweiAscend;
}

IHWDecoderSP AscendDevice::CreateDecoder(DECLARE_LOGGER_ARG, VideoPerformanceInfo info, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
{
    auto it = AscendCodecMap.find(info.Codec);
    if (it != AscendCodecMap.end())
    {
        try
        {
            return std::make_shared<HADecoder>(GET_LOGGER_PTR, shared_from_this(), info, advisor, requirements);
        }
        catch(std::exception const& e)
        {
            _err_ << "Failed to create HuaweiAscend decoder: " << e.what();
        }
    }
    else
    {
        _wrn_ << __FUNCTION__ << ": unsupported codec=0x" << std::hex << info.Codec << "(" << MMSS_PARSEFOURCC(info.Codec) << ")";
    }
    return nullptr;
}

bool AscendDevice::CanProcessOutput() const
{
    return true; // TODO:
}

int AscendDevice::GetDeviceIndex() const
{
    return 0;
}

void AddAscendDevices(std::vector<HWDeviceSP>& devices)
{
    DECLARE_LOGGER_HOLDER = NCorbaHelpers::ShareRefcounted(NLogging::GetDefaultLogger());
    std::call_once(g_loadHuaweiAscendOnce, LoadHuaweiAscend, GET_LOGGER_PTR);
    if (g_libHuaweiAscend.is_loaded())
    {
        try
        {
            auto device = std::make_shared<AscendDevice>();
            devices.emplace_back(device);
        }
        catch(std::exception const& e)
        {
            _err_ << "Failed to initialize HuaweiAscend library: " << e.what();
        }
    }
}
