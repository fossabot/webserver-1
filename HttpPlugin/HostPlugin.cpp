#include <ace/OS.h>
#include <map>
#include <boost/serialization/string.hpp>

#include <HttpServer/json_oarchive.h>
#include <HttpServer/BasicServletImpl.h>
#include <InfraServer_IDL/InfraServerC.h>
#include <InfraServer_IDL/LicenseServiceC.h>
#include <CorbaHelpers/ResolveServant.h>
#include <CorbaHelpers/Unicode.h>

#include "HttpPlugin.h"
#include "Constants.h"
#include "CommonUtility.h"
#include "Tokens.h"

using namespace NHttp;
namespace npu = NPluginUtility;

namespace
{
    const char* const LICENSE_SERVICE = "LicenseService/Server";

    struct DomainNameInfo
    {
        std::string domainName;
        std::string domainFriendlyName;

    private:
        friend class boost::serialization::access;
        template<typename TArchive>
        void serialize(TArchive &ar, const unsigned int /*version*/)
        {
            ar  & boost::serialization::make_nvp("domainName", domainName);
            ar  & boost::serialization::make_nvp("domainFriendlyName", domainFriendlyName);
        }
    };

    struct PlatformInfo
    {
        std::string hostName;
        std::string machine;
        std::string os;

    private:
        friend class boost::serialization::access;
        template<typename TArchive>
        void serialize(TArchive &ar, const unsigned int /*version*/)
        {
            ar    & boost::serialization::make_nvp("hostName", hostName);
            ar    & boost::serialization::make_nvp("machine", machine);
            ar    & boost::serialization::make_nvp("os", os);
        }
    };

    struct HostInfo
    {
        HostInfo(const std::string& nn)
            : nodeName(nn)
            , licenseStatus("License information is not accessible")
        {
        }

        std::string nodeName;
        DomainNameInfo domainInfo;
        PlatformInfo platformInfo;
        std::string licenseStatus;
        long timezone;
        std::vector<std::string> nodes;

        void SetLicenseStatus(InfraServer::LicenseService::LS_Status status)
        {
            switch (status)
            {
            case InfraServer::LicenseService::LS_OK:             licenseStatus = "OK";              break;
            case InfraServer::LicenseService::LS_NoKey:          licenseStatus = "No key";          break;
            case InfraServer::LicenseService::LS_InvalidKey:     licenseStatus = "Invalid key";     break;
            case InfraServer::LicenseService::LS_MismatchingKey: licenseStatus = "Mismatching key"; break;
            case InfraServer::LicenseService::LS_Expired:        licenseStatus = "Expired";         break;
            case InfraServer::LicenseService::LS_DemoActive:     licenseStatus = "Demo active";     break;
            case InfraServer::LicenseService::LS_DemoInactive:   licenseStatus = "Demo inactive";   break;
            case InfraServer::LicenseService::LS_DemoExpired:    licenseStatus = "Demo expired";    break;
            default: break;
            }
        }

    private:
        friend class boost::serialization::access;
        template<typename TArchive>
        void serialize(TArchive &ar, const unsigned int /*version*/)
        {
            using namespace boost::serialization;
            ar  & make_nvp("nodeName", nodeName)
                & make_nvp("domainInfo", domainInfo)
                & make_nvp("platformInfo", platformInfo)
                & make_nvp("licenseStatus", licenseStatus)
                & make_nvp("timeZone", timezone)
                & make_nvp("nodes", nodes);
        }
    };

    class CHostContentImpl : public NHttpImpl::CBasicServletImpl
    {
    public:
        explicit CHostContentImpl(NCorbaHelpers::IContainer *c)
            : m_container(c)
        {}

        virtual void Head(const PRequest req, PResponse resp)
        {
            Send(req, resp, true);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Send(req, resp, false);
        }

    private:
        void Send(const PRequest req, PResponse resp, bool headersOnly)
        {
            enum EHostMode { EUNKNOWN, ELIST, ETARGET };
            EHostMode mode = EUNKNOWN;

            using namespace npu;
            const std::string path = req->GetPathInfo();

            std::string hostName;
            if(Match(path, Empty()))
            {
                mode = ELIST;
            }
            else if(Match(path, Token(hostName)))
            {
                mode = ETARGET;
            }
            else
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            InfraServer::HostAgent_var ha = GetHostAgent(hostName);
            if(CORBA::is_nil(ha))
            {
                Error(resp, IResponse::NotFound);
                return;
            }

            switch(mode)
            {
            case ELIST:   ProcessHostList(resp, ha, headersOnly);             break;
            case ETARGET: ProcessTargetHost(resp, ha, hostName, headersOnly); break;
            default:      Error(resp, IResponse::InternalServerError);        break;
            }
        }

