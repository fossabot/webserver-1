#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/scope_exit.hpp>

#include <HttpServer/json_oarchive.h>
#include <HttpServer/BasicServletImpl.h>
#include <MediaType.h>
#include <PtimeFromQword.h>
#include <MMClient/MMClient.h>

#include "HttpPlugin.h"
#include "SendContext.h"
#include "DataSink.h"
#include "CommonUtility.h"
#include "RegexUtility.h"
#include "Constants.h"
#include "Tokens.h"
#include "Hls.h"
#include "UrlBuilder.h"
#include <CorbaHelpers/ResolveServant.h>

#include "../MMCoding/Initialization.h"

#include <axxonsoft/bl/archive/ArchiveSupport.grpc.pb.h>
#include <NativeBLClient/NativeBLClient.h>

#include "ArchivePlugin.h"

using namespace NHttp;
namespace bpt = boost::posix_time;
namespace npu = NPluginUtility;
namespace bl = axxonsoft::bl;

using IntervalReader_t = NWebGrpc::AsyncResultReader < bl::archive::ArchiveService, bl::archive::GetHistory2Request,
    bl::archive::GetHistory2Response >;
using PIntervalReader_t = std::shared_ptr < IntervalReader_t >;

using EndpointReader_t = NWebGrpc::AsyncResultReader < bl::archive::ArchiveService, bl::archive::CreateReaderEndpointRequest,
    bl::archive::CreateReaderEndpointResponse >;
using PEndpointReader_t = std::shared_ptr < EndpointReader_t >;

namespace ArchivePlugin
{
    const char* const MEDIA = "media";
    const char* const RINFO = "rendered-info";
    const char* const STOP = "stop";    
    const char* const FRAMES = "frames";
    const char* const INTERVALS = "intervals";
    const char* const STATISTICS = "statistics";
    const char* const CAPACITY = "capacity";
    const char* const DEPTH = "depth";
    const char* const TIMEON = "time-on";
    const char* const TIMEOFF = "time-off";   

    const int FRAME_LIMIT = 250;
    const int INTERVAL_LIMIT = 100;    
    const int SCALE_DEFAULT = 0;
    
    const uint32_t THRESHOLD_DEFAULT = 1;

    const std::string PARAM_SPEED_NAME = "speed";
    const std::string PARAM_FORMAT_NAME = "format";
    const std::string PARAM_RUNNING_ID_NAME = "id";
    const std::string PARAM_WIDTH = "w";
    const std::string PARAM_HEIGHT = "h";
    const std::string PARAM_CROP_X = "crop_x";
    const std::string PARAM_CROP_Y = "crop_y";
    const std::string PARAM_CROP_WIDTH = "crop_width";
    const std::string PARAM_CROP_HEIGHT = "crop_height";

    const std::string PARAM_FRAMERATE = "fr";
    const std::string MJPEG_FORMAT = "mjpeg";
    const std::string WEBM_FORMAT = "webm";
    const std::string H264_FORMAT = "h264";
    const std::string PARAM_THRESHOLD = "threshold";
    const char* const VIDEO_SOURCE = "SourceEndpoint.video";

    CArchiveContentImpl::CArchiveContentImpl(NCorbaHelpers::IContainerNamed* c, const NWebGrpc::PGrpcManager grpcManager,
                    const NPluginUtility::PRigthsChecker rightsChecker, const std::string& hlsContentPath,
                    UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache)
            :   m_container(c, NCorbaHelpers::ShareOwnership())
            ,   m_hlsManager(c, hlsContentPath, cache)
            ,   m_connectors(c)
            ,   m_grpcManager(grpcManager)
            ,   m_rightsChecker(rightsChecker)
            ,   m_urlBuilder(rtspUrls)
            ,   m_videoSourceCache(cache)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
            m_mmcoding.reset(new NMMSS::CMMCodingInitialization(GET_LOGGER_PTR));

            m_grpcClientBase = NGrpcHelpers::CreateGrpcClientBase(GET_LOGGER_PTR, NCorbaHelpers::GetReactorInstanceShared()->GetIO());
            m_nativeBLClient = NNativeBL::Client::GetLocalBL(GET_LOGGER_PTR, m_grpcClientBase, "HttpPlugin/ArchivePlugin");

