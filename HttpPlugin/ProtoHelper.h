#ifndef PROTO_HELPER_H__
#define PROTO_HELPER_H__

#include <memory>

#include "Constants.h"
#include "GrpcReader.h"
#include "SendContext.h"
#include "CommonUtility.h"

#include <HttpServer/HttpServer.h>

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/dynamic_message.h>

#include <google/protobuf/message.h>
#include <google/protobuf/util/type_resolver_util.h>

namespace NWebGrpc
{
    const std::string typeGoogleApisComPrefix = "type.googleapis.com";

    typedef std::shared_ptr<google::protobuf::util::TypeResolver> PTypeResolver;

    struct SCallContext
    {
        std::atomic<bool> headersSent{ false };
    };
    typedef std::shared_ptr<SCallContext> PCallContext;

    class SGrpcServiceMethod : public std::enable_shared_from_this<SGrpcServiceMethod>
    {   
    public:
        virtual ~SGrpcServiceMethod() {}
        virtual void Call(PTypeResolver typeResolver, const NHttp::PRequest& req, NHttp::PResponse resp,
            NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials,
            const google::protobuf::Message* msg, const std::string& prefix) = 0;

        template <typename Derived>
        std::shared_ptr<Derived> shared_from_base()
        {
            return std::dynamic_pointer_cast<Derived>(shared_from_this());
        }

    protected:
        void processResult(PTypeResolver typeResolver, NHttp::PResponse resp, PCallContext cc, const std::string& buffer, const std::string& descriptor)
        {
            //auto buffer = r.SerializeAsString();
            //google::protobuf::io::ArrayInputStream zistream(buffer.data(), buffer.size());

            google::protobuf::util::JsonPrintOptions jpo;
            jpo.add_whitespace = true;
            jpo.always_print_primitive_fields = true;
            jpo.preserve_proto_field_names = true;

            std::string result;
            auto stat = google::protobuf::util::BinaryToJsonString(typeResolver.get(),
                descriptor,
                buffer,
                std::addressof(result),
                jpo);

            if (!stat.ok())
            {
                NHttp::Error(resp, NHttp::IResponse::InternalServerError);
                return;
            }

            sendResponse(resp, cc, result);
        }

        virtual void sendResponse(NHttp::PResponse resp, PCallContext cc, const std::string& data) = 0;
    };
    typedef std::shared_ptr<SGrpcServiceMethod> PMethod;

    template <typename TAsyncInterface, typename TRequestType, typename TResponseType>
    using AsyncRpcStreamMethod_t = std::unique_ptr< ::grpc::ClientAsyncReader<TResponseType>>(TAsyncInterface::*)(
        ::grpc::ClientContext*, const TRequestType&, ::grpc::CompletionQueue*, void*);

    template <typename TAsyncInterface, typename TRequestType, typename TResponseType>
    using AsyncRpcResultMethod_t = std::unique_ptr< ::grpc::ClientAsyncResponseReader<TResponseType>>(TAsyncInterface::*)(
        ::grpc::ClientContext*, const TRequestType&, ::grpc::CompletionQueue*);

    template <typename TServiceType, typename TRequestType, typename TResponseType>
    class SGrpcServiceStreamMethod : public virtual SGrpcServiceMethod
    {
        DECLARE_LOGGER_HOLDER;

        using TAsyncInterface = typename TServiceType::Stub;

        using Reader_t = NWebGrpc::AsyncStreamReader < TServiceType, TRequestType, TResponseType >;
        using PReader_t = std::shared_ptr < Reader_t >;

    public:
        SGrpcServiceStreamMethod(DECLARE_LOGGER_ARG, AsyncRpcStreamMethod_t<TAsyncInterface, TRequestType, TResponseType> method)
            : m_method(method)
        {
            INIT_LOGGER_HOLDER;
        }

        void Call(PTypeResolver typeResolver, const NHttp::PRequest& query,  NHttp::PResponse resp, NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials metaCredentials, const google::protobuf::Message* msg, const std::string& prefix) override
        {
            TRequestType req;
            req.CopyFrom(*msg);

            PReader_t reader(new Reader_t
                (GET_LOGGER_PTR, grpcManager, metaCredentials, m_method));
            reader->setAcceptLanguage(NHttp::AcceptLanguage(query->GetHeaders()));
            reader->setIP(query->GetIp());

            PCallContext cc = std::make_shared<SCallContext>();

            reader->asyncRequest(req, [this, typeResolver, resp, cc, prefix](const TResponseType& r, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
            {
                if (!grpcStatus.ok())
                {
                    return NPluginUtility::SendGRPCError(resp, grpcStatus);
                }

                this->processResult(typeResolver, resp, cc, r.SerializeAsString(), prefix + "/" + r.GetDescriptor()->full_name());
            });
        }

    protected:
        void sendResponse(NHttp::PResponse resp, PCallContext cc, const std::string& data) override
        {
            std::size_t dataLen = data.size();

            bool expected = false;
            if (std::atomic_compare_exchange_strong<bool>(&cc->headersSent, &expected, true))
            {
                resp->SetStatus(NHttp::IResponse::OK);
                resp << NHttp::ContentType(std::string("multipart/related; boundary=") + BOUNDARY_HEADER)
                    << NHttp::CacheControlNoCache();
                resp->FlushHeaders();
            }

            std::string contentType(NHttp::GetMIMETypeByExt("json"));
            contentType.append("; charset=utf-8");

            std::stringstream s;
            s                                   << CR << LF
                << BOUNDARY                     << CR << LF
                << CONTENT_TYPE << contentType  << CR << LF
                << CONTENT_LENGTH << dataLen    << CR << LF
                                                << CR << LF;

            NHttp::IResponse::TConstBufferSeq buffs;
            auto pheader = std::make_shared<std::string>(s.str());
            buffs.push_back(boost::asio::buffer(pheader->c_str(), pheader->size()));

            auto pdata = std::make_shared<std::string>(data);
            buffs.emplace_back(boost::asio::buffer(pdata->c_str(), dataLen));

            try
            {
                resp->AsyncWrite(buffs, [pheader, pdata](boost::system::error_code) {});
            }
            catch (const boost::system::system_error&)
            {
            }
        }

    private:
        AsyncRpcStreamMethod_t<TAsyncInterface, TRequestType, TResponseType> m_method;
    };

