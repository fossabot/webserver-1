#ifndef HTTP_PLUGIN_H__
#define HTTP_PLUGIN_H__

#include <ace/OS.h>
#include <CorbaHelpers/Container.h>
#include <HttpServer/HttpServer.h>
#include "HttpPluginExports.h"
#include "UrlBuilder.h"
#include "RightsChecker.h"
#include "GrpcHelpers.h"
#include "Gstreamer.h"
#include "VideoSourceCache.h"
#include "Tokens.h"
#include "StatisticsCache.h"
#include "WebServerConfig.h"

namespace NCorbaHelpers
{
    class IContainer;
}

namespace NHttp
{
    HTTPPLUGIN_DECLSPEC IServlet* CreateRedirectionServlet(const std::string &);
    HTTPPLUGIN_DECLSPEC IServlet* CreateCommonServlet(NCorbaHelpers::IContainer*, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateHostServlet(NCorbaHelpers::IContainer*);
    HTTPPLUGIN_DECLSPEC IServlet* CreateArchiveServlet(NCorbaHelpers::IContainerNamed*, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rightsChecker,
        const std::string& hlsContentPath, UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache);
    HTTPPLUGIN_DECLSPEC IServlet* CreateVideoServlet(NCorbaHelpers::IContainer*, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker,
        const std::string& hlsContentPath, UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache);
    HTTPPLUGIN_DECLSPEC IServlet* CreateLiveSnapshotServlet(NCorbaHelpers::IContainer*, const NPluginUtility::PRigthsChecker);
    HTTPPLUGIN_DECLSPEC IServlet* CreateEventServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker);
    HTTPPLUGIN_DECLSPEC IServlet* CreateTelemetryServlet(NCorbaHelpers::IContainer*, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker);
    HTTPPLUGIN_DECLSPEC IServlet* CreateExportServlet(NCorbaHelpers::IContainer*, const std::string& exportContentPath,
        const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rightsChecker);
    HTTPPLUGIN_DECLSPEC IServlet* CreateAutoSearchServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateFaceSearchServlet(NCorbaHelpers::IContainer* c);
    HTTPPLUGIN_DECLSPEC IServlet* CreateVmdaSearchServlet(NCorbaHelpers::IContainer*, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateStrangerSearchServlet(NCorbaHelpers::IContainerNamed*);
    HTTPPLUGIN_DECLSPEC IServlet* CreateFarStatusServlet(NCorbaHelpers::IContainer*);
    HTTPPLUGIN_DECLSPEC IServlet* CreateGroupServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateMacroServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateArchiveListServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateArchiveHealthServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateArchiveCalendarServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateEventStreamServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet*
        CreateCloudServlet(NCorbaHelpers::IContainer* c, const char* globalObjectName, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateAuditServlet(NCorbaHelpers::IContainer* c, const NPluginUtility::PRigthsChecker);
    HTTPPLUGIN_DECLSPEC IServlet* CreateDetectorServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateWebSocketServlet(NCorbaHelpers::IContainer* c,
                                                         UrlBuilderSP rtspUrls,
                                                         const NWebGrpc::PGrpcManager grpcManager,
                                                         const NPluginUtility::PRigthsChecker rightsChecker,
                                                         NHttp::PVideoSourceCache videoSourceCache,
                                                         const NHttp::PStatisticsCache& statisticsCache,
                                                         NHttp::SConfig& cfg);
    HTTPPLUGIN_DECLSPEC IServlet* CreateLayoutServlet(NCorbaHelpers::IContainer* c, const NPluginUtility::PRigthsChecker rightsChecker);
    HTTPPLUGIN_DECLSPEC IServlet* CreateCameraListServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateVideoAccessPointServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateCameraEventServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, UrlBuilderSP rtspUrls);
    HTTPPLUGIN_DECLSPEC IServlet* CreateHeatMapSearchServlet(NCorbaHelpers::IContainer*, const NWebGrpc::PGrpcManager grpcManager);
    HTTPPLUGIN_DECLSPEC IServlet* CreateDiscoverCamerasServlet(NCorbaHelpers::IContainer*, const NWebGrpc::PGrpcManager grpcManager);

    HTTPPLUGIN_DECLSPEC IServlet* CreateGrpcServlet(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager, const NWebGrpc::PGrpcRegistrator grpcRegistrator);

    HTTPPLUGIN_DECLSPEC IServlet* CreateRtspStatServlet(DECLARE_LOGGER_ARG, const NPluginHelpers::PGstRTSPServer rtspServer);

    HTTPPLUGIN_DECLSPEC IInterceptor* CreateBearerAuthInterceptor(DECLARE_LOGGER_ARG, PInterceptor next = PInterceptor());
    HTTPPLUGIN_DECLSPEC IInterceptor* CreateConnectionPolicyInterceptor(NCorbaHelpers::IContainer* c, const char* globalObjectName);
    HTTPPLUGIN_DECLSPEC IAccessChecker* CreateRightsInterceptor(DECLARE_LOGGER_ARG, const NWebGrpc::PGrpcManager grpcManager);

    class IStatisticsHandler: public virtual IServlet, public virtual IInterceptor
    {
    };
    HTTPPLUGIN_DECLSPEC IStatisticsHandler*
        CreateStatisticsHandler(NCorbaHelpers::IContainer*,
                                const NWebGrpc::PGrpcManager grpcManager,
                                PStatisticsCache& statisticsCache);

    class IProxyServlet : public virtual IServlet, public virtual IInterceptor
    {
    };

    HTTPPLUGIN_DECLSPEC IProxyServlet* CreateProxyServlet(DECLARE_LOGGER_ARG, int port, const std::string& target);
}

#endif // HTTP_PLUGIN_H__
