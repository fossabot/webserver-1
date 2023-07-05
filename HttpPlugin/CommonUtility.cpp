#include "Constants.h"
#include "CommonUtility.h"
#include "SendContext.h"

#include <CorbaHelpers/Envar.h>
#include <CorbaHelpers/ResolveServant.h>

#include <Sample.h>

#include <SecurityManager/SecurityManager.h>

#include <zlib.h>
#include <boost/format.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <json/json.h>

using namespace NHttp;

namespace bl = axxonsoft::bl;

using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
    bl::domain::BatchGetCamerasResponse >;
using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

namespace
{
    NHttp::IResponse::EStatus grpcStatusToHTTPCode(grpc::Status status)
    {
        grpc::StatusCode sc = status.error_code();
        switch (sc)
        {
        case grpc::StatusCode::INVALID_ARGUMENT:
        case grpc::StatusCode::ALREADY_EXISTS:
        case grpc::StatusCode::OUT_OF_RANGE:
            return NHttp::IResponse::BadRequest;
        case grpc::StatusCode::NOT_FOUND:
            return NHttp::IResponse::NotFound;
        case grpc::StatusCode::PERMISSION_DENIED:
            return NHttp::IResponse::Forbidden;
        case grpc::StatusCode::UNAUTHENTICATED:
            return NHttp::IResponse::Unauthorized;
        case grpc::StatusCode::UNAVAILABLE:
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            return NHttp::IResponse::ServiceUnavailable;
        case grpc::StatusCode::UNIMPLEMENTED:
            return NHttp::IResponse::NotImplemented;
        default:
            return NHttp::IResponse::InternalServerError;
        }
    }
}

namespace NPluginUtility
{
    const char* const DEFAULT_MIME_TYPE = "json";
    const std::string GZIP_FORMAT{ "gzip" };

    const std::size_t GZIP_THRESHOLD = 8096;
    const std::uint16_t CHUNK = 16384;
    const std::uint8_t windowBits = 15;
    const std::uint8_t GZIP_ENCODING = 16;

    const int DEFAULT_RPC_TIMEOUT_MS = 5 * 1000;

    bool Compress(const std::string& data, std::string& compressedData)
    {
        unsigned char out[CHUNK];
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits | GZIP_ENCODING, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        {
            return false;
        }
        strm.next_in = (unsigned char*)data.c_str();
        strm.avail_in = data.size();
        do {
            int have;
            strm.avail_out = CHUNK;
            strm.next_out = out;
            if (deflate(&strm, Z_FINISH) == Z_STREAM_ERROR)
            {
                return false;
            }
            have = CHUNK - strm.avail_out;
            compressedData.append((char*)out, have);
        } while (strm.avail_out == 0);
        if (deflateEnd(&strm) != Z_OK)
        {
            return false;
        }
        return true;
    }

    bool supportCompression(const PRequest req)
    {
        boost::optional<const std::string&> h = req->GetHeader("Accept-Encoding");
        if (!h)
            return false;

        std::stringstream ss(*h);
        std::string item;

        while (getline(ss, item, ',')) {
            if (item == GZIP_FORMAT)
                return true;
        }
        return false;
    }

    void SendText(PResponse& response, const std::string& text,
        bool resetResponse, bool headersOnly)
    {
        SendText(response, IResponse::OK, text, resetResponse, headersOnly);
    }

    void SendText(PResponse& response, NHttp::IResponse::EStatus statusCode, const std::string& text,
        bool resetResponse, bool headersOnly)
    {
        response->SetStatus(statusCode);
        std::string contentType(NHttp::GetMIMETypeByExt("json"));
        contentType.append("; charset=utf-8");
        response << ContentLength(text.size())
                 << ContentType(contentType)
                 << CacheControlNoCache();

        response->FlushHeaders();

        if (headersOnly)
            return;

        NContext::PSendContext ctx(NContext::CreateStringContext(response, text));
        ctx->ScheduleWrite();

        if (resetResponse)
            response.reset();
    }

