set(TARGET ItvSdkUtil)
project(${TARGET})

ngp_add_library(
    TARGET ${TARGET}
    TYPE SHARED
    DESCRIPTION "Software development kit for integration objects of NGP"
    COMMENTS ""
    SOURCES
    ./AppDataArgsAdjuster.cpp
    ./AppDataArgsAdjuster.h
    ./BaseEventArgsAdjuster.cpp
    ./BaseEventArgsAdjuster.h
    ./BoneDetectorArgsAdjuster.cpp
    ./BoneDetectorArgsAdjuster.h
    ./CAudioBuffer.cpp
    ./CAudioBuffer.h
    ./CAudioBufferMpeg.cpp
    ./CAudioBufferMpeg.h
    ./CAudioG7xxBuffer.cpp
    ./CAudioG7xxBuffer.h
    ./CAudioPcmBuffer.cpp
    ./CAudioPcmBuffer.h
    ./CBlobFrameFactory.cpp
    ./CCompositeBuffer.cpp
    ./CCompositeBuffer.h
    ./CCompressedBuffer.cpp
    ./CCompressedBuffer.h
    ./CDetectorEventFactory.cpp
    ./CDetectorEventFactory.h
    ./CDetectorEventRaiser.cpp
    ./CDetectorEventRaiser.h
    # ./CEventArgsAdjuster.cpp # ?
    # ./CEventArgsAdjuster.h # ?
    ./CFaceTrackerWrap.h
    ./CFrameFactory.cpp
    ./CFrameFactory.h
    ./CLogger.cpp
    ./CLogger.h
    ./CPlanarBuffer.cpp
    ./CPlanarBuffer.h
    ./DepthDetectorArgsAdjuster.h
    ./EventArgsAdjuster.cpp
    ./EventArgsAdjuster.h
    ./FaceTracker.h
    ./FaceTrackerImpl.cpp
    ./FaceTrackerImpl.h
    ./GlobalTrackerArgsAdjuster.cpp
    ./GlobalTrackerArgsAdjuster.h
    ./ISampleContainer.h
    ./ItvSdkUtil.cpp
    ./ItvSdkUtil.h
    ./MaskEventDataAdjuster.h
    ./MediaBufferImpl.h
    ./MediaFormatDictionary.cpp
    ./MediaFormatDictionary.h
    ./StatisticsSinkImpl.h
    ./TemperatureDetectorArgsAdjuster.cpp
    ./TemperatureDetectorArgsAdjuster.h
)

ngp_use_sdk_module(TARGET ${TARGET} MODULE XERCES PRIVATE xerces-c_static_3)
ngp_use_sdk_module(TARGET ${TARGET} MODULE JSONCPP PRIVATE jsoncpp)
ngp_use_int_module(${TARGET} bl.grpc.proto ItvFramework DeviceInfo MMCoding)

target_compile_definitions(
    ${TARGET}
    PRIVATE ITVSDKUTILES_EXPORTS
    )

target_include_directories(
    ${TARGET}
    PRIVATE ${NGP_HOME}/ITV
    PRIVATE ${NGP_HOME}/ITV/itv-sdk
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/Notification
    )

# TESTS
ngp_add_test(
    UT_TARGET ${TARGET}
    SOURCES
    ./tests/FrameFactoryTest.cpp
    ./tests/MediaFormatDictionaryTest.cpp
    ./tests/TestRepoLoader.cpp
)

target_include_directories(
    ${UT_TARGET}
    PRIVATE ${NGP_HOME}/ITV/itv-sdk
    PRIVATE ${NGP_HOME}/mmss
    )
