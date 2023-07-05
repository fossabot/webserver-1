#include "QSDeviceVA.h"
#include "Logging/log2.h"

#include <drm.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <boost/algorithm/string.hpp>
#include <fstream>

namespace
{
    int GetDriverName(int fd, char* name, int name_size)
    {
        drm_version_t version = {};
        version.name_len = name_size;
        version.name = name;
        return ioctl(fd, DRM_IOWR(0, drm_version), &version);
    }

    int OpenAdapter()
    {
        const std::string ADAPTER_PATH_BASE = "/dev/dri/renderD";
        const std::string INTEL_DRIVER_NAME = "i915";

        char driverName[INTEL_DRIVER_NAME.size() + 1] = {};
        for (int i = 0; i < 16; ++i)
        {
            std::string adapterPath = ADAPTER_PATH_BASE + std::to_string(128 + i);

            int fd = open(adapterPath.c_str(), O_RDWR);
            if (fd >= 0)
            {
                if (!GetDriverName(fd, driverName, INTEL_DRIVER_NAME.size()) && INTEL_DRIVER_NAME == driverName)
                {
                    return fd;
                }
                close(fd);
            }
        }
        return -1;
    }
}

::Display* GlxDisplay{};

void InitializeQSGLRenderer()
{
    GlxDisplay = glXGetCurrentDisplay();
}

QSDeviceVA::~QSDeviceVA()
{
    if (m_initialized)
    {
        vaTerminate(m_display);
    }
    GlxDisplay = nullptr;
    if (m_adapter >= 0)
    {
        close(m_adapter);
    }
}

VADisplay QSDeviceVA::getDisplayGLX()
{
    if (GlxDisplay)
    {
        if (auto display = vaGetDisplayGLX(GlxDisplay))
        {
            return display;
        }
    }
    return nullptr;
}

VADisplay QSDeviceVA::getDisplayDRM()
{
    m_adapter = OpenAdapter();
    if (m_adapter >= 0)
    {
        if (auto display = vaGetDisplayDRM(m_adapter))
        {
            return display;
        }
    }
    return nullptr;
}

bool QSDeviceVA::init(VADisplay display, bool primary)
{
    if (display)
    {
        int major_version = 0, minor_version = 0;
        VAStatus result = vaInitialize(display, &major_version, &minor_version);
        if(result == VA_STATUS_SUCCESS)
        {
            m_display = display;
            m_primary = primary;
            return true;
        }
    }
    return false;
}

class CpuInfo
{
public:
    CpuInfo()
    {
        std::string currentLine;
        Strings tokens;        
        std::ifstream stream("/proc/cpuinfo");
        while (std::getline(stream, currentLine) && !currentLine.empty())
        {
            boost::split(tokens, currentLine, boost::is_any_of(":"));
            if(tokens.size() == 2)
            {
                boost::trim(tokens[0]);
                findString(tokens, "vendor_id", Vendor);
                findString(tokens, "model name", ModelName);
                findInt(tokens, "cpu family", Family);
                findInt(tokens, "model", Model);
            }
        }
    }

    bool NotSupported() const
    {
        //https://01.org/intel-media-for-linux
        //https://en.wikichip.org/wiki/intel/cpuid
        
        const int MAINSTREAM_FAMILY = 6;

        return Vendor.find("Intel") == std::string::npos ||
            Family != MAINSTREAM_FAMILY ||
            boost::to_upper_copy(ModelName).find("XEON") != std::string::npos ||
            IsIvyOrSandy() || 
            IsHaswell() ||
            IsOldCpu();
    }

    bool HevcSupported() const
    {
        return !IsBroadwell();
    }

    bool IsIvyOrSandy() const
    {
        return Model == 0x2A || Model == 0x2D || Model == 0x3A || Model == 0x3E;
    }

    bool IsHaswell() const
    {
        return Model == 0x3C || Model == 0x3F || Model == 0x45 || Model == 0x46;
    }

    bool IsBroadwell() const
    {
        return Model == 0x3D || Model == 0x47 || Model == 0x4F || Model == 0x56;
    }

    bool IsOldCpu() const
    {
        return Model < 0x3D;
    }

private:
    using Strings = std::vector<std::string>;
    void findString(const Strings& tokens, const char* id, std::string& result)
    {
        if(tokens[0] == id)
        {
            result = boost::trim_copy(tokens[1]);
        }
    }

    void findInt(const Strings& tokens, const char* id, int& result)
    {
        if(tokens[0] == id)
        {
            try
            {
                result = std::stoi(tokens[1]);
            }
            catch(...)
            {
            }
        }
    }

public:
    std::string Vendor;
    std::string ModelName;
    int Family{};
    int Model{};

};

void QSDeviceVA::Init(int adapterNum)
{
    QSDevice::Init(adapterNum);

    DECLARE_LOGGER_HOLDER;
    SHARE_LOGGER_HOLDER(NLogging::GetDefaultLogger());

    m_initialized = init(getDisplayGLX(), true) || init(getDisplayDRM(), false);
    if(!m_initialized)
    {
        std::string error = dlerror();
        if(!error.empty())
        {
            _err_ << "Libva initialization error: " << error;
        }
    }

    CpuInfo info;
    _log_ << "CpuInfo result. Vendor: " << info.Vendor << ", ModelName: " << info.ModelName << ", Family: " << info.Family <<
        ", Model: " << info.Model;

    if(info.NotSupported())
    {
        _err_ << "CPU doesn't support Quick Sync";
    }
    else
    {
        m_supportsHEVC = info.HevcSupported();
    }
}

bool QSDeviceVA::IsValid() const
{
    return m_initialized;
}

VADisplay QSDeviceVA::Display() const
{
    return m_display;
}

mfxIMPL GetQuickSyncImpl() 
{
    return MFX_IMPL_VIA_VAAPI;
}

QSDeviceSP CreateQSDevice()
{
    return std::make_shared<QSDeviceVA>();
}
