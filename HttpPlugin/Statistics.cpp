#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/Reactor.h>
#include <HttpServer/BasicServletImpl.h>
#include <HttpServer/json_oarchive.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>

#include "HttpPlugin.h"
#include "CommonUtility.h"
#include "DataSink.h"
#include "SendContext.h"
#include "BLQueryHelper.h"
#include "Constants.h"
#include "RegexUtility.h"

#include <json/json.h>
#include <CorbaHelpers/ResolveServant.h>
#include <InfraServer_IDL/HostAgentC.h>
#include <MMTransport/MMTransport.h>

#include <axxonsoft/bl/statistics/Statistics.grpc.pb.h>

using namespace NHttp;
namespace bl = axxonsoft::bl;

const int STATISTICS_UPDATE_PERIOD_MS=2000;

struct WebserverStatistics;

typedef boost::shared_ptr<WebserverStatistics> PWebserverStatistics;
typedef boost::weak_ptr<WebserverStatistics> WPWebserverStatistics;

namespace
{
    const char* const WATER_LEVEL = "waterlevel";

    using StatisticsReader_t = NWebGrpc::AsyncResultReader < bl::statistics::StatisticService, bl::statistics::StatsRequest,
        bl::statistics::StatsResponse >;
    using PStatisticsReader_t = std::shared_ptr < StatisticsReader_t >;
    using StatisticsCallback_t = std::function < void(const bl::statistics::StatsResponse&, grpc::Status) >;
}

struct WebserverStatistics: boost::enable_shared_from_this<WebserverStatistics>
{
    WebserverStatistics(boost::asio::io_service& io)
        :   m_timer(io)
        ,   start(boost::posix_time::microsec_clock::universal_time())
        ,   now(start)
        ,   previous(now)
        ,   requests(0)
        ,   requestsPrevious(0)
        ,   requestsPerSecond(0)
        ,   bytesOut(0)
        ,   bytesOutPrevious(0)
        ,   bytesOutPerSecond(0)
        ,   streams(0)
    {
    }

    ~WebserverStatistics()
    {
        m_timer.cancel();
    }

    boost::mutex m_mutex;
    boost::asio::deadline_timer m_timer;

    const boost::posix_time::ptime start;
    boost::posix_time::ptime now;
    boost::posix_time::ptime previous;

    uint64_t requests;
    uint64_t requestsPrevious;
    uint64_t requestsPerSecond;

    uint64_t bytesOut;
    uint64_t bytesOutPrevious;
    uint64_t bytesOutPerSecond;
    int streams;
    std::int64_t uptime;

    void update()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        update(lock);
    }
    void update(boost::mutex::scoped_lock&)
    {
        boost::posix_time::ptime previous=now;
        now=boost::posix_time::microsec_clock::universal_time();
        uptime=(now-start).total_seconds();
        uint64_t ms=(now-previous).total_milliseconds();
        if(ms>=STATISTICS_UPDATE_PERIOD_MS)
        {
            requestsPerSecond=1000L*(requests-requestsPrevious)/ms;
            bytesOutPerSecond=1000L*(bytesOut-bytesOutPrevious)/ms;

            requestsPrevious=requests;
            bytesOutPrevious=bytesOut;
            previous=now;
        }
    }

    template<class TArchive>
    void serialize(TArchive &arch, const unsigned int /*version*/)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        //update(lock);
        const auto& strNow = boost::posix_time::to_iso_string(now);
        arch 
            & boost::serialization::make_nvp("now", strNow)
            & boost::serialization::make_nvp("requests", requests)
            & boost::serialization::make_nvp("requestsPerSecond", requestsPerSecond)
            & boost::serialization::make_nvp("bytesOut", bytesOut)
            & boost::serialization::make_nvp("bytesOutPerSecond", bytesOutPerSecond)
            & boost::serialization::make_nvp("streams", streams)
            & boost::serialization::make_nvp("uptime", uptime)
            ;
    }

    static void periodicUpdate(WPWebserverStatistics wp, boost::system::error_code ec)
    {
        if(ec)
            return;
        PWebserverStatistics p = wp.lock();
        if(!p)
            return;
        p->update();
        p->ScheduleUpdate();
    }

    void ScheduleUpdate()
    {
        m_timer.expires_from_now(boost::posix_time::milliseconds(STATISTICS_UPDATE_PERIOD_MS));
        m_timer.async_wait(boost::bind(&periodicUpdate, WPWebserverStatistics(shared_from_this()), _1));
    }
    void IncrementRequests()
    {
        boost::mutex::scoped_lock lock(m_mutex);
        ++requests;
    }
    void IncrementBytesOut(uint64_t s)
    {
        boost::mutex::scoped_lock lock(m_mutex);
        bytesOut+=s;
    }

    static void ModStreams(WPWebserverStatistics wp, int v)
    {
        PWebserverStatistics p = wp.lock();
        if(!p)
            return;
        boost::mutex::scoped_lock lock(p->m_mutex);
        p->streams+=v;
    }
};

