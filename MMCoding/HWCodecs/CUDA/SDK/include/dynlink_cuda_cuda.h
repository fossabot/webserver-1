/*
 * Copyright 1993-2015 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

#ifndef __cuda_cuda_h__
#define __cuda_cuda_h__

#include <stdlib.h>

#ifndef __CUDA_API_VERSION
//11.6
#define __CUDA_API_VERSION 11060
#endif

#ifndef INIT_CUDA_GL
#define INIT_CUDA_GL 1
#endif

#ifndef INIT_CUDA_RTC
#define INIT_CUDA_RTC 1
#endif

/**
 * \defgroup CUDA_DRIVER CUDA Driver API
 *
 * This section describes the low-level CUDA driver application programming
 * interface.
 *
 * @{
 */

/**
 * \defgroup CUDA_TYPES Data types used by CUDA driver
 * @{
 */

/**
 * CUDA API version number
 */
#define CUDA_VERSION __CUDA_API_VERSION

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CUDA device pointer
 */
#if __CUDA_API_VERSION >= 3020

#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64) || defined(__aarch64__)
    typedef unsigned long long CUdeviceptr;
#else
    typedef unsigned int CUdeviceptr;
#endif

#endif /* __CUDA_API_VERSION >= 3020 */

typedef int CUdevice;                                     /**< CUDA device */
typedef struct CUctx_st *CUcontext;                       /**< CUDA context */
typedef struct CUmod_st *CUmodule;                        /**< CUDA module */
typedef struct CUfunc_st *CUfunction;                     /**< CUDA function */
typedef struct CUarray_st *CUarray;                       /**< CUDA array */
typedef struct CUmipmappedArray_st *CUmipmappedArray;     /**< CUDA mipmapped array */
typedef struct CUtexref_st *CUtexref;                     /**< CUDA texture reference */
typedef struct CUsurfref_st *CUsurfref;                   /**< CUDA surface reference */
typedef struct CUevent_st *CUevent;                       /**< CUDA event */
typedef struct CUstream_st *CUstream;                     /**< CUDA stream */
typedef struct CUgraphicsResource_st *CUgraphicsResource; /**< CUDA graphics interop resource */
typedef unsigned long long CUtexObject;                   /**< An opaque value that represents a CUDA texture object */
typedef unsigned long long CUsurfObject;                  /**< An opaque value that represents a CUDA surface object */
typedef struct CUextMemory_st *CUexternalMemory;          /**< CUDA external memory */
typedef struct CUextSemaphore_st *CUexternalSemaphore;    /**< CUDA external semaphore */
typedef struct CUgraph_st *CUgraph;                       /**< CUDA graph */
typedef struct CUgraphNode_st *CUgraphNode;               /**< CUDA graph node */
typedef struct CUgraphExec_st *CUgraphExec;               /**< CUDA executable graph */

typedef struct CUuuid_st                                  /**< CUDA definition of UUID */
{
    char bytes[16];
} CUuuid;

/**
 * Context creation flags
 */
typedef enum CUctx_flags_enum
{
    CU_CTX_SCHED_AUTO          = 0x00, /**< Automatic scheduling */
    CU_CTX_SCHED_SPIN          = 0x01, /**< Set spin as default scheduling */
    CU_CTX_SCHED_YIELD         = 0x02, /**< Set yield as default scheduling */
    CU_CTX_SCHED_BLOCKING_SYNC = 0x04, /**< Set blocking synchronization as default scheduling */
    CU_CTX_BLOCKING_SYNC       = 0x04, /**< Set blocking synchronization as default scheduling \deprecated */
    CU_CTX_MAP_HOST            = 0x08, /**< Support mapped pinned allocations */
    CU_CTX_LMEM_RESIZE_TO_MAX  = 0x10, /**< Keep local memory allocation after launch */
#if __CUDA_API_VERSION < 4000
    CU_CTX_SCHED_MASK          = 0x03,
    CU_CTX_FLAGS_MASK          = 0x1f
#else
    CU_CTX_SCHED_MASK          = 0x07,
    CU_CTX_PRIMARY             = 0x20, /**< Initialize and return the primary context */
    CU_CTX_FLAGS_MASK          = 0x3f
#endif
} CUctx_flags;

/**
 * Stream creation flags
 */
typedef enum CUstream_flags_enum {
    CU_STREAM_DEFAULT = 0x0, /**< Default stream flag */
    CU_STREAM_NON_BLOCKING = 0x1  /**< Stream does not synchronize with stream 0 (the NULL stream) */
} CUstream_flags;

/**
 * Legacy stream handle
 *
 * Stream handle that can be passed as a CUstream to use an implicit stream
 * with legacy synchronization behavior.
 *
 * See details of the \link_sync_behavior
 */
#define CU_STREAM_LEGACY     ((CUstream)0x1)

 /**
  * Per-thread stream handle
  *
  * Stream handle that can be passed as a CUstream to use an implicit stream
  * with per-thread synchronization behavior.
  *
  * See details of the \link_sync_behavior
  */
#define CU_STREAM_PER_THREAD ((CUstream)0x2)

  /**
   * Event creation flags
   */
typedef enum CUevent_flags_enum {
    CU_EVENT_DEFAULT = 0x0, /**< Default event flag */
    CU_EVENT_BLOCKING_SYNC = 0x1, /**< Event uses blocking synchronization */
    CU_EVENT_DISABLE_TIMING = 0x2, /**< Event will not record timing data */
    CU_EVENT_INTERPROCESS = 0x4  /**< Event is suitable for interprocess use. CU_EVENT_DISABLE_TIMING must be set */
} CUevent_flags;

/**
 * Array formats
 */
typedef enum CUarray_format_enum : int
{
    CU_AD_FORMAT_UNSIGNED_INT8  = 0x01, /**< Unsigned 8-bit integers */
    CU_AD_FORMAT_UNSIGNED_INT16 = 0x02, /**< Unsigned 16-bit integers */
    CU_AD_FORMAT_UNSIGNED_INT32 = 0x03, /**< Unsigned 32-bit integers */
    CU_AD_FORMAT_SIGNED_INT8    = 0x08, /**< Signed 8-bit integers */
    CU_AD_FORMAT_SIGNED_INT16   = 0x09, /**< Signed 16-bit integers */
    CU_AD_FORMAT_SIGNED_INT32   = 0x0a, /**< Signed 32-bit integers */
    CU_AD_FORMAT_HALF           = 0x10, /**< 16-bit floating point */
    CU_AD_FORMAT_FLOAT          = 0x20  /**< 32-bit floating point */
} CUarray_format;

/**
 * Texture reference addressing modes
 */
typedef enum CUaddress_mode_enum
{
    CU_TR_ADDRESS_MODE_WRAP   = 0, /**< Wrapping address mode */
    CU_TR_ADDRESS_MODE_CLAMP  = 1, /**< Clamp to edge address mode */
    CU_TR_ADDRESS_MODE_MIRROR = 2, /**< Mirror address mode */
    CU_TR_ADDRESS_MODE_BORDER = 3  /**< Border address mode */
} CUaddress_mode;

/**
 * Texture reference filtering modes
 */
typedef enum CUfilter_mode_enum
{
    CU_TR_FILTER_MODE_POINT  = 0, /**< Point filter mode */
    CU_TR_FILTER_MODE_LINEAR = 1  /**< Linear filter mode */
} CUfilter_mode;

/**
 * Device properties
 */
typedef enum CUdevice_attribute_enum : int
{
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 1,              /**< Maximum number of threads per block */
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X = 2,                    /**< Maximum block dimension X */
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Y = 3,                    /**< Maximum block dimension Y */
    CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_Z = 4,                    /**< Maximum block dimension Z */
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X = 5,                     /**< Maximum grid dimension X */
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Y = 6,                     /**< Maximum grid dimension Y */
    CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_Z = 7,                     /**< Maximum grid dimension Z */
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK = 8,        /**< Maximum shared memory available per block in bytes */
    CU_DEVICE_ATTRIBUTE_SHARED_MEMORY_PER_BLOCK = 8,            /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK */
    CU_DEVICE_ATTRIBUTE_TOTAL_CONSTANT_MEMORY = 9,              /**< Memory available on device for __constant__ variables in a CUDA C kernel in bytes */
    CU_DEVICE_ATTRIBUTE_WARP_SIZE = 10,                         /**< Warp size in threads */
    CU_DEVICE_ATTRIBUTE_MAX_PITCH = 11,                         /**< Maximum pitch in bytes allowed by memory copies */
    CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK = 12,           /**< Maximum number of 32-bit registers available per block */
    CU_DEVICE_ATTRIBUTE_REGISTERS_PER_BLOCK = 12,               /**< Deprecated, use CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK */
    CU_DEVICE_ATTRIBUTE_CLOCK_RATE = 13,                        /**< Peak clock frequency in kilohertz */
    CU_DEVICE_ATTRIBUTE_TEXTURE_ALIGNMENT = 14,                 /**< Alignment requirement for textures */
    CU_DEVICE_ATTRIBUTE_GPU_OVERLAP = 15,                       /**< Device can possibly copy memory and execute a kernel concurrently */
    CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,              /**< Number of multiprocessors on device */
    CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT = 17,               /**< Specifies whether there is a run time limit on kernels */
    CU_DEVICE_ATTRIBUTE_INTEGRATED = 18,                        /**< Device is integrated with host memory */
    CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY = 19,               /**< Device can map host memory into CUDA address space */
    CU_DEVICE_ATTRIBUTE_COMPUTE_MODE = 20,                      /**< Compute mode (See ::CUcomputemode for details) */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_WIDTH = 21,           /**< Maximum 1D texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_WIDTH = 22,           /**< Maximum 2D texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_HEIGHT = 23,          /**< Maximum 2D texture height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH = 24,           /**< Maximum 3D texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT = 25,          /**< Maximum 3D texture height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH = 26,           /**< Maximum 3D texture depth */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_WIDTH = 27,     /**< Maximum texture array width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_HEIGHT = 28,    /**< Maximum texture array height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_ARRAY_NUMSLICES = 29, /**< Maximum slices in a texture array */
    CU_DEVICE_ATTRIBUTE_SURFACE_ALIGNMENT = 30,                 /**< Alignment requirement for surfaces */
    CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS = 31,                /**< Device can possibly execute multiple kernels concurrently */
    CU_DEVICE_ATTRIBUTE_ECC_ENABLED = 32,                       /**< Device has ECC support enabled */
    CU_DEVICE_ATTRIBUTE_PCI_BUS_ID = 33,                        /**< PCI bus ID of the device */
    CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID = 34,                     /**< PCI device ID of the device */
    CU_DEVICE_ATTRIBUTE_TCC_DRIVER = 35                         /**< Device is using TCC driver model */
#if __CUDA_API_VERSION >= 4000
  , CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE = 36,                 /**< Peak memory clock frequency in kilohertz */
    CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH = 37,           /**< Global memory bus width in bits */
    CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE = 38,                     /**< Size of L2 cache in bytes */
    CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_MULTIPROCESSOR = 39,    /**< Maximum resident threads per multiprocessor */
    CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT = 40,                /**< Number of asynchronous engines */
    CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING = 41,                /**< Device uses shares a unified address space with the host */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_WIDTH = 42,   /**< Maximum 1D layered texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LAYERED_LAYERS = 43,   /**< Maximum layers in a 1D layered texture */
    CU_DEVICE_ATTRIBUTE_CAN_TEX2D_GATHER = 44,                              /**< Deprecated, do not use. */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_WIDTH = 45,                /**< Maximum 2D texture width if CUDA_ARRAY3D_TEXTURE_GATHER is set */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_GATHER_HEIGHT = 46,               /**< Maximum 2D texture height if CUDA_ARRAY3D_TEXTURE_GATHER is set */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_WIDTH_ALTERNATE = 47,             /**< Alternate maximum 3D texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_HEIGHT_ALTERNATE = 48,            /**< Alternate maximum 3D texture height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE3D_DEPTH_ALTERNATE = 49,             /**< Alternate maximum 3D texture depth */
    CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID = 50,                                 /**< PCI domain ID of the device */
    CU_DEVICE_ATTRIBUTE_TEXTURE_PITCH_ALIGNMENT = 51,                       /**< Pitch alignment requirement for textures */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_WIDTH = 52,                  /**< Maximum cubemap texture width/height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_WIDTH = 53,          /**< Maximum cubemap layered texture width/height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURECUBEMAP_LAYERED_LAYERS = 54,         /**< Maximum layers in a cubemap layered texture */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_WIDTH = 55,                       /**< Maximum 1D surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_WIDTH = 56,                       /**< Maximum 2D surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_HEIGHT = 57,                      /**< Maximum 2D surface height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_WIDTH = 58,                       /**< Maximum 3D surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_HEIGHT = 59,                      /**< Maximum 3D surface height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE3D_DEPTH = 60,                       /**< Maximum 3D surface depth */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_WIDTH = 61,               /**< Maximum 1D layered surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE1D_LAYERED_LAYERS = 62,              /**< Maximum layers in a 1D layered surface */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_WIDTH = 63,               /**< Maximum 2D layered surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_HEIGHT = 64,              /**< Maximum 2D layered surface height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACE2D_LAYERED_LAYERS = 65,              /**< Maximum layers in a 2D layered surface */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_WIDTH = 66,                  /**< Maximum cubemap surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_WIDTH = 67,          /**< Maximum cubemap layered surface width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_SURFACECUBEMAP_LAYERED_LAYERS = 68,         /**< Maximum layers in a cubemap layered surface */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_LINEAR_WIDTH = 69,                /**< Maximum 1D linear texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_WIDTH = 70,                /**< Maximum 2D linear texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_HEIGHT = 71,               /**< Maximum 2D linear texture height */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_LINEAR_PITCH = 72,                /**< Maximum 2D linear texture pitch in bytes */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_WIDTH = 73,             /**< Maximum mipmapped 2D texture width */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE2D_MIPMAPPED_HEIGHT = 74,            /**< Maximum mipmapped 2D texture height */
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75,                      /**< Major compute capability version number */
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76,                      /**< Minor compute capability version number */
    CU_DEVICE_ATTRIBUTE_MAXIMUM_TEXTURE1D_MIPMAPPED_WIDTH = 77,             /**< Maximum mipmapped 1D texture width */
    CU_DEVICE_ATTRIBUTE_STREAM_PRIORITIES_SUPPORTED = 78,                   /**< Device supports stream priorities */
    CU_DEVICE_ATTRIBUTE_GLOBAL_L1_CACHE_SUPPORTED = 79,                     /**< Device supports caching globals in L1 */
    CU_DEVICE_ATTRIBUTE_LOCAL_L1_CACHE_SUPPORTED = 80,                      /**< Device supports caching locals in L1 */
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_MULTIPROCESSOR = 81,          /**< Maximum shared memory available per multiprocessor in bytes */
    CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_MULTIPROCESSOR = 82,              /**< Maximum number of 32-bit registers available per multiprocessor */
    CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY = 83,                                /**< Device can allocate managed memory on this system */
    CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD = 84,                               /**< Device is on a multi-GPU board */
    CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD_GROUP_ID = 85,                      /**< Unique id for a group of devices on the same multi-GPU board */
    CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED = 86,                  /**< Link between the device and the host supports native atomic operations (this is a placeholder attribute, and is not supported on any current hardware)*/
    CU_DEVICE_ATTRIBUTE_SINGLE_TO_DOUBLE_PRECISION_PERF_RATIO = 87,         /**< Ratio of single precision performance (in floating-point operations per second) to double precision performance */
    CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS = 88,                        /**< Device supports coherently accessing pageable memory without calling cudaHostRegister on it */
    CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS = 89,                     /**< Device can coherently access managed memory concurrently with the CPU */
    CU_DEVICE_ATTRIBUTE_COMPUTE_PREEMPTION_SUPPORTED = 90,                  /**< Device supports compute preemption. */
    CU_DEVICE_ATTRIBUTE_CAN_USE_HOST_POINTER_FOR_REGISTERED_MEM = 91,       /**< Device can access host registered memory at the same virtual address as the CPU */
    CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_MEM_OPS = 92,                        /**< ::cuStreamBatchMemOp and related APIs are supported. */
    CU_DEVICE_ATTRIBUTE_CAN_USE_64_BIT_STREAM_MEM_OPS = 93,                 /**< 64-bit operations are supported in ::cuStreamBatchMemOp and related APIs. */
    CU_DEVICE_ATTRIBUTE_CAN_USE_STREAM_WAIT_VALUE_NOR = 94,                 /**< ::CU_STREAM_WAIT_VALUE_NOR is supported. */
    CU_DEVICE_ATTRIBUTE_COOPERATIVE_LAUNCH = 95,                            /**< Device supports launching cooperative kernels via ::cuLaunchCooperativeKernel */
    CU_DEVICE_ATTRIBUTE_COOPERATIVE_MULTI_DEVICE_LAUNCH = 96,               /**< Device can participate in cooperative kernels launched via ::cuLaunchCooperativeKernelMultiDevice */
    CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN = 97,             /**< Maximum optin shared memory per block */
    CU_DEVICE_ATTRIBUTE_CAN_FLUSH_REMOTE_WRITES = 98,                       /**< Both the ::CU_STREAM_WAIT_VALUE_FLUSH flag and the ::CU_STREAM_MEM_OP_FLUSH_REMOTE_WRITES MemOp are supported on the device. See \ref CUDA_MEMOP for additional details. */
    CU_DEVICE_ATTRIBUTE_HOST_REGISTER_SUPPORTED = 99,                       /**< Device supports host memory registration via ::cudaHostRegister. */
    CU_DEVICE_ATTRIBUTE_PAGEABLE_MEMORY_ACCESS_USES_HOST_PAGE_TABLES = 100, /**< Device accesses pageable memory via the host's page tables. */
    CU_DEVICE_ATTRIBUTE_DIRECT_MANAGED_MEM_ACCESS_FROM_HOST = 101,          /**< The host can directly access managed memory on the device without migration. */
    CU_DEVICE_ATTRIBUTE_VIRTUAL_ADDRESS_MANAGEMENT_SUPPORTED = 102,         /**< Device supports virtual address management APIs like ::cuMemAddressReserve, ::cuMemCreate, ::cuMemMap and related APIs */
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR_SUPPORTED = 103,  /**< Device supports exporting memory to a posix file descriptor with ::cuMemExportToShareableHandle, if requested via ::cuMemCreate */
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_WIN32_HANDLE_SUPPORTED = 104,           /**< Device supports exporting memory to a Win32 NT handle with ::cuMemExportToShareableHandle, if requested via ::cuMemCreate */
    CU_DEVICE_ATTRIBUTE_HANDLE_TYPE_WIN32_KMT_HANDLE_SUPPORTED = 105,       /**< Device supports exporting memory to a Win32 KMT handle with ::cuMemExportToShareableHandle, if requested ::cuMemCreate */
    CU_DEVICE_ATTRIBUTE_MAX
#endif
} CUdevice_attribute;