            m_grpcClientBase->Start();
        }     

    CArchiveContentImpl::~CArchiveContentImpl()
    {
        std::vector<NMMSS::MediaMuxer::PMuxer> staleMuxers;
        std::vector<NMMSS::PSinkEndpoint> staleSinks;
        std::vector<PClientContext> staleContexts;

        NMMSS::MediaMuxer::PMuxer m;
        NMMSS::PSinkEndpoint s;
        PClientContext cc;

        {
            boost::mutex::scoped_lock lock(m_requestMutex);
            BOOST_FOREACH(TRequests::value_type &i, m_requests)
            {
                std::swap(m, i.second->m_muxer);
                std::swap(s, i.second->m_sinkEndpoint);
                std::swap(cc, i.second->m_clientContext);
                staleMuxers.push_back(m);
                if (s)
                    staleSinks.push_back(s);
                if (cc)
                    staleContexts.push_back(cc);
            }
        }

        if (cc)
            cc->Stop();
        if (s)
            s->Destroy();
        if (m)
            m->Stop();

        BOOST_FOREACH(NMMSS::PSinkEndpoint se, staleSinks)
            Destroy(se);

        BOOST_FOREACH(PClientContext cc, staleContexts)
            cc->Stop();

        m_nativeBLClient->Cancel();
        m_nativeBLClient.reset();
        m_grpcClientBase->Shutdown();
        m_grpcClientBase.reset();
    }

    void CArchiveContentImpl::OnFrame(const TRequestId &id, NMMSS::ISample *s)
    {
        bpt::ptime t = NMMSS::PtimeFromQword(s->Header().dtTimeBegin);

        PArchiveRequestContext arc = GetContext(id);
        if (arc)
            arc->m_timestamps.push_back(bpt::to_iso_string(t));
    }

    void CArchiveContentImpl::OnCompleted(const TRequestId &id, bool containsMore)
    {
        PArchiveRequestContext arc = GetContext(id);
        if (!arc)
            return;

        std::stringstream ss;
        {
            boost::archive::json_oarchive ar(ss);
            ar << boost::serialization::make_nvp("frames", arc->m_timestamps);
            ar << boost::serialization::make_nvp("more", containsMore);
        }

        std::string text(ss.str());

        arc->m_response->SetStatus(IResponse::OK);
        std::string contentType(NHttp::GetMIMETypeByExt("json"));
        contentType.append("; charset=utf-8");
        arc->m_response << ContentLength(text.size())
            << ContentType(contentType)
            << CacheControlNoCache();

        arc->m_response->FlushHeaders();

        NContext::PSendContext ctx(NContext::CreateStringContext(arc->m_response, text,
            boost::bind(&CArchiveContentImpl::RequestDone, this, arc, _1)));
        ctx->ScheduleWrite();
    }

    bool CArchiveContentImpl::expired(const NMMSS::ISample* s, const PArchiveRequestContext arc)
    {
        return NMMSS::PtimeFromQword(s->Header().dtTimeBegin) - arc->m_posixStartTime > boost::posix_time::milliseconds(arc->m_threshold);
    }

    void CArchiveContentImpl::OnSnapshotReceived(const TRequestId &id, NMMSS::ISample* s)
    {
        PArchiveRequestContext arc = GetContext(id);
        if (!arc)
            return;
        
        if (NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header()) ||
              (arc->m_thresholdExist && expired(s, arc)))
        {
            Error(arc->m_response, IResponse::NotFound);
            RequestDone(arc);
            return;
        }

        arc->m_response->SetStatus(IResponse::OK);
        arc->m_response << ContentLength(s->Header().nBodySize)
            << ContentType(GetMIMETypeByExt("jpeg"))
            << CacheControlNoCache();

        arc->m_response << SHttpHeader("X-Video-Original-Time", bpt::to_iso_string(NMMSS::PtimeFromQword(s->Header().dtTimeBegin)));

        arc->m_response->FlushHeaders();

        NContext::PSendContext ctx(NContext::CreateSampleContext(arc->m_response, s,
            boost::bind(&CArchiveContentImpl::RequestDone, this, arc, _1)));
        ctx->ScheduleWrite();
    }

    bool CArchiveContentImpl::ParseControls(const PRequest req, PArchiveRequestContext arc)
        /*throw()*/
    {
        using namespace npu;
        PMask media = Mask(MEDIA);
        PObjName ep = ObjName(3, "hosts/");
        const std::string path = req->GetPathInfo();

        if (Match(path, media / Mask(STOP) / Token(arc->StopRunning)))
            arc->m_archiveMode = ESTOP;

        else if (Match(path, media / Mask(RINFO) / Token(arc->LastRendered)))
            arc->m_archiveMode = ERENDERED_INFO;

        else if (Match(path, media / ep / Mask(TIMEON) / Token(arc->LastRendered)))
            arc->m_archiveMode = ETIMEON;

        else if (Match(path, media / ep / Mask(TIMEOFF) / Token(arc->LastRendered)))
            arc->m_archiveMode = ETIMEOFF;

        return arc->m_archiveMode != EUNKNOWN;
    }

    bool CArchiveContentImpl::ParseMedia(const PRequest req, PArchiveRequestContext arc)
        /*throw()*/
    {
        using namespace npu;
        PMask media = Mask(MEDIA);
        PObjName ep = ObjName(3, "hosts/");
        PDate begin = Begin(arc->m_posixStartTime);

        if (!Match(req->GetPathInfo(), media / ep / begin))
            return false;

        arc->m_endpoint = ep->Get().ToString();
        arc->m_archiveMode = ESNAPSHOT;

        try
        {
            GetParam(arc->params, PARAM_SPEED_NAME, arc->m_mediaSpeed);
            arc->m_archiveMode = ELIVE;
        }
        catch (const std::exception &) {}

        GetParam(arc->params, PARAM_RUNNING_ID_NAME, arc->m_id, arc->m_id);
        GetParam<std::string>(
            arc->params,
            PARAM_FORMAT_NAME,
            boost::bind(&CArchiveContentImpl::Convert, _1, boost::ref(arc->m_videoFormat)),
            MJPEG_FORMAT);
        GetParam(arc->params, PARAM_WIDTH, arc->m_width, 0);
        GetParam(arc->params, PARAM_HEIGHT, arc->m_height, 0);
        GetParam(arc->params, PARAM_CROP_X, arc->m_crop_x, 0.f);
        GetParam(arc->params, PARAM_CROP_Y, arc->m_crop_y, 0.f);
        GetParam(arc->params, PARAM_CROP_WIDTH, arc->m_crop_width, 1.f);
        GetParam(arc->params, PARAM_CROP_HEIGHT, arc->m_crop_height, 1.f);


        GetParam(arc->params, PARAM_ENABLE_TOKEN_AUTH, arc->m_enableTokenAuth, 0);
        GetParam(arc->params, PARAM_TOKEN_VALID_HOURS, arc->m_validForHours, 12);

        GetParam<std::uint32_t>(arc->params, KEY_FRAMES_PARAMETER, arc->m_keyFrames, 0);
        GetParam<float>(arc->params, PARAM_FRAMERATE, arc->m_fps, -1.);

        try
        {
            GetParam<uint32_t>(arc->params, PARAM_THRESHOLD, arc->m_threshold);
            arc->m_thresholdExist = true;
        }
        catch (const std::exception&)
        {
            arc->m_thresholdExist = false;
        }

        return true;
    }

    bool CArchiveContentImpl::ParseContents(const PRequest req, PArchiveRequestContext arc)
        /*throw()*/
    {
        using namespace npu;
        PMask contents = Mask(CONTENTS);
        PObjName ep = ObjName(3, "hosts/");
        PDate begin = Begin(arc->m_posixStartTime);
        PDate end = End(arc->m_posixEndTime);

        std::string hostname;

        const std::string path = req->GetPathInfo();
        if (Match(path, contents / Mask(FRAMES) / ep / end / begin))
            arc->m_archiveMode = EFRAMES;

        else if (Match(path, contents / Mask(INTERVALS) / ep / end / begin))
            arc->m_archiveMode = EINTERVALS;

        else if (Match(path, contents / Mask(BOOKMARKS) / Token(hostname) / end / begin))
        {
            arc->m_archiveMode = EBOOKMARKS;
            arc->m_hostname = hostname;
        }

        arc->m_endpoint = ep->Get().ToString();
        return arc->m_archiveMode != EUNKNOWN;
    }

    bool CArchiveContentImpl::ParseStatistics(DECLARE_LOGGER_ARG, const PRequest req, PArchiveRequestContext arc)
        /*throw()*/
    {
        using namespace npu;
        PMask stats = Mask(STATISTICS);
        PObjName ep = ObjName(3, "hosts/");
        PDate begin = Begin(arc->m_posixStartTime);
        PDate end = End(arc->m_posixEndTime);

        const std::string path = req->GetPathInfo();

        if (Match(path, stats / Mask(CAPACITY) / ep / end / begin))
            arc->m_archiveMode = ECAPACITY;

        else if (Match(path, stats / Mask(DEPTH) / ep))
        {
            arc->m_archiveMode = EDEPTH;
            try
            {
                GetParam<uint32_t>(arc->params, PARAM_THRESHOLD, arc->m_threshold);
            }
            catch (const std::exception&)
            {
                _wrn_ << "Required \'threshold\' parameter is absent or incorrectly formated. Fallback to default.";
            }
        }

        arc->m_endpoint = ep->Get().ToString();
        return arc->m_archiveMode != EUNKNOWN;
    }

    bool CArchiveContentImpl::ParseRequest(DECLARE_LOGGER_ARG, const PRequest req, PArchiveRequestContext arc)
        /*throw()*/
    {
        try {
            return ParseControls(req, arc)
                || ParseStatistics(GET_LOGGER_PTR, req, arc)
                || ParseMedia(req, arc)
                || ParseContents(req, arc);
        }
        catch (const std::exception& e)
        {
            _err_ << "Archive request failed. Reason: " << e.what();
        }
        return false;
    }

    void CArchiveContentImpl::Send(const PRequest req, PResponse resp, ESendMode )
    {
        PArchiveRequestContext arc(new ArchiveRequestContext());
        arc->m_id = GenerateRequestId();
        arc->m_response = resp;

        if (!npu::ParseParams(req->GetQuery(), arc->params))
        {
            Error(resp, IResponse::BadRequest);
            return;
        }

        if (m_hlsManager.HandleCommand(req, arc->params, resp))
        {
            // Handle hls control commands.
            return;
        }

        if (!ParseRequest(GET_LOGGER_PTR, req, arc))
        {
            Error(resp, IResponse::BadRequest);
            return;
        }

        const IRequest::AuthSession& as = req->GetAuthSession();
        arc->m_credentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

        if (!arc->m_endpoint.empty())
        {
            npu::GetParam<std::string>(arc->params, PARAM_ARCHIVE, arc->m_archiveName, std::string());
            if (!arc->m_archiveName.empty() && (std::string::npos == arc->m_archiveName.find(HOST_PREFIX)))
                arc->m_archiveName = HOST_PREFIX + arc->m_archiveName;

            NPluginUtility::PEndpointQueryContext ctxOut = std::make_shared<NPluginUtility::SEndpointQueryContext>();
            ctxOut->endpoint = arc->m_endpoint;
            ctxOut->archiveName = arc->m_archiveName;

            NWebBL::TEndpoints eps{ arc->m_endpoint };
            NWebBL::FAction action = boost::bind(&CArchiveContentImpl::onCameraInfo, shared_from_base<CArchiveContentImpl>(),
                req, resp, arc, ctxOut, _1, _2, _3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, eps, action);

            return;
        }

        execSend(req, resp, arc);
    }

    void CArchiveContentImpl::onCameraInfo(const PRequest req, PResponse resp, PArchiveRequestContext ctx, NPluginUtility::PEndpointQueryContext ctxOut,
        const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
    {
        if (!grpcStatus.ok())
        {
            _err_ << "/archive: Not found archive binding for " << ctx->m_endpoint;
            return NPluginUtility::SendGRPCError(resp, grpcStatus);
        }

        NPluginUtility::GetEndpointStorageSource(GET_LOGGER_PTR, items, ctxOut, true, true);

        if (valid == NWebGrpc::_FINISH)
        {
            if (ctxOut->requestedItem.empty())
            {
                _err_ << "Archive: empty accessPoint";
                return Error(resp, IResponse::NotFound);
            }

            ctx->m_storageSourceEndpoint = ctxOut->requestedItem;
            _dbg_ << "using storage endpoint " << ctx->m_storageSourceEndpoint << " for source " << ctx->m_endpoint;

            NCorbaHelpers::GetReactorInstanceShared()->GetIO().post([this, ctx, req, resp]() mutable
            {
                execSend(req, resp, ctx);
            });
        }
    }

    void CArchiveContentImpl::execSend(const PRequest req, PResponse resp, PArchiveRequestContext arc)
    {
        if (!AddContext(arc))
        {
            Error(resp, IResponse::BadRequest);
            return;
        }

        if (EINTERVALS == arc->m_archiveMode)
        {
            ProcessIntervalsRequest(req, arc);
            return;
        }

        auto pCont = m_container.Get();
        if (!pCont)
        {
            _err_ << "Archive: empty container";
            return Error(resp, IResponse::InternalServerError);
        }

        if (!arc->m_endpoint.empty())
        {
            arc->m_storageSource = NCorbaHelpers::ResolveServant<MMSS::StorageSource>(pCont, arc->m_storageSourceEndpoint);

            if (CORBA::is_nil(arc->m_storageSource))
            {
                _err_ << "Archive: can't resolve accessPoint: " << arc->m_storageSourceEndpoint;
                return Error(resp, IResponse::InternalServerError);
            }
        }

        switch (arc->m_archiveMode)
        {
        case ESNAPSHOT:      ProcessSnapshotRequest(arc);                   break;
        case ELIVE:          ProcessLiveRequest(req, arc, resp);            break;
        case EFRAMES:        ProcessFramesRequest(req, arc);                break;
        case EBOOKMARKS:     ProcessBookmarksRequest(req, arc);             break;
        case ESTOP:          ProcessStopping(arc);                          break;
        case ERENDERED_INFO: ProcessRenderedInfo(arc);                      break;
        case ECAPACITY:      ProcessCapacityRequest(arc);                   break;
        case EDEPTH:         ProcessDepthRequest(arc);                      break;
        case ETIMEON:        EnableEventStream(arc);                        break;
        case ETIMEOFF:       DisableEventStream(arc);                       break;
        default:             Error(resp, IResponse::InternalServerError);   break;
        }
    }

    void CArchiveContentImpl::ProcessStopping(PArchiveRequestContext arc)
    {
        try
        {
            m_hlsManager.Erase(arc->StopRunning);
            PArchiveRequestContext context = GetContext(arc->StopRunning);
            if (context)
            {
                SendRenderedInfo(context, arc);
                RequestDone(context);
            }
            else
            {
                Error(arc->m_response, IResponse::NotFound);
            }
        }
        catch (const std::exception &e)
        {
            _err_ << "Couldn't stop the muxer, an error had been occured: " << e.what();
            Error(arc->m_response, IResponse::InternalServerError);
        }
        RequestDone(arc);
    }

    void CArchiveContentImpl::ProcessRenderedInfo(PArchiveRequestContext arc)
    {
        PArchiveRequestContext context = GetContext(arc->LastRendered);
        try
        {
            if (context && context->m_clientContext)
                SendRenderedInfo(context, arc);
            else
                Error(arc->m_response, IResponse::NotFound);
        }
        catch (const std::exception &e)
        {
            _err_ << "Couldn't obtain last rendered sample's information, an error had been occured: " << e.what();
            Error(arc->m_response, IResponse::InternalServerError);
        }
        RequestDone(arc);
    }

    void CArchiveContentImpl::EnableEventStream(PArchiveRequestContext arc)
    {
        PArchiveRequestContext context = GetContext(arc->LastRendered);
        try
        {
            if (context && context->m_clientContext)
            {
                NHttp::PResponse resp = arc->m_response;

                NHttp::SHttpHeader connectionHeader(CONNECTION_HEADER, CONNECTION_TYPE);

                resp->SetStatus(IResponse::OK);
                resp << ContentType(EVENT_STREAM_TYPE)
                    << CacheControlNoCache()
                    << connectionHeader;

                resp->FlushHeaders();


                context->m_clientContext->CreateEventStream(resp);
            }
            else
                Error(arc->m_response, IResponse::NotFound);
        }
        catch (const std::exception &e)
        {
            _err_ << "Couldn't obtain last rendered sample's information, an error had been occured: " << e.what();
            Error(arc->m_response, IResponse::InternalServerError);
        }
        RequestDone(arc);
    }

    void CArchiveContentImpl::DisableEventStream(PArchiveRequestContext arc)
    {
        PArchiveRequestContext context = GetContext(arc->LastRendered);
        try
        {
            if (context && context->m_clientContext)
            {
                context->m_clientContext->RemoveEventStream();
            }
        }
        catch (const std::exception &e)
        {
            _err_ << "Couldn't obtain last rendered sample's information, an error had been occured: " << e.what();
            Error(arc->m_response, IResponse::InternalServerError);
        }
        Error(arc->m_response, IResponse::OK);
        RequestDone(arc);
    }

    void CArchiveContentImpl::SendRenderedInfo(PArchiveRequestContext src, PArchiveRequestContext dest)
    {
        std::stringstream response;
        {
            boost::archive::json_oarchive arch(response);
            std::string str(bpt::to_iso_string(NMMSS::PtimeFromQword(src->m_clientContext->GetTimestamp())));
            arch << boost::serialization::make_nvp("timestamp", str);
        }
        npu::SendText(dest->m_response, response.str(), true);
    }

    void CArchiveContentImpl::ProcessFramesRequest(const PRequest req, PArchiveRequestContext arc)
    {
        NMMSS::PPullStyleSink sink(NPluginHelpers::CreateLimitedSink(
            boost::bind(&CArchiveContentImpl::OnFrame, this, arc->m_id, _1),
            boost::bind(&CArchiveContentImpl::OnCompleted, this, arc->m_id, _1),
            arc->m_posixStartTime,
            arc->m_posixEndTime,
            npu::GetParam(arc->params, LIMIT_MASK, FRAME_LIMIT)));

        auto pself = shared_from_base<CArchiveContentImpl>();
        auto func = [this, pself, sink, arc](MMSS::StorageEndpoint_var ep) {
            arc->m_sinkEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, ep, sink.Get());
        };

        const long playFlags = arc->m_posixEndTime < arc->m_posixStartTime
            ? NMMSS::PMF_REVERSE | NMMSS::PMF_KEYFRAMES
            : NMMSS::PMF_NONE;
            
        UseSourceEndpointReader(arc, bl::archive::START_POSITION_EXACTLY, playFlags, func);
    }

    void CArchiveContentImpl::ProcessLiveRequest(PRequest req, PArchiveRequestContext arc, PResponse resp)
    {
        try
        {
            auto expiresAt = boost::posix_time::second_clock::local_time() +
                boost::posix_time::hours(std::min(abs(arc->m_validForHours), 24 * 7)); // Lease a week at max.

            if (ERTSP == arc->m_videoFormat)
            {
                return m_urlBuilder->handleArchiveRtsp(resp, arc->m_endpoint,
                    to_iso_string(arc->m_posixStartTime), arc->m_mediaSpeed,
                    arc->m_enableTokenAuth, expiresAt);
            }

            if (arc->m_enableTokenAuth)
            {
                // Unregister current request
                {
                    boost::mutex::scoped_lock lock(m_requestMutex);
                    m_requests.erase(arc->m_id);
                }

                npu::TParams params;
                if (!npu::ParseParams(req->GetQuery(), params))
                {
                    Error(arc->m_response, IResponse::BadRequest);
                    return;
                }

                m_urlBuilder->handleHttpToken(resp, req, params, expiresAt);
                return;
            }

            const std::string startTime(bpt::to_iso_string(arc->m_posixStartTime));
            RequestDoneFunc requestDone = boost::bind(&CArchiveContentImpl::RequestDone, this, arc);
            if (arc->m_videoFormat == EHLS)
            {
                m_hlsManager.ConnectToArchiveEnpoint(m_grpcManager, arc->m_credentials, req, resp, arc->m_storageSourceEndpoint, arc->m_archiveName, startTime, arc->params);

                return;
            }

            if (EMP4 == arc->m_videoFormat)
            {
                NWebBL::AuxillaryCallback_t ac = boost::bind(&CArchiveContentImpl::prepareMP4Endpoints,
                    shared_from_base<CArchiveContentImpl>(), resp, arc, requestDone, _1);
                NWebBL::ResolveCameraAudioStream(GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, arc->m_endpoint, ac);
                return;
            }
            else
            {
                PClientContext cc;
                try 
                {
                    NHttp::PCountedSynchronizer sync = boost::make_shared<NHttp::SCountedSynchronizer>(GET_LOGGER_PTR, m_container.Get());

                    NHttp::PArchiveContext aCtx = boost::make_shared<NHttp::SJpegArchiveContext>(GET_LOGGER_PTR, arc->m_width, arc->m_height);
                    aCtx->videoEndpoint = std::move(arc->m_storageSourceEndpoint);
                    aCtx->archiveName = std::move(arc->m_archiveName);
                    aCtx->startTime = startTime;
                    aCtx->keyFrames = arc->m_mediaSpeed < 0;
                    aCtx->speed = arc->m_mediaSpeed;

                    cc = m_videoSourceCache->CreateArchiveMp4Context(m_grpcManager, arc->m_credentials, resp, sync, aCtx, requestDone);
                    if (cc)
                    {
                        cc->Init();
                        arc->m_clientContext = cc;
                        arc->m_sync = sync;

                        sync->Start(arc->m_mediaSpeed);
                    }
                    return;
                }
                catch (...) {}
            }
        }
        catch (const CORBA::Exception&)
        {
        }
        Error(arc->m_response, IResponse::InternalServerError);
    }

    void CArchiveContentImpl::prepareMP4Endpoints(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, NWebBL::SAuxillarySources auSources)
    {
        _log_ << "Found archive audio endpoint: " << auSources.audioSource;

        NWebBL::EndpointCallback_t ec = boost::bind(&CArchiveContentImpl::runMP4Stream,
            shared_from_base<CArchiveContentImpl>(), resp, arc, rdf, _1, _2);

        NWebBL::ResolveCameraStorageSources(GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, arc->m_archiveName, arc->m_endpoint, auSources.audioSource, ec);
    }

    void CArchiveContentImpl::runMP4Stream(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, const std::string& videoEp, const std::string& audioEp)
    {
        _log_ << "Archive " << arc->m_archiveName << " contains: ";
        _log_ << "\tVideo endpoint: " << videoEp;
        _log_ << "\tAudio endpoint: " << audioEp;

        const std::string startTime(bpt::to_iso_string(arc->m_posixStartTime));

        try
        {
            NHttp::PArchiveContext aCtx = boost::make_shared<NHttp::SArchiveContext>(GET_LOGGER_PTR);
            aCtx->videoEndpoint = std::move(videoEp);
            aCtx->audioEndpoint = std::move(audioEp);
            aCtx->archiveName = std::move(arc->m_archiveName);
            aCtx->startTime = startTime;
            aCtx->keyFrames = arc->m_keyFrames;
            aCtx->speed = arc->m_mediaSpeed;

            if (!aCtx->audioEndpoint.empty())
            {
                const long playFlags = (aCtx->speed < 0) ? NMMSS::PMF_REVERSE | NMMSS::PMF_KEYFRAMES
                    : (aCtx->keyFrames) ? NMMSS::PMF_KEYFRAMES
                    : NMMSS::PMF_NONE;

                MMSS::StorageEndpoint_var endpoint =
                    NPluginUtility::ResolveEndoint(m_grpcManager,
                        arc->m_credentials,
                        NCorbaHelpers::PContainer(m_container).Get(),
                        aCtx->videoEndpoint,
                        aCtx->startTime,
                        aCtx->forward ? axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME : axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS,
                        playFlags);

                NMMSS::PPullStyleSink dummy(NPluginHelpers::CreateOneShotSink(GET_LOGGER_PTR,
                    boost::bind(&CArchiveContentImpl::adjustStreamTime, shared_from_base<CArchiveContentImpl>(), resp, arc, rdf, aCtx, _1)));
                arc->m_timestampEndpoint = NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR,
                    endpoint, dummy.Get(), MMSS::EAUTO);
            }
            else
            {
                runClientContext(resp, arc, rdf, aCtx);
            }
        }
        catch (const std::runtime_error& e)
        {
            _err_ << e.what();
            Error(resp, IResponse::InternalServerError);
            return;
        }
    }

    void CArchiveContentImpl::adjustStreamTime(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, NHttp::PArchiveContext aCtx, NMMSS::ISample* s)
    {
        if (arc->m_timestampEndpoint)
        {
            arc->m_timestampEndpoint->Destroy();
            arc->m_timestampEndpoint.Reset();
        }

        if (!s || NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&s->Header()))
        {
            _err_ << "No data for archive " << aCtx->archiveName;
            return Error(resp, IResponse::NotFound);
        }

        aCtx->startTime = boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(s->Header().dtTimeBegin));
        _log_ << "Calculated start time " << aCtx->startTime;

        runClientContext(resp, arc, rdf, aCtx);
    }

    void CArchiveContentImpl::runClientContext(PResponse resp, PArchiveRequestContext arc, RequestDoneFunc rdf, NHttp::PArchiveContext aCtx)
    {
        try
        {
            NHttp::PCountedSynchronizer sync = boost::make_shared<NHttp::SCountedSynchronizer>(GET_LOGGER_PTR, m_container.Get());

            PClientContext cc = m_videoSourceCache->CreateArchiveMp4Context(m_grpcManager, arc->m_credentials, resp, sync, aCtx, rdf);
            if (cc)
            {
                cc->Init();
                arc->m_clientContext = cc;
                arc->m_sync = sync;

                sync->Start(arc->m_mediaSpeed);
            }
        }
        catch (const std::runtime_error& e)
        {
            _err_ << e.what();
            Error(resp, IResponse::InternalServerError);
            return;
        }
    }

    void CArchiveContentImpl::ProcessSnapshotRequest(PArchiveRequestContext arc)
    {
        UseFunction_t func = boost::bind(&CArchiveContentImpl::useSnapshot,
            shared_from_base<CArchiveContentImpl>(), _1, arc);

        UseSourceEndpointReader(arc, arc->m_keyFrames ? bl::archive::START_POSITION_AT_KEY_FRAME : bl::archive::START_POSITION_EXACTLY, 0, func);
    }

    void CArchiveContentImpl::ProcessIntervalsRequest(const PRequest req, PArchiveRequestContext arc)
    {
        using namespace npu;

        static const std::uint64_t zeroTime = NMMSS::PtimeToQword(bpt::time_from_string("1900-01-01 00:00:00.000"));
        
        static const std::uint64_t minTime = NMMSS::PtimeToQword(bpt::ptime(boost::posix_time::min_date_time));
        static const std::uint64_t maxTime = NMMSS::PtimeToQword(bpt::ptime(boost::posix_time::max_date_time));

        std::uint64_t beginTime = minTime;
        std::uint64_t endTime = maxTime;

        if (!arc->m_posixStartTime.is_not_a_date_time())
            beginTime = NMMSS::PtimeToQword(arc->m_posixStartTime);

        if (!arc->m_posixEndTime.is_not_a_date_time())
            endTime = NMMSS::PtimeToQword(arc->m_posixEndTime);

        if (beginTime > endTime)
            std::swap(beginTime, endTime);

        PIntervalReader_t reader(new IntervalReader_t
            (GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, &bl::archive::ArchiveService::Stub::AsyncGetHistory2));

        auto pThis = shared_from_base<CArchiveContentImpl>();

        bl::archive::GetHistory2Request creq;
        creq.set_access_point(arc->m_storageSourceEndpoint);
        creq.set_begin_time(beginTime);
        creq.set_end_time(endTime);
        int limit = GetParam(arc->params, LIMIT_MASK, INTERVAL_LIMIT);
        creq.set_max_count(static_cast<uint32_t>(limit));
        creq.set_min_gap_ms(GetParam(arc->params, SCALE_MASK, SCALE_DEFAULT) * 1000);
 
        if (beginTime == minTime && endTime == maxTime)
            creq.set_scan_mode(axxonsoft::bl::archive::GetHistory2Request::SM_APPROXIMATE);

        reader->asyncRequest(creq, [this, pThis, arc, limit](const bl::archive::GetHistory2Response& res, grpc::Status status)
        {
            BOOST_SCOPE_EXIT(arc, this_)
            {
                this_->RequestDone(arc);
            }
            BOOST_SCOPE_EXIT_END

            if (!status.ok())
            {
                _err_ << "GetHistory2 error :"<< arc->m_storageSourceEndpoint;
                return NPluginUtility::SendGRPCError(arc->m_response, status);
            }

            Json::Value intervals(Json::arrayValue);

            const bool reverseOrder = arc->m_posixStartTime > arc->m_posixEndTime;
          
            int intervalCount = res.intervals_size();
            for (int i = 0; i < intervalCount; ++i)
            {
                const int index = reverseOrder ? (intervalCount - 1 - i) : i;
                const bl::archive::GetHistory2Response::Interval& interval = res.intervals(index);

                Json::Value ivl(Json::objectValue);
                try
                {
                    ivl["begin"] = bpt::to_iso_string(NMMSS::PtimeFromQword(zeroTime + interval.begin_time()));
                    ivl["end"] = bpt::to_iso_string(NMMSS::PtimeFromQword(zeroTime + interval.end_time()));
                }
                catch (const std::out_of_range&)
                {
                    _wrn_ << i << "th interval contains invalid time range";
                    continue;
                }
                
                intervals.append(ivl);
            }

            Json::Value responseObject(Json::objectValue);
            responseObject["intervals"] = intervals;
            responseObject["more"] = res.result() != ::axxonsoft::bl::archive::GetHistory2Response_EResult_FULL;

            npu::SendText(arc->m_response, IResponse::OK, responseObject.toStyledString());
        });
    }

    void CArchiveContentImpl::ProcessCapacityRequest(PArchiveRequestContext arc)
    {
        CORBA::LongLong size = -1, duration = -1;
        std::string startTime(bpt::to_iso_string(arc->m_posixStartTime));
        std::string endTime(bpt::to_iso_string(arc->m_posixEndTime));
        try
        {
            arc->m_storageSource->GetSize(endTime.c_str(), startTime.c_str(), size, duration);
        }
        catch (const CORBA::Exception& e)
        {
            _err_ << "Capacity query failed. Reason: " << e._info();
            Error(arc->m_response, IResponse::InternalServerError);
            return;
        }

        std::stringstream response;
        {
            boost::archive::json_oarchive arch(response);
            arch << boost::serialization::make_nvp("size", size);
            arch << boost::serialization::make_nvp("duration", duration);
        }
        npu::SendText(arc->m_response, response.str(), true);
    }

    void CArchiveContentImpl::ProcessDepthRequest(PArchiveRequestContext arc)
    {
        MMSS::StorageSource::Interval depth;
        try
        {
            MMSS::Days treshold{ arc->m_threshold };
            if (!arc->m_storageSource->GetRelevantHistoryRange(treshold, depth))
            {
                npu::SendText(arc->m_response, "{}");
                return;
            }
        }
        catch (const CORBA::Exception& e)
        {
            _err_ << "Depth query failed. Reason: " << e._info();
            Error(arc->m_response, IResponse::InternalServerError);
            return;
        }

        std::string startTime(boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(depth.beginTime)));
        std::string endTime(boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(depth.endTime)));

        std::stringstream response;
        {
            boost::archive::json_oarchive arch(response);
            arch << boost::serialization::make_nvp("start", startTime);
            arch << boost::serialization::make_nvp("end", endTime);
        }
        npu::SendText(arc->m_response, response.str(), true);
    }

    void CArchiveContentImpl::UseSourceEndpointReader(PArchiveRequestContext arc, axxonsoft::bl::archive::EStartPosition pos, const long playFlags, UseFunction_t func)
    {
        const std::string startTime(bpt::to_iso_string(arc->m_posixStartTime));

        bl::archive::CreateReaderEndpointRequest creq;
        creq.set_access_point(arc->m_storageSourceEndpoint);
        creq.set_begin_time(startTime);
        creq.set_start_pos_flag(pos);
        creq.set_mode(playFlags);
        creq.set_is_realtime(false);
        creq.set_priority(bl::archive::ERP_Mid);

        PEndpointReader_t grpcReader(new EndpointReader_t
            (GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, &bl::archive::ArchiveService::Stub::AsyncCreateReaderEndpoint));

        grpcReader->asyncRequest(creq, [this, arc, func](const bl::archive::CreateReaderEndpointResponse& res, grpc::Status status)
        {
            if (!status.ok())
            {
                _err_ << "NativeBL error: call CreateReaderEndpoint failed";
                return NPluginUtility::SendGRPCError(arc->m_response, status);
            }

            MMSS::StorageEndpoint_var endpoint = NPluginUtility::ResolveEndoint(m_container.Get(), res.endpoint());
            if (CORBA::is_nil(endpoint))
            {
                _err_ << "Cannot resolve access point " << res.endpoint().access_point();
                Error(arc->m_response, IResponse::InternalServerError);
                return;
            }

            try
            {
                func(endpoint);
            }
            catch (std::exception const& e)
            {
                _err_ << "CArchiveContentImpl::UseSourceEndpointReader: unexcpected exception from lambda: " << e.what();
            }
        });
    }

    void CArchiveContentImpl::useMuxer(MMSS::StorageEndpoint_var endpoint, PArchiveRequestContext arc, NMMSS::IPullStyleSink* sink,
        int width, int height, NMMSS::MediaMuxer::EOutputFormat format, float fps)
    {
        NMMSS::MediaMuxer::POutputStreamed os(
            NMMSS::CreateOutputToPullSink(GET_LOGGER_PTR, arc->m_mediaSpeed, 1));

        NMMSS::GetConnectionBroker()->SetConnection(os.Get(), sink, GET_LOGGER_PTR);

        NMMSS::MediaMuxer::PMuxer muxer(
            NMMSS::CreateMuxer(GET_LOGGER_PTR, os.Get(), format, m_container.Get(), -1, 0, 0, 0, 0, 0, width, height, fps));

        arc->m_muxer = muxer;

        muxer->AddEndpoint(endpoint.in());
        muxer->Start();
    }

    void CArchiveContentImpl::useSnapshot(MMSS::StorageEndpoint_var endpoint, PArchiveRequestContext arc)
    {
        try
        {
            NMMSS::PPullStyleSink sink(NPluginHelpers::CreateSink(
                boost::bind(&CArchiveContentImpl::OnSnapshotReceived, this, arc->m_id, _1)));

            NMMSS::MediaMuxer::POutputStreamed os(
                NMMSS::CreateOutputToPullSink(GET_LOGGER_PTR));

            arc->m_output = os;

            NMMSS::GetConnectionBroker()->SetConnection(os.Get(), sink.Get(), GET_LOGGER_PTR);

            arc->SnapshotSession.reset(NMMSS::CreateSnapshot(GET_LOGGER_PTR, os.Get(),
                endpoint.in(), 0, bpt::seconds(60), arc->m_width, arc->m_height,
                true, arc->m_crop_x, arc->m_crop_y, arc->m_crop_width, arc->m_crop_height));
            return;
        }
        catch (const CORBA::Exception& e)
        {
            _err_ << "Archive snapshot error. CORBA exception: " << e._info();
        }
        catch (const std::exception& e)
        {
            _err_ << "Archive snapshot error. std exception: " << e.what();
        }
        Error(arc->m_response, IResponse::InternalServerError);
    }

    void CArchiveContentImpl::Convert(const std::string &src, EVF &dest)
    {
        if (boost::iequals(MJPEG_FORMAT, src))
            dest = EMJPEG;
        else if (boost::iequals(WEBM_FORMAT, src))
            dest = EWEBM;
        else if (boost::iequals(H264_FORMAT, src))
            dest = EH264;
        else if (boost::iequals(FORMAT_VALUE_HLS, src))
            dest = EHLS;
        else if (boost::iequals(FORMAT_VALUE_RTSP, src))
            dest = ERTSP;
        else if (boost::iequals(FORMAT_VALUE_MP4, src))
            dest = EMP4;

    }

    void CArchiveContentImpl::RequestDone(const PArchiveRequestContext context)
    {
        NMMSS::MediaMuxer::PMuxer staleMuxer;
        NMMSS::PSinkEndpoint staleSink;
        PClientContext staleClientContext;

        {
            boost::mutex::scoped_lock lock(m_requestMutex);
            std::swap(staleMuxer, context->m_muxer);
            std::swap(staleSink, context->m_sinkEndpoint);
            std::swap(staleClientContext, context->m_clientContext);
            m_requests.erase(context->m_id);
        }

        if (staleMuxer)
            staleMuxer->Stop();

        if (staleSink)
            Destroy(staleSink);

        if (staleClientContext)
            staleClientContext->Stop();
    }

    void CArchiveContentImpl::RequestDone(const PArchiveRequestContext context, boost::system::error_code ec)
    {
        if ((ELIVE == context->m_archiveMode) && !ec)
            return;
        else
            RequestDone(context);
    }

    bool CArchiveContentImpl::AddContext(PArchiveRequestContext context)
    {
        boost::mutex::scoped_lock lock(m_requestMutex);
        return m_requests.insert(std::make_pair(context->m_id, context)).second;
    }

    CArchiveContentImpl::PArchiveRequestContext CArchiveContentImpl::GetContext(const TRequestId &id) const
    {
        PArchiveRequestContext res;

        boost::mutex::scoped_lock lock(m_requestMutex);
        TRequests::iterator it = m_requests.find(id);
        if (m_requests.end() != it)
            res = it->second;
        return res;
    }

    std::string CArchiveContentImpl::GenerateRequestId() const
    {
        return NCorbaHelpers::GenerateUUIDString();
    }

    void CArchiveContentImpl::Destroy(NMMSS::PSinkEndpoint sink) /*throw()*/
    {
        try
        {
            sink->Destroy();
        }
        catch (const std::exception &) {}
    }
}

namespace NHttp
{
    IServlet* CreateArchiveServlet(NCorbaHelpers::IContainerNamed* c,
        const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rightsChecker,
        const std::string& hlsContentPath, UrlBuilderSP rtspUrls, NHttp::PVideoSourceCache cache)
    {
        return new ArchivePlugin::CArchiveContentImpl(c, grpcManager, rightsChecker, hlsContentPath, rtspUrls, cache);
    }
}
