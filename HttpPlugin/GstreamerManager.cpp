#include "Gstreamer.h"

#include <thread>
#include <mutex>

#include <gst/gst.h>

namespace
{
    class GstManager : public NPluginHelpers::IGstManager
    {
        DECLARE_LOGGER_HOLDER;
    public:
        GstManager(DECLARE_LOGGER_ARG)
            : m_mainLoop(nullptr)
        {
            CLONE_TO_THIS_LOGGER;
            ADD_THIS_LOG_PREFIX("GStreamer");

            auto releaseLog = [](gpointer user_data) {
                if (auto logger = reinterpret_cast<NLogging::ILogger*>(user_data))
                    logger->Release();
            };
            gst_debug_remove_log_function(nullptr);
            gst_debug_add_log_function(&gstLog, dynamic_cast<NLogging::ILogger*>(GET_THIS_LOGGER_PTR->AddRef()), releaseLog);

            GError* err;
            gboolean res = gst_init_check(NULL, NULL, &err);
            if (res)
                _log_ << "Gstreamer initialized successfully";
        }

        ~GstManager()
        {
            _log_ << "Destroying GstManager";

            stopImpl();

            //gst_deinit();
            gst_debug_remove_log_function(&gstLog);

            _log_ << "Destroyed GstManager";
        }

        void Start() override
        {
            if (m_mainLoop)
            {
                // already running
                return;
            }

            m_mainLoop = g_main_loop_new(NULL, FALSE);
            m_loopThread = std::thread([this]() {
                DECLARE_LOGGER_ARG = GET_THIS_LOGGER_PTR;
                _dbg_ << "Gstreamer main loop start";
                g_main_loop_run(m_mainLoop);
                _dbg_ << "Gstreamer main loop finished";
            });
        }

        void Stop() override
        {
            stopImpl();
        }

    private:
        void stopImpl()
        {
            if (m_mainLoop)
            {
                if (g_main_loop_is_running(m_mainLoop))
                {
                    g_main_loop_quit(m_mainLoop);
                }
                g_main_loop_unref(m_mainLoop);
                m_mainLoop = nullptr;

                if (m_loopThread.joinable())
                {
                    m_loopThread.join();
                }
            }
        }

        static int getNGPLogLevelCounterpart(GstDebugLevel level)
        {
            switch (level)
            {
            case GST_LEVEL_NONE:
                return NLogging::LEVEL_OFF;
            case GST_LEVEL_ERROR:
                return NLogging::LEVEL_ERROR;
            case GST_LEVEL_WARNING:
            case GST_LEVEL_FIXME:
                return NLogging::LEVEL_WARNING;
            case GST_LEVEL_INFO:
                return NLogging::LEVEL_INFO;
            case GST_LEVEL_DEBUG:
                return NLogging::LEVEL_DEBUG;
            case GST_LEVEL_LOG:
            case GST_LEVEL_TRACE:
                return NLogging::LEVEL_TRACE;
            case GST_LEVEL_MEMDUMP:
            default:
                return NLogging::LEVEL_MAX;
            }
        }

        static inline const gchar *
            gst_path_basename(const gchar * file_name)
        {
            register const gchar *base;

            base = strrchr(file_name, G_DIR_SEPARATOR);

            {
                const gchar *q = strrchr(file_name, '/');
                if (base == NULL || (q != NULL && q > base))
                    base = q;
            }

            if (base)
                return base + 1;

            if (g_ascii_isalpha(file_name[0]) && file_name[1] == ':')
                return file_name + 2;

            return file_name;
        }

        struct GFreeDtor
        {
            void operator()(gpointer ptr) const
            {
                g_free(ptr);
            }
        };

        static std::unique_ptr<gchar, GFreeDtor> getGObjectDecription(gpointer object)
        {
            return std::unique_ptr<gchar, GFreeDtor>(gst_info_strdup_printf("%p\aA", object));
        }

        static void gstLog(
            GstDebugCategory *category,
            GstDebugLevel level,
            const gchar *file,
            const gchar *function,
            gint line,
            GObject *object,
            GstDebugMessage *message,
            gpointer user_data) G_GNUC_NO_INSTRUMENT;

        std::thread m_loopThread;
        GMainLoop* m_mainLoop;
    };

    void GstManager::gstLog(
        GstDebugCategory *category,
        GstDebugLevel level,
        const gchar *file,
        const gchar *function,
        gint line,
        GObject *object,
        GstDebugMessage *message,
        gpointer user_data)
    {
        DECLARE_LOGGER_ARG = reinterpret_cast<NLogging::ILogger*>(user_data);
        auto ngpLogLevel = getNGPLogLevelCounterpart(level);
        if (NLogging::LEVEL_ERROR == ngpLogLevel || NLogging::LEVEL_WARNING == ngpLogLevel)
            _logn_(ngpLogLevel)
            << gst_debug_category_get_name(category)
            << ' ' << gst_path_basename(file)
            << ':' << line
            << ':' << function
            << ':' << getGObjectDecription(object).get()
            << ' ' << gst_debug_message_get(message);
    }
}

namespace NPluginHelpers
{
    class GstManagerSingleton
    {
    public:
        static PGstManager GetInstance(DECLARE_LOGGER_ARG)
        {
            // Relization Double-Checked Locking. http://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/
            // It's not allowed to use std::shared_ptr<T> as template argument for std::atomic<T>: http://qaru.site/questions/929361/stdatomic-load-method-decreases-the-reference-count-when-used-with-stdsharedptr            

            auto tmp = std::atomic_load(&m_instance);

            if (tmp == nullptr) {
                std::lock_guard<std::mutex> lock(m_mutex);
                tmp = std::atomic_load(&m_instance);
                if (tmp == nullptr) {
                    tmp = std::make_shared<GstManager>(GET_LOGGER_PTR);
                    std::atomic_store(&m_instance, tmp);
                }
            }
            return tmp;
        }
    private:
        static std::mutex m_mutex;
        static PGstManager m_instance;
    };

    PGstManager GstManagerSingleton::m_instance = NULL;
    std::mutex GstManagerSingleton::m_mutex;

    HTTPPLUGIN_CLASS_DECLSPEC PGstManager CreateGstManager(DECLARE_LOGGER_ARG)
    {
        return GstManagerSingleton::GetInstance(GET_LOGGER_PTR);
    }
}