/**
 * Legacy device properties
 */
typedef struct CUdevprop_st
{
    int maxThreadsPerBlock;     /**< Maximum number of threads per block */
    int maxThreadsDim[3];       /**< Maximum size of each dimension of a block */
    int maxGridSize[3];         /**< Maximum size of each dimension of a grid */
    int sharedMemPerBlock;      /**< Shared memory available per block in bytes */
    int totalConstantMemory;    /**< Constant memory available on device in bytes */
    int SIMDWidth;              /**< Warp size in threads */
    int memPitch;               /**< Maximum pitch in bytes allowed by memory copies */
    int regsPerBlock;           /**< 32-bit registers available per block */
    int clockRate;              /**< Clock frequency in kilohertz */
    int textureAlign;           /**< Alignment requirement for textures */
} CUdevprop;

/**
 * Function properties
 */
typedef enum CUfunction_attribute_enum
{
    /**
     * The maximum number of threads per block, beyond which a launch of the
     * function would fail. This number depends on both the function and the
     * device on which the function is currently loaded.
     */
    CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK = 0,

    /**
     * The size in bytes of statically-allocated shared memory required by
     * this function. This does not include dynamically-allocated shared
     * memory requested by the user at runtime.
     */
    CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES = 1,

    /**
     * The size in bytes of user-allocated constant memory required by this
     * function.
     */
    CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES = 2,

    /**
     * The size in bytes of local memory used by each thread of this function.
     */
    CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES = 3,

    /**
     * The number of registers used by each thread of this function.
     */
    CU_FUNC_ATTRIBUTE_NUM_REGS = 4,

    /**
     * The PTX virtual architecture version for which the function was
     * compiled. This value is the major PTX version * 10 + the minor PTX
     * version, so a PTX version 1.3 function would return the value 13.
     * Note that this may return the undefined value of 0 for cubins
     * compiled prior to CUDA 3.0.
     */
    CU_FUNC_ATTRIBUTE_PTX_VERSION = 5,

    /**
     * The binary architecture version for which the function was compiled.
     * This value is the major binary version * 10 + the minor binary version,
     * so a binary version 1.3 function would return the value 13. Note that
     * this will return a value of 10 for legacy cubins that do not have a
     * properly-encoded binary architecture version.
     */
    CU_FUNC_ATTRIBUTE_BINARY_VERSION = 6,

    CU_FUNC_ATTRIBUTE_MAX
} CUfunction_attribute;

/**
 * Function cache configurations
 */
typedef enum CUfunc_cache_enum
{
    CU_FUNC_CACHE_PREFER_NONE    = 0x00, /**< no preference for shared memory or L1 (default) */
    CU_FUNC_CACHE_PREFER_SHARED  = 0x01, /**< prefer larger shared memory and smaller L1 cache */
    CU_FUNC_CACHE_PREFER_L1      = 0x02  /**< prefer larger L1 cache and smaller shared memory */
} CUfunc_cache;

/**
 * Memory types
 */
typedef enum CUmemorytype_enum
{
    CU_MEMORYTYPE_HOST    = 0x01,    /**< Host memory */
    CU_MEMORYTYPE_DEVICE  = 0x02,    /**< Device memory */
    CU_MEMORYTYPE_ARRAY   = 0x03     /**< Array memory */
#if __CUDA_API_VERSION >= 4000
  , CU_MEMORYTYPE_UNIFIED = 0x04     /**< Unified device or host memory */
#endif
} CUmemorytype;

/**
 * Compute Modes
 */
typedef enum CUcomputemode_enum
{
    CU_COMPUTEMODE_DEFAULT           = 0,  /**< Default compute mode (Multiple contexts allowed per device) */
    CU_COMPUTEMODE_EXCLUSIVE         = 1, /**< Compute-exclusive-thread mode (Only one context used by a single thread can be present on this device at a time) */
    CU_COMPUTEMODE_PROHIBITED        = 2  /**< Compute-prohibited mode (No contexts can be created on this device at this time) */
#if __CUDA_API_VERSION >= 4000
  , CU_COMPUTEMODE_EXCLUSIVE_PROCESS = 3  /**< Compute-exclusive-process mode (Only one context used by a single process can be present on this device at a time) */
#endif
} CUcomputemode;

/**
 * Online compiler options
 */
typedef enum CUjit_option_enum
{
    /**
     * Max number of registers that a thread may use.\n
     * Option type: unsigned int
     */
    CU_JIT_MAX_REGISTERS = 0,

    /**
     * IN: Specifies minimum number of threads per block to target compilation
     * for\n
     * OUT: Returns the number of threads the compiler actually targeted.
     * This restricts the resource utilization fo the compiler (e.g. max
     * registers) such that a block with the given number of threads should be
     * able to launch based on register limitations. Note, this option does not
     * currently take into account any other resource limitations, such as
     * shared memory utilization.\n
     * Option type: unsigned int
     */
    CU_JIT_THREADS_PER_BLOCK,

    /**
     * Returns a float value in the option of the wall clock time, in
     * milliseconds, spent creating the cubin\n
     * Option type: float
     */
    CU_JIT_WALL_TIME,

    /**
     * Pointer to a buffer in which to print any log messsages from PTXAS
     * that are informational in nature (the buffer size is specified via
     * option ::CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES) \n
     * Option type: char*
     */
    CU_JIT_INFO_LOG_BUFFER,

    /**
     * IN: Log buffer size in bytes.  Log messages will be capped at this size
     * (including null terminator)\n
     * OUT: Amount of log buffer filled with messages\n
     * Option type: unsigned int
     */
    CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES,

    /**
     * Pointer to a buffer in which to print any log messages from PTXAS that
     * reflect errors (the buffer size is specified via option
     * ::CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES)\n
     * Option type: char*
     */
    CU_JIT_ERROR_LOG_BUFFER,

    /**
     * IN: Log buffer size in bytes.  Log messages will be capped at this size
     * (including null terminator)\n
     * OUT: Amount of log buffer filled with messages\n
     * Option type: unsigned int
     */
    CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES,

    /**
     * Level of optimizations to apply to generated code (0 - 4), with 4
     * being the default and highest level of optimizations.\n
     * Option type: unsigned int
     */
    CU_JIT_OPTIMIZATION_LEVEL,

    /**
     * No option value required. Determines the target based on the current
     * attached context (default)\n
     * Option type: No option value needed
     */
    CU_JIT_TARGET_FROM_CUCONTEXT,

    /**
     * Target is chosen based on supplied ::CUjit_target_enum.\n
     * Option type: unsigned int for enumerated type ::CUjit_target_enum
     */
    CU_JIT_TARGET,

    /**
     * Specifies choice of fallback strategy if matching cubin is not found.
     * Choice is based on supplied ::CUjit_fallback_enum.\n
     * Option type: unsigned int for enumerated type ::CUjit_fallback_enum
     */
    CU_JIT_FALLBACK_STRATEGY

} CUjit_option;

/**
 * Online compilation targets
 */
typedef enum CUjit_target_enum
{
    CU_TARGET_COMPUTE_10 = 0,   /**< Compute device class 1.0 */
    CU_TARGET_COMPUTE_11,       /**< Compute device class 1.1 */
    CU_TARGET_COMPUTE_12,       /**< Compute device class 1.2 */
    CU_TARGET_COMPUTE_13,       /**< Compute device class 1.3 */
    CU_TARGET_COMPUTE_20,       /**< Compute device class 2.0 */
    CU_TARGET_COMPUTE_21        /**< Compute device class 2.1 */
} CUjit_target;

/**
 * Cubin matching fallback strategies
 */
typedef enum CUjit_fallback_enum
{
    CU_PREFER_PTX = 0,  /**< Prefer to compile ptx */

    CU_PREFER_BINARY    /**< Prefer to fall back to compatible binary code */

} CUjit_fallback;

/**
 * Flags to register a graphics resource
 */
typedef enum CUgraphicsRegisterFlags_enum
{
    CU_GRAPHICS_REGISTER_FLAGS_NONE             = 0x00,
    CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY        = 0x01,
    CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD    = 0x02,
    CU_GRAPHICS_REGISTER_FLAGS_SURFACE_LDST     = 0x04,
    CU_GRAPHICS_REGISTER_FLAGS_TEXTURE_GATHER   = 0x08
} CUgraphicsRegisterFlags;

/**
 * Flags for mapping and unmapping interop resources
 */
typedef enum CUgraphicsMapResourceFlags_enum
{
    CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE          = 0x00,
    CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY     = 0x01,
    CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD = 0x02
} CUgraphicsMapResourceFlags;

/**
 * Array indices for cube faces
 */
typedef enum CUarray_cubemap_face_enum
{
    CU_CUBEMAP_FACE_POSITIVE_X  = 0x00, /**< Positive X face of cubemap */
    CU_CUBEMAP_FACE_NEGATIVE_X  = 0x01, /**< Negative X face of cubemap */
    CU_CUBEMAP_FACE_POSITIVE_Y  = 0x02, /**< Positive Y face of cubemap */
    CU_CUBEMAP_FACE_NEGATIVE_Y  = 0x03, /**< Negative Y face of cubemap */
    CU_CUBEMAP_FACE_POSITIVE_Z  = 0x04, /**< Positive Z face of cubemap */
    CU_CUBEMAP_FACE_NEGATIVE_Z  = 0x05  /**< Negative Z face of cubemap */
} CUarray_cubemap_face;

/**
 * Limits
 */
typedef enum CUlimit_enum
{
    CU_LIMIT_STACK_SIZE        = 0x00, /**< GPU thread stack size */
    CU_LIMIT_PRINTF_FIFO_SIZE  = 0x01, /**< GPU printf FIFO size */
    CU_LIMIT_MALLOC_HEAP_SIZE  = 0x02  /**< GPU malloc heap size */
} CUlimit;

/**
 * Resource types
 */
typedef enum CUresourcetype_enum {
    CU_RESOURCE_TYPE_ARRAY = 0x00, /**< Array resoure */
    CU_RESOURCE_TYPE_MIPMAPPED_ARRAY = 0x01, /**< Mipmapped array resource */
    CU_RESOURCE_TYPE_LINEAR = 0x02, /**< Linear resource */
    CU_RESOURCE_TYPE_PITCH2D = 0x03  /**< Pitch 2D resource */
} CUresourcetype;

/**
 * Error codes
 */
