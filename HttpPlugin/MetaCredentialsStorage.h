#pragma once

#include "CommonUtility.h"


namespace NHttp
{
    struct IMetaCredentialsStorage
    {
        virtual ~IMetaCredentialsStorage() {}

        virtual NGrpcHelpers::PCredentials GetMetaCredentials() = 0;
        virtual const NHttp::IRequest::AuthSession& GetAuthSession() = 0;
        virtual void UpdateToken(const std::string&, NHttp::DenyCallback_t dc) = 0;
    };

    using PMetaCredentialsStorage = std::shared_ptr<IMetaCredentialsStorage>;

    PMetaCredentialsStorage CreateMetaCredentialsStorage(DECLARE_LOGGER_ARG,
                                                         const NWebGrpc::PGrpcManager grpcManager,
                                                         NGrpcHelpers::PCredentials metaCredentials,
                                                         const PRequest req = PRequest());
}