    void SendText(const NHttp::PRequest req, NHttp::PResponse response, const std::string& text, NHttp::IResponse::EStatus statusCode, int maxAge)
    {
        response->SetStatus(statusCode);

        const std::size_t textSize = text.size();
        std::string contentType(NHttp::GetMIMETypeByExt("json"));
        contentType.append("; charset=utf-8");
        response << ContentType(contentType);

        if (-1 == maxAge)
            response << CacheControlNoCache();
        else
        {
            NHttp::SHttpHeader cacheControl("Cache-Control", (boost::format("max-age=%1%") %maxAge).str().c_str() );
            response << cacheControl;
        }

        bool isGzipped = false;
        std::string gzipStream;
        if ((textSize > GZIP_THRESHOLD) && supportCompression(req) && Compress(text, gzipStream))
        {
            NHttp::SHttpHeader contentEncodingHeader("Content-Encoding", "gzip");
            response << contentEncodingHeader;
            isGzipped = true;
        }
        response << ContentLength(isGzipped ? gzipStream.size() : textSize);

        response->FlushHeaders();

        NContext::PSendContext ctx(NContext::CreateStringContext(response, isGzipped ? gzipStream : text));
        ctx->ScheduleWrite();
    }

    void ParseHostname(const std::string& epName, std::string& hostName)
    {
        size_t pos1 = epName.find_first_of('/');
        size_t pos2 = epName.find_first_of('/', ++pos1);
        hostName.assign(epName.substr(pos1, pos2 - pos1));
    }

    std::string GetAudioEpFromVideoEp(const std::string &videoEp)
    {
        static const char* const AUDIO_SUFFIX = "audio:0";

        size_t pos = videoEp.find_last_of(".");
        if (std::string::npos != pos)
            return videoEp.substr(0, pos + 1).append(AUDIO_SUFFIX);

        return "";
    }

    std::string convertToMainStream(const std::string& cam)
    {
        return cam.substr(0, cam.size() - 1) + "0";
    }

    ORM::AsipDatabase_ptr GetDBReference(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainer* cont, const std::string &_hostName)
    {
        static const char ASIP_DB_ENDPOINT[] = "/EventDatabase.0/AsipDatabase";

        std::string hostName = _hostName.empty() ? NCorbaHelpers::CEnvar::NgpNodeName() : _hostName;

        if (!boost::starts_with(hostName, HOST_PREFIX))
            hostName = HOST_PREFIX + hostName;

        ORM::AsipDatabase_var db =  NCorbaHelpers::ResolveServant<ORM::AsipDatabase>(cont, hostName + ASIP_DB_ENDPOINT);

        return db._retn();
    }

    NGrpcHelpers::PCredentials  GetCommonCredentials(DECLARE_LOGGER_ARG, const NHttp::IRequest::AuthSession& as)
    {
        if (TOKEN_AUTH_SESSION_ID == as.id)
            return  NGrpcHelpers::NGPAuthTokenCallCredentials(NSecurityManager::CreateSystemSession(GET_LOGGER_PTR, __FUNCTION__));
            
        return NGrpcHelpers::NGPAuthTokenCallCredentials(as.data.first ? *(as.data.first) : "");
    }

    bool IsKeyFrame(NMMSS::ISample* s)
    {
        return !(s->Header().eFlags & (NMMSS::SMediaSampleHeader::EFNeedKeyFrame
            | NMMSS::SMediaSampleHeader::EFNeedPreviousFrame
            | NMMSS::SMediaSampleHeader::EFNeedInitData));
    }

    MMSS::StorageEndpoint_var ResolveEndoint(NCorbaHelpers::IContainer* cont, const bl::media::EndpointRef& in)
    {
        MMSS::StorageEndpoint_var endpoint;
        if (in.object_ref().empty())
        {
            endpoint = NCorbaHelpers::ResolveServant<MMSS::StorageEndpoint>(cont, in.access_point(), DEFAULT_RPC_TIMEOUT_MS);
        }
        else
        {
            try
            {
                CORBA::Object_var obj = cont->GetORB()->string_to_object(in.object_ref().c_str());
                obj = NCorbaHelpers::SetTimeoutPolicyStubwise(obj, DEFAULT_RPC_TIMEOUT_MS);
                endpoint = MMSS::StorageEndpoint::_unchecked_narrow(obj);
            }
            catch (const CORBA::Exception&){}
        }
        return endpoint;
    }

    MMSS::StorageEndpoint_var ResolveEndoint(NWebGrpc::PGrpcManager grpcManager, NGrpcHelpers::PCredentials credentials, NCorbaHelpers::IContainer* cont,
        const std::string& accessPoint, const std::string& startTime, axxonsoft::bl::archive::EStartPosition pos, const long playFlags)
    {
        MMSS::StorageEndpoint_var endpoint;

        auto channel = grpcManager->GetChannel();
        auto stub = channel->NewStub<bl::archive::ArchiveService>();
        grpc::ClientContext context;
        context.set_credentials(credentials);

        bl::archive::CreateReaderEndpointRequest creq;
        creq.set_access_point(accessPoint);
        creq.set_begin_time(startTime.empty() ? boost::posix_time::to_iso_string(boost::posix_time::ptime(boost::posix_time::min_date_time)) :  startTime);
        creq.set_start_pos_flag(pos);
        creq.set_mode(playFlags);
        creq.set_is_realtime(false);
        creq.set_priority(bl::archive::ERP_Mid);

        bl::archive::CreateReaderEndpointResponse res;
        auto resp = stub->CreateReaderEndpoint(&context, creq, &res);
       
        if (resp.ok())
        {
            endpoint = ResolveEndoint(cont, res.endpoint());
        }

        return endpoint;
    }

