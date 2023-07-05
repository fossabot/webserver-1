#ifndef GRPC_READER_H__
#define GRPC_READER_H__

#include "GrpcHelpers.h"

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <grpc++/grpc++.h>

#include <GrpcHelpers/GrpcSpawnCommon.h>
#include <GrpcHelpers/Cancellation.h>
#include <GrpcHelpers/MetaCredentials.h>
#include <GrpcHelpers/NgpMetadata.h>
#include <CorbaHelpers/Reactor.h>

namespace NWebGrpc
{
    const char* const GRPC_ORIGIN_ADDRESS = "x-ngp-request-origin-address";

    enum STREAM_ANSWER {_PROGRESS, _FINISH/*, _ERROR*/};

    using ErrorCallback_t = std::function <void(grpc::Status)>;

    template<typename TInterface, typename TRequest, typename TResponse> 
    struct AsyncStreamReader : public std::enable_shared_from_this<AsyncStreamReader<TInterface, TRequest, TResponse> >
    {
    private:
        DECLARE_LOGGER_HOLDER;
    public:
        using Callback_t = std::function <void(const TResponse&, STREAM_ANSWER, grpc::Status)>;
        using TAsynInterface = typename TInterface::Stub;
        using AsyncRpcMethod_t = std::unique_ptr< ::grpc::ClientAsyncReader<TResponse>>(TAsynInterface::*)(
            ::grpc::ClientContext* context,
            const TRequest& request,
            ::grpc::CompletionQueue* cq,
            void* tag);

        AsyncStreamReader(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials mc, AsyncRpcMethod_t method, std::chrono::duration<int, std::milli> timeout = std::chrono::milliseconds(8000))
            : m_ioService(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_grpcManager(grpcManager)
            , m_metaCredentials(mc)
            , m_operationTimeout(timeout)
            , m_method(method)
            , m_stopped(false)
        {
            INIT_LOGGER_HOLDER;
        }

        void asyncRequest(const TRequest& req, Callback_t onResponse)
        {
            auto pthis = this->shared_from_this();
            NGrpcHelpers::ExcSafeSpawn(m_ioService, [this, pthis, req, onResponse](boost::asio::yield_context asio_yield)
            {
                doAsyncWork(asio_yield, req, onResponse);
            });
        }

        void asyncStop()
        {
            m_stopped = true;
        }

        void doAsyncWork(boost::asio::yield_context asio_yield, const TRequest& req, Callback_t onResponse)
        {
            try
            {
                auto token = m_grpcManager->GetToken();
                grpc::ClientContext context;
                context.set_credentials(m_metaCredentials);
                context.AddMetadata(GRPC_ORIGIN_ADDRESS, m_ip);

                if (m_acceptLanguage)
                    NGrpcHelpers::RequestLocalization(context, m_acceptLanguage.get());

                NGrpcHelpers::YieldContext yield(GET_LOGGER_PTR, m_ioService, asio_yield, token, NGrpcHelpers::make_cancellation(context));
                std::shared_ptr<grpc::CompletionQueue> cq = m_grpcManager->GetCompletionQueue();
                if (!cq)
                    throw std::runtime_error("Empty completion queue!");
                auto channel = m_grpcManager->GetChannel();
                if (!channel)
                    throw std::runtime_error("Can't resolve channel!");
                auto stub = channel->template NewStub<TInterface>();
                // TODO: https://github.com/grpc/grpc/issues/6836
                // Use AsyncWaitForGrpc timeout with cancellation

                std::unique_ptr< ::grpc::ClientAsyncReader<TResponse>> reader;
                bool ok = NGrpcHelpers::AsyncWaitForGrpc([&reader, &stub, this, cq, &req, &context](void* tag)
                {
                    reader = (stub.get()->*m_method)(&context, req, cq.get(), tag);
                }, yield, m_operationTimeout);

                grpc::Status okStatus{ grpc::StatusCode::OK, "" };
                while (ok && !m_stopped)
                {
                    TResponse res;
                    ok = NGrpcHelpers::AsyncRead(*reader, res, yield, m_operationTimeout * 2);
                    if (ok)
                        onResponse(res, _PROGRESS, okStatus);
                }

                grpc::Status status;
                ok = NGrpcHelpers::AsyncWaitForGrpc([&reader, &status](void* tag)
                {
                    reader->Finish(&status, tag);
                }, yield);

                onResponse(TResponse(), _FINISH, status);
            }
            catch (const std::exception& ex)
            {
                _err_ << "[AsyncStreamReader] StreamReader: error reading stream from node: " << " error:" << ex.what() << " req: " << typeid(TRequest).name();

                grpc::Status status{ grpc::StatusCode::UNKNOWN, "GRPC exception" };
                onResponse(TResponse(), _FINISH, status);
            }
        }

        void setAcceptLanguage(boost::optional<std::string> acceptLanguage) { m_acceptLanguage = acceptLanguage; }
        void setIP(const std::string& ip) { m_ip = ip;  }
    private:
        boost::asio::io_service& m_ioService;
        NWebGrpc::PGrpcManager m_grpcManager;
        NGrpcHelpers::PCredentials m_metaCredentials;
        boost::optional<std::string> m_acceptLanguage;
        std::string m_ip;

        std::chrono::duration<int, std::milli> m_operationTimeout;

        AsyncRpcMethod_t m_method;
        std::atomic<bool> m_stopped;
    };

