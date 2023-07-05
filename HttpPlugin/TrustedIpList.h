#ifndef IP_WHITE_CHECKER_H
#define IP_WHITE_CHECKER_H

#include <HttpServer/HttpServer.h>
#include <set>

//#include "UrlBuilder.h"
//#include "HttpPlugin.h"

namespace NCorbaHelpers
{
    class IContainer;
}

NHttp::IInterceptor* CreateTrustedIpListInterceptor(DECLARE_LOGGER_ARG
    , NHttp::PInterceptor next = NHttp::PInterceptor()
    , NCorbaHelpers::IContainer* c = NULL
    );

#endif