    MMSS::EStartPosition ToCORBAPosition(axxonsoft::bl::archive::EStartPosition pos)
    {
        switch (pos)
        {
        case axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME:
            return MMSS::spAtKeyFrame;
        case axxonsoft::bl::archive::START_POSITION_EXACTLY:
            return MMSS::spExactly;
        case axxonsoft::bl::archive::START_POSITION_ONE_FRAME_BACK:
            return MMSS::spOneFrameBack;
        case axxonsoft::bl::archive::START_POSITION_NEAREST_KEY_FRAME:
            return MMSS::spNearestKeyFrame;
        case axxonsoft::bl::archive::START_POSITION_AT_KEY_FRAME_OR_AT_EOS:
            return MMSS::spAtKeyFrameOrAtEos;
        default:
            return MMSS::spAtKeyFrame;
        }
    }

    void GetEndpointStorageSource(DECLARE_LOGGER_ARG, const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams,
        PEndpointQueryContext ctx, bool includeEmbedded, bool activatedOnly)
    {
        std::string archiveName(std::move(ctx->archiveName));

        bool hasArchiveName = !archiveName.empty();
        if (hasArchiveName && !boost::starts_with(archiveName, HOST_PREFIX))
            archiveName = HOST_PREFIX + archiveName;

        int itemCount = cams.size();
        for (int i = 0; i < itemCount; ++i)
        {
            const bl::domain::Camera& c = cams.Get(i);

            int arcCount = c.archive_bindings_size();
            for (int j = 0; j < arcCount; ++j)
            {
                const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);
                
                if ((!includeEmbedded && ab.archive().is_embedded()) || (activatedOnly && !ab.archive().is_activated()))
                {
                    continue;
                }

                auto it = std::find_if(ab.sources().begin(), ab.sources().end(),
                    [&](const bl::domain::StorageSource& c)
                    {
                        return (c.media_source() == ctx->endpoint) && (archiveName.empty() || (c.storage() == archiveName));
                    });

                if (it != ab.sources().end())
                {
                    ctx->requestedItem = it->access_point();
                    ctx->archiveName = ab.storage();

                    if (hasArchiveName || ab.is_default())
                        return;
                }
            }
        }
    }

    void GetCameraStorageSources(DECLARE_LOGGER_ARG, const::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, PCameraQueryContext ctx, bool includeEmbedded)
    {
        if (ctx->videoCtx)
            GetEndpointStorageSource(GET_LOGGER_PTR, cams, ctx->videoCtx, includeEmbedded);

        if (ctx->audioCtx)
            GetEndpointStorageSource(GET_LOGGER_PTR, cams, ctx->audioCtx, includeEmbedded);
    }

    void SendGRPCError(NHttp::PResponse resp, grpc::Status status)
    {
        Json::Value value(Json::objectValue);
        value["grpcErrorCode"] = status.error_code();
        value["errorMessage"] = status.error_message();

        auto jsonString = std::make_shared<std::string>();
        *jsonString = value.toStyledString();
        jsonString->reserve(jsonString->size() + 2);
        jsonString->append(1, '\r').append(1, '\n');

        resp->SetStatus(grpcStatusToHTTPCode(status));
        resp << NHttp::ContentType(std::string(NHttp::GetMIMETypeByExt("json")) + "; charset=utf-8")
             << NHttp::ContentLength(jsonString->size() - 2);
        resp->FlushHeaders();

        try
        {
            resp->AsyncWrite(jsonString->data(), jsonString->size(), [jsonString](boost::system::error_code) {});
        }
        catch(...){}
    }

    std::string parseForCC(std::uint32_t fourCC)
    {
        std::string streamType;
        std::uint8_t* p = reinterpret_cast<std::uint8_t*>(&fourCC);
        for (std::uint8_t i = 0; i < sizeof(std::uint32_t); ++i)
        {
            streamType.push_back(static_cast<char>(p[i]));
        }
        return streamType;
    }
}
