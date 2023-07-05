#include <mutex>

#include <boost/any.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>

#include <HttpServer/BasicServletImpl.h>
#include <HttpServer/HttpRequest.h>
#include <HttpServer/HttpResponse.h>
#include <HttpServer/json_oarchive.h>

#include "BLQueryHelper.h"
#include "Tokens.h"
#include "Constants.h"
#include "HttpPlugin.h"
#include "SendContext.h"
#include "RegexUtility.h"
#include "CommonUtility.h"

#include "../PtimeFromQword.h"
#include <CorbaHelpers/Envar.h>
#include <CorbaHelpers/Uuid.h>
#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Unicode.h>
#include <MMExport/PlainSession.h>

#include <json/json.h>

#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

using namespace NHttp;
namespace npu = NPluginUtility;
namespace bpt = boost::posix_time;
namespace bl = axxonsoft::bl;

using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
    bl::domain::BatchGetCamerasResponse >;
using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

namespace
{
    const char* const VIDEO_SOURCE = "SourceEndpoint.video";
    const char* const AUDIO_SOURCE = "SourceEndpoint.audio";

    const char* const MAX_FILE_SIZE_PARAMETER = "maxfilesize";
    const char* const CROP_AREA_PARAMETER = "croparea";
    const char* const MASK_SPACE_PARAMETER = "maskspace";
    const char* const COMMENT_PARAMETER = "comment";
    const char* const COLOR_PARAMETER = "color";
    const char* const FRAME_FREQUENCY_PARAMETER = "freq";
    const char* const FILENAME_PARAMETER = "filename";
    const char* const SNAPSHOT_PLACE_PARAMETER = "snapshotplace";
    const char* const COMMENT_PLACE_PARAMETER = "commentplace";
    const char* const TIMESTAMP_PLACE_PARAMETER = "tsplace";
    const char* const LAYOUT_PARAMETER = "layout";
    const char* const VIDEO_COMPRESSION_PARAMETER = "vc";
    const char* const AUDIO_COMPRESSION_PARAMETER = "ac";
    const char* const TIMESTAMP_FORMAT_PARAMETER = "tsformat";
    const char* const WAIT_TIMEOUT_PARAMETER = "waittimeout";

    const char* const LIVE_MODE = "live";
    const char* const ARCHIVE_MODE = "archive";

    const char* const STATUS = "status";
    const char* const STOP = "stop";
    const char* const FILE = "file";

    const std::uint16_t POINT_COORD_COUNT = 2;
    const std::uint16_t AREA_ELEMENT_COUNT = 2;
    const std::uint16_t COLOR_ELEMENT_COUNT = 3;
    const std::size_t   POLY_MIN_POINT_COUNT = 3;

    const std::uint32_t INITIAL_CHECK_TIMEOUT = 36060;
    const std::uint32_t CHECK_TIMEOUT = 300;

    const std::int64_t WEB_WAIT_TIMEOUT = 60000;

    const wchar_t* const AXXONNEXTWATERMARKSECRET = L"AXXONNEXTWATERMARKSECRET";

    const std::chrono::hours WAIT_TIME(10);

    struct SExportContext
    {
        std::string id;
        NMMSS::NExport::ESessionState state;
        float progress;
        std::string err;
        std::vector<std::wstring> files;
    };
    typedef std::shared_ptr<SExportContext> PExportContext;

    struct AreaIterator : public std::iterator<std::input_iterator_tag, float>
    {
    private:
        MMSS::Export::ViewArea* m_area;
        float m_currentValue;
    };

    struct SExportSession
    {
        SExportSession(NMMSS::NExport::PPlainSession session)
            : m_session(session)
            , m_lastAccessTime(std::chrono::steady_clock::time_point::max())
        {}

        void SetLastAccessTime()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_lastAccessTime = std::chrono::steady_clock::now();
        }

