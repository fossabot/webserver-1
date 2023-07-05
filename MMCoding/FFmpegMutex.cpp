#include <CorbaHelpers/Envar.h>
#include "FFmpegMutex.h"

#include <mutex>

#ifdef _MSC_VER
#include <stdio.h> // snprintf
#define snprintf _snprintf
#endif

namespace NMMSS
{
    namespace
    {
        std::once_flag FFmpegMutexInitFlag;
        static std::unique_ptr<CFFmpegMutex> theInstance;
    }

    class CFFmpegMutex::Impl
    {
    public:
        DECLARE_LOGGER_HOLDER;
        boost::mutex m_mutex;
    };

    CFFmpegMutex::CFFmpegMutex() :
        m_impl(new Impl)
    {
        av_register_all();
        av_log_set_level(AV_LOG_ERROR);

        if (NCorbaHelpers::CEnvar::EnableFFmpegLog())
            av_log_set_callback(&CFFmpegMutex::FFmpegLogCallback);
    }

    CFFmpegMutex::~CFFmpegMutex()
    {
        delete m_impl;
    }

    CFFmpegMutex& CFFmpegMutex::Instance()
    {
        std::call_once(FFmpegMutexInitFlag, 
            [&] { theInstance.reset(new CFFmpegMutex); });
        return *theInstance;
    }

    boost::mutex& CFFmpegMutex::Get()
    {
        return Instance().m_impl->m_mutex;
    }


    void CFFmpegMutex::FFmpegLogCallbackImpl(
        void* ptr, int level,
        const char* fmt, va_list vl)
    {
        DECLARE_LOGGER_HOLDER;
        SHARE_LOGGER_HOLDER(NLogging::GetDefaultLogger());
        
        char line[1024];
        AVClass* avc = ptr ? *(AVClass**)ptr : NULL;

        line[0] = 0;
        if (avc) {
            if (avc->version >= (50 << 16 | 15 << 8 | 3) && avc->parent_log_context_offset){
                AVClass** parent = *(AVClass***)(((uint8_t*)ptr) + avc->parent_log_context_offset);
                if (parent && *parent){
                    snprintf(line, sizeof(line), "[%s @ %p] ", (*parent)->item_name(parent), parent);
                }
            }
            snprintf(line + strlen(line), sizeof(line) - strlen(line), "[%s @ %p] ", avc->item_name(ptr), ptr);
        }

        vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, vl);

        int ngpLevel = NLogging::LEVEL_DEBUG;
        if (level <= AV_LOG_VERBOSE)
            ngpLevel = NLogging::LEVEL_DEBUG;
        if (level <= AV_LOG_INFO)
            ngpLevel = NLogging::LEVEL_NORMAL;
        if (level <= AV_LOG_ERROR)
            ngpLevel = NLogging::LEVEL_WARNING;
        if (level <= AV_LOG_FATAL)
            ngpLevel = NLogging::LEVEL_ERROR;

        (void) ngpLevel;
        _logn_(ngpLevel) << "FFMPEG: (FFsvr=" << level << "): " << line;
    }

    void CFFmpegMutex::FFmpegLogCallback(void* ptr, int level, const char* fmt, va_list vl)
    {
        Instance().FFmpegLogCallbackImpl(ptr, level, fmt, vl);
    }

}