class EndpointStatisticsSerializer
{
public:
    EndpointStatisticsSerializer(const MMSS::EndpointStatistics& eps_)
        : eps(eps_)
    {
    }
    template<class TArchive>
    void serialize(TArchive &arch, const unsigned int /*version*/)
    {
        arch 
            & boost::serialization::make_nvp("bitrate", eps.bitrate)
            & boost::serialization::make_nvp("fps", eps.fps)
            & boost::serialization::make_nvp("width", eps.width)
            & boost::serialization::make_nvp("height", eps.height)
            & boost::serialization::make_nvp("mediaType", eps.mediaType)
            & boost::serialization::make_nvp("streamType", eps.streamType)
            ;
    }
private:
    const MMSS::EndpointStatistics& eps;
};

class CStatisticsResponseDecorator: public NHttp::IResponse
{
public:
    CStatisticsResponseDecorator(NHttp::PResponse r, PWebserverStatistics s)
        :   m_response(r)
        ,   m_statistics(s)
    {
    }
    virtual void SetVersion (int major, int minor)
    {
        m_response->SetVersion(major, minor);
    }
    virtual void SetStatus  (EStatus code)
    {
        m_response->SetStatus(code);
    }
    virtual void SetHeaders (const NHttp::THttpHeaders& h)
    {
        m_response->SetHeaders(h);
    }

    virtual void AddHeader (const NHttp::SHttpHeader&h)
    {
        m_response->AddHeader(h);
    }

    virtual void AsyncWrite (const void* buf
                              ,size_t bufSize
                              ,const FAsyncWriteHandler& wh)
    {
        m_statistics->IncrementBytesOut(bufSize);
        m_response->AsyncWrite(buf, bufSize, wh);
    }

    virtual void AsyncWrite (const TConstBufferSeq& seq
                              ,const FAsyncWriteHandler& wh)
    {
        uint64_t s=0;
        for(TConstBufferSeq::const_iterator i=seq.begin(); i!=seq.end(); ++i)
            s+=(*i).size();
        m_statistics->IncrementBytesOut(s);
        m_response->AsyncWrite(seq, wh);
    }

    virtual void FlushHeaders()
    {
        m_response->FlushHeaders();
    }

private:
    NHttp::PResponse m_response;
    PWebserverStatistics m_statistics;
};