typedef enum cudaError_enum : int {
    /**
     * The API call returned with no errors. In the case of query calls, this
     * also means that the operation being queried is complete (see
     * ::cuEventQuery() and ::cuStreamQuery()).
     */
    CUDA_SUCCESS = 0,

    /**
     * This indicates that one or more of the parameters passed to the API call
     * is not within an acceptable range of values.
     */
    CUDA_ERROR_INVALID_VALUE = 1,

    /**
     * The API call failed because it was unable to allocate enough memory to
     * perform the requested operation.
     */
    CUDA_ERROR_OUT_OF_MEMORY = 2,

    /**
     * This indicates that the CUDA driver has not been initialized with
     * ::cuInit() or that initialization has failed.
     */
    CUDA_ERROR_NOT_INITIALIZED = 3,

    /**
     * This indicates that the CUDA driver is in the process of shutting down.
     */
    CUDA_ERROR_DEINITIALIZED = 4,

    /**
     * This indicates profiler is not initialized for this run. This can
     * happen when the application is running with external profiling tools
     * like visual profiler.
     */
    CUDA_ERROR_PROFILER_DISABLED = 5,

    /**
     * \deprecated
     * This error return is deprecated as of CUDA 5.0. It is no longer an error
     * to attempt to enable/disable the profiling via ::cuProfilerStart or
     * ::cuProfilerStop without initialization.
     */
    CUDA_ERROR_PROFILER_NOT_INITIALIZED = 6,

    /**
     * \deprecated
     * This error return is deprecated as of CUDA 5.0. It is no longer an error
     * to call cuProfilerStart() when profiling is already enabled.
     */
    CUDA_ERROR_PROFILER_ALREADY_STARTED = 7,

    /**
     * \deprecated
     * This error return is deprecated as of CUDA 5.0. It is no longer an error
     * to call cuProfilerStop() when profiling is already disabled.
     */
    CUDA_ERROR_PROFILER_ALREADY_STOPPED = 8,

    /**
     * This indicates that no CUDA-capable devices were detected by the installed
     * CUDA driver.
     */
    CUDA_ERROR_NO_DEVICE = 100,

    /**
     * This indicates that the device ordinal supplied by the user does not
     * correspond to a valid CUDA device.
     */
    CUDA_ERROR_INVALID_DEVICE = 101,


    /**
     * This indicates that the device kernel image is invalid. This can also
     * indicate an invalid CUDA module.
     */
    CUDA_ERROR_INVALID_IMAGE = 200,

    /**
     * This most frequently indicates that there is no context bound to the
     * current thread. This can also be returned if the context passed to an
     * API call is not a valid handle (such as a context that has had
     * ::cuCtxDestroy() invoked on it). This can also be returned if a user
     * mixes different API versions (i.e. 3010 context with 3020 API calls).
     * See ::cuCtxGetApiVersion() for more details.
     */
    CUDA_ERROR_INVALID_CONTEXT = 201,

    /**
     * This indicated that the context being supplied as a parameter to the
     * API call was already the active context.
     * \deprecated
     * This error return is deprecated as of CUDA 3.2. It is no longer an
     * error to attempt to push the active context via ::cuCtxPushCurrent().
     */
    CUDA_ERROR_CONTEXT_ALREADY_CURRENT = 202,

    /**
     * This indicates that a map or register operation has failed.
     */
    CUDA_ERROR_MAP_FAILED = 205,

    /**
     * This indicates that an unmap or unregister operation has failed.
     */
    CUDA_ERROR_UNMAP_FAILED = 206,

    /**
     * This indicates that the specified array is currently mapped and thus
     * cannot be destroyed.
     */
    CUDA_ERROR_ARRAY_IS_MAPPED = 207,

    /**
     * This indicates that the resource is already mapped.
     */
    CUDA_ERROR_ALREADY_MAPPED = 208,

    /**
     * This indicates that there is no kernel image available that is suitable
     * for the device. This can occur when a user specifies code generation
     * options for a particular CUDA source file that do not include the
     * corresponding device configuration.
     */
    CUDA_ERROR_NO_BINARY_FOR_GPU = 209,

    /**
     * This indicates that a resource has already been acquired.
     */
    CUDA_ERROR_ALREADY_ACQUIRED = 210,

    /**
     * This indicates that a resource is not mapped.
     */
    CUDA_ERROR_NOT_MAPPED = 211,

    /**
     * This indicates that a mapped resource is not available for access as an
     * array.
     */
    CUDA_ERROR_NOT_MAPPED_AS_ARRAY = 212,

    /**
     * This indicates that a mapped resource is not available for access as a
     * pointer.
     */
    CUDA_ERROR_NOT_MAPPED_AS_POINTER = 213,

    /**
     * This indicates that an uncorrectable ECC error was detected during
     * execution.
     */
    CUDA_ERROR_ECC_UNCORRECTABLE = 214,

    /**
     * This indicates that the ::CUlimit passed to the API call is not
     * supported by the active device.
     */
    CUDA_ERROR_UNSUPPORTED_LIMIT = 215,

    /**
     * This indicates that the ::CUcontext passed to the API call can
     * only be bound to a single CPU thread at a time but is already
     * bound to a CPU thread.
     */
    CUDA_ERROR_CONTEXT_ALREADY_IN_USE = 216,

    /**
     * This indicates that peer access is not supported across the given
     * devices.
     */
    CUDA_ERROR_PEER_ACCESS_UNSUPPORTED = 217,

    /**
     * This indicates that a PTX JIT compilation failed.
     */
    CUDA_ERROR_INVALID_PTX = 218,

    /**
     * This indicates an error with OpenGL or DirectX context.
     */
    CUDA_ERROR_INVALID_GRAPHICS_CONTEXT = 219,

    /**
    * This indicates that an uncorrectable NVLink error was detected during the
    * execution.
    */
    CUDA_ERROR_NVLINK_UNCORRECTABLE = 220,

    /**
    * This indicates that the PTX JIT compiler library was not found.
    */
    CUDA_ERROR_JIT_COMPILER_NOT_FOUND = 221,

    /**
     * This indicates that the device kernel source is invalid.
     */
    CUDA_ERROR_INVALID_SOURCE = 300,

    /**
     * This indicates that the file specified was not found.
     */
    CUDA_ERROR_FILE_NOT_FOUND = 301,

    /**
     * This indicates that a link to a shared object failed to resolve.
     */
    CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND = 302,

    /**
     * This indicates that initialization of a shared object failed.
     */
    CUDA_ERROR_SHARED_OBJECT_INIT_FAILED = 303,

    /**
     * This indicates that an OS call failed.
     */
    CUDA_ERROR_OPERATING_SYSTEM = 304,

    /**
     * This indicates that a resource handle passed to the API call was not
     * valid. Resource handles are opaque types like ::CUstream and ::CUevent.
     */
    CUDA_ERROR_INVALID_HANDLE = 400,

    /**
     * This indicates that a resource required by the API call is not in a
     * valid state to perform the requested operation.
     */
    CUDA_ERROR_ILLEGAL_STATE = 401,

    /**
     * This indicates that a named symbol was not found. Examples of symbols
     * are global/constant variable names, texture names, and surface names.
     */
    CUDA_ERROR_NOT_FOUND = 500,

    /**
     * This indicates that asynchronous operations issued previously have not
     * completed yet. This result is not actually an error, but must be indicated
     * differently than ::CUDA_SUCCESS (which indicates completion). Calls that
     * may return this value include ::cuEventQuery() and ::cuStreamQuery().
     */
    CUDA_ERROR_NOT_READY = 600,

    /**
     * While executing a kernel, the device encountered a
     * load or store instruction on an invalid memory address.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_ILLEGAL_ADDRESS = 700,

    /**
     * This indicates that a launch did not occur because it did not have
     * appropriate resources. This error usually indicates that the user has
     * attempted to pass too many arguments to the device kernel, or the
     * kernel launch specifies too many threads for the kernel's register
     * count. Passing arguments of the wrong size (i.e. a 64-bit pointer
     * when a 32-bit int is expected) is equivalent to passing too many
     * arguments and can also result in this error.
     */
    CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES = 701,

    /**
     * This indicates that the device kernel took too long to execute. This can
     * only occur if timeouts are enabled - see the device attribute
     * ::CU_DEVICE_ATTRIBUTE_KERNEL_EXEC_TIMEOUT for more information.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_LAUNCH_TIMEOUT = 702,

    /**
     * This error indicates a kernel launch that uses an incompatible texturing
     * mode.
     */
    CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING = 703,

    /**
     * This error indicates that a call to ::cuCtxEnablePeerAccess() is
     * trying to re-enable peer access to a context which has already
     * had peer access to it enabled.
     */
    CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED = 704,

    /**
     * This error indicates that ::cuCtxDisablePeerAccess() is
     * trying to disable peer access which has not been enabled yet
     * via ::cuCtxEnablePeerAccess().
     */
    CUDA_ERROR_PEER_ACCESS_NOT_ENABLED = 705,

    /**
     * This error indicates that the primary context for the specified device
     * has already been initialized.
     */
    CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE = 708,

    /**
     * This error indicates that the context current to the calling thread
     * has been destroyed using ::cuCtxDestroy, or is a primary context which
     * has not yet been initialized.
     */
    CUDA_ERROR_CONTEXT_IS_DESTROYED = 709,

    /**
     * A device-side assert triggered during kernel execution. The context
     * cannot be used anymore, and must be destroyed. All existing device
     * memory allocations from this context are invalid and must be
     * reconstructed if the program is to continue using CUDA.
     */
    CUDA_ERROR_ASSERT = 710,

    /**
     * This error indicates that the hardware resources required to enable
     * peer access have been exhausted for one or more of the devices
     * passed to ::cuCtxEnablePeerAccess().
     */
    CUDA_ERROR_TOO_MANY_PEERS = 711,

    /**
     * This error indicates that the memory range passed to ::cuMemHostRegister()
     * has already been registered.
     */
    CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED = 712,

    /**
     * This error indicates that the pointer passed to ::cuMemHostUnregister()
     * does not correspond to any currently registered memory region.
     */
    CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED = 713,

    /**
     * While executing a kernel, the device encountered a stack error.
     * This can be due to stack corruption or exceeding the stack size limit.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_HARDWARE_STACK_ERROR = 714,

    /**
     * While executing a kernel, the device encountered an illegal instruction.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_ILLEGAL_INSTRUCTION = 715,

    /**
     * While executing a kernel, the device encountered a load or store instruction
     * on a memory address which is not aligned.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_MISALIGNED_ADDRESS = 716,

    /**
     * While executing a kernel, the device encountered an instruction
     * which can only operate on memory locations in certain address spaces
     * (global, shared, or local), but was supplied a memory address not
     * belonging to an allowed address space.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_INVALID_ADDRESS_SPACE = 717,

    /**
     * While executing a kernel, the device program counter wrapped its address space.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_INVALID_PC = 718,

    /**
     * An exception occurred on the device while executing a kernel. Common
     * causes include dereferencing an invalid device pointer and accessing
     * out of bounds shared memory. Less common cases can be system specific - more
     * information about these cases can be found in the system specific user guide.
     * This leaves the process in an inconsistent state and any further CUDA work
     * will return the same error. To continue using CUDA, the process must be terminated
     * and relaunched.
     */
    CUDA_ERROR_LAUNCH_FAILED = 719,

    /**
     * This error indicates that the number of blocks launched per grid for a kernel that was
     * launched via either ::cuLaunchCooperativeKernel or ::cuLaunchCooperativeKernelMultiDevice
     * exceeds the maximum number of blocks as allowed by ::cuOccupancyMaxActiveBlocksPerMultiprocessor
     * or ::cuOccupancyMaxActiveBlocksPerMultiprocessorWithFlags times the number of multiprocessors
     * as specified by the device attribute ::CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT.
     */
    CUDA_ERROR_COOPERATIVE_LAUNCH_TOO_LARGE = 720,

    /**
     * This error indicates that the attempted operation is not permitted.
     */
    CUDA_ERROR_NOT_PERMITTED = 800,

    /**
     * This error indicates that the attempted operation is not supported
     * on the current system or device.
     */
    CUDA_ERROR_NOT_SUPPORTED = 801,

    /**
     * This error indicates that the system is not yet ready to start any CUDA
     * work.  To continue using CUDA, verify the system configuration is in a
     * valid state and all required driver daemons are actively running.
     * More information about this error can be found in the system specific
     * user guide.
     */
    CUDA_ERROR_SYSTEM_NOT_READY = 802,

    /**
     * This error indicates that there is a mismatch between the versions of
     * the display driver and the CUDA driver. Refer to the compatibility documentation
     * for supported versions.
     */
    CUDA_ERROR_SYSTEM_DRIVER_MISMATCH = 803,

    /**
     * This error indicates that the system was upgraded to run with forward compatibility
     * but the visible hardware detected by CUDA does not support this configuration.
     * Refer to the compatibility documentation for the supported hardware matrix or ensure
     * that only supported hardware is visible during initialization via the CUDA_VISIBLE_DEVICES
     * environment variable.
     */
    CUDA_ERROR_COMPAT_NOT_SUPPORTED_ON_DEVICE = 804,

    /**
     * This error indicates that the operation is not permitted when
     * the stream is capturing.
     */
    CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED = 900,

    /**
     * This error indicates that the current capture sequence on the stream
     * has been invalidated due to a previous error.
     */
    CUDA_ERROR_STREAM_CAPTURE_INVALIDATED = 901,

    /**
     * This error indicates that the operation would have resulted in a merge
     * of two independent capture sequences.
     */
    CUDA_ERROR_STREAM_CAPTURE_MERGE = 902,

    /**
     * This error indicates that the capture was not initiated in this stream.
     */
    CUDA_ERROR_STREAM_CAPTURE_UNMATCHED = 903,

    /**
     * This error indicates that the capture sequence contains a fork that was
     * not joined to the primary stream.
     */
    CUDA_ERROR_STREAM_CAPTURE_UNJOINED = 904,

    /**
     * This error indicates that a dependency would have been created which
     * crosses the capture sequence boundary. Only implicit in-stream ordering
     * dependencies are allowed to cross the boundary.
     */
    CUDA_ERROR_STREAM_CAPTURE_ISOLATION = 905,

    /**
     * This error indicates a disallowed implicit dependency on a current capture
     * sequence from cudaStreamLegacy.
     */
    CUDA_ERROR_STREAM_CAPTURE_IMPLICIT = 906,

    /**
     * This error indicates that the operation is not permitted on an event which
     * was last recorded in a capturing stream.
     */
    CUDA_ERROR_CAPTURED_EVENT = 907,

    /**
     * A stream capture sequence not initiated with the ::CU_STREAM_CAPTURE_MODE_RELAXED
     * argument to ::cuStreamBeginCapture was passed to ::cuStreamEndCapture in a
     * different thread.
     */
    CUDA_ERROR_STREAM_CAPTURE_WRONG_THREAD = 908,

    /**
     * This error indicates that the timeout specified for the wait operation has lapsed.
     */
    CUDA_ERROR_TIMEOUT = 909,

    /**
     * This error indicates that the graph update was not performed because it included
     * changes which violated constraints specific to instantiated graph update.
     */
    CUDA_ERROR_GRAPH_EXEC_UPDATE_FAILURE = 910,

    /**
     * This indicates that an unknown internal error has occurred.
     */
    CUDA_ERROR_UNKNOWN = 999
} CUresult;

#if __CUDA_API_VERSION >= 4000
/**
 * If set, host memory is portable between CUDA contexts.
 * Flag for ::cuMemHostAlloc()
 */
#define CU_MEMHOSTALLOC_PORTABLE        0x01

/**
 * If set, host memory is mapped into CUDA address space and
 * ::cuMemHostGetDevicePointer() may be called on the host pointer.
 * Flag for ::cuMemHostAlloc()
 */
#define CU_MEMHOSTALLOC_DEVICEMAP       0x02

/**
 * If set, host memory is allocated as write-combined - fast to write,
 * faster to DMA, slow to read except via SSE4 streaming load instruction
 * (MOVNTDQA).
 * Flag for ::cuMemHostAlloc()
 */
#define CU_MEMHOSTALLOC_WRITECOMBINED   0x04

/**
 * If set, host memory is portable between CUDA contexts.
 * Flag for ::cuMemHostRegister()
 */
#define CU_MEMHOSTREGISTER_PORTABLE     0x01

/**
 * If set, host memory is mapped into CUDA address space and
 * ::cuMemHostGetDevicePointer() may be called on the host pointer.
 * Flag for ::cuMemHostRegister()
 */
#define CU_MEMHOSTREGISTER_DEVICEMAP    0x02

/**
 * If set, peer memory is mapped into CUDA address space and
 * ::cuMemPeerGetDevicePointer() may be called on the host pointer.
 * Flag for ::cuMemPeerRegister()
 */
#define CU_MEMPEERREGISTER_DEVICEMAP    0x02
#endif

#if __CUDA_API_VERSION >= 3020
/**
 * 2D memory copy parameters
 */
typedef struct CUDA_MEMCPY2D_st
{
    size_t srcXInBytes;         /**< Source X in bytes */
    size_t srcY;                /**< Source Y */

    CUmemorytype srcMemoryType; /**< Source memory type (host, device, array) */
    const void *srcHost;        /**< Source host pointer */
    CUdeviceptr srcDevice;      /**< Source device pointer */
    CUarray srcArray;           /**< Source array reference */
    size_t srcPitch;            /**< Source pitch (ignored when src is array) */

    size_t dstXInBytes;         /**< Destination X in bytes */
    size_t dstY;                /**< Destination Y */

    CUmemorytype dstMemoryType; /**< Destination memory type (host, device, array) */
    void *dstHost;              /**< Destination host pointer */
    CUdeviceptr dstDevice;      /**< Destination device pointer */
    CUarray dstArray;           /**< Destination array reference */
    size_t dstPitch;            /**< Destination pitch (ignored when dst is array) */

    size_t WidthInBytes;        /**< Width of 2D memory copy in bytes */
    size_t Height;              /**< Height of 2D memory copy */
} CUDA_MEMCPY2D;

/**
 * 3D memory copy parameters
 */
