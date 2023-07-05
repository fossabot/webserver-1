#include "GrpcHelpers.h"

#include <boost/format.hpp>

#include <GrpcHelpers/GrpcClient.h>
#include <GrpcHelpers/GrpcHelpers.h>
#include <CorbaHelpers/Envar.h>
#include <CorbaHelpers/Reactor.h>
#include <SecurityManager/NodeKeys.h>

#include <RawGrpcPlugin.h>

namespace
{
    std::shared_ptr<grpc::ChannelCredentials> getClientCredentials()
    {
        grpc::SslCredentialsOptions ssl_opts;
        ssl_opts.pem_root_certs = NSecurityManager::GetRootCertificate();
        auto creds = grpc::SslCredentials(ssl_opts);
        return creds;
    }

    class CGrpcManager : public NWebGrpc::IGrpcManager
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CGrpcManager(DECLARE_LOGGER_ARG)
            : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
            , m_grpcClient(NGrpcHelpers::CreateGrpcClientBase(GET_LOGGER_PTR, m_reactor->GetIO()))
        {
            INIT_LOGGER_HOLDER;
        }

        void Start() override
        {
            grpc::ChannelArguments args;
            args.SetSslTargetNameOverride(NSecurityManager::GetServerCommonName());
            std::string channelAddress(boost::str(boost::format("localhost:%1%") % NCorbaHelpers::CEnvar::NativeBLPort()));
            std::shared_ptr<grpc::Channel> channel = std::shared_ptr<grpc::Channel>(grpc::CreateCustomChannel(channelAddress, getClientCredentials(), args));
            m_nblChannel = std::make_shared<NGrpcHelpers::Channel>(channel);

            m_grpcClient->Start();
        }

        void Stop() override
        {
            m_grpcClient->Shutdown();
        }

        std::shared_ptr<grpc::CompletionQueue> GetCompletionQueue() const override
        {
            return m_grpcClient->GetCompletionQueue();
        }

        NGrpcHelpers::Cancellable::Token GetToken() override
        {
            return m_grpcClient->GetCancellationToken();
        }

        NGrpcHelpers::PChannel GetChannel() const override
        {
            return m_nblChannel;
        }

        NGrpcHelpers::PChannel GetExternalChannel(const NGrpcHelpers::YieldContext&) const override
        {
            return m_nblChannel;
        }

    private:
        NGrpcHelpers::PChannel m_nblChannel;

        NCorbaHelpers::PReactor m_reactor;
        std::shared_ptr<NGrpcHelpers::IGrpcClientBase> m_grpcClient;
    };

    class CGrpcRegistrator : public NWebGrpc::IGrpcRegistrator
    {
        DECLARE_LOGGER_HOLDER;

    public:
        CGrpcRegistrator(DECLARE_LOGGER_ARG)
        {
            INIT_LOGGER_HOLDER;
        }

        void RegisterService(const std::string& serviceName, NWebGrpc::PGrpcService svc) override
        {
            m_services.insert(std::make_pair(serviceName, svc));
        }

        NWebGrpc::PGrpcService GetService(const std::string& serviceName) const override
        {
            NWebGrpc::PGrpcService svc;
            std::map<std::string, NWebGrpc::PGrpcService>::const_iterator it = m_services.find(serviceName);
            if (m_services.end() != it)
                svc = it->second;

            return svc;
        }

    private:
        std::map<std::string, NWebGrpc::PGrpcService> m_services;
    };
}

namespace NWebGrpc
{
    PGrpcManager CreateGrpcManager(DECLARE_LOGGER_ARG)
    {
        return PGrpcManager(new CGrpcManager(GET_LOGGER_PTR));
    }

    PGrpcRegistrator CreateGrpcRegistrator(DECLARE_LOGGER_ARG)
    {
        PGrpcRegistrator grpcRegistrator = std::make_shared<CGrpcRegistrator>(GET_LOGGER_PTR);
        RegisterAcfaService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterArchiveService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterArchiveVolumeService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterAuditEventInjector(GET_LOGGER_PTR, grpcRegistrator);
        RegisterAuthenticationService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterBackupSourceService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterCloudService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterConfigurationService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterConfigurationManager(GET_LOGGER_PTR, grpcRegistrator);
        RegisterDevicesCatalog(GET_LOGGER_PTR, grpcRegistrator);
        RegisterDynamicParametersService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterDiscoveryService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterDomainManager(GET_LOGGER_PTR, grpcRegistrator);
        RegisterDomainService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterFileSystemBrowser(GET_LOGGER_PTR, grpcRegistrator);
        RegisterGroupManager(GET_LOGGER_PTR, grpcRegistrator);
        RegisterHeatMapService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterLayoutManager(GET_LOGGER_PTR, grpcRegistrator);
        RegisterLicenseService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterLogicService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterMapService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterDomainNotifier(GET_LOGGER_PTR, grpcRegistrator);
        RegisterSecurityService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterStateControlService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterStatisticService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterEventHistoryService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterTagAndTrackService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterTelemetryService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterTimeZoneManager(GET_LOGGER_PTR, grpcRegistrator);
        RegisterVMDAService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterRealtimeRecognizerService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterVideowallService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterExportService(GET_LOGGER_PTR, grpcRegistrator);
        RegisterEMailNotifier(GET_LOGGER_PTR, grpcRegistrator);
        RegisterGSMNotifier(GET_LOGGER_PTR, grpcRegistrator);

        return grpcRegistrator;
    }
}
