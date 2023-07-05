set(TARGET MMCoding)
project(${TARGET})

set(HWCodecs_SOURCE_GROUP
    ./HWCodecs/BaseHWDecoderTransform.cpp
    ./HWCodecs/DecoderPerformance.cpp
    ./HWCodecs/DecoderPerformance.h
    ./HWCodecs/HWCodecsDeclarations.h
    ./HWCodecs/HWDevicePool.cpp
    ./HWCodecs/HWDevicePool.h
    ./HWCodecs/HWDevicePoolSpecific.cpp
    ./HWCodecs/HWUtils.cpp
    ./HWCodecs/HWUtils.h
    ./HWCodecs/IGLRenderer.h
    ./HWCodecs/IHWDecoder.h
    ./HWCodecs/IHWDevice.h
    ./HWCodecs/VideoMemorySample.cpp
    ./HWCodecs/VideoMemorySample.h
    ./HWAccelerated.cpp
    ./HWAccelerated.h
    )

set(HWCodecs_CUDA_SOURCE_GROUP
    ./HWCodecs/CUDA/CudaDevice.cpp
    ./HWCodecs/CUDA/CudaDevice.h
    ./HWCodecs/CUDA/CudaDewarpFilter.cpp
    ./HWCodecs/CUDA/CudaGLRenderer.cpp
    ./HWCodecs/CUDA/CudaGLRenderer.h
    ./HWCodecs/CUDA/CudaProcessor.cpp
    ./HWCodecs/CUDA/CudaProcessor.h
    ./HWCodecs/CUDA/CudaSample.cpp
    ./HWCodecs/CUDA/CudaSample.h
    ./HWCodecs/CUDA/CudaSampleHolder.cpp
    ./HWCodecs/CUDA/CudaSampleHolder.h
    ./HWCodecs/CUDA/CudaSurface.cpp
    ./HWCodecs/CUDA/CudaSurface.h
    ./HWCodecs/CUDA/MemorySampleTransformer.cpp
    ./HWCodecs/CUDA/MemorySampleTransformer.h
    ./HWCodecs/CUDA/NVDecoderGL.cpp
    ./HWCodecs/CUDA/NVDecoderGL.h
    )

set(HWCodecs_CUDA_Shaders_SOURCE_GROUP
    ./HWCodecs/CUDA/Helper.inl
    ./HWCodecs/CUDA/Map.inl
    ./HWCodecs/CUDA/NV12ToARGB.inl
    )

set(HWCodecs_CUDA_SDK_SOURCE_GROUP
    ./HWCodecs/CUDA/SDK/include/dynlink_cuda.h
    ./HWCodecs/CUDA/SDK/include/dynlink_cudaGL.h
    ./HWCodecs/CUDA/SDK/include/dynlink_cuda_cuda.h
    ./HWCodecs/CUDA/SDK/include/dynlink_cuviddec.h
    ./HWCodecs/CUDA/SDK/include/dynlink_nvcuvid.h
    ./HWCodecs/CUDA/SDK/include/dynlink_nvrtc.h
    ./HWCodecs/CUDA/SDK/src/dynlink_cuda.cpp
    ./HWCodecs/CUDA/SDK/src/dynlink_nvcuvid.cpp
    )

set(HWCodecs_QuickSync_SOURCE_GROUP
    ./HWCodecs/QuickSync/BaseSampleTransformer.cpp
    ./HWCodecs/QuickSync/BaseSampleTransformer.h
    ./HWCodecs/QuickSync/D3DSample.cpp
    ./HWCodecs/QuickSync/D3DSample.h
    ./HWCodecs/QuickSync/D3DWrapper.cpp
    ./HWCodecs/QuickSync/D3DWrapper.h
    ./HWCodecs/QuickSync/DXAllocator.cpp
    ./HWCodecs/QuickSync/DXAllocator.h
    ./HWCodecs/QuickSync/DXGLRenderer.cpp
    ./HWCodecs/QuickSync/DXGLRenderer.h
    ./HWCodecs/QuickSync/HiddenDXDevice.cpp
    ./HWCodecs/QuickSync/HiddenDXDevice.h
    ./HWCodecs/QuickSync/QSDecoderD3D.cpp
    ./HWCodecs/QuickSync/QSDecoderD3D.h
    ./HWCodecs/QuickSync/QSDevice.cpp
    ./HWCodecs/QuickSync/QSDevice.h
    ./HWCodecs/QuickSync/QSSharedDecoder.cpp
    ./HWCodecs/QuickSync/QSSharedDecoder.h
    ./HWCodecs/QuickSync/SampleTransformerD3D.cpp
    )