typedef struct CUDA_MEMCPY3D_st
{
    size_t srcXInBytes;         /**< Source X in bytes */
    size_t srcY;                /**< Source Y */
    size_t srcZ;                /**< Source Z */
    size_t srcLOD;              /**< Source LOD */
    CUmemorytype srcMemoryType; /**< Source memory type (host, device, array) */
    const void *srcHost;        /**< Source host pointer */
    CUdeviceptr srcDevice;      /**< Source device pointer */
    CUarray srcArray;           /**< Source array reference */
    void *reserved0;            /**< Must be NULL */
    size_t srcPitch;            /**< Source pitch (ignored when src is array) */
    size_t srcHeight;           /**< Source height (ignored when src is array; may be 0 if Depth==1) */

    size_t dstXInBytes;         /**< Destination X in bytes */
    size_t dstY;                /**< Destination Y */
    size_t dstZ;                /**< Destination Z */
    size_t dstLOD;              /**< Destination LOD */
    CUmemorytype dstMemoryType; /**< Destination memory type (host, device, array) */
    void *dstHost;              /**< Destination host pointer */
    CUdeviceptr dstDevice;      /**< Destination device pointer */
    CUarray dstArray;           /**< Destination array reference */
    void *reserved1;            /**< Must be NULL */
    size_t dstPitch;            /**< Destination pitch (ignored when dst is array) */
    size_t dstHeight;           /**< Destination height (ignored when dst is array; may be 0 if Depth==1) */

    size_t WidthInBytes;        /**< Width of 3D memory copy in bytes */
    size_t Height;              /**< Height of 3D memory copy */
    size_t Depth;               /**< Depth of 3D memory copy */
} CUDA_MEMCPY3D;

/**
 * 3D memory cross-context copy parameters
 */
typedef struct CUDA_MEMCPY3D_PEER_st
{
    size_t srcXInBytes;         /**< Source X in bytes */
    size_t srcY;                /**< Source Y */
    size_t srcZ;                /**< Source Z */
    size_t srcLOD;              /**< Source LOD */
    CUmemorytype srcMemoryType; /**< Source memory type (host, device, array) */
    const void *srcHost;        /**< Source host pointer */
    CUdeviceptr srcDevice;      /**< Source device pointer */
    CUarray srcArray;           /**< Source array reference */
    CUcontext srcContext;       /**< Source context (ignored with srcMemoryType is ::CU_MEMORYTYPE_ARRAY) */
    size_t srcPitch;            /**< Source pitch (ignored when src is array) */
    size_t srcHeight;           /**< Source height (ignored when src is array; may be 0 if Depth==1) */

    size_t dstXInBytes;         /**< Destination X in bytes */
    size_t dstY;                /**< Destination Y */
    size_t dstZ;                /**< Destination Z */
    size_t dstLOD;              /**< Destination LOD */
    CUmemorytype dstMemoryType; /**< Destination memory type (host, device, array) */
    void *dstHost;              /**< Destination host pointer */
    CUdeviceptr dstDevice;      /**< Destination device pointer */
    CUarray dstArray;           /**< Destination array reference */
    CUcontext dstContext;       /**< Destination context (ignored with dstMemoryType is ::CU_MEMORYTYPE_ARRAY) */
    size_t dstPitch;            /**< Destination pitch (ignored when dst is array) */
    size_t dstHeight;           /**< Destination height (ignored when dst is array; may be 0 if Depth==1) */

    size_t WidthInBytes;        /**< Width of 3D memory copy in bytes */
    size_t Height;              /**< Height of 3D memory copy */
    size_t Depth;               /**< Depth of 3D memory copy */
} CUDA_MEMCPY3D_PEER;

/**
 * Array descriptor
 */
typedef struct CUDA_ARRAY_DESCRIPTOR_st
{
    size_t Width;             /**< Width of array */
    size_t Height;            /**< Height of array */

    CUarray_format Format;    /**< Array format */
    unsigned int NumChannels; /**< Channels per array element */
} CUDA_ARRAY_DESCRIPTOR;

/**
 * 3D array descriptor
 */
typedef struct CUDA_ARRAY3D_DESCRIPTOR_st
{
    size_t Width;             /**< Width of 3D array */
    size_t Height;            /**< Height of 3D array */
    size_t Depth;             /**< Depth of 3D array */

    CUarray_format Format;    /**< Array format */
    unsigned int NumChannels; /**< Channels per array element */
    unsigned int Flags;       /**< Flags */
} CUDA_ARRAY3D_DESCRIPTOR;

#endif /* __CUDA_API_VERSION >= 3020 */

#if __CUDA_API_VERSION >= 5000

/**
 * CUDA Resource descriptor
 */
typedef struct CUDA_RESOURCE_DESC_st
{
    CUresourcetype resType;                   /**< Resource type */

    union {
        struct {
            CUarray hArray;                   /**< CUDA array */
        } array;
        struct {
            CUmipmappedArray hMipmappedArray; /**< CUDA mipmapped array */
        } mipmap;
        struct {
            CUdeviceptr devPtr;               /**< Device pointer */
            CUarray_format format;            /**< Array format */
            unsigned int numChannels;         /**< Channels per array element */
            size_t sizeInBytes;               /**< Size in bytes */
        } linear;
        struct {
            CUdeviceptr devPtr;               /**< Device pointer */
            CUarray_format format;            /**< Array format */
            unsigned int numChannels;         /**< Channels per array element */
            size_t width;                     /**< Width of the array in elements */
            size_t height;                    /**< Height of the array in elements */
            size_t pitchInBytes;              /**< Pitch between two rows in bytes */
        } pitch2D;
        struct {
            int reserved[32];
        } reserved;
    } res;

    unsigned int flags;                       /**< Flags (must be zero) */
} CUDA_RESOURCE_DESC;

/**
 * Texture descriptor
 */
typedef struct CUDA_TEXTURE_DESC_st {
    CUaddress_mode addressMode[3];  /**< Address modes */
    CUfilter_mode filterMode;       /**< Filter mode */
    unsigned int flags;             /**< Flags */
    unsigned int maxAnisotropy;     /**< Maximum anisotropy ratio */
    CUfilter_mode mipmapFilterMode; /**< Mipmap filter mode */
    float mipmapLevelBias;          /**< Mipmap level bias */
    float minMipmapLevelClamp;      /**< Mipmap minimum level clamp */
    float maxMipmapLevelClamp;      /**< Mipmap maximum level clamp */
    float borderColor[4];           /**< Border Color */
    int reserved[12];
} CUDA_TEXTURE_DESC;

/**
 * Resource view format
 */
typedef enum CUresourceViewFormat_enum
{
    CU_RES_VIEW_FORMAT_NONE = 0x00, /**< No resource view format (use underlying resource format) */
    CU_RES_VIEW_FORMAT_UINT_1X8 = 0x01, /**< 1 channel unsigned 8-bit integers */
    CU_RES_VIEW_FORMAT_UINT_2X8 = 0x02, /**< 2 channel unsigned 8-bit integers */
    CU_RES_VIEW_FORMAT_UINT_4X8 = 0x03, /**< 4 channel unsigned 8-bit integers */
    CU_RES_VIEW_FORMAT_SINT_1X8 = 0x04, /**< 1 channel signed 8-bit integers */
    CU_RES_VIEW_FORMAT_SINT_2X8 = 0x05, /**< 2 channel signed 8-bit integers */
    CU_RES_VIEW_FORMAT_SINT_4X8 = 0x06, /**< 4 channel signed 8-bit integers */
    CU_RES_VIEW_FORMAT_UINT_1X16 = 0x07, /**< 1 channel unsigned 16-bit integers */
    CU_RES_VIEW_FORMAT_UINT_2X16 = 0x08, /**< 2 channel unsigned 16-bit integers */
    CU_RES_VIEW_FORMAT_UINT_4X16 = 0x09, /**< 4 channel unsigned 16-bit integers */
    CU_RES_VIEW_FORMAT_SINT_1X16 = 0x0a, /**< 1 channel signed 16-bit integers */
    CU_RES_VIEW_FORMAT_SINT_2X16 = 0x0b, /**< 2 channel signed 16-bit integers */
    CU_RES_VIEW_FORMAT_SINT_4X16 = 0x0c, /**< 4 channel signed 16-bit integers */
    CU_RES_VIEW_FORMAT_UINT_1X32 = 0x0d, /**< 1 channel unsigned 32-bit integers */
    CU_RES_VIEW_FORMAT_UINT_2X32 = 0x0e, /**< 2 channel unsigned 32-bit integers */
    CU_RES_VIEW_FORMAT_UINT_4X32 = 0x0f, /**< 4 channel unsigned 32-bit integers */
    CU_RES_VIEW_FORMAT_SINT_1X32 = 0x10, /**< 1 channel signed 32-bit integers */
    CU_RES_VIEW_FORMAT_SINT_2X32 = 0x11, /**< 2 channel signed 32-bit integers */
    CU_RES_VIEW_FORMAT_SINT_4X32 = 0x12, /**< 4 channel signed 32-bit integers */
    CU_RES_VIEW_FORMAT_FLOAT_1X16 = 0x13, /**< 1 channel 16-bit floating point */
    CU_RES_VIEW_FORMAT_FLOAT_2X16 = 0x14, /**< 2 channel 16-bit floating point */
    CU_RES_VIEW_FORMAT_FLOAT_4X16 = 0x15, /**< 4 channel 16-bit floating point */
    CU_RES_VIEW_FORMAT_FLOAT_1X32 = 0x16, /**< 1 channel 32-bit floating point */
    CU_RES_VIEW_FORMAT_FLOAT_2X32 = 0x17, /**< 2 channel 32-bit floating point */
    CU_RES_VIEW_FORMAT_FLOAT_4X32 = 0x18, /**< 4 channel 32-bit floating point */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC1 = 0x19, /**< Block compressed 1 */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC2 = 0x1a, /**< Block compressed 2 */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC3 = 0x1b, /**< Block compressed 3 */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC4 = 0x1c, /**< Block compressed 4 unsigned */
    CU_RES_VIEW_FORMAT_SIGNED_BC4 = 0x1d, /**< Block compressed 4 signed */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC5 = 0x1e, /**< Block compressed 5 unsigned */
    CU_RES_VIEW_FORMAT_SIGNED_BC5 = 0x1f, /**< Block compressed 5 signed */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC6H = 0x20, /**< Block compressed 6 unsigned half-float */
    CU_RES_VIEW_FORMAT_SIGNED_BC6H = 0x21, /**< Block compressed 6 signed half-float */
    CU_RES_VIEW_FORMAT_UNSIGNED_BC7 = 0x22  /**< Block compressed 7 */
} CUresourceViewFormat;

/**
 * Resource view descriptor
 */
typedef struct CUDA_RESOURCE_VIEW_DESC_st
{
    CUresourceViewFormat format;   /**< Resource view format */
    size_t width;                  /**< Width of the resource view */
    size_t height;                 /**< Height of the resource view */
    size_t depth;                  /**< Depth of the resource view */
    unsigned int firstMipmapLevel; /**< First defined mipmap level */
    unsigned int lastMipmapLevel;  /**< Last defined mipmap level */
    unsigned int firstLayer;       /**< First layer index */
    unsigned int lastLayer;        /**< Last layer index */
    unsigned int reserved[16];
} CUDA_RESOURCE_VIEW_DESC;

/**
 * GPU Direct v3 tokens
 */
typedef struct CUDA_POINTER_ATTRIBUTE_P2P_TOKENS_st {
    unsigned long long p2pToken;
    unsigned int vaSpaceToken;
} CUDA_POINTER_ATTRIBUTE_P2P_TOKENS;

#endif /* __CUDA_API_VERSION >= 5000 */

/**
* Access flags that specify the level of access the current context's device has
* on the memory referenced.
*/
typedef enum CUDA_POINTER_ATTRIBUTE_ACCESS_FLAGS_enum {
    CU_POINTER_ATTRIBUTE_ACCESS_FLAG_NONE = 0x0,   /**< No access, meaning the device cannot access this memory at all, thus must be staged through accessible memory in order to complete certain operations */
    CU_POINTER_ATTRIBUTE_ACCESS_FLAG_READ = 0x1,   /**< Read-only access, meaning writes to this memory are considered invalid accesses and thus return error in that case. */
    CU_POINTER_ATTRIBUTE_ACCESS_FLAG_READWRITE = 0x3    /**< Read-write access, the device has full read-write access to the memory */
} CUDA_POINTER_ATTRIBUTE_ACCESS_FLAGS;

/**
 * Kernel launch parameters
 */
typedef struct CUDA_LAUNCH_PARAMS_st {
    CUfunction function;         /**< Kernel to launch */
    unsigned int gridDimX;       /**< Width of grid in blocks */
    unsigned int gridDimY;       /**< Height of grid in blocks */
    unsigned int gridDimZ;       /**< Depth of grid in blocks */
    unsigned int blockDimX;      /**< X dimension of each thread block */
    unsigned int blockDimY;      /**< Y dimension of each thread block */
    unsigned int blockDimZ;      /**< Z dimension of each thread block */
    unsigned int sharedMemBytes; /**< Dynamic shared-memory size per thread block in bytes */
    CUstream hStream;            /**< Stream identifier */
    void **kernelParams;         /**< Array of pointers to kernel parameters */
} CUDA_LAUNCH_PARAMS;

/**
 * External memory handle types
 */
typedef enum CUexternalMemoryHandleType_enum {
    /**
     * Handle is an opaque file descriptor
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD = 1,
    /**
     * Handle is an opaque shared NT handle
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    /**
     * Handle is an opaque, globally shared handle
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    /**
     * Handle is a D3D12 heap object
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP = 4,
    /**
     * Handle is a D3D12 committed resource
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE = 5,
    /**
     * Handle is a shared NT handle to a D3D11 resource
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE = 6,
    /**
     * Handle is a globally shared handle to a D3D11 resource
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE_KMT = 7,
    /**
     * Handle is an NvSciBuf object
     */
    CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF = 8
} CUexternalMemoryHandleType;

/**
 * Indicates that the external memory object is a dedicated resource
 */
#define CUDA_EXTERNAL_MEMORY_DEDICATED   0x1

 /** When the /p flags parameter of ::CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS
  * contains this flag, it indicates that signaling an external semaphore object
  * should skip performing appropriate memory synchronization operations over all
  * the external memory objects that are imported as ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF,
  * which otherwise are performed by default to ensure data coherency with other
  * importers of the same NvSciBuf memory objects.
  */
#define CUDA_EXTERNAL_SEMAPHORE_SIGNAL_SKIP_NVSCIBUF_MEMSYNC 0x01

  /** When the /p flags parameter of ::CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS
   * contains this flag, it indicates that waiting on an external semaphore object
   * should skip performing appropriate memory synchronization operations over all
   * the external memory objects that are imported as ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF,
   * which otherwise are performed by default to ensure data coherency with other
   * importers of the same NvSciBuf memory objects.
   */
#define CUDA_EXTERNAL_SEMAPHORE_WAIT_SKIP_NVSCIBUF_MEMSYNC 0x02

   /**
    * When /p flags of ::cuDeviceGetNvSciSyncAttributes is set to this,
    * it indicates that application needs signaler specific NvSciSyncAttr
    * to be filled by ::cuDeviceGetNvSciSyncAttributes.
    */
#define CUDA_NVSCISYNC_ATTR_SIGNAL 0x1

    /**
     * When /p flags of ::cuDeviceGetNvSciSyncAttributes is set to this,
     * it indicates that application needs waiter specific NvSciSyncAttr
     * to be filled by ::cuDeviceGetNvSciSyncAttributes.
     */
#define CUDA_NVSCISYNC_ATTR_WAIT 0x2
     /**
      * External memory handle descriptor
      */
