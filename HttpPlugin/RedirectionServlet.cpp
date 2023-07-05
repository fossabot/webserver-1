#include <ace/OS.h>
#include <HttpServer/BasicServletImpl.h>
#include "HttpPlugin.h"

using namespace NHttp;

namespace
{
    class CRedirectionServlet
        :   public NHttpImpl::CBasicServletImpl
    {
    public:
        explicit CRedirectionServlet(const std::string &suffix)
            :   m_suffix(suffix)
        {}

    private:
        virtual void Head(const PRequest req, PResponse resp) { Redirect(req, resp); }
        virtual void Get(const PRequest req, PResponse resp)  { Redirect(req, resp); }
        virtual void Post(const PRequest req, PResponse resp) { Redirect(req, resp); }
        virtual void Put(const PRequest req, PResponse resp)  { Redirect(req, resp); }

    private:
        void Redirect(const PRequest req, PResponse resp) const
        {
            resp->SetStatus(IResponse::MovedTemporarily);
            resp << SHttpHeader("Location", req->GetDestination() + m_suffix);
            resp->FlushHeaders();
        }

    private:
        const std::string m_suffix;
    };
}

namespace NHttp
{
    IServlet* CreateRedirectionServlet(const std::string &suffix)
    {
        return new CRedirectionServlet(suffix);
    }
}
