set(TARGET DeviceIpint_3)
project(${TARGET})

ngp_add_library(
    TARGET ${TARGET}
    TYPE SHARED
    DESCRIPTION "Device driver pack integration object for NGP"
    COMMENTS ""
    SOURCES
    ./../PTZCalibration/PTZCalibrationImpl.cpp
    ./../PTZCalibration/PTZCalibrationImpl.h
    ./../PTZCalibration/PTZCalibrationlib.cpp
    ./../PTZCalibration/PTZCalibrationlib.h
    ./AsyncActionHandler.h
    ./AsyncPushSinkHelper.h
    ./AudioDestination.cpp
    ./AudioDestination.h
    ./CachedHistoryRequester.cpp
    ./CachedHistoryRequester.h
    ./CachedHistoryRequester.inl
    ./CAdaptiveSource.cpp
    ./CAdaptiveSource.h
    ./CAsyncChannel.h
    ./CAudioSource.cpp
    ./CAudioSource.h
    ./CChannel.cpp
    ./CChannel.h
    ./CDiscovery.cpp
    ./CDiscovery.h
    ./CIoPanel.cpp
    ./CIoPanel.h
    ./CIpInt30.cpp
    ./CIpInt30.h
    ./CRelayStateControl.h
    ./CTelemetry.cpp
    ./CTelemetry.h
    ./CTextEventSource.cpp
    ./CTextEventSource.h
    ./CVideoAnalytics.cpp
    ./CVideoAnalytics.h
    ./CVideoSource.cpp
    ./CVideoSource.h
    ./CVisualElementContext.cpp
    ./CVisualElementContext.h
    ./DeviceControl.cpp
    ./DeviceControl.h
    ./DeviceInfo.cpp
    ./DeviceInfo.h
    ./DeviceInformation.cpp
    ./DeviceInformation.h
    ./DeviceIpint.cpp
    ./DeviceIpint.h
    ./DeviceIpintApp.cpp
    ./DeviceIpintModule.cpp
    ./DeviceIpint_Exports.h
    ./DeviceNode.cpp
    ./DeviceNode.h
    ./DeviceSettings.h
    ./EmbeddedStorage.cpp
    ./EmbeddedStorage.h
    ./FakeDeviceManager.cpp
    ./FakeDeviceManager.h
    ./IIPManager3.h
    ./IObjectsGroupHolder.h
    ./IpintDestroyer.h
    ./LiveContext.cpp
    ./LiveContext.h
    ./Notify.cpp
    ./Notify.h
    ./ObjectTracker.cpp
    ./Observer.cpp
    ./Observer.h
    ./ObserverServant.cpp
    ./ParamContext.h
    ./ParamIterator.h
    ./PlaybackControl.cpp
    ./PlaybackControl.h
    ./PlaybackControl.inl
    ./PositionPredictor.cpp
    ./PositionPredictor.h
    ./PullStyleSourceImpl.h
    ./PullToPushAdapter.h
    ./RecordingPlayback.cpp
    ./RecordingPlayback.h
    ./RecordingPlaybackFactory.h
    ./RecordingSearch.cpp
    ./RecordingSearch.h
    ./RecordingsInfoRequester.cpp
    ./RecordingsInfoRequester.h
    ./sdkHelpers.h
    ./SinkEndpointImpl.cpp
    ./SinkEndpointImpl.h
    ./StorageDataTypes.h
    ./StorageEndpoint.cpp
    ./StorageEndpoint.h
    ./StorageEndpointImpl.cpp
    ./StorageEndpointImpl.h
    ./StorageSource.cpp
    ./StorageSource.h
    ./TelemetryHelper.cpp
    ./TelemetryHelper.h
    ./TimeStampHelpers.h
    ./Utility.cpp
    ./Utility.h
)

ngp_add_dictionary_sources(${TARGET} ./DeviceIpint.xml)

ngp_use_int_module(${TARGET} DeviceManager Grabber MMClient)

target_compile_definitions(
    ${TARGET}
    PRIVATE DEVICEIPINT_EXPORTS
    )

target_include_directories(
    ${TARGET}
    PRIVATE ${NGP_HOME}/CIS
    PRIVATE ${NGP_HOME}/ITV
    PRIVATE ${NGP_HOME}/ITV/itv-sdk
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/Notification
    )

# TESTS
ngp_add_test(
    UT_TARGET ${TARGET}
    SOURCES
    ./CachedHistoryRequester.cpp
    ./CachedHistoryRequester.h
    ./CChannel.cpp
    ./CChannel.h
    ./EmbeddedStorage.cpp
    ./EmbeddedStorage.h
    ./Notify.cpp
    ./Notify.h
    ./PositionPredictor.cpp
    ./PositionPredictor.h
    ./SinkEndpointImpl.cpp
    ./SinkEndpointImpl.h
    ./Utility.cpp
    ./Utility.h
    ./tests/FakeIpintDevice.cpp
    ./tests/MockRecordingSearch.cpp
    ./tests/MockRecordingSearch.h
    ./tests/MockStorageDevice.cpp
    ./tests/TestCachedHistoryRequester.cpp
    # ./tests/TestPlaybackControl.cpp # ?
    ./tests/TestPositionPredictor.cpp
    ./tests/TestPullToPushStyleAdapter.cpp
    ./tests/TestRecordingPlaybackFactory.cpp
    ./tests/TestRecordingSearch.cpp
    ./tests/TestRecordingsInfoRequester.cpp
    ./tests/TestSinkEndpointImpl.cpp
    ./tests/TestStorageSource.cpp
    ./tests/TestUtils.cpp
    ./tests/TestUtils.h
    ./tests/TestCTelemetry.cpp
    ./ObjectTracker.cpp
    ./Observer.cpp
    ./ObserverServant.cpp
    ./TelemetryHelper.cpp
    ./TelemetryHelper.h
    ./CTelemetry.h
    ./CTelemetry.cpp
    ./../PTZCalibration/PTZCalibrationImpl.cpp
    ./../PTZCalibration/PTZCalibrationImpl.h
    ./../PTZCalibration/PTZCalibrationlib.cpp
    ./../PTZCalibration/PTZCalibrationlib.h
)

target_compile_definitions(
    ${UT_TARGET}
    PRIVATE DEVICEIPINT_EXPORTS
    )

target_include_directories(
    ${UT_TARGET}
    PRIVATE ${NGP_HOME}/CIS
    PRIVATE ${NGP_HOME}/ITV
    PRIVATE ${NGP_HOME}/ITV/itv-sdk
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/Notification
    )