typedef struct CUDA_EXTERNAL_MEMORY_HANDLE_DESC_st {
    /**
     * Type of the handle
     */
    CUexternalMemoryHandleType type;
    union {
        /**
         * File descriptor referencing the memory object. Valid
         * when type is
         * ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD
         */
        int fd;
        /**
         * Win32 handle referencing the semaphore object. Valid when
         * type is one of the following:
         * - ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32
         * - ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT
         * - ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP
         * - ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE
         * - ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE
         * - ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE_KMT
         * Exactly one of 'handle' and 'name' must be non-NULL. If
         * type is one of the following:
         * ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT
         * ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE_KMT
         * then 'name' must be NULL.
         */
        struct {
            /**
             * Valid NT handle. Must be NULL if 'name' is non-NULL
             */
            void *handle;
            /**
             * Name of a valid memory object.
             * Must be NULL if 'handle' is non-NULL.
             */
            const void *name;
        } win32;
        /**
         * A handle representing an NvSciBuf Object. Valid when type
         * is ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF
         */
        const void *nvSciBufObject;
    } handle;
    /**
     * Size of the memory allocation
     */
    unsigned long long size;
    /**
     * Flags must either be zero or ::CUDA_EXTERNAL_MEMORY_DEDICATED
     */
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_MEMORY_HANDLE_DESC;

/**
 * External memory buffer descriptor
 */
typedef struct CUDA_EXTERNAL_MEMORY_BUFFER_DESC_st {
    /**
     * Offset into the memory object where the buffer's base is
     */
    unsigned long long offset;
    /**
     * Size of the buffer
     */
    unsigned long long size;
    /**
     * Flags reserved for future use. Must be zero.
     */
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_MEMORY_BUFFER_DESC;

/**
 * External memory mipmap descriptor
 */
typedef struct CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC_st {
    /**
     * Offset into the memory object where the base level of the
     * mipmap chain is.
     */
    unsigned long long offset;
    /**
     * Format, dimension and type of base level of the mipmap chain
     */
    CUDA_ARRAY3D_DESCRIPTOR arrayDesc;
    /**
     * Total number of levels in the mipmap chain
     */
    unsigned int numLevels;
    unsigned int reserved[16];
} CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC;

/**
 * External semaphore handle types
 */
typedef enum CUexternalSemaphoreHandleType_enum {
    /**
     * Handle is an opaque file descriptor
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD = 1,
    /**
     * Handle is an opaque shared NT handle
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32 = 2,
    /**
     * Handle is an opaque, globally shared handle
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3,
    /**
     * Handle is a shared NT handle referencing a D3D12 fence object
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE = 4,
    /**
     * Handle is a shared NT handle referencing a D3D11 fence object
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE = 5,
    /**
     * Opaque handle to NvSciSync Object
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_NVSCISYNC = 6,
    /**
     * Handle is a shared NT handle referencing a D3D11 keyed mutex object
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_KEYED_MUTEX = 7,
    /**
     * Handle is a globally shared handle referencing a D3D11 keyed mutex object
     */
    CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_KEYED_MUTEX_KMT = 8
} CUexternalSemaphoreHandleType;

/**
 * External semaphore handle descriptor
 */
typedef struct CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC_st {
    /**
     * Type of the handle
     */
    CUexternalSemaphoreHandleType type;
    union {
        /**
         * File descriptor referencing the semaphore object. Valid
         * when type is
         * ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD
         */
        int fd;
        /**
         * Win32 handle referencing the semaphore object. Valid when
         * type is one of the following:
         * - ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32
         * - ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT
         * - ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE
         * - ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE
         * - ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_KEYED_MUTEX
         * Exactly one of 'handle' and 'name' must be non-NULL. If
         * type is one of the following:
         * ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_KMT
         * ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_KEYED_MUTEX_KMT
         * then 'name' must be NULL.
         */
        struct {
            /**
             * Valid NT handle. Must be NULL if 'name' is non-NULL
             */
            void *handle;
            /**
             * Name of a valid synchronization primitive.
             * Must be NULL if 'handle' is non-NULL.
             */
            const void *name;
        } win32;
        /**
         * Valid NvSciSyncObj. Must be non NULL
         */
        const void* nvSciSyncObj;
    } handle;
    /**
     * Flags reserved for the future. Must be zero.
     */
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC;

/**
 * External semaphore signal parameters
 */
typedef struct CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS_st {
    struct {
        /**
         * Parameters for fence objects
         */
        struct {
            /**
             * Value of fence to be signaled
             */
            unsigned long long value;
        } fence;
        union {
            /**
             * Pointer to NvSciSyncFence. Valid if ::CUexternalSemaphoreHandleType
             * is of type ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_NVSCISYNC.
             */
            void *fence;
            unsigned long long reserved;
        } nvSciSync;
        /**
         * Parameters for keyed mutex objects
         */
        struct {
            /**
             * Value of key to release the mutex with
             */
            unsigned long long key;
        } keyedMutex;
        unsigned int reserved[12];
    } params;
    /**
     * Only when ::CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS is used to
     * signal a ::CUexternalSemaphore of type
     * ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_NVSCISYNC, the valid flag is
     * ::CUDA_EXTERNAL_SEMAPHORE_SIGNAL_SKIP_NVSCIBUF_MEMSYNC which indicates
     * that while signaling the ::CUexternalSemaphore, no memory synchronization
     * operations should be performed for any external memory object imported
     * as ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF.
     * For all other types of ::CUexternalSemaphore, flags must be zero.
     */
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS;

/**
 * External semaphore wait parameters
 */
typedef struct CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS_st {
    struct {
        /**
         * Parameters for fence objects
         */
        struct {
            /**
             * Value of fence to be waited on
             */
            unsigned long long value;
        } fence;
        /**
         * Pointer to NvSciSyncFence. Valid if CUexternalSemaphoreHandleType
         * is of type CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_NVSCISYNC.
         */
        union {
            void *fence;
            unsigned long long reserved;
        } nvSciSync;
        /**
         * Parameters for keyed mutex objects
         */
        struct {
            /**
             * Value of key to acquire the mutex with
             */
            unsigned long long key;
            /**
             * Timeout in milliseconds to wait to acquire the mutex
             */
            unsigned int timeoutMs;
        } keyedMutex;
        unsigned int reserved[10];
    } params;
    /**
     * Only when ::CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS is used to wait on
     * a ::CUexternalSemaphore of type ::CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_NVSCISYNC,
     * the valid flag is ::CUDA_EXTERNAL_SEMAPHORE_WAIT_SKIP_NVSCIBUF_MEMSYNC
     * which indicates that while waiting for the ::CUexternalSemaphore, no memory
     * synchronization operations should be performed for any external memory
     * object imported as ::CU_EXTERNAL_MEMORY_HANDLE_TYPE_NVSCIBUF.
     * For all other types of ::CUexternalSemaphore, flags must be zero.
     */
    unsigned int flags;
    unsigned int reserved[16];
} CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS;


typedef unsigned long long CUmemGenericAllocationHandle;

/**
 * Flags for specifying particular handle types
 */
typedef enum CUmemAllocationHandleType_enum {
    CU_MEM_HANDLE_TYPE_POSIX_FILE_DESCRIPTOR = 0x1,  /**< Allows a file descriptor to be used for exporting. Permitted only on POSIX systems. (int) */
    CU_MEM_HANDLE_TYPE_WIN32 = 0x2,  /**< Allows a Win32 NT handle to be used for exporting. (HANDLE) */
    CU_MEM_HANDLE_TYPE_WIN32_KMT = 0x4,  /**< Allows a Win32 KMT handle to be used for exporting. (D3DKMT_HANDLE) */
    CU_MEM_HANDLE_TYPE_MAX = 0xFFFFFFFF
} CUmemAllocationHandleType;

/**
 * Specifies the memory protection flags for mapping.
 */
typedef enum CUmemAccess_flags_enum {
    CU_MEM_ACCESS_FLAGS_PROT_NONE = 0x0,  /**< Default, make the address range not accessible */
    CU_MEM_ACCESS_FLAGS_PROT_READ = 0x1,  /**< Make the address range read accessible */
    CU_MEM_ACCESS_FLAGS_PROT_READWRITE = 0x3,  /**< Make the address range read-write accessible */
    CU_MEM_ACCESS_FLAGS_PROT_MAX = 0xFFFFFFFF
} CUmemAccess_flags;

/**
 * Specifies the type of location
 */
typedef enum CUmemLocationType_enum {
    CU_MEM_LOCATION_TYPE_INVALID = 0x0,
    CU_MEM_LOCATION_TYPE_DEVICE = 0x1,  /**< Location is a device location, thus id is a device ordinal */
    CU_MEM_LOCATION_TYPE_MAX = 0xFFFFFFFF
} CUmemLocationType;

/**
* Defines the allocation types available
*/
typedef enum CUmemAllocationType_enum {
    CU_MEM_ALLOCATION_TYPE_INVALID = 0x0,

    /** This allocation type is 'pinned', i.e. cannot migrate from its current
      * location while the application is actively using it
      */
    CU_MEM_ALLOCATION_TYPE_PINNED = 0x1,
    CU_MEM_ALLOCATION_TYPE_MAX = 0xFFFFFFFF
} CUmemAllocationType;

/**
* Flag for requesting different optimal and required granularities for an allocation.
*/
typedef enum CUmemAllocationGranularity_flags_enum {
    CU_MEM_ALLOC_GRANULARITY_MINIMUM = 0x0,     /**< Minimum required granularity for allocation */
    CU_MEM_ALLOC_GRANULARITY_RECOMMENDED = 0x1      /**< Recommended granularity for allocation for best performance */
} CUmemAllocationGranularity_flags;

/**
 * Sparse subresource types
 */
typedef enum CUarraySparseSubresourceType_enum {
    CU_ARRAY_SPARSE_SUBRESOURCE_TYPE_SPARSE_LEVEL = 0,
    CU_ARRAY_SPARSE_SUBRESOURCE_TYPE_MIPTAIL = 1
} CUarraySparseSubresourceType;

/**
 * Memory operation types
 */
typedef enum CUmemOperationType_enum {
    CU_MEM_OPERATION_TYPE_MAP = 1,
    CU_MEM_OPERATION_TYPE_UNMAP = 2
} CUmemOperationType;

/**
 * Memory handle types
 */
typedef enum CUmemHandleType_enum {
    CU_MEM_HANDLE_TYPE_GENERIC = 0
} CUmemHandleType;

/**
 * Specifies the CUDA array or CUDA mipmapped array memory mapping information
 */
typedef struct CUarrayMapInfo_st {
    CUresourcetype resourceType;                    /**< Resource type */

    union {
        CUmipmappedArray mipmap;
        CUarray array;
    } resource;

    CUarraySparseSubresourceType subresourceType;   /**< Sparse subresource type */

    union {
        struct {
            unsigned int level;                     /**< For CUDA mipmapped arrays must a valid mipmap level. For CUDA arrays must be zero */
            unsigned int layer;                     /**< For CUDA layered arrays must be a valid layer index. Otherwise, must be zero */
            unsigned int offsetX;                   /**< Starting X offset in elements */
            unsigned int offsetY;                   /**< Starting Y offset in elements */
            unsigned int offsetZ;                   /**< Starting Z offset in elements */
            unsigned int extentWidth;               /**< Width in elements */
            unsigned int extentHeight;              /**< Height in elements */
            unsigned int extentDepth;               /**< Depth in elements */
        } sparseLevel;
        struct {
            unsigned int layer;                     /**< For CUDA layered arrays must be a valid layer index. Otherwise, must be zero */
            unsigned long long offset;              /**< Offset within mip tail */
            unsigned long long size;                /**< Extent in bytes */
        } miptail;
    } subresource;

    CUmemOperationType memOperationType;            /**< Memory operation type */
    CUmemHandleType memHandleType;                  /**< Memory handle type */

    union {
        CUmemGenericAllocationHandle memHandle;
    } memHandle;

    unsigned long long offset;                      /**< Offset within the memory */
    unsigned int deviceBitMask;                     /**< Device ordinal bit mask */
    unsigned int flags;                             /**< flags for future use, must be zero now. */
    unsigned int reserved[2];                       /**< Reserved for future use, must be zero now. */
} CUarrayMapInfo;

/**
 * Specifies a location for an allocation.
 */
typedef struct CUmemLocation_st {
    CUmemLocationType type; /**< Specifies the location type, which modifies the meaning of id. */
    int id;                 /**< identifier for a given this location's ::CUmemLocationType. */
} CUmemLocation;

/**
 * Specifies compression attribute for an allocation.
 */
typedef enum CUmemAllocationCompType_enum {
    CU_MEM_ALLOCATION_COMP_NONE = 0x0, /**< Allocating non-compressible memory */
    CU_MEM_ALLOCATION_COMP_GENERIC = 0x1 /**< Allocating  compressible memory */
} CUmemAllocationCompType;

/**
 * This flag if set indicates that the memory will be used as a tile pool.
 */
#define CU_MEM_CREATE_USAGE_TILE_POOL    0x1

 /**
 * Specifies the allocation properties for a allocation.
 */
typedef struct CUmemAllocationProp_st {
    /** Allocation type */
    CUmemAllocationType type;
    /** requested ::CUmemAllocationHandleType */
    CUmemAllocationHandleType requestedHandleTypes;
    /** Location of allocation */
    CUmemLocation location;
    /**
     * Windows-specific LPSECURITYATTRIBUTES required when
     * ::CU_MEM_HANDLE_TYPE_WIN32 is specified.  This security attribute defines
     * the scope of which exported allocations may be tranferred to other
     * processes.  In all other cases, this field is required to be zero.
     */
    void *win32HandleMetaData;
    struct {
        /**
        * Allocation hint for requesting compressible memory.
        * On devices that support Compute Data Compression, compressible
        * memory can be used to accelerate accesses to data with unstructured
        * sparsity and other compressible data patterns. Applications are
        * expected to query allocation property of the handle obtained with
        * ::cuMemCreate using ::cuMemGetAllocationPropertiesFromHandle to
        * validate if the obtained allocation is compressible or not. Note that
        * compressed memory may not be mappable on all devices.
        */
        unsigned char compressionType;
        unsigned char gpuDirectRDMACapable;
        /** Bitmask indicating intended usage for this allocation */
        unsigned short usage;
        unsigned char reserved[4];
    } allocFlags;
} CUmemAllocationProp;

/**
* Memory access descriptor
*/
typedef struct CUmemAccessDesc_st {
    CUmemLocation location;         /**< Location on which the request is to change it's accessibility */
    CUmemAccess_flags flags;       /**< ::CUmemProt accessibility flags to set on the request */
} CUmemAccessDesc;

typedef enum CUgraphExecUpdateResult_enum {
    CU_GRAPH_EXEC_UPDATE_SUCCESS = 0x0, /**< The update succeeded */
    CU_GRAPH_EXEC_UPDATE_ERROR = 0x1, /**< The update failed for an unexpected reason which is described in the return value of the function */
    CU_GRAPH_EXEC_UPDATE_ERROR_TOPOLOGY_CHANGED = 0x2, /**< The update failed because the topology changed */
    CU_GRAPH_EXEC_UPDATE_ERROR_NODE_TYPE_CHANGED = 0x3, /**< The update failed because a node type changed */
    CU_GRAPH_EXEC_UPDATE_ERROR_FUNCTION_CHANGED = 0x4, /**< The update failed because the function of a kernel node changed */
    CU_GRAPH_EXEC_UPDATE_ERROR_PARAMETERS_CHANGED = 0x5, /**< The update failed because the parameters changed in a way that is not supported */
    CU_GRAPH_EXEC_UPDATE_ERROR_NOT_SUPPORTED = 0x6  /**< The update failed because something about the node is not supported */
} CUgraphExecUpdateResult;

/**
 * If set, each kernel launched as part of ::cuLaunchCooperativeKernelMultiDevice only
 * waits for prior work in the stream corresponding to that GPU to complete before the
 * kernel begins execution.
 */
#define CUDA_COOPERATIVE_LAUNCH_MULTI_DEVICE_NO_PRE_LAUNCH_SYNC   0x01

 /**
  * If set, any subsequent work pushed in a stream that participated in a call to
  * ::cuLaunchCooperativeKernelMultiDevice will only wait for the kernel launched on
  * the GPU corresponding to that stream to complete before it begins execution.
  */
#define CUDA_COOPERATIVE_LAUNCH_MULTI_DEVICE_NO_POST_LAUNCH_SYNC  0x02

/**
 * If set, the CUDA array is a collection of layers, where each layer is either a 1D
 * or a 2D array and the Depth member of CUDA_ARRAY3D_DESCRIPTOR specifies the number
 * of layers, not the depth of a 3D array.
 */
#define CUDA_ARRAY3D_LAYERED        0x01

/**
 * Deprecated, use CUDA_ARRAY3D_LAYERED
 */
#define CUDA_ARRAY3D_2DARRAY        0x01

/**
 * This flag must be set in order to bind a surface reference
 * to the CUDA array
 */
#define CUDA_ARRAY3D_SURFACE_LDST   0x02

/**
 * Override the texref format with a format inferred from the array.
 * Flag for ::cuTexRefSetArray()
 */
#define CU_TRSA_OVERRIDE_FORMAT 0x01

/**
 * Read the texture as integers rather than promoting the values to floats
 * in the range [0,1].
 * Flag for ::cuTexRefSetFlags()
 */
#define CU_TRSF_READ_AS_INTEGER         0x01

/**
 * Use normalized texture coordinates in the range [0,1) instead of [0,dim).
 * Flag for ::cuTexRefSetFlags()
 */
#define CU_TRSF_NORMALIZED_COORDINATES  0x02

/**
 * Perform sRGB->linear conversion during texture read.
 * Flag for ::cuTexRefSetFlags()
 */
#define CU_TRSF_SRGB  0x10

/**
 * End of array terminator for the \p extra parameter to
 * ::cuLaunchKernel
 */
#define CU_LAUNCH_PARAM_END            ((void*)0x00)

/**
 * Indicator that the next value in the \p extra parameter to
 * ::cuLaunchKernel will be a pointer to a buffer containing all kernel
 * parameters used for launching kernel \p f.  This buffer needs to
 * honor all alignment/padding requirements of the individual parameters.
 * If ::CU_LAUNCH_PARAM_BUFFER_SIZE is not also specified in the
 * \p extra array, then ::CU_LAUNCH_PARAM_BUFFER_POINTER will have no
 * effect.
 */
#define CU_LAUNCH_PARAM_BUFFER_POINTER ((void*)0x01)

/**
 * Indicator that the next value in the \p extra parameter to
 * ::cuLaunchKernel will be a pointer to a size_t which contains the
 * size of the buffer specified with ::CU_LAUNCH_PARAM_BUFFER_POINTER.
 * It is required that ::CU_LAUNCH_PARAM_BUFFER_POINTER also be specified
 * in the \p extra array if the value associated with
 * ::CU_LAUNCH_PARAM_BUFFER_SIZE is not zero.
 */
#define CU_LAUNCH_PARAM_BUFFER_SIZE    ((void*)0x02)

/**
 * For texture references loaded into the module, use default texunit from
 * texture reference.
 */
#define CU_PARAM_TR_DEFAULT -1

/**
 * CUDA API made obselete at API version 3020
 */
#if defined(__CUDA_API_VERSION_INTERNAL)
    #define CUdeviceptr                  CUdeviceptr_v1
    #define CUDA_MEMCPY2D_st             CUDA_MEMCPY2D_v1_st
    #define CUDA_MEMCPY2D                CUDA_MEMCPY2D_v1
    #define CUDA_MEMCPY3D_st             CUDA_MEMCPY3D_v1_st
    #define CUDA_MEMCPY3D                CUDA_MEMCPY3D_v1
    #define CUDA_ARRAY_DESCRIPTOR_st     CUDA_ARRAY_DESCRIPTOR_v1_st
    #define CUDA_ARRAY_DESCRIPTOR        CUDA_ARRAY_DESCRIPTOR_v1
    #define CUDA_ARRAY3D_DESCRIPTOR_st   CUDA_ARRAY3D_DESCRIPTOR_v1_st
    #define CUDA_ARRAY3D_DESCRIPTOR      CUDA_ARRAY3D_DESCRIPTOR_v1
#endif /* CUDA_FORCE_LEGACY32_INTERNAL */

#if defined(__CUDA_API_VERSION_INTERNAL) || __CUDA_API_VERSION < 3020
typedef unsigned int CUdeviceptr;

typedef struct CUDA_MEMCPY2D_st
{
    unsigned int srcXInBytes;   /**< Source X in bytes */
    unsigned int srcY;          /**< Source Y */
    CUmemorytype srcMemoryType; /**< Source memory type (host, device, array) */
    const void *srcHost;        /**< Source host pointer */
    CUdeviceptr srcDevice;      /**< Source device pointer */
    CUarray srcArray;           /**< Source array reference */
    unsigned int srcPitch;      /**< Source pitch (ignored when src is array) */

    unsigned int dstXInBytes;   /**< Destination X in bytes */
    unsigned int dstY;          /**< Destination Y */
    CUmemorytype dstMemoryType; /**< Destination memory type (host, device, array) */
    void *dstHost;              /**< Destination host pointer */
    CUdeviceptr dstDevice;      /**< Destination device pointer */
    CUarray dstArray;           /**< Destination array reference */
    unsigned int dstPitch;      /**< Destination pitch (ignored when dst is array) */

    unsigned int WidthInBytes;  /**< Width of 2D memory copy in bytes */
    unsigned int Height;        /**< Height of 2D memory copy */
} CUDA_MEMCPY2D;

typedef struct CUDA_MEMCPY3D_st
{
    unsigned int srcXInBytes;   /**< Source X in bytes */
    unsigned int srcY;          /**< Source Y */
    unsigned int srcZ;          /**< Source Z */
    unsigned int srcLOD;        /**< Source LOD */
    CUmemorytype srcMemoryType; /**< Source memory type (host, device, array) */
    const void *srcHost;        /**< Source host pointer */
    CUdeviceptr srcDevice;      /**< Source device pointer */
    CUarray srcArray;           /**< Source array reference */
    void *reserved0;            /**< Must be NULL */
    unsigned int srcPitch;      /**< Source pitch (ignored when src is array) */
    unsigned int srcHeight;     /**< Source height (ignored when src is array; may be 0 if Depth==1) */

    unsigned int dstXInBytes;   /**< Destination X in bytes */
    unsigned int dstY;          /**< Destination Y */
    unsigned int dstZ;          /**< Destination Z */
    unsigned int dstLOD;        /**< Destination LOD */
    CUmemorytype dstMemoryType; /**< Destination memory type (host, device, array) */
    void *dstHost;              /**< Destination host pointer */
    CUdeviceptr dstDevice;      /**< Destination device pointer */
    CUarray dstArray;           /**< Destination array reference */
    void *reserved1;            /**< Must be NULL */
    unsigned int dstPitch;      /**< Destination pitch (ignored when dst is array) */
    unsigned int dstHeight;     /**< Destination height (ignored when dst is array; may be 0 if Depth==1) */

    unsigned int WidthInBytes;  /**< Width of 3D memory copy in bytes */
    unsigned int Height;        /**< Height of 3D memory copy */
    unsigned int Depth;         /**< Depth of 3D memory copy */
} CUDA_MEMCPY3D;

typedef struct CUDA_ARRAY_DESCRIPTOR_st
{
    unsigned int Width;         /**< Width of array */
    unsigned int Height;        /**< Height of array */

    CUarray_format Format;      /**< Array format */
    unsigned int NumChannels;   /**< Channels per array element */
} CUDA_ARRAY_DESCRIPTOR;

typedef struct CUDA_ARRAY3D_DESCRIPTOR_st
{
    unsigned int Width;         /**< Width of 3D array */
    unsigned int Height;        /**< Height of 3D array */
    unsigned int Depth;         /**< Depth of 3D array */

    CUarray_format Format;      /**< Array format */
    unsigned int NumChannels;   /**< Channels per array element */
    unsigned int Flags;         /**< Flags */
} CUDA_ARRAY3D_DESCRIPTOR;

#endif /* (__CUDA_API_VERSION_INTERNAL) || __CUDA_API_VERSION < 3020 */

/*
 * If set, the CUDA array contains an array of 2D slices
 * and the Depth member of CUDA_ARRAY3D_DESCRIPTOR specifies
 * the number of slices, not the depth of a 3D array.
 */
#define CUDA_ARRAY3D_2DARRAY        0x01

/**
 * This flag must be set in order to bind a surface reference
 * to the CUDA array
 */
#define CUDA_ARRAY3D_SURFACE_LDST   0x02

/**
 * Override the texref format with a format inferred from the array.
 * Flag for ::cuTexRefSetArray()
 */
#define CU_TRSA_OVERRIDE_FORMAT 0x01

/**
 * Read the texture as integers rather than promoting the values to floats
 * in the range [0,1].
 * Flag for ::cuTexRefSetFlags()
 */
#define CU_TRSF_READ_AS_INTEGER         0x01

/**
 * Use normalized texture coordinates in the range [0,1) instead of [0,dim).
 * Flag for ::cuTexRefSetFlags()
 */
#define CU_TRSF_NORMALIZED_COORDINATES  0x02

/**
 * Perform sRGB->linear conversion during texture read.
 * Flag for ::cuTexRefSetFlags()
 */
#define CU_TRSF_SRGB  0x10

/**
 * For texture references loaded into the module, use default texunit from
 * texture reference.
 */
#define CU_PARAM_TR_DEFAULT -1

/** @} */ /* END CUDA_TYPES */

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
    #define CUDAAPI __stdcall
#else
    #define CUDAAPI
#endif


typedef CUresult CUDAAPI tcuGetErrorString(CUresult error, const char **pStr);
typedef CUresult CUDAAPI tcuGetErrorName(CUresult error, const char **pStr);

/**
 * \defgroup CUDA_INITIALIZE Initialization
 *
 * This section describes the initialization functions of the low-level CUDA
 * driver application programming interface.
 *
 * @{
 */

/*********************************
 ** Initialization
 *********************************/
typedef CUresult  CUDAAPI tcuInit(unsigned int Flags);

/*********************************
 ** Driver Version Query
 *********************************/
typedef CUresult  CUDAAPI tcuDriverGetVersion(int *driverVersion);

/************************************
 **
 **    Device management
 **
 ***********************************/

typedef CUresult  CUDAAPI tcuDeviceGet(CUdevice *device, int ordinal);
typedef CUresult  CUDAAPI tcuDeviceGetCount(int *count);
typedef CUresult  CUDAAPI tcuDeviceGetName(char *name, int len, CUdevice dev);
typedef CUresult  CUDAAPI tcuDeviceGetUuid(CUuuid *uuid, CUdevice dev);
typedef CUresult  CUDAAPI tcuDeviceGetLuid(char *luid, unsigned int *deviceNodeMask, CUdevice dev);
typedef CUresult  CUDAAPI tcuDeviceComputeCapability(int *major, int *minor, CUdevice dev);
#if __CUDA_API_VERSION >= 3020
    typedef CUresult  CUDAAPI tcuDeviceTotalMem(size_t *bytes, CUdevice dev);
#else
    typedef CUresult  CUDAAPI tcuDeviceTotalMem(unsigned int *bytes, CUdevice dev);
#endif

typedef CUresult  CUDAAPI tcuDeviceGetProperties(CUdevprop *prop, CUdevice dev);
typedef CUresult  CUDAAPI tcuDeviceGetAttribute(int *pi, CUdevice_attribute attrib, CUdevice dev);

/************************************
 **
 **    Context management
 **
 ***********************************/
typedef CUresult  CUDAAPI tcuCtxCreate(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult  CUDAAPI tcuCtxDestroy(CUcontext ctx);
typedef CUresult  CUDAAPI tcuCtxAttach(CUcontext *pctx, unsigned int flags);
typedef CUresult  CUDAAPI tcuCtxDetach(CUcontext ctx);
typedef CUresult  CUDAAPI tcuCtxPushCurrent(CUcontext ctx);
typedef CUresult  CUDAAPI tcuCtxPopCurrent(CUcontext *pctx);
typedef CUresult  CUDAAPI tcuDevicePrimaryCtxRelease(CUdevice dev);
typedef CUresult  CUDAAPI tcuDevicePrimaryCtxReset(CUdevice dev);
typedef CUresult  CUDAAPI tcuDevicePrimaryCtxSetFlags(CUdevice dev, unsigned int flags);

typedef CUresult  CUDAAPI tcuCtxSetCurrent(CUcontext ctx);
typedef CUresult  CUDAAPI tcuCtxGetCurrent(CUcontext *pctx);

typedef CUresult  CUDAAPI tcuCtxGetDevice(CUdevice *device);
typedef CUresult  CUDAAPI tcuCtxSynchronize(void);
typedef CUresult  CUDAAPI tcuCtxGetApiVersion(CUcontext ctx, unsigned int* version);
typedef CUresult  CUDAAPI tcuCtxGetFlags(unsigned int* flags);


/************************************
 **
 **    Module management
 **
 ***********************************/
typedef CUresult  CUDAAPI tcuModuleLoad(CUmodule *module, const char *fname);
typedef CUresult  CUDAAPI tcuModuleLoadData(CUmodule *module, const void *image);
typedef CUresult  CUDAAPI tcuModuleLoadDataEx(CUmodule *module, const void *image, unsigned int numOptions, CUjit_option *options, void **optionValues);
typedef CUresult  CUDAAPI tcuModuleLoadFatBinary(CUmodule *module, const void *fatCubin);
typedef CUresult  CUDAAPI tcuModuleUnload(CUmodule hmod);
typedef CUresult  CUDAAPI tcuModuleGetFunction(CUfunction *hfunc, CUmodule hmod, const char *name);

#if __CUDA_API_VERSION >= 3020
    typedef CUresult  CUDAAPI tcuModuleGetGlobal(CUdeviceptr *dptr, size_t *bytes, CUmodule hmod, const char *name);
#else
    typedef CUresult  CUDAAPI tcuModuleGetGlobal(CUdeviceptr *dptr, unsigned int *bytes, CUmodule hmod, const char *name);
#endif

typedef CUresult  CUDAAPI tcuModuleGetTexRef(CUtexref *pTexRef, CUmodule hmod, const char *name);
typedef CUresult  CUDAAPI tcuModuleGetSurfRef(CUsurfref *pSurfRef, CUmodule hmod, const char *name);

/************************************
 **
 **    Memory management
 **
 ***********************************/
#if __CUDA_API_VERSION >= 3020
    typedef CUresult CUDAAPI tcuMemGetInfo(size_t *free, size_t *total);
    typedef CUresult CUDAAPI tcuMemAlloc(CUdeviceptr *dptr, size_t bytesize);
    typedef CUresult CUDAAPI tcuMemGetAddressRange(CUdeviceptr *pbase, size_t *psize, CUdeviceptr dptr);
    typedef CUresult CUDAAPI tcuMemAllocPitch(CUdeviceptr *dptr,
                                              size_t *pPitch,
                                              size_t WidthInBytes,
                                              size_t Height,
                                              // size of biggest r/w to be performed by kernels on this memory
                                              // 4, 8 or 16 bytes
                                              unsigned int ElementSizeBytes
                                             );
#else
    typedef CUresult CUDAAPI tcuMemGetInfo(unsigned int *free, unsigned int *total);
    typedef CUresult CUDAAPI tcuMemAlloc(CUdeviceptr *dptr, unsigned int bytesize);
    typedef CUresult CUDAAPI tcuMemGetAddressRange(CUdeviceptr *pbase, unsigned int *psize, CUdeviceptr dptr);
    typedef CUresult CUDAAPI tcuMemAllocPitch(CUdeviceptr *dptr,
                                              unsigned int *pPitch,
                                              unsigned int WidthInBytes,
                                              unsigned int Height,
                                              // size of biggest r/w to be performed by kernels on this memory
                                              // 4, 8 or 16 bytes
                                              unsigned int ElementSizeBytes
                                             );
#endif

typedef CUresult CUDAAPI tcuMemFree(CUdeviceptr dptr);

#if __CUDA_API_VERSION >= 3020
    typedef CUresult CUDAAPI tcuMemAllocHost(void **pp, size_t bytesize);
#else
    typedef CUresult CUDAAPI tcuMemAllocHost(void **pp, unsigned int bytesize);
#endif

typedef CUresult CUDAAPI tcuMemFreeHost(void *p);
typedef CUresult CUDAAPI tcuMemHostAlloc(void **pp, size_t bytesize, unsigned int Flags);

typedef CUresult CUDAAPI tcuMemHostGetDevicePointer(CUdeviceptr *pdptr, void *p, unsigned int Flags);
typedef CUresult CUDAAPI tcuMemHostGetFlags(unsigned int *pFlags, void *p);

typedef CUresult CUDAAPI tcuMemHostRegister(void *p, size_t bytesize, unsigned int Flags);
typedef CUresult CUDAAPI tcuMemHostUnregister(void *p);;
typedef CUresult CUDAAPI tcuMemcpy(CUdeviceptr dst, CUdeviceptr src, size_t ByteCount);
typedef CUresult CUDAAPI tcuMemcpyPeer(CUdeviceptr dstDevice, CUcontext dstContext, CUdeviceptr srcDevice, CUcontext srcContext, size_t ByteCount);

/************************************
 **
 **    Synchronous Memcpy
 **
 ** Intra-device memcpy's done with these functions may execute in parallel with the CPU,
 ** but if host memory is involved, they wait until the copy is done before returning.
 **
 ***********************************/
// 1D functions
#if __CUDA_API_VERSION >= 3020
    // system <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoD(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);
    typedef CUresult  CUDAAPI tcuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount);

    // device <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice, size_t ByteCount);

    // device <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyDtoA(CUarray dstArray, size_t dstOffset, CUdeviceptr srcDevice, size_t ByteCount);
    typedef CUresult  CUDAAPI tcuMemcpyAtoD(CUdeviceptr dstDevice, CUarray srcArray, size_t srcOffset, size_t ByteCount);

    // system <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoA(CUarray dstArray, size_t dstOffset, const void *srcHost, size_t ByteCount);
    typedef CUresult  CUDAAPI tcuMemcpyAtoH(void *dstHost, CUarray srcArray, size_t srcOffset, size_t ByteCount);

    // array <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyAtoA(CUarray dstArray, size_t dstOffset, CUarray srcArray, size_t srcOffset, size_t ByteCount);
#else
    // system <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoD(CUdeviceptr dstDevice, const void *srcHost, unsigned int ByteCount);
    typedef CUresult  CUDAAPI tcuMemcpyDtoH(void *dstHost, CUdeviceptr srcDevice, unsigned int ByteCount);

    // device <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyDtoD(CUdeviceptr dstDevice, CUdeviceptr srcDevice, unsigned int ByteCount);

    // device <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyDtoA(CUarray dstArray, unsigned int dstOffset, CUdeviceptr srcDevice, unsigned int ByteCount);
    typedef CUresult  CUDAAPI tcuMemcpyAtoD(CUdeviceptr dstDevice, CUarray srcArray, unsigned int srcOffset, unsigned int ByteCount);

    // system <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoA(CUarray dstArray, unsigned int dstOffset, const void *srcHost, unsigned int ByteCount);
    typedef CUresult  CUDAAPI tcuMemcpyAtoH(void *dstHost, CUarray srcArray, unsigned int srcOffset, unsigned int ByteCount);

    // array <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyAtoA(CUarray dstArray, unsigned int dstOffset, CUarray srcArray, unsigned int srcOffset, unsigned int ByteCount);
#endif

// 2D memcpy
typedef CUresult  CUDAAPI tcuMemcpy2D(const CUDA_MEMCPY2D *pCopy);
typedef CUresult  CUDAAPI tcuMemcpy2DUnaligned(const CUDA_MEMCPY2D *pCopy);

// 3D memcpy
typedef CUresult  CUDAAPI tcuMemcpy3D(const CUDA_MEMCPY3D *pCopy);

/************************************
 **
 **    Asynchronous Memcpy
 **
 ** Any host memory involved must be DMA'able (e.g., allocated with cuMemAllocHost).
 ** memcpy's done with these functions execute in parallel with the CPU and, if
 ** the hardware is available, may execute in parallel with the GPU.
 ** Asynchronous memcpy must be accompanied by appropriate stream synchronization.
 **
 ***********************************/

#if __CUDA_API_VERSION >= 4000
    typedef CUresult CUDAAPI tcuMemcpyAsync(CUdeviceptr dst, CUdeviceptr src, size_t ByteCount, CUstream hStream);
    typedef CUresult CUDAAPI tcuMemcpyPeerAsync(CUdeviceptr dstDevice, CUcontext dstContext, CUdeviceptr srcDevice, CUcontext srcContext, size_t ByteCount, CUstream hStream);
    typedef CUresult CUDAAPI tcuDeviceCanAccessPeer(int *canAccessPeer, CUdevice dev, CUdevice peerDev);
    typedef CUresult CUDAAPI tcuCtxEnablePeerAccess(CUcontext peerContext, unsigned int Flags);
    typedef CUresult CUDAAPI tcuCtxDisablePeerAccess(CUcontext peerContext);
#endif

// 1D functions
#if __CUDA_API_VERSION >= 3020
    // system <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoDAsync(CUdeviceptr dstDevice,
                                                 const void *srcHost, size_t ByteCount, CUstream hStream);
    typedef CUresult  CUDAAPI tcuMemcpyDtoHAsync(void *dstHost,
                                                 CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);

    // device <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyDtoDAsync(CUdeviceptr dstDevice,
                                                 CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);

    // system <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoAAsync(CUarray dstArray, size_t dstOffset,
                                                 const void *srcHost, size_t ByteCount, CUstream hStream);
    typedef CUresult  CUDAAPI tcuMemcpyAtoHAsync(void *dstHost, CUarray srcArray, size_t srcOffset,
                                                 size_t ByteCount, CUstream hStream);
#else
    // system <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoDAsync(CUdeviceptr dstDevice,
                                                 const void *srcHost, unsigned int ByteCount, CUstream hStream);
    typedef CUresult  CUDAAPI tcuMemcpyDtoHAsync(void *dstHost,
                                                 CUdeviceptr srcDevice, unsigned int ByteCount, CUstream hStream);

    // device <-> device memory
    typedef CUresult  CUDAAPI tcuMemcpyDtoDAsync(CUdeviceptr dstDevice,
                                                 CUdeviceptr srcDevice, unsigned int ByteCount, CUstream hStream);

    // system <-> array memory
    typedef CUresult  CUDAAPI tcuMemcpyHtoAAsync(CUarray dstArray, unsigned int dstOffset,
                                                 const void *srcHost, unsigned int ByteCount, CUstream hStream);
    typedef CUresult  CUDAAPI tcuMemcpyAtoHAsync(void *dstHost, CUarray srcArray, unsigned int srcOffset,
                                                 unsigned int ByteCount, CUstream hStream);