class CStatisticsServlet 
    : public NHttp::IStatisticsHandler
    , public NHttpImpl::CBasicServletImpl
{
    DECLARE_LOGGER_HOLDER;

    struct SStatisticContext
    {
        std::mutex mutex;
        std::map<std::string, NMMSS::PSinkEndpoint> videoStreams;
        std::map<std::string, std::string> detector2camera;
    };
    typedef std::shared_ptr<SStatisticContext> PStatisticContext;

    typedef std::shared_ptr<Json::Value> PJsonValue;

public:
    CStatisticsServlet(NCorbaHelpers::IContainer* c,
                       const NWebGrpc::PGrpcManager grpcManager,
                       PStatisticsCache& statisticsCache)
        : m_reactor(NCorbaHelpers::GetReactorInstanceShared())
        , m_statistics(new WebserverStatistics(m_reactor->GetIO()))
        , m_container(c)
        , m_grpcManager(grpcManager)
        , m_statisticsCache(statisticsCache)
    {
        INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        m_statistics->ScheduleUpdate();
        NPluginHelpers::SetSinkCounterHandler(
            boost::bind(&WebserverStatistics::ModStreams, 
                WPWebserverStatistics(m_statistics), _1));
    }

    virtual void Head(const NHttp::PRequest req, NHttp::PResponse resp)
    {
        Handle(req, resp, true);
    }

    virtual void Get(const NHttp::PRequest req, NHttp::PResponse resp)
    {
        Handle(req, resp, false);
    }

    virtual void Post(const NHttp::PRequest req, NHttp::PResponse resp)
    {
        NPluginUtility::TParams params;
        if (!NPluginUtility::ParseParams(req->GetQuery(), params))
        {
            Error(resp, IResponse::BadRequest);
            return;
        }

        std::vector<uint8_t> body(req->GetBody());
        std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());


        PJsonValue json = std::make_shared<Json::Value>();
        Json::CharReaderBuilder reader;
        std::string err;
        std::istringstream is(bodyContent);
        if (!Json::parseFromStream(reader, is, &(*json), &err))
        {
            _err_ << "Statistics plugin : Error occured(" << err << ") during parsing body content : " << bodyContent;
            Error(resp, IResponse::BadRequest);
            return;
        }

        if (!json->isArray())
        {
            _err_ << "Statistics plugin: request body must contains array of endpoints. Body: " << bodyContent;
            Error(resp, IResponse::BadRequest);
            return;
        }

        const IRequest::AuthSession& as = req->GetAuthSession();
        NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

        NWebBL::TEndpoints eps;
        Json::ArrayIndex cameraCount = json->size();
        for (Json::ArrayIndex i = 0; i < cameraCount; ++i)
        {
            eps.push_back((*json)[i].asString());
        }

        PStatisticContext ctxOut = std::make_shared<SStatisticContext>();

        NWebBL::FAction action = boost::bind(&CStatisticsServlet::onCameraInfo, shared_from_base<CStatisticsServlet>(),
            req, resp, metaCredentials, ctxOut, _1, _2, _3);
        NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, metaCredentials, eps, action);
    }

    virtual NHttp::PResponse Process(const NHttp::PRequest req, NHttp::PResponse resp)
    {
        m_statistics->IncrementRequests();
        return NHttp::PResponse(new CStatisticsResponseDecorator(resp, m_statistics));
    }