    template <typename TServiceType, typename TRequestType, typename TResponseType>
    class SGrpcServiceResultMethod : public virtual SGrpcServiceMethod
    {
        DECLARE_LOGGER_HOLDER;

        using TAsyncInterface = typename TServiceType::Stub;

        using Reader_t = NWebGrpc::AsyncResultReader < TServiceType, TRequestType, TResponseType >;
        using PReader_t = std::shared_ptr < Reader_t >;

    public:
        SGrpcServiceResultMethod(DECLARE_LOGGER_ARG, AsyncRpcResultMethod_t<TAsyncInterface, TRequestType, TResponseType> method)
            : m_method(method)
        {
            INIT_LOGGER_HOLDER;
        }

        void Call(PTypeResolver typeResolver, const NHttp::PRequest& query, NHttp::PResponse resp, NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials metaCredentials, const google::protobuf::Message* msg, const std::string& prefix)
        {
            TRequestType req;
            req.CopyFrom(*msg);

            PReader_t reader(new Reader_t
                (GET_LOGGER_PTR, grpcManager, metaCredentials, m_method));
            reader->setAcceptLanguage(NHttp::AcceptLanguage(query->GetHeaders()));
            reader->setIP(query->GetIp());

            PCallContext cc = std::make_shared<SCallContext>();

            reader->asyncRequest(req, [this, typeResolver, resp, cc, prefix](const TResponseType& r, grpc::Status status)
            {
                if (!status.ok())
                {
                    return NPluginUtility::SendGRPCError(resp, status);
                }

                this->processResult(typeResolver, resp, cc, r.SerializeAsString(), prefix + "/" + r.GetDescriptor()->full_name());
            });
        }

    protected:
        void sendResponse(NHttp::PResponse resp, PCallContext cc, const std::string& data) override
        {
            bool expected = false;
            if (std::atomic_compare_exchange_strong<bool>(&cc->headersSent, &expected, true))
            {
                resp->SetStatus(NHttp::IResponse::OK);
                std::string contentType(NHttp::GetMIMETypeByExt("json"));
                contentType.append("; charset=utf-8");
                resp << NHttp::ContentLength(data.size())
                     << NHttp::ContentType(contentType)
                     << NHttp::CacheControlNoCache();
                resp->FlushHeaders();
            }

            NContext::PSendContext ctx(NContext::CreateStringContext(resp, data));
            ctx->ScheduleWrite();
        }

    private:
        AsyncRpcResultMethod_t<TAsyncInterface, TRequestType, TResponseType> m_method;
    };

    class CGrpcService
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CGrpcService(DECLARE_LOGGER_ARG)
        {
            INIT_LOGGER_HOLDER;
        }

        template <typename TServiceType, typename TRequestType, typename TResponseType>
        void RegisterStreamMethod(const std::string& methodName, AsyncRpcStreamMethod_t<typename TServiceType::Stub, TRequestType, TResponseType> method)
        {
            m_methods.insert(std::make_pair(methodName, PMethod(new NWebGrpc::SGrpcServiceStreamMethod < TServiceType, TRequestType, TResponseType >
                (GET_LOGGER_PTR, method))));
        }

        template <typename TServiceType, typename TRequestType, typename TResponseType>
        void RegisterResultMethod(const std::string& methodName, AsyncRpcResultMethod_t<typename TServiceType::Stub, TRequestType, TResponseType> method)
        {
            m_methods.insert(std::make_pair(methodName, PMethod(new NWebGrpc::SGrpcServiceResultMethod < TServiceType, TRequestType, TResponseType >
                (GET_LOGGER_PTR, method))));
        }

        void Call(const std::string& methodName, PTypeResolver typeResolver, const NHttp::PRequest& req, NHttp::PResponse resp,
            NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials metaCredentials, const google::protobuf::Message* msg, const std::string& prefix)
        {
            std::map<std::string, PMethod>::iterator it = m_methods.find(methodName);
            if (m_methods.end() != it)
                it->second->Call(typeResolver, req, resp, grpcManager, metaCredentials, msg, prefix);
            else
                _wrn_ << "Grpc service does not contain method " << methodName;
        }
    private:
        std::map<std::string, PMethod> m_methods;
    };
    typedef std::shared_ptr<CGrpcService> PGrpcService;
}

#endif // PROTO_HELPER_H__