#endif

// 2D memcpy
typedef CUresult  CUDAAPI tcuMemcpy2DAsync(const CUDA_MEMCPY2D *pCopy, CUstream hStream);

// 3D memcpy
typedef CUresult  CUDAAPI tcuMemcpy3DAsync(const CUDA_MEMCPY3D *pCopy, CUstream hStream);

/************************************
 **
 **    Memset
 **
 ***********************************/
typedef CUresult  CUDAAPI tcuMemsetD8(CUdeviceptr dstDevice, unsigned char uc, unsigned int N);
typedef CUresult  CUDAAPI tcuMemsetD16(CUdeviceptr dstDevice, unsigned short us, unsigned int N);
typedef CUresult  CUDAAPI tcuMemsetD32(CUdeviceptr dstDevice, unsigned int ui, unsigned int N);

#if __CUDA_API_VERSION >= 3020
    typedef CUresult  CUDAAPI tcuMemsetD2D8(CUdeviceptr dstDevice, unsigned int dstPitch, unsigned char uc, size_t Width, size_t Height);
    typedef CUresult  CUDAAPI tcuMemsetD2D16(CUdeviceptr dstDevice, unsigned int dstPitch, unsigned short us, size_t Width, size_t Height);
    typedef CUresult  CUDAAPI tcuMemsetD2D32(CUdeviceptr dstDevice, unsigned int dstPitch, unsigned int ui, size_t Width, size_t Height);
