#ifndef GRPCWEBPRXOYGO_WATCHDOG_
#define GRPCWEBPRXOYGO_WATCHDOG_

#include "GrpcHelpers.h"
#include <CorbaHelpers/Container.h>

namespace NPluginHelpers
{
    class IGrpcWebProxyManager
    {
    public:
        virtual ~IGrpcWebProxyManager() {}
        virtual void Start() = 0;
        virtual void Stop() = 0;
    };

    using PGrpcWebProxyManager = std::shared_ptr<IGrpcWebProxyManager>;


    PGrpcWebProxyManager CreateGrpcWebManager(DECLARE_LOGGER_ARG, boost::asio::io_service& service, NCorbaHelpers::IContainer* c);
}

#endif