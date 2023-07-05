#ifndef VIDEO_SOURCE_CACHE_H__
#define VIDEO_SOURCE_CACHE_H__

#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <HttpServer/HttpResponse.h>
#include "MMCache.h"
#include "Gstreamer.h"
#include "DataBuffer.h"
#include "../MMSS.h"
#include <MMClient/Synchroniser.h>

#include <GrpcHelpers/MetaCredentials.h>

namespace NCorbaHelpers
{
    class IContainer;
}

namespace NPluginHelpers
{
    struct IMixerSink;
}

namespace NHttp
{
    struct IClientContext : public boost::enable_shared_from_this<IClientContext>
    {
        virtual ~IClientContext() {}

        virtual void Init() {}
        virtual void Init(DataFunction) {}
        virtual void CreateEventStream(NHttp::PResponse) {}
        virtual void RemoveEventStream() {}
        virtual void Stop() {}

        virtual void onStreamFormat(NPluginHelpers::EStreamContainer) {}
        virtual void onSample(PDataBuffer) {}
        virtual void onSourceDataExhausted() {}
        virtual void finishProcessing() {}

        virtual std::uint64_t GetTimestamp() const { return 0; }

        template <typename Derived>
        boost::shared_ptr<Derived> shared_from_base()
        {
            return boost::dynamic_pointer_cast<Derived>(shared_from_this());
        }
    };
    typedef boost::weak_ptr<IClientContext> WClientContext;
    typedef boost::shared_ptr<IClientContext> PClientContext;

    struct SCountedSynchronizer
    {
        DECLARE_LOGGER_HOLDER;

        SCountedSynchronizer(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* cont) 
            : m_syncContainer(cont->CreateContainer())
            , m_sync(NMMSS::CreateMonitorSynchronizer(GET_LOGGER_PTR, m_syncContainer.Get()))
            , m_syncCounter(0)
        {
            INIT_LOGGER_HOLDER;
        }

        ~SCountedSynchronizer() 
        {
            _log_ << "SCountedSynchronizer dtor";

            int sinkCount = m_syncCounter.load();
            for (int i = 0; i < sinkCount; ++i)
                m_sync->DetachOutputSink(i);
        }

        int GetNextSyncIndex()
        {
            return m_syncCounter.fetch_add(1, std::memory_order_relaxed);
        }

        NMMSS::PSynchronizer GetSynchronizer() 
        {
            return m_sync;
        }

        void Start(int speed) 
        {
            m_sync->SetPlaybackDirection(speed < 0);
            m_sync->SetPlaybackSpeed(std::abs(speed), 1);
        }

        NCorbaHelpers::PContainerTrans m_syncContainer;
        NMMSS::PSynchronizer m_sync;
        std::atomic<int> m_syncCounter;
    };
    typedef boost::shared_ptr<SCountedSynchronizer> PCountedSynchronizer;

    struct SArchiveContext
    {
        DECLARE_LOGGER_HOLDER;

        SArchiveContext(DECLARE_LOGGER_ARG) 
            : keyFrames(false)
            , speed(1)
            , forward(true)
        {
            INIT_LOGGER_HOLDER;
        }

        virtual ~SArchiveContext() 
        {
            _dbg_ << "SArchiveContext dtor";
        }

        std::string videoEndpoint;
        std::string audioEndpoint;
        std::string archiveName;
        std::string startTime;
        std::string stopTime;
        bool keyFrames;
        int speed;
        bool forward;
    };
    typedef boost::shared_ptr<SArchiveContext> PArchiveContext;

    struct SJpegArchiveContext : public SArchiveContext
    {
        SJpegArchiveContext(DECLARE_LOGGER_ARG, int width, int height) 
            : SArchiveContext(GET_LOGGER_PTR), m_width(width), m_height(height)
        {}

        ~SJpegArchiveContext()
        {
            _this_dbg_ << "SJpegArchiveContext dtor";
        }

        int m_width;
        int m_height;
    };

    struct SHlsArchiveContext : public SArchiveContext
    {
        SHlsArchiveContext(DECLARE_LOGGER_ARG, const std::string& filePath) 
            : SArchiveContext(GET_LOGGER_PTR)
            , m_filePath(filePath)
        {}

        ~SHlsArchiveContext()
        {
            _this_log_ << "SHlsArchiveContext dtor";
        }

        std::string m_filePath;
    };

    struct IVideoSourceCache
    {
        typedef boost::function0<void> TOnDisconnected;

        virtual ~IVideoSourceCache() {}

        virtual PClientContext CreateClientContext(const std::string& address,
            int width, int height, int compression, NMMSS::PPullStyleSink sink, bool keyFrames = false, float fps = -1.0) = 0;

        virtual PClientContext CreateMp4Context(NHttp::PResponse r, const std::string& videoSource,
            const std::string& audioSource, const std::string& textSource, TOnDisconnected, bool keyFrames = false) = 0;

        virtual PClientContext CreateArchiveMp4Context(const NWebGrpc::PGrpcManager grpcManager,
            NGrpcHelpers::PCredentials credentials, NHttp::PResponse r, NHttp::PCountedSynchronizer sync,
            PArchiveContext ctx, TOnDisconnected) = 0;

        virtual PClientContext CreateHlsContext(NHttp::PResponse r, const std::string& videoSource, const std::string& rootPath, TOnDisconnected) = 0;

        virtual PClientContext CreateRawContext(const NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NHttp::PResponse r, PArchiveContext ctx) = 0;
    };
    typedef boost::shared_ptr<IVideoSourceCache> PVideoSourceCache;

    PVideoSourceCache GetVideoSourceCache(NCorbaHelpers::IContainer* c, PMMCache);
}

#endif // VIDEO_SOURCE_CACHE_H__
