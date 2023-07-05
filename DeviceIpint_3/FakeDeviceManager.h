#ifndef FAKEDEVICEMANAGER_IDEVICEMANAGER_H
#define FAKEDEVICEMANAGER_IDEVICEMANAGER_H

#include <DeviceManager/IDeviceManager.h>
#include <Logging/log2.h>
#include <CorbaHelpers/Container.h>
#include <boost/thread/thread.hpp>

#include <string>
#include <vector>
#include <fstream>
#include <exception>

// 2Женя:
// Скопировал из Itv\DeviceManager\src\DeviceData.h, т.к. файлы из директории  src в ItvSdk не входят
// и на build сервере их нет!
namespace Utility
{
    inline std::string safeCopyString(const char* buf)
    {
        return buf ? std::string(buf) : std::string();
    }
    inline bool safeCompareString(const std::string& lhs, const char* rhs)
    {
        if (rhs)
            return lhs == rhs;
        else
            return lhs.empty();
    }
}
namespace ITV8
{
namespace MMD
{
class IpDeviceSearchResult : public ITV8::GDRV::IIpDeviceSearchResult
{
public:
    IpDeviceSearchResult() : m_port(0) {}

    explicit IpDeviceSearchResult(const ITV8::GDRV::IIpDeviceSearchResult& rhs)
        : m_brand(Utility::safeCopyString(rhs.GetBrand())),
        m_model(Utility::safeCopyString(rhs.GetModel())),
        m_firmware(Utility::safeCopyString(rhs.GetFirmware())),
        m_lanAddress(Utility::safeCopyString(rhs.GetLANAddress())),
        m_wanAddress(Utility::safeCopyString(rhs.GetWANAddress())),
        m_mac(Utility::safeCopyString(rhs.GetMac())),
        m_driverName(Utility::safeCopyString(rhs.GetDriverName())),
        m_driverVersion(rhs.GetDriverVersion()),
        m_port(rhs.GetPort())
    {}

    bool operator==(const ITV8::GDRV::IIpDeviceSearchResult& rhs) const
    {
        return Utility::safeCompareString(m_brand, rhs.GetBrand()) &&
            Utility::safeCompareString(m_model, rhs.GetModel()) &&
            Utility::safeCompareString(m_firmware, rhs.GetFirmware()) &&
            Utility::safeCompareString(m_lanAddress, rhs.GetLANAddress()) &&
            Utility::safeCompareString(m_wanAddress, rhs.GetWANAddress()) &&
            Utility::safeCompareString(m_mac, rhs.GetMac()) &&
            Utility::safeCompareString(m_driverName, rhs.GetDriverName()) &&
            m_driverVersion == rhs.GetDriverVersion() &&
            m_port == rhs.GetPort();
    }

    // ITV8::IContract interface implementation.
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IDeviceSearchResult)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IIpDeviceSearchResult)
        ITV8_CONTRACT_ENTRY(ITV8::MMD::IpDeviceSearchResult)
        ITV8_END_CONTRACT_MAP()

        // ITV8::GDRV::IDeviceSearchResult interface implementation.
        virtual const char* GetBrand() const { return m_brand.c_str(); }
    virtual const char* GetModel() const { return m_model.c_str(); }
    virtual const char* GetFirmware() const { return m_firmware.c_str(); }
    virtual const char* GetDriverName() const { return m_driverName.c_str(); }
    virtual uint32_t GetDriverVersion() const { return m_driverVersion; }

    // ITV8::GDRV::IIpDeviceSearchResult interface implementation.
    virtual const char* GetLANAddress() const { return m_lanAddress.c_str(); }
    virtual const char* GetWANAddress() const { return m_wanAddress.c_str(); }
    virtual const char* GetMac() const { return m_mac.c_str(); }
    virtual uint32_t GetPort() const { return m_port; }

public:
    std::string	m_brand;
    std::string	m_model;
    std::string	m_firmware;
    std::string	m_lanAddress;
    std::string	m_wanAddress;
    std::string	m_mac;
    std::string m_driverName;
    uint32_t	m_driverVersion;
    uint32_t	m_port;
};

}//namespace MMD
}//namespace ITV8
// Фальшивый DeviceManager
// Не работает ни с какими драйверами и способен только осуществлять Autodetect
// Выбирает из файла нужные девайсы
class FakeDeviceManager: public ITV8::MMD::IDeviceManager
{
private:
	DECLARE_LOGGER_HOLDER;

	std::vector<ITV8::MMD::IpDeviceSearchResult> m_searchResults;

	boost::thread m_thread;

public:
	// FakeDeviceManager implementation
	ITV8_BEGIN_CONTRACT_MAP()
		ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::MMD::IDeviceManager)
		ITV8_CONTRACT_ENTRY(ITV8::MMD::IDeviceManager)
	ITV8_END_CONTRACT_MAP()

	FakeDeviceManager(DECLARE_LOGGER_ARG);
	~FakeDeviceManager();

	void RunAutodetect(ITV8::MMD::IAutodetectHandler* handler, const std::string& searchLanAddress, 
		const std::string& searchBrandName);

	// ITV8::MMD::IDeviceManager implementation
	void Autodetect(ITV8::MMD::IAutodetectHandler* handler, const char* connectInfo);
	void Autodetect(ITV8::MMD::IAutodetectHandler* handler,
		const char* connectInfo, const char* brandName);

	void StopAutodetect(const char* connectInfo);

	void StartSearch(ITV8::MMD::IDeviceSearchHandler* handler, ITV8::bool_t continuous);

	void StopSearch();

	void GetDeviceInfo(ITV8::MMD::IDeviceInfoWriter* writer, ITV8::bool_t newestDriver);

	ITV8::GDRV::IDevice* CreateDevice(ITV8::GDRV::IDeviceHandler* handler,
		const char* brand, const char* model,
		const char* firmware, const char* driverName, ITV8::uint32_t driverVersion);

private:
	// FakeDeviceManager implementation
	void Done(ITV8::MMD::IAutodetectHandler* handler);

	void LoadFromFile(std::string fileName);
	ITV8::MMD::IpDeviceSearchResult String2IpSearchResult(std::string);

	std::string PopValue(std::string& line);
};

#endif
