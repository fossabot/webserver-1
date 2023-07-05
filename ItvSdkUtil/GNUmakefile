TARGET_OBJ  = ItvSdkUtil

DEPEND_DIRS = Primitives/Logging mmss mmss/MMCoding ITV/ItvFramework mmss/MMIDL \
              mmss/DeviceInfo BL/bl.grpc.proto

INCLUDE_PATH = Primitives mmss mmss/MMClient mmss/MMServer \
               Notification/Notification_IDL Notification CIS \
               ITV BL BL/bl.grpc.proto

DEFINITIONS  = ITVSDKUTILES_EXPORTS

INT_LIBS = Logging ItvFramework MMSS MMCoding DeviceInfo blgrpcproto
EXT_LIBS = $(TAO_COMMON_LIBS) fmt jsoncpp
BOOST_LIBS = system date_time

OBJECTS = AppDataArgsAdjuster \
          BaseEventArgsAdjuster \
          BoneDetectorArgsAdjuster \
          CAudioBuffer \
          CAudioBufferMpeg \
          CAudioG7xxBuffer \
          CAudioPcmBuffer \
          CBlobFrameFactory \
          CCompositeBuffer \
          CCompressedBuffer \
          CDetectorEventFactory \
          CDetectorEventRaiser \
          CFrameFactory \
          CLogger \
          CPlanarBuffer \
          EventArgsAdjuster \
          FaceTrackerImpl \
          GlobalTrackerArgsAdjuster \
          ItvSdkUtil \
          MediaFormatDictionary \
          TemperatureDetectorArgsAdjuster

CXXFLAGS = -Werror

UT_OBJECTS = tests/TestRepoLoader \
    tests/FrameFactoryTest \
    tests/MediaFormatDictionaryTest

UT_DEPEND_DIRS = Primitives/Logging mmss/DeviceInfo mmss
UT_INCLUDE_PATH = ITV mmss Primitives
UT_INT_LIBS = Logging DeviceInfo MMSS
UT_BOOST_LIBS = filesystem system thread

include ../../Makefile.common