        std::chrono::steady_clock::time_point GetLastAccessTime() const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_lastAccessTime;
        }

        NMMSS::NExport::PPlainSession m_session;
    private:
        mutable std::mutex m_mutex;
        std::chrono::steady_clock::time_point m_lastAccessTime;
    };
    typedef std::shared_ptr<SExportSession> PExportSession;


    struct SCamContext
    {
        std::string audioEp;
        std::string textEp;
        std::string archiveEp;
    };

    typedef std::shared_ptr<SCamContext> PCamContext;
        
    class CExportContentImpl : public NHttpImpl::CBasicServletImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CExportContentImpl(NCorbaHelpers::IContainer *c, const std::string& exportContentPath, const NWebGrpc::PGrpcManager grpcManager
            , const NPluginUtility::PRigthsChecker rightsChecker)
            : m_container(c)
            , m_grpcManager(grpcManager)
            , m_exportContentPath(exportContentPath, boost::filesystem::detail::utf8_codecvt_facet())
            , m_timer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_rightsChecker(rightsChecker)
        {            
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);

            auto n_cores = std::max(NCorbaHelpers::CEnvar::NgpHardwareConcurrency(), 1U);
            m_pool = NExecutors::CreateDynamicThreadPool(c->GetLogger(), "ExpSession", 256, 0, n_cores*4);
        }

        ~CExportContentImpl()
        {
            if (exists(m_exportContentPath))
            {
                boost::filesystem::directory_iterator it1(m_exportContentPath), it2;
                for (; it1 != it2; ++it1)
                {
                    try
                    {
                        boost::filesystem::remove_all(it1->path());
                    }
                    catch(std::exception const& e)
                    {
                        _err_ << "Failed to remove " << it1->path() << ": " << e.what();
                    }
                }
            }

            if (m_pool)
            {
                m_pool->Shutdown();
                m_pool.reset();
            }
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Handle(req, resp);
        }

        virtual void Post(const PRequest req, PResponse resp)
        {
            m_rightsChecker->HasGlobalPermissions({ axxonsoft::bl::security::FEATURE_ACCESS_EXPORT }, req->GetAuthSession(),
              [=](bool hasPermissions)
                {
                    if (!hasPermissions)
                    {
                        Error(resp, IResponse::Forbidden);
                        return;
                    }

                    try
                    {
                        NCorbaHelpers::PContainer cont = m_container;
                        if (!cont)
                        {
                            Error(resp, IResponse::InternalServerError);
                            return;
                        }

                        npu::TParams params;
                        if (!npu::ParseParams(req->GetQuery(), params))
                        {
                            Error(resp, IResponse::BadRequest);
                            return;
                        }

                        std::vector<uint8_t> body(req->GetBody());
                        std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

                        Json::Value json;
                        Json::CharReaderBuilder reader;
                        std::string err;
                        std::istringstream is(bodyContent);
                        if (!Json::parseFromStream(reader, is, &json, &err))
                        {
                            _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                            Error(resp, IResponse::BadRequest);
                            return;
                        }

                        NMMSS::NExport::SExportSettings ctx;

                        if (!(ParseExportCommand(ARCHIVE_MODE, req, resp, cont, json, ctx, params) ||
                            ParseExportCommand(LIVE_MODE, req, resp, cont, json, ctx, params))
                            )
                        {
                            Error(resp, IResponse::BadRequest);
                            return;
                        }
                    }
                    catch (const std::invalid_argument& e)
                    {
                        _err_ << e.what();
                        Error(resp, IResponse::BadRequest);
                        return;
                    }
                    catch (const Json::LogicError& e)
                    {
                        _err_ << e.what();
                        Error(resp, IResponse::BadRequest);
                        return;
                    }
                }
               
            );
        }

        virtual void Delete(const PRequest req, PResponse resp)
        {
            if (!m_pool->Post([=]() {
                try {
                    ParseStopCommand(req, resp);
                }
                catch (...)
                {
                    _err_ << "DynamicThreadPool error: Export stop processing failed";
                }
            }))
            {
                _err_ << "Error scheduling export stop command: task queue is full";
                Error(resp, IResponse::TooManyRequests);
            }
        }

        virtual void Options(const NHttp::PRequest, NHttp::PResponse resp)
        {
            NHttp::SHttpHeader allowHeadersHeader("Access-Control-Allow-Headers", "Authorization");
            NHttp::SHttpHeader allowMethodsHeader("Access-Control-Allow-Methods", "POST, GET, DELETE, OPTIONS");

            resp->SetStatus(IResponse::OK);
            resp << allowHeadersHeader << allowMethodsHeader;
            resp->FlushHeaders();
        }

    private:
        void Handle(const PRequest req, PResponse resp)
        {
            try
            {
                if (!(ParseStatusCommand(req, resp) ||
                    ParseFileCommand(req, resp))
                    )
                {
                    _err_ << "Unknown query: " << req->GetPathInfo();
                    Error(resp, IResponse::BadRequest);
                    return;
                }
            }
            catch (const std::invalid_argument& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::BadRequest);
                return;
            }
        }

        bool ParseExportCommand(const char* const mode, PRequest req, PResponse resp, NCorbaHelpers::PContainer c, const Json::Value& params, 
            NMMSS::NExport::SExportSettings& ctx, const npu::TParams& reqParams)
        {
            npu::PMask mask = npu::Mask(mode);
            npu::PObjName ep = npu::ObjName(3, "hosts/");
            bpt::ptime startTime, endTime;
            npu::PDate begin = npu::Begin(startTime);
            npu::PDate end = npu::End(endTime);

            if (!Match(req->GetPathInfo(), mask / ep / begin / end))
                return false;

            if (startTime > endTime)
                std::swap(startTime, endTime);

            SetDefaultParameters(ctx);

            ctx.FileFormat(ConvertToOutputFormat(ParseParameter(params, FORMAT_PARAMETER, std::string())));
            ctx.StartTime(NMMSS::PtimeToQword(startTime));
            ctx.EndTime(NMMSS::PtimeToQword(endTime));

            std::int64_t timeOut;
            npu::GetParam<std::int64_t>(reqParams, WAIT_TIMEOUT_PARAMETER, timeOut, WEB_WAIT_TIMEOUT);
            ctx.Wait(timeOut);

            bool isStream = IsStreamMode(ctx);

            std::string targetEp(ep->Get().ToString());

            if (targetEp.empty())
                return false;

            std::string archiveEp;
            bool archiveMode = false;
            bool bNeedDefaultArc = false;

            if (IsArchiveMode(mask))
            {
                archiveMode = true;
                npu::GetParam<std::string>(reqParams, PARAM_ARCHIVE, archiveEp, std::string());
                bNeedDefaultArc = archiveEp.empty();
            }

            std::string exportId(NCorbaHelpers::GenerateUUIDString());

            if (isStream || bNeedDefaultArc)
            {
                bl::domain::BatchGetCamerasRequest creq;
                {
                    bl::domain::ResourceLocator* rl = creq.add_items();
                    rl->set_access_point(NPluginUtility::convertToMainStream(targetEp));
                }

                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                PBatchCameraReader_t grpcReader(new BatchCameraReader_t
                    (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncBatchGetCameras));

                auto camCtx = std::make_shared<SCamContext>();
                grpcReader->asyncRequest(creq, [this, camCtx, ctx, startTime, endTime, targetEp, isStream, c, params, exportId,
                    archiveEp, req, resp, bNeedDefaultArc, archiveMode](const bl::domain::BatchGetCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus) mutable
                {
                    if (!grpcStatus.ok())
                    {
                        NHttp::SHttpHeader contentDispositionHeader("Location", req->GetPrefix() + req->GetContextPath() + "/" + exportId);
                        NHttp::SHttpHeader exposeHeadersHeader("Access-Control-Expose-Headers", "Location");

                        resp->SetStatus(IResponse::InternalServerError);
                        resp
                            << exposeHeadersHeader
                            << contentDispositionHeader
                            << CacheControlNoCache();
                        resp->FlushHeaders();
                    }

                    processCameras(isStream, bNeedDefaultArc, camCtx, res.items(), archiveMode);

                    if (NWebGrpc::_FINISH == status)
                    {
                        //don't call big code from courutine
                        NCorbaHelpers::GetReactorInstanceShared()->GetIO().post(
                            std::bind(&CExportContentImpl::execParseExportCommand, this, 
                               c, ctx, params, startTime, endTime, targetEp, bNeedDefaultArc ? camCtx->archiveEp : archiveEp,
                              exportId, isStream, req, resp, camCtx->textEp, camCtx->audioEp));
                    }
                });
                return true;
            }

            execParseExportCommand(c, ctx, params, startTime, endTime, targetEp, archiveEp, exportId, isStream
                , req, resp, "", "");
            return true;
        }

        static void processCameras(bool isStream, bool bGetDefaultArchive, PCamContext camCtx, 
            const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, bool archiveMode)
        {
            if (cams.size() > 0)
            {                
                const bl::domain::Camera& c = cams.Get(0);

                if (isStream)
                {
                    //takes 1st audioStream
                    for(auto& mic : c.microphones())
                    {
                        if ((archiveMode && mic.microphone_access() == bl::security::MICROPHONE_ACCESS_FULL) 
                            || (!archiveMode && mic.is_activated() && mic.microphone_access() >= bl::security::MICROPHONE_ACCESS_MONITORING))
                        {
                            camCtx->audioEp = c.microphones(0).access_point();
                            break;
                        }
                    }

                    int textSourceSize = c.text_sources_size();

                    if (1 == textSourceSize)
                        camCtx->textEp = c.text_sources(0).access_point();
                    else if (textSourceSize > 1)  //берём наиболее короткий (из hosts/I-BONDARENKO/DeviceIpint.4/SourceEndpoint.textEvent:0:<guid> и hosts/I-BONDARENKO/DeviceIpint.4/SourceEndpoint.textEvent:0)
                    {
                        camCtx->textEp = c.text_sources(0).access_point();

                        for (int j = 1; j < textSourceSize; ++j)
                        {
                            auto txtSrc = c.text_sources(j).access_point();

                            if (camCtx->textEp.size() > txtSrc.size())
                                camCtx->textEp = txtSrc;
                        }
                    }
                }

                if (bGetDefaultArchive)
                {
                    int arcCount = c.archive_bindings_size();
                    for (int j = 0; j < arcCount; ++j)
                    {
                        const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                        if (ab.is_default())
                        {
                            camCtx->archiveEp = ab.storage();
                            return;
                        }
                    }

                    for (int j = 0; j < arcCount; ++j)
                    {
                        const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                        if (!ab.archive().is_embedded())
                        {
                            auto it = std::find_if(ab.sources().begin(), ab.sources().end(), [&](const bl::domain::StorageSource& c)
                            {
                                return boost::contains(c.media_source(), VIDEO_SOURCE) && c.origin_storage().empty() && c.origin_storage_source().empty();
                            });

                            if (it != ab.sources().end())
                            {
                                camCtx->archiveEp = ab.storage();
                                return;
                            }
                        }
                    }

                    for (int j = 0; j < arcCount; ++j)
                    {
                        const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                        if (!ab.archive().is_embedded() && ab.sources_size() > 0)
                        {
                            camCtx->archiveEp = ab.storage();
                            return;
                        }
                    }

                    for (int j = 0; j < arcCount; ++j)
                    {
                        const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                        if (ab.sources_size() > 0)
                        {
                            camCtx->archiveEp = ab.storage();
                            return;
                        }
                    }
                }
            }
        }

        std::string extractClientLanguage(const PRequest& req) const noexcept
        {
            std::string preferredLocale;
            if (auto acceptLanguage = req->GetHeader("Accept-Language"))
            {
                if (!acceptLanguage->empty() && *acceptLanguage != "*")
                {
                    auto langPos = acceptLanguage->find(',');
                    if (langPos != std::string::npos)
                        preferredLocale = acceptLanguage->substr(0, langPos);
                    else
                        preferredLocale = *acceptLanguage;
                }
            }

            return preferredLocale;
        }

        /*private*/
        void execParseExportCommand(NCorbaHelpers::PContainer c, NMMSS::NExport::SExportSettings &ctx, const Json::Value &params, const bpt::ptime &startTime, const bpt::ptime &endTime
            , std::string &targetEp, const std::string &archiveEp, const std::string &exportId, bool isStream
            , PRequest req, PResponse resp
            , const std::string &textSource, const std::string &audioSource)
        {
            NCorbaHelpers::CObjectName on = NCorbaHelpers::CObjectName::FromString(targetEp);

            NMMSS::NExport::SContextSettings mmc;
            mmc.Name = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(on.GetObjectType()).c_str());
            mmc.Id = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(on.GetObjectId()).c_str());
            mmc.CropArea(ParseArea(params[CROP_AREA_PARAMETER]));
            mmc.CommonMask(ParseMask(params[MASK_SPACE_PARAMETER]));

            FillSources(c, mmc, targetEp, archiveEp, isStream, ctx.GetSourceCount(), textSource, audioSource);

            ctx.Contexts.push_back(mmc);

            auto fileName = ParseParameter<std::wstring>(params, FILENAME_PARAMETER, std::wstring());
            if (fileName.empty())
            {
                fileName = GenerateFileName(exportId, targetEp, bpt::to_iso_string(startTime), bpt::to_iso_string(endTime), isStream).wstring();
            }
            else
            {
                fileName = prepareExportPath(exportId, fileName).wstring();
            }

            ctx.FileName(CORBA::wstring_dup(fileName.c_str()));
            ctx.MaxFileSize(ParseParameter<std::uint64_t>(params, MAX_FILE_SIZE_PARAMETER, 0));
            ctx.VideoCompression(CheckCompressionLevel(ParseParameter<int>(params, VIDEO_COMPRESSION_PARAMETER, NMMSS::VCP_Normal)));
            ctx.AudioCompression(CheckCompressionLevel(ParseParameter<int>(params, AUDIO_COMPRESSION_PARAMETER, 0)));
            ctx.FrameFrequency(ConvertToFrequency(ParseParameter<std::uint32_t>(params, FRAME_FREQUENCY_PARAMETER, 0)));
            ctx.TimestampFormat(CORBA::wstring_dup(NLogging::s2ws(ParseParameter(params, TIMESTAMP_FORMAT_PARAMETER, std::string())).c_str()));
            ctx.TextColor(ParseColor(ParseParameter(params, COLOR_PARAMETER, std::string())));

            ParseSubtitle(ctx, params);

            if (IsPDF(ctx))
            {
                ctx.SnapshotPlace(ParseArea(params[SNAPSHOT_PLACE_PARAMETER]));
                ctx.CommentPlace(ParseArea(params[COMMENT_PLACE_PARAMETER]));
                ctx.TimestampPlace(ParseArea(params[TIMESTAMP_PLACE_PARAMETER]));
                ctx.PdfLayout(ConvertToPdfLayout(ParseParameter<std::uint32_t>(params, LAYOUT_PARAMETER, 0)));
            }

            std::call_once(m_stalledFileChecker,
                [&]()
            {
                _dbg_ << "Start export checking...";
                m_timer.expires_from_now(boost::posix_time::seconds(INITIAL_CHECK_TIMEOUT));
                m_timer.async_wait(std::bind(&CExportContentImpl::handle_timeout,
                    boost::weak_ptr<CExportContentImpl>(shared_from_base<CExportContentImpl>()),
                    std::placeholders::_1));
            });

            const auto clientLanguage = extractClientLanguage(req);
            if (!clientLanguage.empty())
            {
                _dbg_ << "Export: client language " << clientLanguage << " for file " << NCorbaHelpers::ToUtf8(ctx.FileName());
                ctx.ClientLanguage(clientLanguage);
            }

            NMMSS::NExport::PPlainSession exportSession(NMMSS::NExport::CreateSession());

            PExportSession es(new SExportSession(exportSession));
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_exports.insert(std::make_pair(exportId, es));
            }
            exportSession->Start(c->CreateContainer(), ctx, bpt::milliseconds(-1));

            NHttp::SHttpHeader contentDispositionHeader("Location", req->GetPrefix() + req->GetContextPath() + "/" + exportId);
            NHttp::SHttpHeader exposeHeadersHeader("Access-Control-Expose-Headers", "Location");

            resp->SetStatus(IResponse::Accepted);
            resp
                << exposeHeadersHeader
                << contentDispositionHeader
                << CacheControlNoCache();
            resp->FlushHeaders();
        }

        bool ParseStatusCommand(const PRequest req, PResponse resp)
        {
            std::string exportId;
            npu::PToken id = npu::Token(exportId);
            npu::PMask status = npu::Mask(STATUS);

            if (!Match(req->GetPathInfo(), id / status))
                return false;

            PExportSession webExportSession = GetSession(exportId);
            if (!webExportSession)
            {
                _err_ << "Export session " << exportId << " not found";
                Error(resp, IResponse::NotFound);
                return true;
            }

            NMMSS::NExport::PPlainSession exportSession = webExportSession->m_session;

            PExportContext ctx = std::make_shared<SExportContext>();
            ctx->id = exportId;
            ctx->state = exportSession->State();
            ctx->progress = exportSession->Progress();

            if (NMMSS::NExport::STOPPED == ctx->state)
            {
                MMSS::Export::YieldFileSeq_var res = exportSession->GetResult(0, false);
                for (CORBA::ULong i = 0; i < res->length(); ++i)
                {
                    boost::filesystem::path exportedFilePath(res[i].Path.in());
                    ctx->files.push_back(exportedFilePath.filename().wstring());
                }
                webExportSession->SetLastAccessTime();
            }

            ctx->err = exportSession->Error();

            WriteResponse(req, resp, ctx);
            return true;
        }

        void ParseStopCommand(PRequest req, PResponse resp)
        {
            std::string exportId;
            npu::PToken id = npu::Token(exportId);

            if (!Match(req->GetPathInfo(), id))
            {
                _err_ << "Incorrect stop command";
                Error(resp, IResponse::BadRequest);
                return;
            }

            PExportSession webExportSession = GetSession(exportId);
            if (!webExportSession)
            {
                _err_ << "Request session with id " << exportId << " not found";
                Error(resp, IResponse::NotFound);
                return;
            }

            webExportSession->m_session->Stop();

            RemoveExport(exportId);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_exports.erase(exportId);
            }

            NHttp::SHttpHeader allowOriginHeader("Access-Control-Allow-Origin", "*");

            resp->SetStatus(IResponse::NoContent);
            resp << allowOriginHeader;
            resp->FlushHeaders();
        }

        bool ParseFileCommand(PRequest req, PResponse resp)
        {
            npu::TParams params;
            if (!npu::ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return true;
            }

            std::string exportId;
            npu::PToken id = npu::Token(exportId);
            npu::PMask file = npu::Mask(FILE);

            if (!Match(req->GetPathInfo(), id / file))
                return false;

            PExportSession webExportSession = GetSession(exportId);
            if (!webExportSession)
            {
                _err_ << "Export session " << exportId << " not found";
                Error(resp, IResponse::NotFound);
                return true;
            }

            std::string presentationName(npu::GetParam(params, "name", std::string()));
            auto decodedName = m_exportContentPath / exportId;
            decodedName.append(presentationName, boost::filesystem::detail::utf8_codecvt_facet());

            _log_ << "Requested file is " << decodedName;

            boost::filesystem::path canPath = boost::filesystem::canonical(decodedName).make_preferred();
            if (!boost::filesystem::is_directory(canPath))
                canPath = canPath.parent_path();

            size_t pos = canPath.string().find(boost::filesystem::canonical(m_exportContentPath).make_preferred().string());
            
            if (0 != pos || (!(boost::filesystem::exists(decodedName) && boost::filesystem::is_regular_file(decodedName))))
            {
                _err_ << "Requested file not found";
                Error(resp, IResponse::NotFound);
                return true;
            }

            webExportSession->SetLastAccessTime();

            NContext::PSendContext ctx(NContext::CreateFileContext(GET_LOGGER_PTR, resp, presentationName.c_str(), decodedName,
                [&](const boost::system::error_code&) {}));
            ctx->ScheduleWrite();
            return true;
        }

        //// GEOMETRY PARSING ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        void ParsePoint(const Json::Value& data, std::vector<NMMSS::NExport::Point>& points)
        {
            if (POINT_COORD_COUNT != data.size())
                throw std::invalid_argument(str(boost::format("Point description %1% contains an invalid element count") % data.toStyledString()));

            NMMSS::NExport::Point p;
            p.x(data[0].asFloat());
            p.y(data[1].asFloat());
            points.push_back(p);
        }

        bool ParsePointCollection(const Json::Value& data, std::vector<NMMSS::NExport::Point>& points)
        {
            for (const Json::Value& p : data)
            {
                ParsePoint(p, points);
            }
            return true;
        }

        NMMSS::NExport::PolygonList ParseMask(const Json::Value& mask)
        {
            NMMSS::NExport::PolygonList polys;
            if (mask.isNull())
                return polys;

            NMMSS::NExport::PolygonList polygons;
            for (const Json::Value& p : mask)
            {
                std::vector<NMMSS::NExport::Point> points;
                ParsePointCollection(p, points);

                if (POLY_MIN_POINT_COUNT > points.size())
                    throw std::invalid_argument(str(boost::format("Polygon must contains as minimum %1% points") % POLY_MIN_POINT_COUNT));

                NMMSS::NExport::Polygon poly;
                boost::geometry::assign_points(poly, points);

                polygons.push_back(poly);
            }
            polys = polygons;
            return polys;
        }

        MMSS::Export::ViewArea ParseArea(const Json::Value& area)
        {
            MMSS::Export::ViewArea ret = { 0.0f, 0.0f, 0.0f, 0.0f };
            if (area.isNull())
                return ret;

            std::vector<NMMSS::NExport::Point> areaPoints;
            ParsePointCollection(area, areaPoints);

            if (areaPoints.size() != AREA_ELEMENT_COUNT)
                throw std::invalid_argument("Area has the wrong number of elements");

            FillArea(ret, std::move(areaPoints));
            return ret;
        }

        void FillArea(MMSS::Export::ViewArea& area, std::vector<NMMSS::NExport::Point> elems)
        {
            area.Left = static_cast<float>(elems[0].x());
            area.Top = static_cast<float>(elems[0].y());
            area.Right = static_cast<float>(elems[1].x());
            area.Bottom = static_cast<float>(elems[1].y());
        }

        /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

        MMSS::Export::ColorInfo ParseColor(std::string colorData)
        {
            MMSS::Export::ColorInfo colorInfo = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (colorData.empty())
                return colorInfo;

            size_t pos = colorData.find('#');
            if (0 != pos)
                throw std::invalid_argument(str(boost::format("Incorrectly formatted color value %1%. Supported format is #HHHHHH.") % colorData));
            colorData.replace(0, 1, "0x");
            std::uint32_t color = stoul(colorData, 0, 16);

            colorInfo.Red = ((color & 0x00FF0000) >> 16) / static_cast<float>(255);
            colorInfo.Green = ((color & 0x0000FF00) >> 8) / static_cast<float>(255);
            colorInfo.Blue = ((color & 0x000000FF)) / static_cast<float>(255);

            return colorInfo;
        }

        void ParseSubtitle(NMMSS::NExport::SExportSettings& ctx, const Json::Value& data)
        {
            std::string comment(ParseParameter(data, COMMENT_PARAMETER, std::string()));
            if (!comment.empty())
            {
                ctx.Comment(CORBA::wstring_dup(NCorbaHelpers::FromUtf8(comment).c_str()));
            }
        }

        int CheckCompressionLevel(int value)
        {
            if (0 > value || value > 6)
            {
                _wrn_ << "Wrong compression level " << value << ". Adjust to default (3).";
                return NMMSS::VCP_Normal;
            }
            return value;
        }

        MMSS::Export::EFrameFrequency ConvertToFrequency(std::uint32_t freq)
        {
            switch (freq)
            {
            case 0:
                return MMSS::Export::FF_ORIGINAL;
            case 1:
                return MMSS::Export::FF_HALF;
            case 2:
                return MMSS::Export::FF_QUARTER;
            case 3:
                return MMSS::Export::FF_OCTANT;
            default:
                throw std::invalid_argument(str(boost::format("Unsupported value %1% for parameter \'freq\'") % freq));
            }
        }

        MMSS::Export::EPdfLayout ConvertToPdfLayout(std::uint32_t layout)
        {
            switch (layout)
            {
            case 0:
                return MMSS::Export::PL_PORTRAIT;
            case 1:
                return MMSS::Export::PL_LANDSCAPE;
            default:
                throw std::invalid_argument(str(boost::format("Unsupported value %1% for parameter \'layout\'") % layout));
            }
        }

        MMSS::Export::EOutputFormat ConvertToOutputFormat(std::string format)
        {
            if (EXPORT_FORMAT_JPG == format)
                return MMSS::Export::OF_JPG;
            else if (EXPORT_FORMAT_PDF == format)
                return MMSS::Export::OF_PDF;
            else if (EXPORT_FORMAT_MKV == format)
                return MMSS::Export::OF_MKV;
            else if (EXPORT_FORMAT_AVI == format)
                return MMSS::Export::OF_AVI;
            else if (EXPORT_FORMAT_EXE == format)
                return MMSS::Export::OF_EXE;
            else if (EXPORT_FORMAT_MP4 == format)
                return MMSS::Export::OF_MP4;
            else
                throw std::invalid_argument(str(boost::format("Unsupported value %1% for parameter \'format\'") % format));
        }

        PExportSession GetSession(const std::string& id)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            TExports::iterator it = m_exports.find(id);
            if (m_exports.end() == it)
                return {};
            return it->second;
        }

        boost::filesystem::path GenerateFileName(const std::string& exportId, std::string& epName, std::string startTime, std::string endTime, bool isStreamMode)
        {
            _dbg_ << "Export start time: " << startTime;
            _dbg_ << "Export end time: " << endTime;

            std::size_t pos1 = epName.find_first_of('/') + 1;
            std::size_t pos2 = epName.find_first_of('/', pos1);
            std::string host(epName.substr(pos1, pos2 - pos1));
            std::size_t pos3 = epName.find_first_of('/', ++pos2);
            std::string device(epName.substr(pos2, pos3 - pos2));

            std::string timeSlice("[");
            timeSlice.append(startTime);
            if (isStreamMode)
            {
                timeSlice.append("-");
                timeSlice.append(endTime);
            }
            timeSlice.append("]");

            std::wstringstream name; 
            name << host.c_str() << "_" << device.c_str() << timeSlice.c_str();
            return prepareExportPath(exportId, name.str());
        }

        bool IsArchiveMode(npu::PMask mask)
        {
            return mask->GetExpression().find(ARCHIVE_MODE) != std::string::npos;
        }

        bool IsStreamMode(NMMSS::NExport::SExportSettings& ctx) const
        {
            return ctx.FileFormat() != MMSS::Export::OF_JPG && ctx.FileFormat() != MMSS::Export::OF_PDF;
        }

        bool IsPDF(NMMSS::NExport::SExportSettings& ctx) const
        {
            return ctx.FileFormat() == MMSS::Export::OF_PDF;
        }

        bool IsEXE(NMMSS::NExport::SExportSettings& ctx) const
        {
            return ctx.FileFormat() == MMSS::Export::OF_EXE;
        }

        void SetDefaultParameters(NMMSS::NExport::SExportSettings& ctx)
        {
            ctx.VideoFlags(0);
            ctx.AudioFlags(0);
            ctx.VideoCompression(NMMSS::VCP_Normal);
            ctx.AudioCompression(0);
            ctx.FrameFrequency(MMSS::Export::FF_ORIGINAL);
            ctx.BurnSubtitle(true);
            ctx.TimestampFormat(L"%Y-%b-%d %H:%M:%S");
            ctx.Font({ L"", L"", 0, 0, 0 });
            ctx.Wait(WEB_WAIT_TIMEOUT);
            ctx.PdfLayout(MMSS::Export::PL_PORTRAIT);
            ctx.MakeSignature(true);
        }

        void WriteResponse(const PRequest req, PResponse resp, PExportContext ctx)
        {
            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            auto ctxOut = std::make_shared<std::unordered_map<std::string, std::string>>();
            NWebBL::TEndpoints eps;
            for (const auto& file : ctx->files)
            {
                size_t pos;
                std::string endpoint = getEndpointFromExportFileName(file, pos);
                if (!endpoint.empty())
                    eps.emplace_back(endpoint);
            }

            NWebBL::FAction action = boost::bind(&CExportContentImpl::onCameraInfo, shared_from_base<CExportContentImpl>(),
                req, resp, metaCredentials, ctx, ctxOut, _1, _2, _3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, metaCredentials, eps, action);
        }

        void onCameraInfo(const PRequest req, PResponse resp, NGrpcHelpers::PCredentials metaCredentials, PExportContext ctx,
            std::shared_ptr<std::unordered_map<std::string, std::string > > ctxOut,
            const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& items, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                _err_ << "/export: GetCamerasByComponents method failed";
                return NPluginUtility::SendGRPCError(resp, grpcStatus);
            }

            getCameraGetDisplayName(items, ctxOut);

            if (valid == NWebGrpc::_FINISH)
            {
                std::stringstream ss;
                {
                    boost::archive::json_oarchive arch(ss);
                    uint16_t s = ctx->state;
                    auto convertedFiles = convertFiles(ctx->files, *ctxOut);
                    arch << boost::serialization::make_nvp("id", ctx->id);
                    arch << boost::serialization::make_nvp("state", s);
                    arch << boost::serialization::make_nvp("progress", ctx->progress);
                    arch << boost::serialization::make_nvp("error", ctx->err);
                    arch << boost::serialization::make_nvp("files", ctx->files);
                    arch << boost::serialization::make_nvp("filesFriendly", convertedFiles);
                }

                NPluginUtility::SendText(req, resp, ss.str());
            }
        }

        void FillSources(NCorbaHelpers::PContainer c, NMMSS::NExport::SContextSettings& mmc, const std::string& videoEp, const std::string& archiveEp, bool isStreamMode, std::uint16_t idCounter
            , std::string textSource, std::string audioSource)
        {

            if (!videoEp.empty())
            {
                NMMSS::NExport::MMSource vms(videoEp.c_str(), archiveEp.c_str());
                mmc.Sources.insert(std::make_pair(idCounter++, vms));
            }

            if (isStreamMode)
            {
                if (!textSource.empty())
                {
                    NMMSS::NExport::MMSource tms(textSource.c_str(), archiveEp.c_str());
                    mmc.Sources.insert(std::make_pair(idCounter++, tms));
                }

                if (!audioSource.empty())
                {
                    NMMSS::NExport::MMSource ams(audioSource.c_str(), archiveEp.c_str());
                    mmc.Sources.insert(std::make_pair(idCounter++, ams));
                }
            }
        }


        static std::string getHostname(std::string endpoint)
        {
            size_t pos = endpoint.find_first_of('/');
            return endpoint.substr(0, pos);
        }

        static std::wstring convertFile(const std::wstring &file, size_t pos, std::string endpoint, std::string displayName)
        {
            auto hostname = getHostname(endpoint);

            return NCorbaHelpers::FromUtf8(hostname) + L"_" + 
                NCorbaHelpers::FromUtf8(displayName)+file.substr(pos);
        }

        static std::vector<std::wstring> convertFiles(const std::vector<std::wstring>& files
                                                      , const std::unordered_map<std::string, std::string> &table)
        {
            std::vector<std::wstring> res;

            for (const auto & file : files)
            {
                size_t posBracket;
                auto s = getEndpointFromExportFileName(file, posBracket);

                if (!s.empty())
                {
                    auto it = table.find(s);

                    if (it != table.end())
                        res.push_back(convertFile(file, posBracket, it->first, it->second));
                    else
                        res.push_back(file);
                    
                }
            }

            return res;
        }

        boost::filesystem::path prepareExportPath(const std::string& exportId, const std::wstring& name)
        {
            auto fileName = m_exportContentPath / exportId;
            _log_ << "Generated path name: " << fileName;

            if (exists(fileName))
                throw std::invalid_argument(str(boost::format("Directory %1% already exists") % exportId));

            if (!boost::filesystem::create_directory(fileName))
                throw std::invalid_argument(str(boost::format("Can not create directory %1%") % fileName));

            fileName.append(name, boost::filesystem::detail::utf8_codecvt_facet());
            return fileName;
        }

        void getCameraGetDisplayName(const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, std::shared_ptr<std::unordered_map<std::string, std::string>> table)
        {
            if (cams.size() > 0)
            {
                const bl::domain::Camera& c = cams.Get(0);
                (*table)[hostnameAndEndpoint(c.access_point())]=c.display_id()+"."+c.display_name();
            }
        }

        static std::string getEndpointFromExportFileName(const std::wstring &filename, size_t &pos)
        {
            pos = filename.find(L"[");

            if (std::wstring::npos == pos || 0 == pos)
                return "";

            auto s = filename.substr(0, pos);
            auto pos2 = s.find_last_of('_');

            if (std::wstring::npos != pos2)
                s[pos2] = '/';

            return NCorbaHelpers::ToUtf8(s);
        }

        static std::string hostnameAndEndpoint(const std::string &epName)
        {
            size_t pos1 = epName.find_first_of('/');
            size_t pos2 = epName.find_first_of('/', ++pos1);
            pos2 = epName.find_first_of('/', ++pos2);

            return epName.substr(pos1, pos2 - pos1);
        }

        static void handle_timeout(boost::weak_ptr<CExportContentImpl> eci, const boost::system::error_code& error)
        {
            if (!error)
            {
                boost::shared_ptr<CExportContentImpl> ec = eci.lock();
                if (ec)
                {
                    TExports forDelete;
                    {
                        auto now = std::chrono::steady_clock::now();
                        std::lock_guard<std::mutex> lock(ec->m_mutex);
                        TExports::iterator it1 = ec->m_exports.begin(), it2 = ec->m_exports.end();
                        for (; it1 != it2; )
                        {
                            if ((now - it1->second->GetLastAccessTime()) >= WAIT_TIME)
                            {
                                ec->Log((boost::format("Export %1% selected for delete") % it1->first).str().c_str());
                                forDelete.insert(*it1);
                                it1 = ec->m_exports.erase(it1);
                            }
                            else
                                ++it1;
                        }
                    }

                    TExports::iterator it1 = forDelete.begin(), it2 = forDelete.end();
                    for (; it1 != it2; )
                    {
                        try
                        {
                            ec->RemoveExport(it1->first);
                            it1 = forDelete.erase(it1);
                        }
                        catch (const boost::filesystem::filesystem_error& e)
                        {
                            ++it1;
                            ec->Warn((boost::format("Export %1% delete error: %2%. Rescheduling...") % it1->first % e.what()).str().c_str());
                        }
                    }

                    if (!forDelete.empty())
                    {
                        std::lock_guard<std::mutex> lock(ec->m_mutex);
                        ec->m_exports.insert(forDelete.begin(), forDelete.end());
                    }

                    ec->m_timer.expires_from_now(boost::posix_time::seconds(CHECK_TIMEOUT));
                    ec->m_timer.async_wait(std::bind(&CExportContentImpl::handle_timeout,
                        eci, std::placeholders::_1));
                }
            }
        }     

        void RemoveExport(const std::string& exportId)
        {
            auto decodedName = m_exportContentPath / exportId;

            boost::filesystem::remove_all(decodedName);
        }

        void Log(const char* const msg)
        {
            _log_ << msg;
        }

        void Warn(const char* const msg)
        {
            _wrn_ << msg;
        }

        template <typename TRes, typename TParam>
        TRes ParseParameter(const Json::Value& data, TParam param, TRes defaultValue)
        {
            if (!data.isMember(param))
                return defaultValue;
            return getValue<TRes>(data[param]);
        }

        template <typename T>
        T getValue(const Json::Value& data);

        NCorbaHelpers::WPContainer m_container;
        const NWebGrpc::PGrpcManager m_grpcManager;
        boost::filesystem::path m_exportContentPath;

        std::mutex m_mutex;
        typedef std::map<std::string, PExportSession> TExports;
        TExports m_exports;

        std::once_flag m_stalledFileChecker;
        boost::asio::deadline_timer m_timer;

        const NPluginUtility::PRigthsChecker m_rightsChecker;
        NExecutors::PDynamicThreadPool m_pool;
    };
    
    template <typename T>
    T CExportContentImpl::getValue(const Json::Value& data)
    {
        throw std::invalid_argument("Unsupported type");
    }

    template <>
    std::string CExportContentImpl::getValue<std::string>(const Json::Value& data)
    {
        return data.asString();
    }

    template <>
    std::wstring CExportContentImpl::getValue<std::wstring>(const Json::Value& data)
    {
        const auto value = data.asString();
        return std::wstring(value.begin(), value.end());
    }

    template <>
    std::uint64_t CExportContentImpl::getValue<std::uint64_t>(const Json::Value& data)
    {
        return data.asUInt64();
    }

    template <>
    std::uint32_t CExportContentImpl::getValue<std::uint32_t>(const Json::Value& data)
    {
        return data.asUInt();
    }

    template <>
    int CExportContentImpl::getValue<int>(const Json::Value& data)
    {
        return data.asInt();
    }

}
namespace NHttp
{
    IServlet* CreateExportServlet(NCorbaHelpers::IContainer* c, const std::string& exportContentPath,
        const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rightsChecker)
    {
        return new CExportContentImpl(c, exportContentPath, grpcManager, rightsChecker);
    }
}
