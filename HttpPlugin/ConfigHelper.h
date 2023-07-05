#ifndef CONFIG_HELPER_H_
#define CONFIG_HELPER_H_

#include <map>
#include <boost/thread/mutex.hpp>

#include <InfraServer_IDL/InfraServerC.h>
#include <CorbaHelpers/Container.h>
#include <CorbaHelpers/ObjectName.h>

namespace IPINT30
{
    struct SDeviceSettings;
}
namespace nis = InfraServer::New;

struct Degree;
struct TelemetryInfo;

class CConfigHelper
{
public:
    class XFailed : public std::runtime_error
    {
    public:
        explicit XFailed(const std::string &message) : std::runtime_error(message) {}
    };
    class XChanged : public std::runtime_error
    {
    public:
        XChanged() : std::runtime_error("The repository has already been changed.") {}
    };
    class XRetry : public XChanged
    {
    public:
        XRetry() {}
    };

public:
    typedef nis::SPacketVersion TRevision;
    typedef boost::shared_ptr<IPINT30::SDeviceSettings> PIpintConfig;
    typedef std::pair<nis::SPacketVersion, PIpintConfig> TIpintConfigSnapshot;

public:
    explicit CConfigHelper(NCorbaHelpers::IContainer *c)
        : m_container(c->CreateContainer())
    {}

    nis::SFriendlyName LoadFriendlyName(const char* hostName, const char* objectName);

    // Вернет конфигурацию, если указанная ревизия НЕ совпадает с рабочей ревизией репозитория.
    // Иначе, параметры rev и dest останутся без изменения
    // В остальных случаях генерирует XFailed.
    bool GetConfigIfChanged(const NCorbaHelpers::CObjectName &name, TRevision &rev, PIpintConfig dest) const;
        /*throw(XFailed)*/

    // Сохраняет конфигурацию, если указанная ревизия совпадает с рабочей ревизией репозитория.
    // Иначе, генерирует XChanged.
    // В остальных случаях генерирует XFailed.
    void SetConfig(const NCorbaHelpers::CObjectName &name, const TRevision &rev, const PIpintConfig src);
        /*throw(XChanged, XFailed)*/

    // Очистить кеш репозиториев
    void InvalidateAll() const;

    nis::ConfigRepository_var GetRepository(const std::string &host) const;

private:
    nis::SIdSet GetCurrent(nis::ConfigRepository *repo) const;
    nis::ConfigRepository* GetRepository(NCorbaHelpers::PContainerTrans cont, const std::string &host) const;
    static void Convert(const InfraServer::New::SMainConfig& src, PIpintConfig dest);
    static void Convert(const PIpintConfig src, InfraServer::New::SMainConfig& dest);
    void InvalidateRef(const std::string &host) const;

private:
    typedef std::map<std::string, nis::ConfigRepository_var> TRepositories;

private:
    NCorbaHelpers::PContainerTrans m_container;

    mutable TRepositories m_repositories;
    mutable boost::mutex m_mutex;
};

#endif // CONFIG_HELPER_H_
