#ifndef GSTREAMER_H__
#define GSTREAMER_H__

#include <boost/function.hpp>

#include "UrlBuilder.h"
#include "GrpcHelpers.h"
#include "DataBuffer.h"
#include "MMCache.h"

#include <MMSS.h>
#include <PullStylePinsBaseImpl.h>
#include <HttpServer/HttpRequest.h>

#include "HttpPluginExports.h"

namespace NCorbaHelpers
{
    class IContainerNamed;
}

namespace NPluginHelpers
{
    enum EStreamFormat
    {
        EUNSUPPORTED,
        EH264,
        EH265,
        EJPEG,
        EMPG4
    };

    enum EStreamContainer
    {
        ENO_CONTAINER,
        EMP4_CONTAINER,
        EHLS_CONTAINER
    };
}

typedef boost::function3 < NHttp::IRequest::PChangedAuthSessionData
    , const std::wstring&
    , const std::wstring&
    , const std::wstring& > FUserAuthenticator;

typedef std::function<void()> FOnFinishProcessing;
typedef std::function<void(NHttp::PDataBuffer)> FOnData;
typedef std::function<void(NPluginHelpers::EStreamContainer)> FOnFormat;

namespace NPluginHelpers
{
    struct IMuxerSource : public virtual NMMSS::CPullStyleSourceBasePureRefcounted
        , public virtual NCorbaHelpers::IWeakReferable
    {
        virtual ~IMuxerSource() {}

        virtual void Init(FOnData, FOnFinishProcessing, FOnFormat) = 0;
        virtual void Stop() = 0;

        virtual NMMSS::IPullStyleSink* GetVideoSink() = 0;
        virtual NMMSS::IPullStyleSink* GetAudioSink() = 0;
        virtual NMMSS::IPullStyleSink* GetTextSink() = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IMuxerSource> PMuxerSource;
    typedef NCorbaHelpers::CWeakPtr<IMuxerSource> WMuxerSource;

    IMuxerSource* CreateMP4Muxer(DECLARE_LOGGER_ARG, int speed, EStreamContainer,
                                 bool hasSubtitles, const std::string& filePath);

    class IGstManager
    {
    public:
        virtual ~IGstManager() {}
        virtual void Start() = 0;
        virtual void Stop() = 0;
    };

    typedef std::shared_ptr<IGstManager> PGstManager;

    HTTPPLUGIN_CLASS_DECLSPEC PGstManager CreateGstManager(DECLARE_LOGGER_ARG);

    struct RtspStat
    {
        std::string GetStr() const;
        std::map<std::string, std::map<std::string, int>> mStat;
    };

    class IGstRTSPServer : public std::enable_shared_from_this<IGstRTSPServer>
    {
    public:
        virtual ~IGstRTSPServer() {}

        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual RtspStat GetStat() const = 0;

        template <typename Derived>
        std::shared_ptr<Derived> shared_from_base()
        {
            return std::dynamic_pointer_cast<Derived>(shared_from_this());
        }
    };

    typedef std::shared_ptr<IGstRTSPServer> PGstRTSPServer;

    PGstRTSPServer CreateRTSPServer(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed*, int rtspPort, 
        const NWebGrpc::PGrpcManager, TokenAuthenticatorSP, FUserAuthenticator, NHttp::PMMCache);
}

#endif // GSTREAMER_H__