ngp_add_library(
    TARGET ${TARGET}
    TYPE SHARED
    DESCRIPTION "Multi-media coding components"
    COMMENTS ""
    SOURCES
    ${HWCodecs_SOURCE_GROUP}
    ${HWCodecs_CUDA_SOURCE_GROUP}
    ${HWCodecs_CUDA_SDK_SOURCE_GROUP}
    ${HWCodecs_CUDA_Shaders_SOURCE_GROUP}
    ${HWCodecs_QuickSync_SOURCE_GROUP}
    ./AreaFilter.cpp
    ./AudioCodec.cpp
    ./AugmentedSource.cpp
    ./AugmentedSourceFactory.cpp
    ./AugmentedSourceFactory.h
    ./BIMWCodec.cpp
    ./BIMWCodec.h
    ./BufferFilter.cpp
    ./BurnSubtitleFilter.cpp
    ./BurnTextFilter.cpp
    ./Callback.h
    ./Codec.cpp
    ./CoordinateTransform.h
    ./DecimationFilter.cpp
    ./DewarpFilter.cpp
    # ./ExynosVideoCodec.cpp # ?
    # ./ExynosVideoCodec.h # ?
    # ./ExynosVideoFilter.cpp # ?
    ./FFmpegAllocator.h
    ./FFMPEGCodec.cpp
    ./FFMPEGCodec.h
    ./FFmpegFilter.cpp
    ./FFmpegFilter.h
    ./FFmpegMutex.cpp
    ./FFmpegMutex.h
    ./FrameBuilder.cpp
    ./FrameBuilder.h
    ./FrameGeometryAdvisor.h
    ./FrameGeometryAdvisorImpl.h
    ./FrameInfo.h
    ./FrameInfoH264.cpp
    ./FrameInfoH265.cpp
    ./FrameInfoJPEG.cpp
    ./FrameInfoJPEG2000.cpp
    ./FrameInfoMPEG2.cpp
    ./FrameInfoVP89.cpp
    ./FrameLagHandler.h
    # ./FreetypeLib.cpp # ?
    # ./FreetypeLib.h # ?
    ./GateFilter.h
    ./GetBits.h
    ./HooksPluggable.h
    # ./HWCodecs/Ascend/Decoder.cpp # ?
    # ./HWCodecs/Ascend/Decoder.h # ?
    # ./HWCodecs/Ascend/Device.cpp # ?
    # ./HWCodecs/Ascend/Device.h # ?
    ./ImageTransformerBase.h
    ./ImageUtils.cpp
    ./ImmerVisionDewarpFilter.cpp
    ./Initialization.cpp
    ./Initialization.h
    ./ITVCodec.cpp
    ./ITVCodec.h
    ./LinkFFmpeg.h
    ./MaskFilter.cpp
    ./MMCodingExports.h
    ./MPEG4Allocator.h
    ./MPEG4Codec.cpp
    ./OverlayFilter.cpp
    ./OVSCodec.cpp
    ./OVSCodec.h
    ./PixelMaskFilter.cpp
    ./PixelMaskProvider.cpp
    ./Points.h
    ./ScaleFilter.cpp
    ./SDLttfLib.cpp
    ./SDLttfLib.h
    ./SessionWatcher.h
    ./ShapeMaskProvider.cpp
    ./SieveFilter.cpp
    ./SizeTransformer.cpp
    ./TrackOverlayProvider.cpp
    ./TrafficFilter.cpp
    ./Transforms.h
    ./TweakableFilter.cpp
    ./TweakableFilter.h
    ./TweakableFilterImpl.h
    ./UtcTimeToLocal.cpp
    ./UtcTimeToLocal.h
    ./VideoDecoder.cpp
    ./WatermarkFilter.cpp
    ./WXWLCodec.cpp
    ./WXWLCodec.h
)

