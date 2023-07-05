#include "FakeDeviceManager.h"

#include "../../mmss/DeviceInfo/include/PropertyContainers.h"
#include <boost/bind.hpp>

// FakeDeviceManager implementation
FakeDeviceManager::FakeDeviceManager(DECLARE_LOGGER_ARG)
{
	INIT_LOGGER_HOLDER;

	_log_ << "FakeDeviceManager: Created." << std::endl;

	LoadFromFile("fakedevices.txt");
}

FakeDeviceManager::~FakeDeviceManager()
{
	m_thread.join();

	_log_ << "FakeDeviceManager: Deleted." << std::endl;

}

void FakeDeviceManager::RunAutodetect(ITV8::MMD::IAutodetectHandler* handler, const std::string& connectionInfo, 
								   const std::string& searchBrandName)
{
	//формат connectionInfo такой как прин€то в ip-int: "ip:port:login:password"
	std::string searchLanAddress = connectionInfo.substr(0, connectionInfo.find_first_of(':'));
	std::vector<ITV8::MMD::IpDeviceSearchResult>::const_iterator it = m_searchResults.begin();

	for (; it!= m_searchResults.end(); it++)
	{
		if ( (searchLanAddress == (*it).m_lanAddress) && 
				((searchBrandName == (*it).m_brand) || searchBrandName.empty()))
		{
			handler->Autodetected(0, *it);
		}
	}

	new boost::thread(boost::bind(&FakeDeviceManager::Done, this, handler));
}

void FakeDeviceManager::LoadFromFile(std::string fileName)
{
	std::ifstream fakeDeviceFile(fileName.c_str(),	std::ios::in);

	if(!fakeDeviceFile)
	{
		_log_<<"File with fake IpDevices was not found"<<std::endl;
		return;
	}

	std::string readLine;


	std::vector<std::string> lineList;

	while(getline(fakeDeviceFile, readLine))
	{
		if(!readLine.empty() && !(readLine[0]=='#'))
		{
			lineList.push_back(readLine);
		}
	}

	fakeDeviceFile.close();

	std::vector<std::string>::const_iterator it = lineList.begin();

	for (; it!= lineList.end(); it++)
	{
		m_searchResults.push_back(String2IpSearchResult(*it));
	}
}

std::string FakeDeviceManager::PopValue(std::string& line)
{
	size_t found = line.find_first_of("|");

	std::string val;

	if (found)
	{
		val = line.substr(0, found);
		line.erase(0,found+1);
	}
	else
	{
		val = line;
		line.clear();
	}

	return val;
}

// Ip-address | Vendor | Model | Firmware | DriverName | DriverVersion| Mac | WLAN | Port

ITV8::MMD::IpDeviceSearchResult FakeDeviceManager::String2IpSearchResult(std::string line)
{
	ITV8::MMD::IpDeviceSearchResult searchResult;

	// ѕќ–яƒќ  Ќ≈ ћ≈Ќя“№! —Ћќћј≈“—я!
	searchResult.m_lanAddress = PopValue(line);
	searchResult.m_brand = PopValue(line);
	searchResult.m_model = PopValue(line);
	searchResult.m_firmware = PopValue(line);
	searchResult.m_driverName = PopValue(line);

	std::string driverVersion = PopValue(line);

	ITV8::GDRV::Version vers;
	
	if (vers.fromString(driverVersion))
	{
		searchResult.m_driverVersion = ITV8::uint32_t(vers);
	}
	else
	{
		_log_<<"Wrong driver version in "<<line<<std::endl;
	}

	searchResult.m_mac = PopValue(line);
	searchResult.m_wanAddress = PopValue(line);

	// ∆естко, но надежно!
	searchResult.m_port = 80;

	return searchResult;
}

void FakeDeviceManager::Done(ITV8::MMD::IAutodetectHandler* handler)
{
	m_thread.join();

	// «десь бы тоже ему IContract какой-нить ему дать, но какой и зачем, если он нам там не нужен?
	handler->Done(0);
}

void FakeDeviceManager::Autodetect(ITV8::MMD::IAutodetectHandler* handler, const char* connectInfo)
{
	const char EMPTY_BRAND[] = "";
	Autodetect(handler, connectInfo, EMPTY_BRAND);
}

void FakeDeviceManager::Autodetect(ITV8::MMD::IAutodetectHandler* handler,
								   const char* connectInfo, const char* brandName)
{
	m_thread = boost::move(
		boost::thread(boost::bind(&FakeDeviceManager::RunAutodetect, this, handler, connectInfo, std::string((brandName != 0) ? brandName: ""))));
}

// ITV8::MMD::IDeviceManager implementation
void FakeDeviceManager::StopAutodetect(const char* connectInfo)
{
	throw std::runtime_error("Method is not implemented");
}

void FakeDeviceManager::StartSearch(ITV8::MMD::IDeviceSearchHandler* handler, ITV8::bool_t continuous)
{
	throw std::runtime_error("Method is not implemented");
}

void FakeDeviceManager::StopSearch()
{
	throw std::runtime_error("Method is not implemented");
}

void FakeDeviceManager::GetDeviceInfo(ITV8::MMD::IDeviceInfoWriter* writer, ITV8::bool_t newestDriver)
{
	throw std::runtime_error("Method is not implemented");
}

ITV8::GDRV::IDevice* FakeDeviceManager::CreateDevice(ITV8::GDRV::IDeviceHandler* handler,
								  const char* brand, const char* model,
								  const char* firmware, const char* driverName, ITV8::uint32_t driverVersion)
{
	throw std::runtime_error("Method is not implemented");
}