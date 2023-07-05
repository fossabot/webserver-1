set(TARGET HttpPlugin)
project(${TARGET})

ngp_add_library(
    TARGET ${TARGET}
    TYPE SHARED
    DESCRIPTION "HTTP API implementation"
    COMMENTS "REST HTTP API implementation"
    SOURCES
    ${NGP_HOME}/CIS/HttpServer/BasicServlet.cpp
    ${NGP_HOME}/CIS/HttpServer/json_oarchive.cpp
    ./AlertsHandler.cpp
    ./ArchiveCalendarPlugin.cpp
    ./ArchiveHealthPlugin.cpp
    ./ArchiveListPlugin.cpp
    ./ArchivePlugin.cpp
    ./ArchivePlugin.h
    ./ArchivePluginBookmarks.cpp
    ./AuditPlugin.cpp
    ./BearerAuthInterceptor.cpp
    ./BLQueryHelper.cpp
    ./BLQueryHelper.h
    ./CameraEventPlugin.cpp
    ./CameraPlugin.cpp
    ./CloudPlugin.cpp
    ./CommonPlugin.cpp
    ./CommonUtility.cpp
    ./CommonUtility.h
    ./CommonUtilityDetectors.cpp
    ./ConfigHelper.cpp
    ./ConfigHelper.h
    ./ConnectionPolicyInterceptor.cpp
    ./Constants.cpp
    ./Constants.h
    ./DataBuffer.cpp
    ./DataBuffer.h
    ./DataSink.cpp
    ./DataSink.h
    ./DetectorPlugin.cpp
    ./DetectorsHandler.cpp
    ./DiscoverCamerasPlugin.cpp
    ./EventStream.cpp
    ./EventStream.h
    ./ExportPlugin.cpp
    ./FaceRatingPlugin.cpp
    ./GroupPlugin.cpp
    ./GrpcHelpers.cpp
    ./GrpcHelpers.h
    ./GrpcIncludes.h
    ./GrpcPlugin.cpp
    ./GrpcReader.h
    ./GrpcWebProxyGoManager.cpp
    ./GrpcWebProxyGoManager.h
    ./Gstreamer.h
    ./GstreamerManager.cpp
    ./GstreamerMeta.cpp
    ./GstreamerMeta.h
    ./GstreamerMuxer.cpp
    ./GstreamerRTSP.cpp
    ./Hls.cpp
    ./Hls.h
    ./HostPlugin.cpp
    ./HttpParsingHelper.h
    ./HttpPlugin.h
    ./HttpPluginExports.h
    ./LayoutPlugin.cpp
    ./ListQueryHelper.h
    ./LivePlugin.cpp
    ./LiveSnapshotServlet.cpp
    ./MacroPlugin.cpp
    ./MMCache.cpp
    ./MMCache.h
    ./MetaCredentialsStorage.cpp
    ./MetaCredentialsStorage.h
    ./OrmHandler.h
    ./OrmPlugin.cpp
    # ./ParserUtility.cpp # ?
    ./Platform.h
    ./ProtoHelper.h
    ./ProxyServlet.cpp
    ./RedirectionServlet.cpp
    ./RegexUtility.cpp
    ./RegexUtility.h
    ./RightsChecker.cpp
    ./RightsChecker.h
    ./RightsInterceptor.cpp
    ./SearchPlugin.cpp
    ./SearchPlugin.h
    ./SearchPluginAuto.cpp
    ./SearchPluginFace.cpp
    ./SearchPluginHeatMap.cpp
    ./SearchPluginStranger.cpp
    ./SearchPluginVmda.cpp
    ./SendContext.cpp
    ./SendContext.h
    ./Service.cpp
    ./Statistics.cpp
    ./StatisticsCache.cpp
    ./StatisticsCache.h
    ./TelemetryPlugin.cpp
    ./Tokens.cpp
    ./Tokens.h
    ./TrustedIpList.cpp
    ./TrustedIpList.h
    ./UrlBuilder.cpp
    ./UrlBuilder.h
    ./VideoAPServlet.cpp
    ./VideoSourceCache.cpp
    ./VideoSourceCache.h
    ./WebServerConfig.h
    ./WebSocket.cpp
    ./WebSocketSession.cpp
    ./WebSocketSession.h
    ./UriCodec.cpp
    ./UriCodec.h
)

