#include "HiddenDXDevice.h"
#include "QSDecoderD3D.h"
#include "HWAccelerated.h"
#include "D3DWrapper.h"
#include "QSSharedDecoder.h"
#include "../MediaType.h"

#include <dxgi.h>
#include <d3d11.h>

#include <vector>
#include <map>

CComPtr<IDXGIFactory> GetDXGIFactory()
{
    CComPtr<IDXGIFactory> result;
    CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&result);
    return result;
}

HiddenDxDevice::HiddenDxDevice()
{
}

HiddenDxDevice::~HiddenDxDevice()
{
}

void HiddenDxDevice::Init(int adapterNum)
{
    QSDevice::Init(adapterNum);

    m_device = createDevice(adapterNum);
    if (m_device != nullptr)
    {
        m_wrapper = std::make_shared<D3DWrapper>(m_device);
    }
}

CComPtr<IDXGIAdapter> GetAdapter(int adapterIndex)
{
    CComPtr<IDXGIFactory> factory = GetDXGIFactory();
    CComPtr<IDXGIAdapter> adapter;
    return (factory && SUCCEEDED(factory->EnumAdapters(adapterIndex, &adapter))) ? adapter : nullptr;
}

std::string ToAscii(const std::wstring& s)
{
    return std::string(s.begin(), s.end());
}

CComPtr<IDXGIAdapter> GetAdapter(const std::string& device, LUID adapterLUID)
{
    for (int adapterIndex = 0; CComPtr<IDXGIAdapter> adapter = GetAdapter(adapterIndex); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc)))
        {
            std::string name = ToAscii(desc.Description);
            if (name.find(device) != std::wstring::npos && !memcmp(&desc.AdapterLuid, &adapterLUID, sizeof(LUID)))
            {
                return adapter;
            }
        }
    }
    return nullptr;
}

CComPtr<IDXGIOutput> GetOutput(CComPtr<IDXGIAdapter> adapter, int outputIndex)
{
    CComPtr<IDXGIOutput> output;
    return (adapter && SUCCEEDED(adapter->EnumOutputs(outputIndex, &output))) ? output : nullptr;
}

DISPLAY_DEVICE GetPrimaryDisplay()
{
    DISPLAY_DEVICE dd{};
    dd.cb = sizeof(DISPLAY_DEVICE);
    for (int devNum = 0; ::EnumDisplayDevices(nullptr, devNum, &dd, 0); ++devNum)
    {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            return dd;
        }
    }
    return DISPLAY_DEVICE{};
}

bool IsPrimaryAdapter(CComPtr<IDXGIAdapter> adapter)
{
    DXGI_ADAPTER_DESC desc{};
    if (adapter && SUCCEEDED(adapter->GetDesc(&desc)))
    {
        DECLARE_LOGGER_HOLDER;
        SHARE_LOGGER_HOLDER(NLogging::GetDefaultLogger());

        std::string deviceName = ToAscii(desc.Description);
        _log_ << "IsPrimaryAdapter. Device = " << deviceName << ", LUID = " << desc.AdapterLuid.HighPart << "_" << desc.AdapterLuid.LowPart <<
            ", VendorId = " << desc.VendorId << ", DeviceId = " << desc.DeviceId <<
            ", VideoMemory = " << desc.DedicatedVideoMemory << ", SystemMemory = " << desc.DedicatedSystemMemory << ", SharedSystemMemory = " << desc.SharedSystemMemory;

        DISPLAY_DEVICE primaryDisplay = GetPrimaryDisplay();
        _log_ << "Primary adapter is " << ToAscii(primaryDisplay.DeviceString) << " on " << ToAscii(primaryDisplay.DeviceName);

        for (int outputIndex = 0; CComPtr<IDXGIOutput> output = GetOutput(adapter, outputIndex); ++outputIndex)
        {
            DXGI_OUTPUT_DESC outputDesc{};
            if (SUCCEEDED(output->GetDesc(&outputDesc)) && !wcscmp(outputDesc.DeviceName, primaryDisplay.DeviceName))
            {
                _log_ << deviceName << " is primary";
                return true;
            }
        }
        _log_ << deviceName << " is not primary";
    }
    return false;
}

bool IsPrimaryAdapter(const std::string& device, LUID adapterLUID)
{
    return IsPrimaryAdapter(GetAdapter(device, adapterLUID));
}

bool IsIvyOrSandy(int d)
{
    return d == 0x01;
}

bool IsHaswell(int d)
{
    return d == 0x04 || d == 0x0A || d == 0x0D || d == 0x0C;
}

bool IsBroadwell(int d)
{
    return d == 0x16 || d == 0x0B;
}

int GetDeviceFamily(unsigned int deviceId)
{
    return (deviceId & 0xFF00) >> 8;
}

bool HEVCSupported(unsigned int deviceId)
{
    int d = GetDeviceFamily(deviceId);
    return !(IsIvyOrSandy(d) || IsHaswell(d) || IsBroadwell(d));
}

bool IsNvDxInterop2Supported(unsigned int deviceId)
{
    int d = GetDeviceFamily(deviceId);
    return !(IsIvyOrSandy(d) || IsHaswell(d));
}

CComPtr<ID3D11Device> HiddenDxDevice::createDevice(int adapterNum)
{
    ::timeBeginPeriod(1);
    if (CComPtr<IDXGIAdapter> adapter = GetAdapter(adapterNum))
    {
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc)))
        {
            static D3D_FEATURE_LEVEL FeatureLevels[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0
            };
            D3D_FEATURE_LEVEL featureLevelsOut;

            CComPtr<ID3D11Device> device;
            CComPtr<ID3D11DeviceContext> ctx;

            HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 
                D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT /*| D3D11_CREATE_DEVICE_DEBUG*/,
                FeatureLevels, 4, D3D11_SDK_VERSION, &device, &featureLevelsOut, &ctx);
            if (SUCCEEDED(hr) && featureLevelsOut >= D3D_FEATURE_LEVEL_11_1)
            {
                CComQIPtr<ID3D10Multithread> mt(device);
                if (mt)
                {
                    mt->SetMultithreadProtected(true);
                    m_primary = IsPrimaryAdapter(adapter) && IsNvDxInterop2Supported(desc.DeviceId);
                    m_supportsHEVC = HEVCSupported(desc.DeviceId);
                    return device;
                }
            }
        }
    }
    return nullptr;
}

bool HiddenDxDevice::IsValid() const
{
    return !!m_device;
}

CComPtr<ID3D11Device> HiddenDxDevice::GetDevice()
{
    return m_device;
}

std::shared_ptr<D3DWrapper> HiddenDxDevice::GetWrapper()
{
    return m_wrapper;
}

mfxIMPL GetQuickSyncImpl()
{
    return MFX_IMPL_VIA_D3D11;
}

QSDeviceSP CreateQSDevice()
{
    return std::make_shared<HiddenDxDevice>();
}
