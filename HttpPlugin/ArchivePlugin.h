#ifndef ARCHIVE_PLUGIN_H__
#define ARCHIVE_PLUGIN_H__

#include <vector>
#include <unordered_map>

#include <boost/shared_ptr.hpp>
#include <boost/thread/shared_mutex.hpp>

#include <json/json.h>

#include "BLQueryHelper.h"
#include "VideoSourceCache.h"

using namespace NHttp;

namespace bpt = boost::posix_time;
namespace npu = NPluginUtility;
namespace bl = axxonsoft::bl;

namespace ArchivePlugin
{
    const char* const BOOKMARKS = "bookmarks";
    const char* const CONTENTS = "contents";
    const int SPEED_DEFAULT = 0;

    class CManagerResourceHandle
    {
    public:
        CManagerResourceHandle(boost::function<void(void)> method);  
        ~CManagerResourceHandle();
    private:
        boost::function<void(void)> m_method;
    };

class CArchiveContentImpl
    : public NHttpImpl::CBasicServletImpl
{
private:
    typedef std::string TRequestId;
    typedef boost::shared_ptr<NCorbaHelpers::IResource> PSnapshotSession;

private:
    enum EArchiveMode
    {
        EUNKNOWN = 0,
        EMEDIA,
        ELIVE,
        ESNAPSHOT,
        EFRAMES,
        EINTERVALS,
        EBOOKMARKS,
        ERENDERED_INFO,
        ESTOP,
        ECAPACITY,
        EDEPTH,
        ETIMEON,
        ETIMEOFF
    };

    enum EVF
    {
        EMJPEG = 1,
        EWEBM = 2,
        EH264 = 4,
        EHLS = 8,
        ERTSP = 16,
        EMP4 = 32,
        ESUPPORTED = EMJPEG | EWEBM | EH264 | EHLS | ERTSP | EMP4
    };

    enum ESendMode { SM_Headers, SM_Full };

    struct ArchiveRequestContext
    {
        ArchiveRequestContext()
            : m_archiveMode(EUNKNOWN)
            , m_videoFormat(EMJPEG)
            , m_posixStartTime(boost::date_time::min_date_time)
            , m_posixEndTime(boost::date_time::max_date_time)
            , m_mediaSpeed(SPEED_DEFAULT)
            , m_enableTokenAuth(false)
            , m_validForHours(0)
            , m_threshold(1)
            , m_keyFrames(0)
        {
        }

        TRequestId m_id;
        NHttp::PResponse m_response;

        EArchiveMode m_archiveMode;
        EVF m_videoFormat;

        std::string m_hostname;
        std::string m_endpoint;
        std::string m_archiveName;
        std::string m_storageSourceEndpoint;
        MMSS::StorageSource_var m_storageSource;

        bpt::ptime m_posixStartTime;
        bpt::ptime m_posixEndTime;

        int m_mediaSpeed;
        int m_width;
        int m_height;
        float m_crop_x;
        float m_crop_y;
        float m_crop_width;
        float m_crop_height;

        NMMSS::MediaMuxer::POutput m_output;
        NMMSS::MediaMuxer::PMuxer m_muxer;
        NMMSS::PSinkEndpoint m_sinkEndpoint;

        std::vector<std::string> m_timestamps;

        // ќстановить проигрывание архива, которое было начато с указанным идентификатором.
        TRequestId StopRunning;

        // «апрос информации о последнем отображенном кадре при проигрывании архива.
        TRequestId LastRendered;

        PSnapshotSession SnapshotSession;

        int m_enableTokenAuth;
        int m_validForHours;

        uint32_t m_threshold;
        bool m_thresholdExist;

        npu::TParams params;

        PClientContext m_clientContext;
        NHttp::PCountedSynchronizer m_sync;

        std::uint32_t m_keyFrames;

        float m_fps;

        NGrpcHelpers::PCredentials m_credentials;

        NMMSS::PSinkEndpoint m_timestampEndpoint;
    };

    class ConnectorsHandler
    {
        using SupplierMap = std::map<std::string, NCorbaHelpers::CAutoPtr<NCommonNotification::CEventSupplier>>;
    public:
        ConnectorsHandler(NCorbaHelpers::IContainerNamed* c);

        NCommonNotification::IEventSupplierSink *Get(const std::string &hostname);
    private:
        NCommonNotification::IEventSupplierSink *get(const std::string &hostname) const;
        NCommonNotification::IEventSupplierSink * insert(const std::string &hostname);
    private:
        NCorbaHelpers::WPContainerNamed  m_container;
        SupplierMap m_SupplierTable;
        std::list<std::unique_ptr<NCorbaHelpers::IResource>> m_ResList;

        mutable boost::shared_mutex m_mutex;
    };

    typedef boost::shared_ptr<ArchiveRequestContext> PArchiveRequestContext;
    typedef std::map<TRequestId, PArchiveRequestContext> TRequests;

    class CamContextOut
    {
    public:
        void AddCam(const std::string &endpoint, const std::string &friendly_name);
        void AddArc(const std::string &arc, const std::string &friendly_name);
        Json::Value GetJson() const;
        std::string GetJsonStr() const;
        std::string GetCamFriendlyName(const std::string &endpoint);
        std::string GetArchiveFriendlyName(const std::string &archive);
    private:
        std::unordered_map<std::string, std::string> m_mapCams;
        std::unordered_map<std::string, std::string> m_mapArcs;
        mutable std::mutex m_mutex;
    };

    struct CamContextIn
    {
        CamContextIn(const Json::Value &data, const std::string &user);

        const Json::Value m_data;
        std::unordered_set<std::string> m_archives;
        std::unordered_set<std::string> m_cameras;
        const std::string m_user;
    };

    using PCamContextOut = std::shared_ptr<CamContextOut>;
    using PCamContextIn = std::shared_ptr<CamContextIn>;
    using MapTable = std::unordered_map < std::string, std::unique_ptr<std::vector<Json::Value>> >;
    using RequestDoneFunc = boost::function<void(void)>;

public:
    CArchiveContentImpl(NCorbaHelpers::IContainerNamed* c, const NWebGrpc::PGrpcManager grpcManager,
        const NPluginUtility::PRigthsChecker rightsChecker, const std::string& hlsContentPath,
        UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache);

    ~CArchiveContentImpl();

private:
    virtual void Head(const PRequest req, PResponse resp)
    {
        Send(req, resp, SM_Headers);
    }

    virtual void Get(const PRequest req, PResponse resp)
    {
        Send(req, resp, SM_Full);
    }

    virtual void Post(const PRequest req, PResponse resp) override;
    virtual void Delete(const PRequest req, PResponse resp) override;

    static void setEventFromJson(ORM::JsonEvent &ev, Json::Value data, const std::string &user);
    static std::string lexemsGener(const std::vector<std::string> &vStrings);
    static void setAuditEventFromJson(ORM::JsonEvent &ev, const Json::Value &dataIn, std::string user, PCamContextOut ctxOut);
    static void processCameras(PCamContextIn ctxIn, PCamContextOut ctxOut, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams);
    static MapTable getTableJson(const Json::Value &data);

    void procEditBookmark(const PRequest req, PResponse resp);
    void procCreateBookmarks(const PRequest req, PResponse resp);

    void OnFrame(const TRequestId &id, NMMSS::ISample *s);
    void OnCompleted(const TRequestId &id, bool containsMore);
    void OnSnapshotReceived(const TRequestId &id, NMMSS::ISample* s);

    static bool expired(const NMMSS::ISample* s, const PArchiveRequestContext arc);
    static bool ParseControls(const PRequest req, PArchiveRequestContext arc);
    static bool ParseMedia(const PRequest req, PArchiveRequestContext arc);
    static bool ParseContents(const PRequest req, PArchiveRequestContext arc);
    static bool ParseStatistics(DECLARE_LOGGER_ARG, const PRequest req, PArchiveRequestContext arc);
    static bool ParseRequest(DECLARE_LOGGER_ARG, const PRequest req, PArchiveRequestContext arc);

    void Send(const PRequest req, PResponse resp, ESendMode sm);
    void execSend(const PRequest req, PResponse resp, PArchiveRequestContext arc);

    void ProcessStopping(PArchiveRequestContext arc);
    void ProcessRenderedInfo(PArchiveRequestContext arc);
    void EnableEventStream(PArchiveRequestContext arc);
    void DisableEventStream(PArchiveRequestContext arc);

    static void SendRenderedInfo(PArchiveRequestContext src, PArchiveRequestContext dest);

    void ProcessFramesRequest(const PRequest req, PArchiveRequestContext arc);
    void ProcessLiveRequest(PRequest req, PArchiveRequestContext arc, PResponse resp);
    void ProcessIntervalsRequest(const PRequest req, PArchiveRequestContext arc);

    void ProcessBookmarksRequest(const PRequest req, PArchiveRequestContext arc);

    void ProcessSnapshotRequest(PArchiveRequestContext arc);
    void ProcessCapacityRequest(PArchiveRequestContext arc);
    void ProcessDepthRequest(PArchiveRequestContext arc);

    void prepareMP4Endpoints(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, NWebBL::SAuxillarySources);
    void runMP4Stream(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, const std::string& videoEp, const std::string& audioEp);
    void adjustStreamTime(PResponse, PArchiveRequestContext, RequestDoneFunc, NHttp::PArchiveContext, NMMSS::ISample*);

    void runClientContext(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, NHttp::PArchiveContext aCtx);

    using UseFunction_t = boost::function<void(MMSS::StorageEndpoint_var)>;

    void UseSourceEndpointReader(PArchiveRequestContext arc, axxonsoft::bl::archive::EStartPosition pos, const long playFlags, UseFunction_t func);

    void useSnapshot(MMSS::StorageEndpoint_var endpoint, PArchiveRequestContext arc);
    void useMuxer(MMSS::StorageEndpoint_var endpoint, PArchiveRequestContext arc, NMMSS::IPullStyleSink* sink, int width,
        int height, NMMSS::MediaMuxer::EOutputFormat format, float fps = -1.);

    static void Convert(const std::string &src, EVF &dest);

    void RequestDone(const PArchiveRequestContext context);
    void RequestDone(const PArchiveRequestContext context, boost::system::error_code ec);
    bool AddContext(PArchiveRequestContext context);
    PArchiveRequestContext GetContext(const TRequestId &id) const;
    std::string GenerateRequestId() const;
    static void Destroy(NMMSS::PSinkEndpoint sink);

    void onCameraInfo(const PRequest req, PResponse resp, PArchiveRequestContext ctx, NPluginUtility::PEndpointQueryContext ctxOut,
        const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus);
private:
    NCorbaHelpers::PContainerNamed m_container;
    NPluginHelpers::HlsSourceManager m_hlsManager;
    ConnectorsHandler m_connectors;
    const NWebGrpc::PGrpcManager m_grpcManager;
    const NPluginUtility::PRigthsChecker m_rightsChecker;

    DECLARE_LOGGER_HOLDER;
    std::auto_ptr<NMMSS::CMMCodingInitialization> m_mmcoding;
    mutable TRequests m_requests;
    mutable boost::mutex m_requestMutex;
    UrlBuilderSP m_urlBuilder;
    NHttp::PVideoSourceCache m_videoSourceCache;

    NGrpcHelpers::PGrpcClientBase m_grpcClientBase;
    NGrpcHelpers::PBoundGrpcClient m_nativeBLClient;
};

}



#endif