#else
    typedef CUresult  CUDAAPI tcuMemsetD2D8(CUdeviceptr dstDevice, unsigned int dstPitch, unsigned char uc, unsigned int Width, unsigned int Height);
    typedef CUresult  CUDAAPI tcuMemsetD2D16(CUdeviceptr dstDevice, unsigned int dstPitch, unsigned short us, unsigned int Width, unsigned int Height);
    typedef CUresult  CUDAAPI tcuMemsetD2D32(CUdeviceptr dstDevice, unsigned int dstPitch, unsigned int ui, unsigned int Width, unsigned int Height);
#endif

/************************************
 **
 **    Function management
 **
 ***********************************/


typedef CUresult CUDAAPI tcuFuncSetBlockShape(CUfunction hfunc, int x, int y, int z);
typedef CUresult CUDAAPI tcuFuncSetSharedSize(CUfunction hfunc, unsigned int bytes);
typedef CUresult CUDAAPI tcuFuncGetAttribute(int *pi, CUfunction_attribute attrib, CUfunction hfunc);
typedef CUresult CUDAAPI tcuFuncSetCacheConfig(CUfunction hfunc, CUfunc_cache config);
typedef CUresult CUDAAPI tcuLaunchKernel(CUfunction f,
                                         unsigned int gridDimX,  unsigned int gridDimY,  unsigned int gridDimZ,
                                         unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
                                         unsigned int sharedMemBytes,
                                         CUstream hStream, void **kernelParams, void **extra);

/************************************
 **
 **    Array management
 **
 ***********************************/

typedef CUresult  CUDAAPI tcuArrayCreate(CUarray *pHandle, const CUDA_ARRAY_DESCRIPTOR *pAllocateArray);
typedef CUresult  CUDAAPI tcuArrayGetDescriptor(CUDA_ARRAY_DESCRIPTOR *pArrayDescriptor, CUarray hArray);
typedef CUresult  CUDAAPI tcuArrayDestroy(CUarray hArray);

typedef CUresult  CUDAAPI tcuArray3DCreate(CUarray *pHandle, const CUDA_ARRAY3D_DESCRIPTOR *pAllocateArray);
typedef CUresult  CUDAAPI tcuArray3DGetDescriptor(CUDA_ARRAY3D_DESCRIPTOR *pArrayDescriptor, CUarray hArray);

/************************************
 **
 **    Vitual memory management
 **
 ***********************************/
typedef CUresult CUDAAPI tcuMemAddressReserve(CUdeviceptr *ptr, size_t size, size_t alignment, CUdeviceptr addr, unsigned long long flags);
typedef CUresult CUDAAPI tcuMemAddressFree(CUdeviceptr ptr, size_t size);
typedef CUresult CUDAAPI tcuMemCreate(CUmemGenericAllocationHandle *handle, size_t size, const CUmemAllocationProp *prop, unsigned long long flags);
typedef CUresult CUDAAPI tcuMemRelease(CUmemGenericAllocationHandle handle);
typedef CUresult CUDAAPI tcuMemMap(CUdeviceptr ptr, size_t size, size_t offset, CUmemGenericAllocationHandle handle, unsigned long long flags);
typedef CUresult CUDAAPI tcuMemUnmap(CUdeviceptr ptr, size_t size);
typedef CUresult CUDAAPI tcuMemSetAccess(CUdeviceptr ptr, size_t size, const CUmemAccessDesc *desc, size_t count);
typedef CUresult CUDAAPI tcuMemGetAccess(unsigned long long *flags, const CUmemLocation *location, CUdeviceptr ptr);
typedef CUresult CUDAAPI tcuMemExportToShareableHandle(void *shareableHandle, CUmemGenericAllocationHandle handle, CUmemAllocationHandleType handleType, unsigned long long flags);
typedef CUresult CUDAAPI tcuMemImportFromShareableHandle(CUmemGenericAllocationHandle *handle, void *osHandle, CUmemAllocationHandleType shHandleType);
typedef CUresult CUDAAPI tcuMemGetAllocationGranularity(size_t *granularity, const CUmemAllocationProp *prop, CUmemAllocationGranularity_flags option);
typedef CUresult CUDAAPI tcuMemGetAllocationPropertiesFromHandle(CUmemAllocationProp *prop, CUmemGenericAllocationHandle handle);
typedef CUresult CUDAAPI tcuMemRetainAllocationHandle(CUmemGenericAllocationHandle *handle, void *addr);

/************************************
 **
 **    Texture reference management
 **
 ***********************************/
typedef CUresult  CUDAAPI tcuTexRefCreate(CUtexref *pTexRef);
typedef CUresult  CUDAAPI tcuTexRefDestroy(CUtexref hTexRef);

typedef CUresult  CUDAAPI tcuTexRefSetArray(CUtexref hTexRef, CUarray hArray, unsigned int Flags);

#if __CUDA_API_VERSION >= 3020
    typedef CUresult  CUDAAPI tcuTexRefSetAddress(size_t *ByteOffset, CUtexref hTexRef, CUdeviceptr dptr, size_t bytes);
    typedef CUresult  CUDAAPI tcuTexRefSetAddress2D(CUtexref hTexRef, const CUDA_ARRAY_DESCRIPTOR *desc, CUdeviceptr dptr, size_t Pitch);
#else
    typedef CUresult  CUDAAPI tcuTexRefSetAddress(unsigned int *ByteOffset, CUtexref hTexRef, CUdeviceptr dptr, unsigned int bytes);
    typedef CUresult  CUDAAPI tcuTexRefSetAddress2D(CUtexref hTexRef, const CUDA_ARRAY_DESCRIPTOR *desc, CUdeviceptr dptr, unsigned int Pitch);
#endif

typedef CUresult  CUDAAPI tcuTexRefSetFormat(CUtexref hTexRef, CUarray_format fmt, int NumPackedComponents);
typedef CUresult  CUDAAPI tcuTexRefSetAddressMode(CUtexref hTexRef, int dim, CUaddress_mode am);
typedef CUresult  CUDAAPI tcuTexRefSetFilterMode(CUtexref hTexRef, CUfilter_mode fm);
typedef CUresult  CUDAAPI tcuTexRefSetFlags(CUtexref hTexRef, unsigned int Flags);

typedef CUresult  CUDAAPI tcuTexRefGetAddress(CUdeviceptr *pdptr, CUtexref hTexRef);
typedef CUresult  CUDAAPI tcuTexRefGetArray(CUarray *phArray, CUtexref hTexRef);
typedef CUresult  CUDAAPI tcuTexRefGetAddressMode(CUaddress_mode *pam, CUtexref hTexRef, int dim);
typedef CUresult  CUDAAPI tcuTexRefGetFilterMode(CUfilter_mode *pfm, CUtexref hTexRef);
typedef CUresult  CUDAAPI tcuTexRefGetFormat(CUarray_format *pFormat, int *pNumChannels, CUtexref hTexRef);
typedef CUresult  CUDAAPI tcuTexRefGetFlags(unsigned int *pFlags, CUtexref hTexRef);

typedef CUresult  CUDAAPI tcuTexObjectCreate(CUtexObject *pTexObject, const CUDA_RESOURCE_DESC *pResDesc, const CUDA_TEXTURE_DESC *pTexDesc, const CUDA_RESOURCE_VIEW_DESC *pResViewDesc);
typedef CUresult  CUDAAPI tcuTexObjectDestroy(CUtexObject texObject);
typedef CUresult  CUDAAPI tcuTexObjectGetResourceDesc(CUDA_RESOURCE_DESC *pResDesc, CUtexObject texObject);
typedef CUresult  CUDAAPI tcuTexObjectGetTextureDesc(CUDA_TEXTURE_DESC *pTexDesc, CUtexObject texObject);
typedef CUresult  CUDAAPI tcuTexObjectGetResourceViewDesc(CUDA_RESOURCE_VIEW_DESC *pResViewDesc, CUtexObject texObject);

