#ifndef __NVRTC_H__
#define __NVRTC_H__

#if INIT_CUDA_RTC

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    NVRTC_SUCCESS = 0,
    NVRTC_ERROR_OUT_OF_MEMORY = 1,
    NVRTC_ERROR_PROGRAM_CREATION_FAILURE = 2,
    NVRTC_ERROR_INVALID_INPUT = 3,
    NVRTC_ERROR_INVALID_PROGRAM = 4,
    NVRTC_ERROR_INVALID_OPTION = 5,
    NVRTC_ERROR_COMPILATION = 6,
    NVRTC_ERROR_BUILTIN_OPERATION_FAILURE = 7,
    NVRTC_ERROR_NO_NAME_EXPRESSIONS_AFTER_COMPILATION = 8,
    NVRTC_ERROR_NO_LOWERED_NAMES_BEFORE_COMPILATION = 9,
    NVRTC_ERROR_NAME_EXPRESSION_NOT_VALID = 10,
    NVRTC_ERROR_INTERNAL_ERROR = 11
} nvrtcResult;



typedef struct _nvrtcProgram *nvrtcProgram;


//__stdcall?

typedef nvrtcResult CUDAAPI tnvrtcCreateProgram(nvrtcProgram *prog, const char *src, const char *name, int numHeaders, const char * const *headers, const char * const *includeNames);
typedef nvrtcResult CUDAAPI tnvrtcDestroyProgram(nvrtcProgram *prog);
typedef nvrtcResult CUDAAPI tnvrtcCompileProgram(nvrtcProgram prog, int numOptions, const char * const *options);
typedef nvrtcResult CUDAAPI tnvrtcGetPTXSize(nvrtcProgram prog, size_t *ptxSizeRet);
typedef nvrtcResult CUDAAPI tnvrtcGetPTX(nvrtcProgram prog, char *ptx);
typedef nvrtcResult CUDAAPI tnvrtcGetProgramLogSize(nvrtcProgram prog, size_t *logSizeRet);
typedef nvrtcResult CUDAAPI tnvrtcGetProgramLog(nvrtcProgram prog, char *log);


extern tnvrtcCreateProgram*     nvrtcCreateProgram;
extern tnvrtcDestroyProgram*    nvrtcDestroyProgram;
extern tnvrtcCompileProgram*    nvrtcCompileProgram;
extern tnvrtcGetPTXSize*        nvrtcGetPTXSize;
extern tnvrtcGetPTX*            nvrtcGetPTX;
extern tnvrtcGetProgramLogSize* nvrtcGetProgramLogSize;
extern tnvrtcGetProgramLog*     nvrtcGetProgramLog;


#ifdef __cplusplus
};
#endif

#endif

#endif