source_group("HWCodecs" FILES ${HWCodecs_SOURCE_GROUP})
source_group("HWCodecs/CUDA" FILES ${HWCodecs_CUDA_SOURCE_GROUP})
source_group("HWCodecs/CUDA/SDK" FILES ${HWCodecs_CUDA_SDK_SOURCE_GROUP})
source_group("HWCodecs/CUDA/Shaders" FILES ${HWCodecs_CUDA_Shaders_SOURCE_GROUP})
source_group("HWCodecs/QuickSync" FILES ${HWCodecs_QuickSync_SOURCE_GROUP})

ngp_link_library(TARGET ${TARGET} PRIVATE d3d11 d3dcompiler dxgi imm32 opengl32 setupapi version)
ngp_use_sdk_module(TARGET ${TARGET} MODULE FREETYPE PRIVATE freetype)
ngp_use_sdk_module(TARGET ${TARGET} MODULE GLM)
ngp_use_sdk_module(TARGET ${TARGET} MODULE INTELMEDIA PRIVATE libmfx_vs2015)
ngp_use_sdk_module(TARGET ${TARGET} MODULE IVE PRIVATE IMV1)
ngp_use_sdk_module(TARGET ${TARGET} MODULE JPEGTURBO PRIVATE jpeg-static)
ngp_use_sdk_module(TARGET ${TARGET} MODULE PNG PUBLIC libpng16_static)
ngp_use_sdk_module(TARGET ${TARGET} MODULE SDL2 PRIVATE SDL2)
ngp_use_sdk_module(TARGET ${TARGET} MODULE SDL2TTF PRIVATE SDL2_ttf)
ngp_use_sdk_module(TARGET ${TARGET} MODULE WAVELIB PRIVATE WaveLib3dll WaveLib6dll)
ngp_use_sdk_module(TARGET ${TARGET} MODULE ZLIB PUBLIC zlibstatic)
ngp_use_sdk_module(TARGET ${TARGET} MODULE JSONCPP PRIVATE jsoncpp)
ngp_use_int_module(${TARGET} OVCodec MPEG4 MMIDL mmss)

target_compile_options(
    ${TARGET}
    PRIVATE /sdl-
    PRIVATE /wd4251
    PRIVATE /wd4275
    )

target_compile_definitions(
    ${TARGET}
    PRIVATE HAVE_LIBC
    PRIVATE MMCODING_EXPORTS
    )

target_include_directories(
    ${TARGET}
    PRIVATE .
    PRIVATE ./HWCodecs/CUDA/SDK/include
    PRIVATE ${NGP_HOME}/ITV
    PRIVATE ${NGP_HOME}/ITV/itv-sdk
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/Notification
    )

target_link_options(
    ${TARGET}
    PRIVATE /NODEFAULTLIB:libcmt
    )

# TESTS
ngp_add_test(
    UT_TARGET ${TARGET}
    SOURCES
    ./HWCodecs/HWUtils.cpp
    ./HWCodecs/HWUtils.h
    ./tests/Jpeg2000TestData.h
    ./tests/TestHWDecoder.cpp
    ./tests/TestJPEG2000FrameInfo.cpp
    ./tests/TestPlugin.cpp
    ../tests/Samples.cpp
    ../tests/Samples.h
)

target_compile_definitions(
    ${UT_TARGET}
    PRIVATE HAVE_LIBC
    PRIVATE MMCODING_EXPORTS
    )

target_include_directories(
    ${UT_TARGET}
    PRIVATE ${NGP_HOME}/mmss
    PRIVATE ${NGP_HOME}/mmss/MMCoding
    PRIVATE ${NGP_HOME}/Notification
    )