private:
    void onCameraInfo(const PRequest req, PResponse resp, NGrpcHelpers::PCredentials metaCredentials, PStatisticContext ctxOut,
        const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
    {
        if (!grpcStatus.ok())
        {
            return NPluginUtility::SendGRPCError(resp, grpcStatus);
        }

        processCameras(ctxOut, items);

        if (valid == NWebGrpc::_FINISH)
        {
            NCorbaHelpers::GetReactorInstanceShared()->GetIO().post([this, req, resp, ctxOut]() mutable
            {
                execStatistics(req, resp, ctxOut);
            });
        }
    }

    //detectorTable - map detector->camera
    void execStatistics(const NHttp::PRequest req, NHttp::PResponse resp, PStatisticContext ctx)
    {
        const IRequest::AuthSession& as = req->GetAuthSession();
        NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

        PStatisticsReader_t grpcReader(new StatisticsReader_t
            (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::statistics::StatisticService::Stub::AsyncGetStatistics));

        bl::statistics::StatsRequest grpcReq;

        {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            for (auto streamInfo : ctx->videoStreams)
            {
                const std::string name(streamInfo.first);
                addStatisticsRequirement(grpcReq, bl::statistics::SPT_LiveFPS, name);
                addStatisticsRequirement(grpcReq, bl::statistics::SPT_LiveBitrate, name);
                addStatisticsRequirement(grpcReq, bl::statistics::SPT_LiveWidth, name);
                addStatisticsRequirement(grpcReq, bl::statistics::SPT_LiveHeight, name);
                addStatisticsRequirement(grpcReq, bl::statistics::SPT_LiveMediaType, name);
                addStatisticsRequirement(grpcReq, bl::statistics::SPT_LiveStreamType, name);
            }
        }

        for (auto t : ctx->detector2camera)
            addStatisticsRequirement(grpcReq, bl::statistics::SPT_WaterLevel, t.first);

        StatisticsCallback_t sc = std::bind(&CStatisticsServlet::onStatisticsResponse,
            shared_from_base<CStatisticsServlet>(), resp,  ctx,
            std::placeholders::_1, std::placeholders::_2);

        grpcReader->asyncRequest(grpcReq, sc);
    }

    void Handle(const NHttp::PRequest req, NHttp::PResponse resp, bool headersOnly)
    {
        std::string pathInfo(req->GetPathInfo());

        if ("/webserver" == pathInfo)
        {
            std::stringstream stream;
            {
                boost::archive::json_oarchive arch(stream);
                arch << boost::serialization::make_naked_object(*m_statistics);
            }
            NPluginUtility::SendText(resp, stream.str(), true, headersOnly);
        }
        else if (0 == pathInfo.find("/hardware"))
        {
            NCorbaHelpers::PContainer cont(m_container);
            if (!cont) // shutting down
            {
                Error(resp, NHttp::IResponse::ServiceUnavailable, "Service unavailable");
                return;
            }

            std::map<std::string, Json::Value> hostInfo;

            InfraServer::HostAgent_var ha = NCorbaHelpers::ResolveServantOrThrow<InfraServer::HostAgent>(cont, "HostAgent/HostAgent");
            if (std::string::npos == pathInfo.find("/domain"))
            {
                addStatInfo(ha, hostInfo);
            }
            else
            {
                InfraServer::Domain_var domain = ha->GetDomain();
                if (CORBA::is_nil(domain))
                {
                    Error(resp, NHttp::IResponse::ServiceUnavailable, "Domain is not accessible.");
                    return;
                }
                InfraServer::HostStatusSeq_var seq = domain->EnumerateHostStatuses(true, true);
                for (CORBA::ULong i = 0; i < seq->length(); ++i)
                {
                    const std::string nodeName(seq[i].HostName.in());
                    const std::string endpoints(seq[i].Endpoints.in());

                    if (seq[i].State == InfraServer::EHostState::HOST_STARTED)
                    {
                        try
                        {
                            const int MAX_RESOLVE_MS = 3000;
                            InfraServer::HostAgent_var agent = NCorbaHelpers::ResolveRemoteServantOrThrow<InfraServer::HostAgent>(cont, endpoints, "HostAgent/HostAgent", MAX_RESOLVE_MS);
                            if (!CORBA::is_nil(agent))
                            {
                                addStatInfo(agent, hostInfo);
                            }
                        }
                        catch (const CORBA::Exception&)
                        {
                            _wrn_ << "Hardware statistics for node " << nodeName << " is not accessible";
                        }
                    }
                }
            }

            Json::Value hstats = Json::Value{ Json::arrayValue };
            std::map<std::string, Json::Value>::iterator it1 = hostInfo.begin(), it2 = hostInfo.end();
            for (; it1 != it2; ++it1)
                hstats.append(it1->second);

            NPluginUtility::SendText(req, resp, hstats.toStyledString());
        }
        else
        {
            NCorbaHelpers::PContainer cont(m_container);
            if(!cont) // shutting down
            {
                Error(resp, NHttp::IResponse::ServiceUnavailable, "Service unavailable");
                return;
            }

            try
            {
                std::string s = "hosts" + pathInfo;
                CORBA::Object_var o=cont->GetRootNC()->resolve_str(s.c_str());
                MMSS::Endpoint_var ep=MMSS::Endpoint::_narrow(o);
                if (!CORBA::is_nil(ep))
                {
                    MMSS::EndpointStatistics eps = ep->GetStatistics();
                    std::stringstream stream;
                    {
                        boost::archive::json_oarchive arch(stream);
                        EndpointStatisticsSerializer serializer(eps);
                        arch << boost::serialization::make_naked_object(serializer);
                    }

                    NPluginUtility::SendText(req, resp, stream.str());
                    return;
                }
            }
            catch(const CORBA::Exception& e)
            {
                _err_ << e._info();
            }
            Error(resp, NHttp::IResponse::NotFound);
        }
    }

    void addStatInfo(InfraServer::HostAgent_var ha, std::map<std::string, Json::Value>& stats)
    {
        std::string hostName(ha->HostName());

        Json::Value hstat;
        hstat["name"] = hostName;

        InfraServer::PlatformStat_var stat = ha->GetPlatformStat();
        hstat["totalCPU"] = boost::lexical_cast<std::string>(stat->CpuTotalUsage);
        hstat["netMaxUsage"] = boost::lexical_cast<std::string>(stat->NetMaxUsage);

        Json::Value drives(Json::arrayValue);

        InfraServer::DiskInfoList& dil = stat->DiskInformation;
        CORBA::ULong diskCount = dil.length();
        for (CORBA::ULong i = 0; i < diskCount; ++i)
        {
            Json::Value drive(Json::objectValue);
            drive["name"] = NCorbaHelpers::ToUtf8(dil[i].Name.in());
            drive["capacity"] = static_cast<Json::Value::UInt64>(dil[i].Capacity);
            drive["freeSpace"] = static_cast<Json::Value::UInt64>(dil[i].FreeSpace);

            drives.append(drive);
        }

        hstat["drives"] = drives;

        stats[hostName] = hstat;
    }

    void onStatisticsResponse(PResponse resp, PStatisticContext ctx, const bl::statistics::StatsResponse& res, grpc::Status status)
    {
        if (!status.ok())
        {
            return NPluginUtility::SendGRPCError(resp, status);
        }

        Json::Value responseObject(Json::objectValue);
        {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            for (auto streamInfo : ctx->videoStreams)
            {
                responseObject[streamInfo.first] = Json::Value(Json::objectValue);
                if (streamInfo.second)
                {
                    streamInfo.second->Destroy();
                    streamInfo.second.Reset();
                }
            }

            const ::google::protobuf::RepeatedPtrField< bl::statistics::StatPoint >& stats = res.stats();
            m_statisticsCache->AddOrUpdateStatisticsData(stats);
            int statCount = stats.size();
            for (int i = 0; i < statCount; ++i)
            {
                auto stat_name = stats[i].key().name();
                std::map<std::string, std::string>::const_iterator it;
                if (ctx->videoStreams.find(stat_name) != ctx->videoStreams.end())
                {
                    addField(responseObject[stat_name], stats[i]);
                }
                else if ((it = ctx->detector2camera.find(stat_name)) != ctx->detector2camera.end())
                {
                    addWaterField(responseObject[it->second], stats[i]);
                }
            }

            ctx->videoStreams.clear();
        }
        NPluginUtility::SendText(resp, NHttp::IResponse::OK, responseObject.toStyledString());
    }

    static void addField(Json::Value &json, const bl::statistics::StatPoint& sp)
    {
        switch (sp.key().type())
        {
        case bl::statistics::SPT_LiveFPS:
            json["fps"] = sp.value_double();
            break;
        case bl::statistics::SPT_LiveBitrate:
            json["bitrate"] = sp.value_uint64();
            break;
        case bl::statistics::SPT_LiveWidth:
            json["width"] = sp.value_uint32();
            break;
        case bl::statistics::SPT_LiveHeight:
            json["height"] = sp.value_uint32();
            break;
        case bl::statistics::SPT_LiveMediaType:
            json["mediaType"] = sp.value_uint32();
            break;
        case bl::statistics::SPT_LiveStreamType:
            json["streamType"] = sp.value_uint32();
            break;
        default:
            break;
        }
    }
 
    static void addWaterField(Json::Value &json, const bl::statistics::StatPoint& sp)
    {
        if (!json.isMember("waterLevel"))
            json["waterLevel"] = Json::Value(Json::objectValue);

        json["waterLevel"][sp.key().name()] = sp.value_double();
    }

    static void addStatisticsRequirement(bl::statistics::StatsRequest& grpcReq, bl::statistics::StatPointType spt, const std::string& name)
    {
        bl::statistics::StatPointKey* pointKey = grpcReq.add_keys();
        pointKey->set_type(spt);
        pointKey->set_name(name);
    }

    void processCameras(PStatisticContext ctxOut, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
    {
        int itemCount = cams.size();
        for (int i = 0; i < itemCount; ++i)
        {
            const bl::domain::Camera& c = cams.Get(i);

            int streamCount = c.video_streams_size();
            for (int k = 0; k < streamCount; ++k)
            {
                const bl::domain::VideoStreaming& vs = c.video_streams(k);
                
                if (c.breaks_unused_connections()/* && !c.is_activated()*/)
                {
                    setBUCContext(ctxOut, vs.stream_acess_point());
                }
                else
                {
                    std::unique_lock<std::mutex> lock(ctxOut->mutex);
                    ctxOut->videoStreams.insert(std::make_pair(vs.stream_acess_point(), NMMSS::PSinkEndpoint{}));
                }
            }
            

            int detectorCount = c.detectors_size();
            for (int j = 0; j < detectorCount; ++j)
            {
                const bl::domain::Detector& d = c.detectors(j);                
                
                if (d.is_activated() &&  isWaterLevel(d.events()))
                {
                    (ctxOut->detector2camera)[d.access_point()] = c.access_point();
                }
            }
        }
    }

    void setBUCContext(PStatisticContext ctx, const std::string& accessPoint)
    {
        NCorbaHelpers::PContainer cont = m_container;
        if (cont)
        {
            std::unique_lock<std::mutex> lock(ctx->mutex);
            if (ctx->videoStreams.end() == ctx->videoStreams.find(accessPoint))
            {
                _log_ << "Statistics: Activating BUC connection for " << accessPoint;
                NMMSS::PPullStyleSink dummy(NPluginHelpers::CreateDummySink(GET_LOGGER_PTR));
                ctx->videoStreams.insert(std::make_pair(accessPoint,
                    NMMSS::CreatePullConnectionByNsref(GET_LOGGER_PTR,
                        accessPoint.c_str(), cont->GetRootNC(), dummy.Get(),
                        MMSS::EAUTO)));
            }
        }
        else
            _wrn_ << "Source " << accessPoint << " in BUC mode";
    }

    static bool isWaterLevel(const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::DetectorEventInfo > &events)
    {
        int itemCount = events.size();
        for (int i = 0; i < itemCount; ++i)
        {
            const auto &detectorInfo = events.Get(i);
            if (detectorInfo.id() == "LevelOut")
                return true;
        }

        return false;
    }

    NCorbaHelpers::PReactor m_reactor;
    PWebserverStatistics m_statistics;
    NCorbaHelpers::WPContainer m_container;
    const NWebGrpc::PGrpcManager m_grpcManager;
    PStatisticsCache m_statisticsCache;
};

namespace NHttp
{
    IStatisticsHandler* CreateStatisticsHandler(NCorbaHelpers::IContainer* c,
                                                const NWebGrpc::PGrpcManager grpcManager,
                                                PStatisticsCache& statisticsCache)
    {
        return new CStatisticsServlet(c, grpcManager, statisticsCache);
    }

}