        InfraServer::HostAgent* GetHostAgent(const std::string &hostName) const
            /*throw()*/
        {
            std::string ref = "HostAgent/HostAgent";
            if(!hostName.empty())
                ref = "hosts/" + hostName + "/" + ref;

            InfraServer::HostAgent_var res;
            if(NCorbaHelpers::PContainer cont = m_container)
                res = NCorbaHelpers::ResolveServant<InfraServer::HostAgent>(cont.Get(), ref.c_str());
            return res._retn();
        }

        void ProcessHostList(NHttp::PResponse resp, InfraServer::HostAgent *ha, bool headersOnly)
        {
            try
            {
                InfraServer::Domain_var pDomain = ha->GetDomain();
                if (!CORBA::is_nil(pDomain))
                {
                    std::vector<std::string> hosts;

                    InfraServer::HostStatusSeq_var hostStatusList = pDomain->EnumerateHostStatuses(true, true);
                    CORBA::ULong len = hostStatusList->length();
                    for (CORBA::ULong i = 0; i < len; ++i)
                    {
                        hosts.push_back(hostStatusList[i].HostName.in());
                    }

                    std::stringstream ss;
                    {
                        boost::archive::json_oarchive ar(ss);
                        ar << boost::serialization::make_naked_object(hosts);
                    }

                    NPluginUtility::SendText(resp, ss.str(), true, headersOnly);
                    return;
                }
            }
            catch (const CORBA::Exception&) {}
            Error(resp, IResponse::InternalServerError);
        }

        void ProcessTargetHost(
            NHttp::PResponse resp, InfraServer::HostAgent *ha, const std::string& hostName, bool headersOnly)
        {
            try
            {
                HostInfo hi(hostName);

                InfraServer::PlatformInfo_var pi = ha->GetPlatformInfo();
                {
                    std::wstring physicalName;
                    physicalName.assign(pi->ComputerName.in());
                    hi.platformInfo.hostName = NCorbaHelpers::ToUtf8(physicalName.c_str());
                    hi.platformInfo.machine = pi->OsMachine.in();
                    hi.platformInfo.os = pi->OsSysName.in();
                }

                hi.timezone = ha->TimeZone();

                InfraServer::Domain_var pDomain = ha->GetDomain();
                if (!CORBA::is_nil(pDomain))
                {
                    hi.domainInfo.domainName = pDomain->GetName();
                    std::wstring friendlyName;
                    friendlyName.assign(pDomain->GetFriendlyName());

                    hi.domainInfo.domainFriendlyName =
                        NCorbaHelpers::ToUtf8(friendlyName.c_str());

                    std::string licenseService(LICENSE_SERVICE);
                    if (!hostName.empty())
                    {
                        licenseService.assign(HOST_PREFIX);
                        licenseService.append(hostName).append("/").append(LICENSE_SERVICE);
                    }

                    if(NCorbaHelpers::PContainer cont = m_container)
                    {
                        InfraServer::LicenseService_var ls = NCorbaHelpers::ResolveServant<InfraServer::LicenseService>(cont.Get(),
                            licenseService);
                        if (!CORBA::is_nil(ls))
                            hi.SetLicenseStatus(ls->GetStatus());
                    }

                    InfraServer::HostStatusSeq_var hostStatusList = pDomain->EnumerateHostStatuses(true, true);
                    CORBA::ULong len = hostStatusList->length();
                    for (CORBA::ULong i = 0; i < len; ++i)
                    {
                        hi.nodes.push_back(hostStatusList[i].HostName.in());
                    }

                    std::stringstream ss;
                    {
                        boost::archive::json_oarchive ar(ss);
                        ar << boost::serialization::make_naked_object(hi);
                    }

                    NPluginUtility::SendText(resp, ss.str(), true, headersOnly);
                    return;
                }
            }
            catch (const CORBA::Exception&) {}
            Error(resp, IResponse::InternalServerError);
        }

    private:
        NCorbaHelpers::WPContainer m_container;
    };
}

namespace NHttp
{
    IServlet* CreateHostServlet(NCorbaHelpers::IContainer* c)
    {
        return new CHostContentImpl(c);
    }
}
