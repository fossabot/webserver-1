#include "HttpPlugin.h"
#include "Constants.h"
#include "ProtoHelper.h"
#include "CommonUtility.h"

#include <HttpServer/BasicServletImpl.h>

#include <json/json.h>

using namespace NHttp;

namespace
{
    const char* const METHOD_PARAMETER = "method";
    const char* const DATA_PARAMETER = "data";

    class CGrpcContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CGrpcContentImpl(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const NWebGrpc::PGrpcRegistrator grpcRegistrator)
            : m_grpcManager(grpcManager)
            , m_grpcRegistrator(grpcRegistrator)
            , m_pool(google::protobuf::DescriptorPool::generated_pool())
            , m_typeResolver(google::protobuf::util::NewTypeResolverForDescriptorPool(NWebGrpc::typeGoogleApisComPrefix, m_pool))
        {
            INIT_LOGGER_HOLDER;
        }

        void Post(const PRequest req, PResponse resp) override
        {
            std::vector<uint8_t> body(req->GetBody());
            std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

            Json::Value json;
            Json::CharReaderBuilder reader;
            std::string err;
            try
            {
                std::istringstream is(bodyContent);
                if (!Json::parseFromStream(reader, is, &json, &err))
                {
                    _err_ << "GRPC plugin: Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                    return Error(resp, IResponse::BadRequest);
                }

                if (!json.isMember(METHOD_PARAMETER) || !json.isMember(DATA_PARAMETER))
                {
                    _err_ << "Parameters 'method' and 'data' is required";
                    return Error(resp, IResponse::BadRequest);
                }
            }
            catch (...)
            {
                _err_ << "/grpc: Process JSON error on entity " << json.toStyledString();
                return Error(resp, IResponse::BadRequest);
            }

            const google::protobuf::MethodDescriptor* md = m_pool->FindMethodByName(json[METHOD_PARAMETER].asString());
            if (nullptr == md)
            {
                _err_ << "Method " << json[METHOD_PARAMETER].asString() << " is not registered";
                Error(resp, IResponse::InternalServerError);
                return;
            }

            const google::protobuf::ServiceDescriptor* sd = md->service();

            NWebGrpc::PGrpcService svc = m_grpcRegistrator->GetService(sd->name());
            if (!svc)
            {
                _err_ << "GRPC service " << sd->name() << " is not registered";
                Error(resp, IResponse::InternalServerError);
                return;
            }

            google::protobuf::Message* reqMsg = nullptr;

            const google::protobuf::Descriptor* reqType = md->input_type();

            const google::protobuf::Message* reqProto = m_messageFactory.GetPrototype(reqType);
            if (nullptr != reqProto)
            {
                reqMsg = reqProto->New();
            }

            auto status = google::protobuf::util::JsonStringToMessage(json[DATA_PARAMETER].toStyledString(), reqMsg);
            if (!status.ok())
            {
                _err_ << "Cannot convert json to type " << reqType->full_name();
                Error(resp, IResponse::InternalServerError);
                return;
            }

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            svc->Call(md->name(), m_typeResolver, req, resp, m_grpcManager, metaCredentials, reqMsg, NWebGrpc::typeGoogleApisComPrefix);
        }

    private:
        const NWebGrpc::PGrpcManager m_grpcManager;
        const NWebGrpc::PGrpcRegistrator m_grpcRegistrator;

        google::protobuf::DynamicMessageFactory m_messageFactory;
        const google::protobuf::DescriptorPool* m_pool;
        NWebGrpc::PTypeResolver m_typeResolver;
    };
}

namespace NHttp
{
    IServlet* CreateGrpcServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const NWebGrpc::PGrpcRegistrator grpcRegistrator)
    {
        return new CGrpcContentImpl(GET_LOGGER_PTR, grpcManager, grpcRegistrator);
    }
}
