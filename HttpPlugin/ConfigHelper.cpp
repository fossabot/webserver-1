#include <boost/serialization/map.hpp>

#include <CorbaHelpers/ObjectName.h>
#include <Lifecycle/ConfigParser.h>
#include <InfraServer_IDL/MainConfig.h>
#include <DeviceIpint_3/DeviceSettings.h>
#include <InfraServer/utils.h>

#include "ConfigHelper.h"
#include "Constants.h"

namespace
{
    const char* const CONFIG_MANAGEMENT = "ConfigManagement/Manager";
}

nis::SIdSet CConfigHelper::GetCurrent(nis::ConfigRepository *repo) const
    /*throw(XFailed)*/
{
    if(CORBA::is_nil(repo))
        throw XFailed("The repository is not available.");

    try
    {
        return nis::SIdSet_var(repo->Current());
    }
    catch(const CORBA::Exception &)
    {
        throw XFailed("Couldn't get the current revision.");
    }
}

nis::SFriendlyName CConfigHelper::LoadFriendlyName(const char* hostName, const char* objectName)
{
    nis::ConfigRepository_var configRepo = GetRepository(hostName);

    if (!CORBA::is_nil(configRepo))
    {
        nis::SConfigPacket_var cp = configRepo->GetConfigPart(
            InfraServer::TName_var(NCorbaHelpers::StringToCosName(objectName)),
            nis::CONF_META);
        return cp->Meta.FriendlyName;
    }

    throw std::runtime_error("Friendly name is not accessible");
}

void CConfigHelper::Convert(const InfraServer::New::SMainConfig& src, PIpintConfig dest)
    /*throw(XFailed)*/
{
    std::string error;
    try
    {
        XmlConfigUtils::FromMainConfigBuffer(src, [dest](std::istream& stream) {
            boost::archive::xml_iarchive ai(stream);
            dest->Serialize(ai);
        } );
        return;
        
    }
    catch(const std::exception &e)
    {
        error = e.what();
    }
    throw XFailed(error);
}

void CConfigHelper::Convert(const PIpintConfig src, InfraServer::New::SMainConfig& dest)
{
    std::stringstream error;
    try
    {
        using namespace InfraServerUtils::Config::Xml;
        XmlConfigUtils::ToMainConfigBuffer(dest, IPINT30::LATEST_CONFIG_VERSION, [src](std::ostream& os)     
        {
            boost::archive::xml_oarchive ar(os);
            src->Serialize(ar);
        });
        return;
    }
    catch(const CORBA::Exception &e)
    {
        error << "Couldn't write to blob iterator, an error had been occured: " << e._name();
    }
    catch(const std::exception &e)
    {
        error << "Couldn't serialize the configuration, an error had been occured: " << e.what();
    }
    throw XFailed(error.str());
}

bool CConfigHelper::GetConfigIfChanged(const NCorbaHelpers::CObjectName &name, TRevision &rev, PIpintConfig dest) const
    /*throw(XRetry, XFailed)*/
{
    const std::string host = name.GetObjectParent();
    nis::ConfigRepository_var repo = GetRepository(host);
    if(CORBA::is_nil(repo))
        throw XFailed("The repository is not available.");

    try
    {
        nis::TConfigPacketsInfo req;
        req.length(1);
        CosNaming::Name_var cosName = name.ToCosName();
        req[0].ServiceName = cosName;
        req[0].Version = rev;
        nis::TConfigPackets_var packets = repo->GetChangedConfig(req, nis::CONF_MAIN);
        if (packets->length() == 0)
            return false; // does not changed
        const auto& packet = packets[CORBA::ULong(0)];
        if (packet.Mask == nis::CONF_NONE)
        {
            // config not found
            rev = TRevision();
            dest.reset();
            return true;
        }
        // Changed
        Convert(packet.Main, dest);
        rev = packet.Version;
        return true;
    }
    catch(const CORBA::TRANSIENT &)
    {
        InvalidateRef(host);
        throw XFailed("The repository is not available.");
    }
    catch(const CORBA::Exception &e)
    {
        throw XFailed(std::string("Repository error: ") + e._name());
    }
    return false;
}

void CConfigHelper::SetConfig(const NCorbaHelpers::CObjectName &name, const TRevision &rev, const PIpintConfig src)
    /*throw(XChanged, XFailed)*/
{
    const std::string host = name.GetObjectParent();
    nis::ConfigRepository_var repo = GetRepository(host);
    if(CORBA::is_nil(repo))
        throw XFailed("The repository is not available.");

    while(true)
    {
        try
        {
            nis::SConfigPacket pack = {};
            pack.ServiceName = CosNaming::Name_var(name.ToCosName());
            pack.Mask = nis::CONF_MAIN;
            pack.Version = rev;
            Convert(src, pack.Main);

            nis::SChangeSet changes = {};
            changes.Modified.length(1);
            changes.Modified[CORBA::ULong(0)] = pack;

            repo->Commit(changes, "http plugin's changes");
            break;
        }
        catch(const nis::ConfigRepository::XRetry &) {}
        catch(const nis::ConfigRepository::XCurrentStateChanged &) { throw XChanged(); }
        catch(const CORBA::Exception &)
        {
            InvalidateRef(host);
            throw XFailed("Couldn't commit the changes.");
        }
        catch(const XFailed &)
        {
            InvalidateRef(host);
            throw;
        }
    }
}

nis::ConfigRepository_var CConfigHelper::GetRepository(const std::string &host) const
    /*throw()*/
{
    try
    {
        {
            boost::mutex::scoped_lock lock(m_mutex);
            const TRepositories::const_iterator it = m_repositories.find(host);
            if (it != m_repositories.end())
            {
                nis::ConfigRepository_var cr = it->second;
                if (!cr->_non_existent())
                    return cr;

                m_repositories.erase(it);
            }
        }

        nis::ConfigRepository_var res;
        NCorbaHelpers::PContainerTrans cont = m_container;
        if (!cont)
            return res;

        if (res = GetRepository(cont, host))
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_repositories[host] = res;
        }
        return res;
    }
    catch (const CORBA::Exception &) {}
    catch (...){}
    return nis::ConfigRepository::_nil();
}

nis::ConfigRepository* CConfigHelper::GetRepository(NCorbaHelpers::PContainerTrans cont, const std::string &host) const
    /*throw()*/
{
    std::string repoPath(HOST_PREFIX);
    repoPath.append(host).append("/").append(CONFIG_MANAGEMENT);

    try
    {
        CORBA::Object_var objRef = cont->GetRootNC()->resolve_str(repoPath.c_str());
        InfraServer::ConfigManager_var configManager = InfraServer::ConfigManager::_narrow(objRef);
        if(!CORBA::is_nil(configManager))
            return configManager->ConfigRepository();
    }
    catch(const CORBA::Exception &) {}
    return nis::ConfigRepository::_nil();
}

void CConfigHelper::InvalidateRef(const std::string &host) const
{
    boost::mutex::scoped_lock lock(m_mutex);
    m_repositories.erase(host);
}

void CConfigHelper::InvalidateAll() const
{
    boost::mutex::scoped_lock lock(m_mutex);
    m_repositories.clear();
}