    template<typename TInterface, typename TRequest, typename TResponse>
    struct AsyncResultReader : public std::enable_shared_from_this<AsyncResultReader<TInterface, TRequest, TResponse> >
    {
    private:
        DECLARE_LOGGER_HOLDER;
    public:

        typedef TRequest Request_t;
        typedef TResponse Responce_t;

        using Callback_t = std::function <void(const TResponse&, grpc::Status)>;
        using TAsynInterface = typename TInterface::Stub;
        using AsyncRpcMethod_t = std::unique_ptr< ::grpc::ClientAsyncResponseReader<TResponse>>(TAsynInterface::*)(
            ::grpc::ClientContext* context,
            const TRequest& request,
            ::grpc::CompletionQueue* cq);

        AsyncResultReader(DECLARE_LOGGER_ARG, NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials mc, AsyncRpcMethod_t method)
            : m_ioService(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_grpcManager(grpcManager)
            , m_metaCredentials(mc)
            , m_operationTimeout(std::chrono::seconds(8))
            , m_method(method)
        {
            INIT_LOGGER_HOLDER;
        }

        void asyncRequest(const TRequest& req, Callback_t onResponse)
        {
            auto pthis = this->shared_from_this();
            NGrpcHelpers::ExcSafeSpawn(m_ioService, [this, pthis, req, onResponse](boost::asio::yield_context asio_yield)
            {
                doAsyncWork(asio_yield, req, onResponse);
            });
        }

        void doAsyncWork(boost::asio::yield_context asio_yield, const TRequest& req, Callback_t onResponse)
        {
            try
            {
                auto token = m_grpcManager->GetToken();
                grpc::ClientContext context;
                context.set_credentials(m_metaCredentials);   
                context.AddMetadata(GRPC_ORIGIN_ADDRESS, m_ip);

                if (m_acceptLanguage)
                    NGrpcHelpers::RequestLocalization(context, m_acceptLanguage.get());

                NGrpcHelpers::YieldContext yield(GET_LOGGER_PTR, m_ioService, asio_yield, token, NGrpcHelpers::make_cancellation(context));
                std::shared_ptr<grpc::CompletionQueue> cq = m_grpcManager->GetCompletionQueue();
                if (!cq)
                    throw std::runtime_error("Empty completion queue!");
                auto channel = m_grpcManager->GetChannel();
                if (!channel)
                    throw std::runtime_error("Can't resolve channel!");
                auto stub = channel->template NewStub<TInterface>();

                auto rpc = (stub.get()->*m_method)(&context, req, cq.get());
                TResponse res;
                grpc::Status status;
                bool finalOk = NGrpcHelpers::AsyncWaitForGrpc(
                    [&rpc, &res, &status](void* tag)
                {
                    rpc->Finish(&res, &status, tag);
                }, yield);

                (void)finalOk;
                m_ioService.post(boost::bind(onResponse, res, status));
            }
            catch (const std::exception& ex)
            {
                _err_ << "[AsyncResultReader] StreamReader: error reading result from node: " << " error:" << ex.what() << " req: " << typeid(TRequest).name();

                grpc::Status status{ grpc::StatusCode::UNKNOWN, "GRPC exception" };
                m_ioService.post(boost::bind(onResponse, TResponse(), status));
            }
        }

        void setAcceptLanguage(boost::optional<std::string> acceptLanguage) { m_acceptLanguage = acceptLanguage; }
        void setIP(const std::string& ip) { m_ip = ip; }
    private:
        boost::asio::io_service& m_ioService;
        NWebGrpc::PGrpcManager m_grpcManager;
        NGrpcHelpers::PCredentials m_metaCredentials;
        boost::optional<std::string> m_acceptLanguage;
        std::string m_ip;

        std::chrono::duration<int, std::milli> m_operationTimeout;

        AsyncRpcMethod_t m_method;
    };
}

#endif // GRPC_READER_H__
