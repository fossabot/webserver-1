#include <boost/asio.hpp>

#include "TrustedIpList.h"
#include "UrlBuilder.h"
#include "Constants.h"
#include <SecurityManager/SecurityManager.h>
#include <CorbaHelpers/ResolveServant.h>
#include <InfraServer_IDL/EventTypeTraits.h>
#include <GrpcHelpers/MetaCredentials.h>

using boost::asio::ip::tcp;

namespace
{
    using namespace NHttp;

    class IpWhiteListInterceptor : public IInterceptor
    {
        DECLARE_LOGGER_HOLDER;

        PInterceptor m_next;
        //NCorbaHelpers::WPContainer m_container;
        //InfraServer::SecurityManager::ISecurityManager_var m_sm;
        typedef std::vector<std::string> TIPList;
        TIPList m_localIps;
    public:
        IpWhiteListInterceptor(DECLARE_LOGGER_ARG,  PInterceptor next, NCorbaHelpers::IContainer* c) :
            m_next(next)
           //, m_container(c)
           //, m_sm(NCorbaHelpers::ResolveServant<InfraServer::SecurityManager::ISecurityManager>(c, "SecurityManager/Server"))
           , m_localIps(1, "127.0.0.1")
        {
            INIT_LOGGER_HOLDER;

            boost::asio::io_service io_service;
            tcp::resolver resolver(io_service);
            tcp::resolver::query query(boost::asio::ip::host_name(), "");
            boost::system::error_code ec;
            tcp::resolver::iterator iter = resolver.resolve(query, ec);
            if (ec)
                _wrn_ << "Resolve error: " << ec.message();
            tcp::resolver::iterator end; // End marker.
            while (iter != end)
            {
                tcp::endpoint ep = *iter++;
                m_localIps.push_back(ep.address().to_string());
            }
        }

        PResponse Process(const PRequest req, PResponse resp)
        {
            using namespace NSecurityManager;

            std::string ctx = req->GetContextPath();
            if (("/" == ctx) || ("OPTIONS" == req->GetMethod()) || (0 == ctx.find("/v1/authentication/authenticate")))
                return resp;

            //bool trusted_ip = false;

            //std::string requestIp(req->GetIp());

            //bool isLocal = isLocalIp(requestIp);
            //if (!isLocal)
            //{
            //    
            //    InfraServer::SecurityManager::SConfig_var cfg;
            //    if (!CORBA::is_nil(m_sm) && (cfg = m_sm->GetConfig()))
            //    {
            //        const auto &vIpList = cfg->IPFilters;
            //        for (size_t i = 0; i < vIpList.length(); ++i)
            //        {
            //            if (vIpList[i].ipAddress.in() == req->GetIp())
            //            {
            //                trusted_ip = true;
            //                break;
            //            }
            //        }

            //        //if (trusted_ip)
            //        //{
            //        //    IRequest::AuthSession authSession;
            //        //    authSession.id = TOKEN_AUTH_SESSION_ID;

            //        //    //Bondarenko: authSession.data.second (name) shouldn't matter. I use "root"
            //        //    authSession.data = std::make_pair(std::make_shared<std::string>(NSecurityManager::CreateSystemSession(GET_LOGGER_PTR)), std::make_shared<std::string>("root"));                        

            //        //    req->SetAuthSession(authSession);
            //        //    return resp;
            //        //}
            //    }
            //}
            //else
            //{
            //    trusted_ip = true;
            //}

            //if (!trusted_ip)
            //{
            //    resp->SetStatus(IResponse::Forbidden);
            //    resp->FlushHeaders();
            //    return PResponse();
            //}

            if (m_next)
            {
                return m_next->Process(req, resp);
            }

            resp->SetStatus(IResponse::Forbidden);
            resp->FlushHeaders();
            _log_ << "IpWhiteListInterceptor check is failed.";
            return PResponse();
       }

    private:
         bool isLocalIp(const std::string& ip)
         {
             TIPList::const_iterator it = std::find(m_localIps.begin(), m_localIps.end(), ip);
             return m_localIps.end() == it ? false : true;
         }
    };
}


NHttp::IInterceptor* CreateTrustedIpListInterceptor(DECLARE_LOGGER_ARG
    , NHttp::PInterceptor next 
    , NCorbaHelpers::IContainer* c
    )
{
    return new IpWhiteListInterceptor(GET_LOGGER_PTR,  next, c);
}
