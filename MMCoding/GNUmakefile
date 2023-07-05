TARGET_OBJ  = MMCoding

DEPEND_DIRS  = Primitives/Logging Primitives/Executors mmss Codecs/MPEG4 mmss/MMIDL \
	       ITV/ItvFramework \
               Codecs/DeltaWavletDH Primitives/CorbaHelpers Primitives/ChaosMonkey

INCLUDE_PATH = mmss Primitives ITV mmss/MMCoding Notification mmss/MMCoding/HWCodecs/CUDA/SDK/include

INT_LIBS = Logging Executors MMSS MPEG4 OVCodec CorbaHelpers ChaosMonkey ItvFramework
EXT_LIBS = $(AVFORMAT) $(AVCODEC) $(AVUTIL) $(AVSWSCALE) $(AVSWRESAMPLE) $(TAO_COMMON_LIBS) dl \
           bz2 expat GL

EXT_LIBS_x86_64 = mfx va va-glx va-drm

BOOST_LIBS = system thread regex filesystem coroutine context

EXT_LIBS_STATIC = SDL2 cairo freetype png jpeg pixman-1 fontconfig z jsoncpp

DEFINITIONS = MMCODING_EXPORTS __STDC_CONSTANT_MACROS BOOST_COROUTINES_NO_DEPRECATION_WARNING

OBJECTS_HuaweiAscend = \
          HWCodecs/Ascend/Decoder \
          HWCodecs/Ascend/Device

OBJECTS = AudioCodec     \
          AugmentedSource \
          AugmentedSourceFactory \
          BufferFilter   \
          BurnTextFilter \
          DecimationFilter \
          DewarpFilter \
          TweakableFilter \
          BIMWCodec      \
          Codec          \
          FFMPEGCodec    \
          FFmpegMutex    \
          FrameBuilder   \
          FrameInfoH264  \
          FrameInfoH265  \
          FrameInfoJPEG  \
          FrameInfoJPEG2000 \
          FrameInfoMPEG2  \
          FrameInfoVP89  \
          ImageUtils     \
          Initialization \
          MPEG4Codec     \
          OVSCodec       \
          ScaleFilter    \
          VideoDecoder   \
          AreaFilter     \
          MaskFilter     \
          ShapeMaskProvider  \
          SieveFilter    \
          BurnSubtitleFilter \
          HWAccelerated \
          FreetypeLib \
          FFmpegFilter \
          UtcTimeToLocal \
          WatermarkFilter \
          SizeTransformer \
          TrafficFilter \
          TrackOverlayProvider \
          OverlayFilter \
          PixelMaskProvider \
          PixelMaskFilter \
          HWCodecs/DecoderPerformance \
          HWCodecs/BaseHWDecoderTransform \
          HWCodecs/HWDevicePool \
          HWCodecs/HWDevicePoolSpecific \
          HWCodecs/HWUtils \
          HWCodecs/VideoMemorySample \
          HWCodecs/CUDA/CudaDevice \
          HWCodecs/CUDA/CudaGLRenderer \
          HWCodecs/CUDA/CudaProcessor \
          HWCodecs/CUDA/CudaSample \
          HWCodecs/CUDA/CudaSampleHolder \
          HWCodecs/CUDA/CudaSurface \
          HWCodecs/CUDA/MemorySampleTransformer \
          HWCodecs/CUDA/NVDecoderGL \
          HWCodecs/CUDA/SDK/src/dynlink_cuda \
          HWCodecs/CUDA/SDK/src/dynlink_nvcuvid

OBJECTS_x86_64 = \
          HWCodecs/QuickSync/BaseSampleTransformer \
          HWCodecs/QuickSync/QSDecoderD3D \
          HWCodecs/QuickSync/QSDevice \
          HWCodecs/QuickSync/QSDeviceVA \
          HWCodecs/QuickSync/QSSharedDecoder \
          HWCodecs/QuickSync/SampleTransformerVA \
          HWCodecs/QuickSync/VAGLRenderer

OBJECTS_odroidu3 = \
          ExynosVideoCodec \
          ExynosVideoFilter

CXXFLAGS = -Werror


UT_OBJECTS = tests/TestHWDecoder \
             tests/TestJPEG2000FrameInfo \
             tests/TestPlugIn \
             ../tests/Samples \
             HWCodecs/HWUtils


UT_INCLUDE_PATH = mmss mmss/MMCoding Notification Primitives

UT_INT_LIBS = Logging MMSS CorbaHelpers ChaosMonkey
UT_BOOST_LIBS = filesystem thread system

include ../../Makefile.common