ngp_add_proto_http_sources(
    ${TARGET}
    ${NGP_HOME}/BL/bl.grpc.proto/protos/axxonsoft
    ./bl/acfa/AcfaService.proto
    ./bl/archive/ArchiveSupport.proto
    ./bl/archive/ArchiveVolumeService.proto
    ./bl/audit/Audit.proto
    ./bl/archive/BackupSource.proto
    ./bl/auth/Authentication.proto
    ./bl/cloud/Cloud.proto
    ./bl/config/ConfigurationService.proto
    ./bl/config/DevicesCatalog.proto
    ./bl/config/DynamicParametersService.proto
    ./bl/config/FileSystemBrowser.proto
    # ./bl/config/SharedKeyValueStorage.proto # ?
    ./bl/discovery/Discovery.proto
    ./bl/domain/Domain.Internal.proto
    ./bl/domain/Domain.proto
    ./bl/domain/DomainManager.proto
    # ./bl/events/EventHistory.Internal.proto # ?
    ./bl/events/EventHistory.proto
    # ./bl/events/Events.Internal.proto # ?
    # ./bl/events/Events.proto # ?
    ./bl/events/Notification.Internal.proto
    ./bl/events/Notification.proto
    ./bl/groups/GroupManager.proto
    ./bl/heatmap/HeatMap.proto
    # ./bl/layout/LayoutImagesManager.proto # ?
    ./bl/layout/LayoutManager.proto
    # ./bl/layout/Layouts.Internal.proto # ?
    ./bl/license/LicenseService.proto
    # ./bl/logic/LogicService.Internal.proto # ?
    ./bl/logic/LogicService.proto
    # ./bl/maps/Maps.Internal.proto # ?
    ./bl/maps/MapService.proto
    # ./bl/media/Media.proto # ?
    # ./bl/media/MediaService.proto # ?
    # ./bl/metadata/MetadataService.proto # ?
    # ./bl/mmexport/Export.proto # ?
    # ./bl/mmexport/ExportService.Internal.proto # ?
    ./bl/mmexport/ExportService.proto
    ./bl/maintenance/ConfigurationManager.proto
    # ./bl/primitive/Color.proto # ?
    # ./bl/primitive/Primitives.proto # ?
    ./bl/ptz/TagAndTrack.proto
    ./bl/ptz/Telemetry.proto
    # ./bl/ptz/TelemetryHelper.proto # ?
    ./bl/realtimeRecognizer/RealtimeRecognizer.proto
    # ./bl/realtimeRecognizer/RealtimeRecognizerExternal.proto # ?
    # ./bl/security/GlobalPermissions.proto # ?
    # ./bl/security/GroupsPermissions.proto # ?
    # ./bl/security/ObjectsPermissions.proto # ?
    # ./bl/security/SecurityService.Internal.proto # ?
    ./bl/security/SecurityService.proto
    ./bl/state/StateControl.proto
    ./bl/statistics/Statistics.proto
    ./bl/tz/TimeZonesManager.proto
    # ./bl/videowall/Videowall.Internal.proto # ?
    ./bl/videowall/Videowall.proto
    ./bl/vmda/VMDA.proto
    # ./bl/vmda2/VMDA2.proto # ?
    # ./integration/Driver.proto # ?
    # ./integration/Properties.proto # ?
    ./bl/notifications/EMailNotifier.proto
    ./bl/notifications/GSMNotifier.proto
    )

ngp_use_sdk_module(TARGET ${TARGET} MODULE GSTREAMER PRIVATE gstaudio-1.0 gstreamer-1.0 gstrtp-1.0 gstrtsp-1.0 gstrtspserver-1.0)
ngp_use_sdk_module(TARGET ${TARGET} MODULE GLIB PRIVATE glib-2.0 gobject-2.0)
ngp_use_int_module(${TARGET} HttpServer HeatMap MMExport ONVIFServerGenerated ONVIFServerLib ONVIFServer NotificationService_IDL)

target_compile_definitions(
    ${TARGET}
    PRIVATE BOOST_NETWORK_ENABLE_HTTPS
    PRIVATE FD_SETSIZE=1024
    PRIVATE HTTPPLUGIN_EXPORTS
    )

target_include_directories(
    ${TARGET}
    PRIVATE ./.
    PRIVATE ${NGP_HOME}/BL
    PRIVATE ${NGP_HOME}/CIS
    PRIVATE ${NGP_HOME}/ITV/itv-sdk
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/mmss/ONVIFServer
    PRIVATE ${NGP_HOME}/mmss/ONVIFServer/onvifserverlib
    PRIVATE ${NGP_HOME}/mmss/ONVIFServer/onvifserverlib/AutoGenerated
    PRIVATE ${NGP_HOME}/Notification
    )

# TESTS
ngp_add_test(
    UT_TARGET ${TARGET}
    SOURCES
    ./Tokens.cpp
    ./Tokens.h
    ./URICodec.cpp
    ./URICodec.h
    ./tests/TestGetParam.cpp
    ./tests/TestGStreamer.cpp
    ./tests/TestHttpParsingHelper.cpp
    ./tests/TestRegexUtility.cpp
    ./tests/TestTokens.cpp
    ./tests/TestUtils.cpp
)

target_compile_definitions(
    ${UT_TARGET}
    PRIVATE BOOST_NETWORK_ENABLE_HTTPS
    PRIVATE FD_SETSIZE=1024
    PRIVATE INFRA_SERVER_IMPL_EXPORTS
    )

target_include_directories(
    ${UT_TARGET}
    PRIVATE ${NGP_HOME}/BL
    PRIVATE ${NGP_HOME}/CIS
    PRIVATE ${NGP_HOME}/mmss
    )
