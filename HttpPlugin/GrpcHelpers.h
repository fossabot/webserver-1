#pragma once
#include <memory>

#include <Logging/log2.h>
#include <GrpcHelpers/Cancellation.h>
#include <GrpcHelpers/ConnectionManager.h>

namespace NGrpcHelpers
{
    class YieldContext;
}

namespace NWebGrpc
{
    struct IGrpcManager : public std::enable_shared_from_this < IGrpcManager >
    {
        virtual ~IGrpcManager() {}

        virtual void Start() = 0;
        virtual void Stop() = 0;

        virtual std::shared_ptr<grpc::CompletionQueue> GetCompletionQueue() const = 0;
        virtual NGrpcHelpers::Cancellable::Token GetToken() = 0;
        virtual NGrpcHelpers::PChannel GetChannel() const = 0;
        virtual NGrpcHelpers::PChannel GetExternalChannel(const NGrpcHelpers::YieldContext&) const = 0;
    };
    typedef std::shared_ptr<IGrpcManager> PGrpcManager;

    class CGrpcService;
    typedef std::shared_ptr<CGrpcService> PGrpcService;

    struct IGrpcRegistrator
    {
        virtual ~IGrpcRegistrator() {}

        virtual void RegisterService(const std::string&, PGrpcService) = 0;
        virtual PGrpcService GetService(const std::string&) const = 0;
    };
    typedef std::shared_ptr<IGrpcRegistrator> PGrpcRegistrator;

    PGrpcManager CreateGrpcManager(DECLARE_LOGGER_ARG);
    PGrpcRegistrator CreateGrpcRegistrator(DECLARE_LOGGER_ARG);

}
