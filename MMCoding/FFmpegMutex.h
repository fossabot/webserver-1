#ifndef NGP_FFMPEG_MUTEX__H
#define NGP_FFMPEG_MUTEX__H

#include "../mIntTypes.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

#include <boost/thread.hpp>

#include "../../Primitives/Logging/log2.h"
#include "MMCodingExports.h"

namespace NMMSS
{

class MMCODING_CLASS_DECLSPEC CFFmpegMutex
{
public:
    ~CFFmpegMutex();

    static boost::mutex& Get();
    
    static void FFmpegLogCallback(void* ptr,
        int level,const char* fmt, va_list vl);

private:
    CFFmpegMutex();
    CFFmpegMutex(const CFFmpegMutex&) = delete;
    CFFmpegMutex& operator=(const CFFmpegMutex&) = delete;

    void FFmpegLogCallbackImpl(void* ptr,
        int level, const char* fmt, va_list vl);

    static CFFmpegMutex& Instance();

private:
    class Impl;
    Impl* const m_impl;
};

}


#endif //NGP_FFMPEG_MUTEX__H
