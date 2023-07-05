TARGET_OBJ  = DeviceIpint_3

DEPEND_DIRS = Primitives/Logging  Primitives/Lifecycle Primitives/CorbaHelpers Primitives/Crypto Primitives/Executors \
              Notification/Notification_IDL Notification/ORM_IDL Notification/CommonNotificationCpp \
              CIS/InfraServer_IDL CIS/Discovery_IDL  \
              ITV/ItvFramework ITV/DeviceManager Primitives/ChaosMonkey \
              mmss/ItvSdkUtil mmss/DeviceInfo mmss/Grabber mmss/MMCoding mmss/MMClient mmss/MMIDL

INCLUDE_PATH = Primitives Notification CIS ITV mmss

DEFINITIONS = DEVICEIPINT_EXPORTS

INT_LIBS = Logging Lifecycle CorbaHelpers Crypto Executors \
           Notification_IDL ORM_IDL CommonNotificationCpp \
           InfraServer_IDL Discovery_IDL \
           ItvFramework DeviceManager ChaosMonkey \
           ItvSdkUtil DeviceInfo MMSS Grabber MMCoding MMTransport MMIDL MMClient

EXT_LIBS = $(TAO_COMMON_LIBS) TAO_CosTrading ssl crypto fmt jsoncpp
BOOST_LIBS = $(BOOST_COMMON_LIBS) $(BOOST_SERIALIZATION_LIBS) program_options random chrono

OBJECTS = AudioDestination \
          CachedHistoryRequester \
          CAdaptiveSource \
          CAudioSource \
          CChannel \
          CDiscovery \
          CIoPanel \
          CIpInt30 \
          CTelemetry \
          CTextEventSource \
          CVideoAnalytics \
          DeviceNode \
          CVideoSource \
          CVisualElementContext \
          DeviceControl \
          DeviceInfo \
          DeviceInformation \
          DeviceIpintApp \
          DeviceIpint \
          DeviceIpintModule \
          EmbeddedStorage \
          FakeDeviceManager \
          LiveContext \
          Notify \
          ObjectTracker \
          Observer \
          ObserverServant \
          PlaybackControl \
          PositionPredictor \
          RecordingPlayback \
          RecordingSearch \
          RecordingsInfoRequester \
          SinkEndpointImpl \
          StorageEndpoint \
          StorageEndpointImpl \
          StorageSource \
          TelemetryHelper \
          Utility \

OBJECTS += ../PTZCalibration/PTZCalibrationImpl \
           ../PTZCalibration/PTZCalibrationLib \

DICTIONARY_SOURCES = DeviceIpint
CXXFLAGS = -Werror

UT_OBJECTS = \
    tests/TestCachedHistoryRequester \
    tests/TestPullToPushStyleAdapter \
    tests/TestRecordingPlaybackFactory \
    tests/TestRecordingSearch \
    tests/TestRecordingsInfoRequester \
    tests/TestSinkEndpointImpl \
	tests/TestPositionPredictor \
    tests/FakeIpintDevice \
    tests/MockRecordingSearch \
    tests/MockStorageDevice \
    tests/TestStorageSource \
    tests/TestUtils \
    tests/TestCTelemetry \
    CChannel \
    Observer \
    ObserverServant \
    ObjectTracker \
    EmbeddedStorage \
    Notify \
    SinkEndpointImpl \
    PositionPredictor \
    TelemetryHelper \
    Utility \
    ../PTZCalibration/PTZCalibrationImpl \
    ../PTZCalibration/PTZCalibrationLib \
    CTelemetry

UT_INCLUDE_PATH = mmss Primitives Notification ITV CIS
UT_INT_LIBS = Logging Executors MMIDL CorbaHelpers MMSS InfraServer_IDL MMClient Notification_IDL MMTransport 
UT_EXT_LIBS = $(TAO_COMMON_LIBS) fmt jsoncpp 
UT_BOOST_LIBS = $(BOOST_COMMON_LIBS) random chrono

include ../../Makefile.common