/************************************
 **
 **    Surface reference management
 **
 ***********************************/
typedef CUresult  CUDAAPI tcuSurfRefSetArray(CUsurfref hSurfRef, CUarray hArray, unsigned int Flags);
typedef CUresult  CUDAAPI tcuSurfRefGetArray(CUarray *phArray, CUsurfref hSurfRef);

typedef CUresult  CUDAAPI tcuSurfObjectCreate(CUsurfObject *pSurfObject, const CUDA_RESOURCE_DESC *pResDesc);
typedef CUresult  CUDAAPI tcuSurfObjectDestroy(CUsurfObject surfObject);

/************************************
 **
 **    Parameter management
 **
 ***********************************/

typedef CUresult  CUDAAPI tcuParamSetSize(CUfunction hfunc, unsigned int numbytes);
typedef CUresult  CUDAAPI tcuParamSeti(CUfunction hfunc, int offset, unsigned int value);
typedef CUresult  CUDAAPI tcuParamSetf(CUfunction hfunc, int offset, float value);
typedef CUresult  CUDAAPI tcuParamSetv(CUfunction hfunc, int offset, void *ptr, unsigned int numbytes);
typedef CUresult  CUDAAPI tcuParamSetTexRef(CUfunction hfunc, int texunit, CUtexref hTexRef);


/************************************
 **
 **    Launch functions
 **
 ***********************************/

typedef CUresult CUDAAPI tcuLaunch(CUfunction f);
typedef CUresult CUDAAPI tcuLaunchGrid(CUfunction f, int grid_width, int grid_height);
typedef CUresult CUDAAPI tcuLaunchGridAsync(CUfunction f, int grid_width, int grid_height, CUstream hStream);

/************************************
 **
 **    Events
 **
 ***********************************/
typedef CUresult CUDAAPI tcuEventCreate(CUevent *phEvent, unsigned int Flags);
typedef CUresult CUDAAPI tcuEventRecord(CUevent hEvent, CUstream hStream);
typedef CUresult CUDAAPI tcuEventQuery(CUevent hEvent);
typedef CUresult CUDAAPI tcuEventSynchronize(CUevent hEvent);
typedef CUresult CUDAAPI tcuEventDestroy(CUevent hEvent);
typedef CUresult CUDAAPI tcuEventElapsedTime(float *pMilliseconds, CUevent hStart, CUevent hEnd);

/************************************
 **
 **    Streams
 **
 ***********************************/
typedef CUresult CUDAAPI  tcuStreamCreate(CUstream *phStream, unsigned int Flags);
typedef CUresult CUDAAPI  tcuStreamQuery(CUstream hStream);
typedef CUresult CUDAAPI  tcuStreamSynchronize(CUstream hStream);
typedef CUresult CUDAAPI  tcuStreamDestroy(CUstream hStream);

/************************************
 **
 **    Graphics interop
 **
 ***********************************/
typedef CUresult CUDAAPI tcuGraphicsUnregisterResource(CUgraphicsResource resource);
typedef CUresult CUDAAPI tcuGraphicsSubResourceGetMappedArray(CUarray *pArray, CUgraphicsResource resource, unsigned int arrayIndex, unsigned int mipLevel);

#if __CUDA_API_VERSION >= 3020
    typedef CUresult CUDAAPI tcuGraphicsResourceGetMappedPointer(CUdeviceptr *pDevPtr, size_t *pSize, CUgraphicsResource resource);
#else
    typedef CUresult CUDAAPI tcuGraphicsResourceGetMappedPointer(CUdeviceptr *pDevPtr, unsigned int *pSize, CUgraphicsResource resource);
#endif

typedef CUresult CUDAAPI tcuGraphicsResourceSetMapFlags(CUgraphicsResource resource, unsigned int flags);
typedef CUresult CUDAAPI tcuGraphicsMapResources(unsigned int count, CUgraphicsResource *resources, CUstream hStream);
typedef CUresult CUDAAPI tcuGraphicsUnmapResources(unsigned int count, CUgraphicsResource *resources, CUstream hStream);

/************************************
 **
 **    Export tables
 **
 ***********************************/
typedef CUresult CUDAAPI tcuGetExportTable(const void **ppExportTable, const CUuuid *pExportTableId);

/************************************
 **
 **    Limits
 **
 ***********************************/

typedef CUresult CUDAAPI tcuCtxSetLimit(CUlimit limit, size_t value);
typedef CUresult CUDAAPI tcuCtxGetLimit(size_t *pvalue, CUlimit limit);


extern tcuGetErrorString               *cuGetErrorString;
extern tcuGetErrorName                 *cuGetErrorName;

extern tcuDriverGetVersion             *cuDriverGetVersion;
extern tcuDeviceGet                    *cuDeviceGet;
extern tcuDeviceGetCount               *cuDeviceGetCount;
extern tcuDeviceGetName                *cuDeviceGetName;
extern tcuDeviceGetUuid                *cuDeviceGetUuid;
extern tcuDeviceGetLuid                *cuDeviceGetLuid;
extern tcuDeviceComputeCapability      *cuDeviceComputeCapability;
extern tcuDeviceGetProperties          *cuDeviceGetProperties;
extern tcuDeviceGetAttribute           *cuDeviceGetAttribute;
extern tcuCtxDestroy                   *cuCtxDestroy;
extern tcuCtxAttach                    *cuCtxAttach;
extern tcuCtxDetach                    *cuCtxDetach;
extern tcuCtxPushCurrent               *cuCtxPushCurrent;
extern tcuCtxPopCurrent                *cuCtxPopCurrent;
extern tcuDevicePrimaryCtxRelease      *cuDevicePrimaryCtxRelease;
extern tcuDevicePrimaryCtxReset        *cuDevicePrimaryCtxReset;
extern tcuDevicePrimaryCtxSetFlags     *cuDevicePrimaryCtxSetFlags;

extern tcuCtxSetCurrent                *cuCtxSetCurrent;
extern tcuCtxGetCurrent                *cuCtxGetCurrent;

extern tcuCtxGetDevice                 *cuCtxGetDevice;
extern tcuCtxSynchronize               *cuCtxSynchronize;
extern tcuCtxGetApiVersion             *cuCtxGetApiVersion;
extern tcuCtxGetFlags                  *cuCtxGetFlags;

extern tcuModuleLoad                   *cuModuleLoad;
extern tcuModuleLoadData               *cuModuleLoadData;
extern tcuModuleLoadDataEx             *cuModuleLoadDataEx;
extern tcuModuleLoadFatBinary          *cuModuleLoadFatBinary;
extern tcuModuleUnload                 *cuModuleUnload;
extern tcuModuleGetFunction            *cuModuleGetFunction;
extern tcuModuleGetTexRef              *cuModuleGetTexRef;
extern tcuModuleGetSurfRef             *cuModuleGetSurfRef;
extern tcuMemFreeHost                  *cuMemFreeHost;
extern tcuMemHostAlloc                 *cuMemHostAlloc;
extern tcuMemHostGetFlags              *cuMemHostGetFlags;

extern tcuMemHostRegister              *cuMemHostRegister;
extern tcuMemHostUnregister            *cuMemHostUnregister;
extern tcuMemcpy                       *cuMemcpy;
extern tcuMemcpyPeer                   *cuMemcpyPeer;

extern tcuDeviceTotalMem               *cuDeviceTotalMem;
extern tcuCtxCreate                    *cuCtxCreate;
extern tcuModuleGetGlobal              *cuModuleGetGlobal;
extern tcuMemGetInfo                   *cuMemGetInfo;
extern tcuMemAlloc                     *cuMemAlloc;
extern tcuMemAllocPitch                *cuMemAllocPitch;
extern tcuMemFree                      *cuMemFree;
extern tcuMemGetAddressRange           *cuMemGetAddressRange;
extern tcuMemAllocHost                 *cuMemAllocHost;
extern tcuMemHostGetDevicePointer      *cuMemHostGetDevicePointer;
extern tcuFuncSetBlockShape            *cuFuncSetBlockShape;
extern tcuFuncSetSharedSize            *cuFuncSetSharedSize;
extern tcuFuncGetAttribute             *cuFuncGetAttribute;
extern tcuFuncSetCacheConfig           *cuFuncSetCacheConfig;
extern tcuLaunchKernel                 *cuLaunchKernel;
extern tcuArrayDestroy                 *cuArrayDestroy;
extern tcuTexRefCreate                 *cuTexRefCreate;
extern tcuTexRefDestroy                *cuTexRefDestroy;
extern tcuTexRefSetArray               *cuTexRefSetArray;
extern tcuTexRefSetFormat              *cuTexRefSetFormat;
extern tcuTexRefSetAddressMode         *cuTexRefSetAddressMode;
extern tcuTexRefSetFilterMode          *cuTexRefSetFilterMode;
extern tcuTexRefSetFlags               *cuTexRefSetFlags;
extern tcuTexRefGetArray               *cuTexRefGetArray;
extern tcuTexRefGetAddressMode         *cuTexRefGetAddressMode;
extern tcuTexRefGetFilterMode          *cuTexRefGetFilterMode;
extern tcuTexRefGetFormat              *cuTexRefGetFormat;
extern tcuTexRefGetFlags               *cuTexRefGetFlags;
extern tcuTexObjectCreate              *cuTexObjectCreate;
extern tcuTexObjectDestroy             *cuTexObjectDestroy;
extern tcuTexObjectGetResourceDesc     *cuTexObjectGetResourceDesc;
extern tcuTexObjectGetTextureDesc      *cuTexObjectGetTextureDesc;
extern tcuTexObjectGetResourceViewDesc *cuTexObjectGetResourceViewDesc;

extern tcuSurfRefSetArray              *cuSurfRefSetArray;
extern tcuSurfRefGetArray              *cuSurfRefGetArray;
extern tcuSurfObjectCreate             *cuSurfObjectCreate;
extern tcuSurfObjectDestroy            *cuSurfObjectDestroy;

extern tcuParamSetSize                 *cuParamSetSize;
extern tcuParamSeti                    *cuParamSeti;
extern tcuParamSetf                    *cuParamSetf;
extern tcuParamSetv                    *cuParamSetv;
extern tcuParamSetTexRef               *cuParamSetTexRef;
extern tcuLaunch                       *cuLaunch;
extern tcuLaunchGrid                   *cuLaunchGrid;
extern tcuLaunchGridAsync              *cuLaunchGridAsync;
extern tcuEventCreate                  *cuEventCreate;
extern tcuEventRecord                  *cuEventRecord;
extern tcuEventQuery                   *cuEventQuery;
extern tcuEventSynchronize             *cuEventSynchronize;
extern tcuEventDestroy                 *cuEventDestroy;
extern tcuEventElapsedTime             *cuEventElapsedTime;
extern tcuStreamCreate                 *cuStreamCreate;
extern tcuStreamQuery                  *cuStreamQuery;
extern tcuStreamSynchronize            *cuStreamSynchronize;
extern tcuStreamDestroy                *cuStreamDestroy;
extern tcuGraphicsUnregisterResource   *cuGraphicsUnregisterResource;
extern tcuGraphicsSubResourceGetMappedArray  *cuGraphicsSubResourceGetMappedArray;
extern tcuGraphicsResourceSetMapFlags  *cuGraphicsResourceSetMapFlags;
extern tcuGraphicsMapResources         *cuGraphicsMapResources;
extern tcuGraphicsUnmapResources       *cuGraphicsUnmapResources;
extern tcuGetExportTable               *cuGetExportTable;
extern tcuCtxSetLimit                  *cuCtxSetLimit;
extern tcuCtxGetLimit                  *cuCtxGetLimit;

// These functions could be using the CUDA 3.2 interface (_v2)
extern tcuMemcpyHtoD                   *cuMemcpyHtoD;
extern tcuMemcpyDtoH                   *cuMemcpyDtoH;
extern tcuMemcpyDtoD                   *cuMemcpyDtoD;
extern tcuMemcpyDtoA                   *cuMemcpyDtoA;
extern tcuMemcpyAtoD                   *cuMemcpyAtoD;
extern tcuMemcpyHtoA                   *cuMemcpyHtoA;
extern tcuMemcpyAtoH                   *cuMemcpyAtoH;
extern tcuMemcpyAtoA                   *cuMemcpyAtoA;
extern tcuMemcpy2D                     *cuMemcpy2D;
extern tcuMemcpy2DUnaligned            *cuMemcpy2DUnaligned;
extern tcuMemcpy3D                     *cuMemcpy3D;
extern tcuMemcpyAsync                  *cuMemcpyAsync;
extern tcuMemcpyHtoDAsync              *cuMemcpyHtoDAsync;
extern tcuMemcpyDtoHAsync              *cuMemcpyDtoHAsync;
extern tcuMemcpyDtoDAsync              *cuMemcpyDtoDAsync;
extern tcuMemcpyHtoAAsync              *cuMemcpyHtoAAsync;
extern tcuMemcpyAtoHAsync              *cuMemcpyAtoHAsync;
extern tcuMemcpy2DAsync                *cuMemcpy2DAsync;
extern tcuMemcpy3DAsync                *cuMemcpy3DAsync;
extern tcuMemcpyPeerAsync              *cuMemcpyPeerAsync;
extern tcuMemsetD8                     *cuMemsetD8;
extern tcuMemsetD16                    *cuMemsetD16;
extern tcuMemsetD32                    *cuMemsetD32;
extern tcuMemsetD2D8                   *cuMemsetD2D8;
extern tcuMemsetD2D16                  *cuMemsetD2D16;
extern tcuMemsetD2D32                  *cuMemsetD2D32;
extern tcuArrayCreate                  *cuArrayCreate;
extern tcuArrayGetDescriptor           *cuArrayGetDescriptor;
extern tcuArray3DCreate                *cuArray3DCreate;
extern tcuArray3DGetDescriptor         *cuArray3DGetDescriptor;

extern tcuMemAddressReserve                     *cuMemAddressReserve;
extern tcuMemAddressFree                        *cuMemAddressFree;
extern tcuMemCreate                             *cuMemCreate;
extern tcuMemRelease                            *cuMemRelease;
extern tcuMemMap                                *cuMemMap;
extern tcuMemUnmap                              *cuMemUnmap;
extern tcuMemSetAccess                          *cuMemSetAccess;
extern tcuMemGetAccess                          *cuMemGetAccess;
extern tcuMemExportToShareableHandle            *cuMemExportToShareableHandle;
extern tcuMemImportFromShareableHandle          *cuMemImportFromShareableHandle;
extern tcuMemGetAllocationGranularity           *cuMemGetAllocationGranularity;
extern tcuMemGetAllocationPropertiesFromHandle  *cuMemGetAllocationPropertiesFromHandle;
extern tcuMemRetainAllocationHandle             *cuMemRetainAllocationHandle;

extern tcuTexRefSetAddress             *cuTexRefSetAddress;
extern tcuTexRefSetAddress2D           *cuTexRefSetAddress2D;
extern tcuTexRefGetAddress             *cuTexRefGetAddress;
extern tcuGraphicsResourceGetMappedPointer   *cuGraphicsResourceGetMappedPointer;

extern tcuDeviceCanAccessPeer           *cuDeviceCanAccessPeer;
extern tcuCtxEnablePeerAccess           *cuCtxEnablePeerAccess;
extern tcuCtxDisablePeerAccess          *cuCtxDisablePeerAccess;

/************************************/
CUresult CUDAAPI cuInit   (unsigned int, int cudaVersion, int& foundCudaVersion);
bool cuRtcLoaded();
/************************************/

#ifdef __cplusplus
}
#endif

#endif //__cuda_cuda_h__
