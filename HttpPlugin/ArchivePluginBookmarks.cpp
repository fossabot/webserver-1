#include <HttpServer/BasicServletImpl.h>
#include <MMClient/MMClient.h>
#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/ResolveServant.h>

#include "GrpcReader.h"
#include "HttpPlugin.h"
#include "SendContext.h"
#include "DataSink.h"
#include "CommonUtility.h"
#include "RegexUtility.h"
#include "Constants.h"
#include "Hls.h"
#include "UrlBuilder.h"

#include "../MMCoding/Initialization.h"

#include <NativeBLClient/NativeBLClient.h>
#include <NativeBLClient/Helpers.h>
#include <axxonsoft/bl/archive/ArchiveSupport.grpc.pb.h>
#include <axxonsoft/bl/events/EventHistory.grpc.pb.h>

#include "ArchivePlugin.h"

#include <ORM_IDL/ORM.h>

namespace bpt = boost::posix_time;
namespace npu = NPluginUtility;
namespace bl = axxonsoft::bl;

namespace ArchivePlugin
{    
    const char* const GROUP = "group";
    const char* const EVENT_CHANNEL = "/EventChannel.1/EventConsumer";
    const char* const VIDEO_SOURCE = "video";

    const int BOOKMARKS_LIMIT = 100;

    using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
        bl::domain::BatchGetCamerasResponse >;

    using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

    template <typename TResponse>
    using CameraListCallback_t = std::function<void(const TResponse&, NWebGrpc::STREAM_ANSWER)>; 

    using BookmarkReader_t = NWebGrpc::AsyncResultReader<bl::archive::ArchiveService, ::axxonsoft::bl::archive::ChangeBookmarksRequest, ::axxonsoft::bl::archive::ChangeBookmarksResponse >;
    using PBookmarkReader_t = std::shared_ptr < BookmarkReader_t >;

    using BookmarkQuery_t = NWebGrpc::AsyncStreamReader<bl::events::EventHistoryService,
        ::axxonsoft::bl::events::ReadBookmarksRequest, ::axxonsoft::bl::events::ReadBookmarksResponse >;
    using PBookmarkQuery_t = std::shared_ptr < BookmarkQuery_t >;
    
    //////////////////////////////////////

    CManagerResourceHandle::CManagerResourceHandle(boost::function<void(void)> method) :
        m_method(method)
    {

    }

    CManagerResourceHandle::~CManagerResourceHandle()
    {
        m_method();
    }
    //////////////////////////////////
    void CArchiveContentImpl::CamContextOut::AddCam(const std::string &endpoint, const std::string &friendly_name)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_mapCams[endpoint] = friendly_name;
    }

    void CArchiveContentImpl::CamContextOut::AddArc(const std::string &arc, const std::string &friendly_name)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_mapArcs[arc] = friendly_name;
    }

    Json::Value CArchiveContentImpl::CamContextOut::GetJson() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        Json::Value res;

        res["cameras"] = Json::Value(Json::arrayValue);

        for (const auto &t : m_mapCams)
        {
            Json::Value camJson(Json::objectValue);
            camJson["endpoint"] = t.first;
            camJson["friendly_name"] = t.second;
            res["cameras"].append(camJson);
        }

        res["archives"] = Json::Value(Json::arrayValue);

        for (const auto &t : m_mapArcs)
        {
            Json::Value arcJson(Json::objectValue);
            arcJson["storage"] = t.first;
            arcJson["friendly_name"] = t.second;
            res["archives"].append(arcJson);
        }

        return res;
    }
    std::string CArchiveContentImpl::CamContextOut::GetJsonStr() const
    {
        return Json::FastWriter().write(GetJson()).c_str();
    }

    std::string CArchiveContentImpl::CamContextOut::GetCamFriendlyName(const std::string &endpoint)
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        return m_mapCams[endpoint];
    }

    std::string CArchiveContentImpl::CamContextOut::GetArchiveFriendlyName(const std::string &archive)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_mapArcs[archive];
    }
    //////////////////////////////////
    CArchiveContentImpl::CamContextIn::CamContextIn(const Json::Value &data, const std::string &user) :
        m_data(data)
        , m_user(user)
    {
            for (unsigned i = 0; i < data.size(); ++i)
            {
                m_cameras.insert(data[i]["endpoint"].asString());
                m_archives.insert(data[i]["storage_id"].asString());
            }
    }
    //////////////////////////////////
    CArchiveContentImpl::ConnectorsHandler::ConnectorsHandler(NCorbaHelpers::IContainerNamed* c) :
        m_container(c)
    {
    }

    NCommonNotification::IEventSupplierSink *CArchiveContentImpl::ConnectorsHandler::Get(const std::string &hostname)
    {
        if (hostname.empty())
            return nullptr;

        auto p = get(hostname);

        if (nullptr == p)
            return insert(hostname);

        return p;
    }

    NCommonNotification::IEventSupplierSink *CArchiveContentImpl::ConnectorsHandler::get(const std::string &hostname) const
    {
        boost::shared_lock<boost::shared_mutex> lock(m_mutex);

        SupplierMap::const_iterator it = m_SupplierTable.find(hostname);
        if (it == m_SupplierTable.end())
            return nullptr;

        return it->second.Get();
    }

    NCommonNotification::IEventSupplierSink * CArchiveContentImpl::ConnectorsHandler::insert(const std::string &hostname)
    {
        std::lock_guard<boost::shared_mutex> lock(m_mutex);

        NCorbaHelpers::PContainerNamed cont = m_container;
        if (!cont)
        {
            return nullptr;
        }

        SupplierMap::iterator it = m_SupplierTable.find(hostname);

        if (it != m_SupplierTable.end())
            return it->second.Get();

        std::string s = "hosts/" + hostname + EVENT_CHANNEL;

        try
        {
            NCorbaHelpers::CAutoPtr<NCommonNotification::CEventSupplier> eventSupplier(
                NCommonNotification::CreateEventSupplierServant(cont.Get(), s.c_str(), NCommonNotification::DontRefreshCachedEvents));

            m_ResList.push_back(std::unique_ptr<NCorbaHelpers::IResource>(NCorbaHelpers::ActivateServant(cont.Get(), eventSupplier.Dup(), s.c_str())));

            it = m_SupplierTable.insert(std::make_pair(hostname, eventSupplier)).first;
            return it->second.Get();
        }
        catch (const CORBA::Exception &)
        {
        }

        return nullptr;
    }
    //////////////////////////////////////////////
    void CArchiveContentImpl::Post(const PRequest req, PResponse resp)
    {
        using namespace npu;

        const std::string path = req->GetPathInfo();
        PMask contents = Mask(CONTENTS);

        if (Match(path, contents / Mask(BOOKMARKS)))
            return procEditBookmark(req, resp);

        if (Match(path, contents / Mask(BOOKMARKS) / Mask("create")))
            return procCreateBookmarks(req, resp);

        return Error(resp, IResponse::NotFound);
    }

    void CArchiveContentImpl::Delete(const PRequest req, PResponse resp) 
    {
        NPluginUtility::IRigthsChecker::TPermissions perms = { axxonsoft::bl::security::FEATURE_ACCESS_ALLOW_DELETE_RECORDS };
        m_rightsChecker->HasGlobalPermissions(perms, req->GetAuthSession(),
            [this, req, resp](bool valid)
        {
            if (!valid)
            {
                Error(resp, IResponse::Forbidden);
                return;
            }

            npu::TParams params;
            if (!npu::ParseParams(req->GetQuery(), params))
            {
                Error(resp, IResponse::BadRequest);
                return;
            }

            std::string sCam;
            std::string sArc;
            std::string sBeg;
            std::string sEnd;

            npu::GetParam<std::string>(params, "storage_id", sArc, std::string());
            npu::GetParam<std::string>(params, "endpoint", sCam, std::string());
            npu::GetParam<std::string>(params, "begins_at", sBeg, std::string());
            npu::GetParam<std::string>(params, "ends_at", sEnd, std::string());

            auto p = NCorbaHelpers::ResolveServant<MMSS::StorageSource>(m_container.Get(), NNativeBL::Client::GetStorageSourceForWeb(GET_LOGGER_PTR, m_nativeBLClient, sCam, "video", sArc), 10000);
            if (!p)
                return Error(resp, IResponse::NotFound);

            if (sArc.empty() || sCam.empty() || sBeg.empty() || sEnd.empty())
                return Error(resp, IResponse::BadRequest);

            p->ClearInterval(sBeg.c_str(), sEnd.c_str());

            Error(resp, IResponse::OK);
        });
    }

    void CArchiveContentImpl::setEventFromJson(ORM::JsonEvent &ev, Json::Value data, const std::string &user)
    {
        ev.Type = ORM::JE_Comment;
        std::string sCam = data["endpoint"].asString();
        std::string id = data["id"].asString();
        Notification::StringToGuid(id, ev.Id);

        ev.Timestamp.value = CORBA::string_dup(boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time()).c_str());
        ev.ObjectId = CORBA::string_dup(sCam.c_str());

        data["user_id"] = user;

        data.removeMember("endpoint");
        data.removeMember("id");
        data.removeMember("timestamp");
        data.removeMember("hostname");

        ev.AnyValues = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(Json::FastWriter().write(data)).c_str());
    }

    std::string CArchiveContentImpl::lexemsGener(const std::vector<std::string> &vStrings)
    {
        std::string res;

        for (const auto &t : vStrings)
        {
            auto t3 = NCorbaHelpers::FromUtf8(t);
            t3 = boost::to_lower_copy(t3);

            for (size_t i = 1; i < t3.size(); ++i)
                res += " " + NCorbaHelpers::ToUtf8(t3.substr(i));
        }

        return res;
    }

    void CArchiveContentImpl::setAuditEventFromJson(ORM::JsonEvent &ev, const Json::Value &dataIn, std::string user, PCamContextOut ctxOut)
    {
        ev.Type = ORM::JE_Audit;
        ev.Id = Notification::GenerateUUID();
        ev.Timestamp.value = CORBA::string_dup(boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time()).c_str());
        ev.ObjectId = (std::string("hosts/") + dataIn["hostname"].asString() + "/Audit.0").c_str();

        Json::Value data;

        data["host"] = dataIn["hostname"];
        data["type"] = Audit::AE_ARCHIVE_COMMENT_EDIT;
        data["user"] = user;
        data["device"] = ctxOut->GetCamFriendlyName(dataIn["endpoint"].asString());
        data["archive"] = ctxOut->GetArchiveFriendlyName(dataIn["storage_id"].asString());
        data["lexems"] = lexemsGener({ data["user"].asString(), data["device"].asString(), data["archive"].asString() });

        ev.AnyValues = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(Json::FastWriter().write(data)).c_str());
    }

    void CArchiveContentImpl::processCameras(PCamContextIn ctxIn, PCamContextOut ctxOut, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
    {
        int itemCount = cams.size();
        for (int i = 0; i < itemCount; ++i)
        {
            std::string access_point;

            const bl::domain::Camera& c = cams.Get(i);

            int vsCount = c.video_streams_size();

            if (vsCount > 0)
                access_point = c.video_streams(0).stream_acess_point();

            ctxOut->AddCam(access_point, c.display_name());


            int arcCount = c.archive_bindings_size();
            for (int j = 0; j < arcCount; ++j)
            {
                const bl::domain::ArchiveBinding& ab = c.archive_bindings(j);

                std::find_if(ab.sources().begin(), ab.sources().end(),
                    [&](const bl::domain::StorageSource& c)
                {
                    return boost::contains(c.media_source(), VIDEO_SOURCE);
                });

                std::string archive = ab.storage();

                if (ctxIn->m_archives.find(archive) == ctxIn->m_archives.end())
                    continue;

                std::string display_name = (ab.has_archive() ?
                    ab.archive().display_name() : std::string());

                ctxOut->AddArc(archive, display_name);
            }
        }
    }

    ArchivePlugin::CArchiveContentImpl::MapTable CArchiveContentImpl::getTableJson(const Json::Value &data)
    {
        MapTable res;
        for (unsigned i = 0; i < data.size(); ++i)
        {
            std::string key = data[i]["hostname"].asString();
            MapTable::iterator it = res.find(key);

            if (it == res.end())
                it = res.insert(std::make_pair(key, std::make_unique<std::vector<Json::Value>>())).first;

            it->second->push_back(data[i]);
        }

        return res;
    }

    void CArchiveContentImpl::procEditBookmark(const PRequest req, PResponse resp)
    {
        m_rightsChecker->IsCommentAllowed(req->GetAuthSession(),
            [this, req, resp](int valid) mutable
        {
            if (valid < axxonsoft::bl::security::BOOKMARK_ACCESS_CREATE_PROTECT_EDIT_DELETE)
            {
                Error(resp, IResponse::Forbidden);
                return;
            }

            //TODO: refactor. Join with createBookmarks
            try
            {
                std::vector<uint8_t> body(req->GetBody());
                std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

                Json::Value data;
                Json::CharReaderBuilder jReader;
                std::string err;
                std::istringstream is(bodyContent);
                if (!bodyContent.empty() && !Json::parseFromStream(jReader, is, &data, &err))
                {
                    _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                if (!data.isArray())
                    return Error(resp, IResponse::BadRequest);

                if (data.size() <= 0)
                    return npu::SendText(resp, std::string(""), true);

                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                PBookmarkReader_t reader(new BookmarkReader_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::archive::ArchiveService::Stub::AsyncChangeBookmarks));

                ::axxonsoft::bl::archive::ChangeBookmarksRequest reqGrpc;

                for (unsigned i = 0; i < data.size(); ++i)
                {
                    ::axxonsoft::bl::archive::ChangeBookmarksRequest_BookmarkToChange *b = reqGrpc.add_changed();

                    ::axxonsoft::bl::archive::ChangeBookmarksRequest_BookmarkInternal *p
                        = new  ::axxonsoft::bl::archive::ChangeBookmarksRequest_BookmarkInternal();

                    p->set_message(data[i]["comment"].asString());

                    auto endpoint = data[i]["endpoint"].asString();
                    p->set_archive_ap(data[i]["storage_id"].asString());
                    p->set_camera_ap(endpoint);

                    p->set_group_id(data[i]["group_id"].asString());

                    std::string hostName;
                    NPluginUtility::ParseHostname(endpoint, hostName);
                    p->set_node_name(hostName);

                    p->set_is_protected(data[i]["is_protected"].asBool());

                    ::axxonsoft::bl::primitive::TimeRange *time = new ::axxonsoft::bl::primitive::TimeRange();

                    time->set_begin_time(data[i]["begins_at"].asString());
                    time->set_end_time(data[i]["ends_at"].asString());

                    p->set_allocated_range(time);
                    b->set_allocated_bookmark_internal(p);

                    b->set_book_mark_guid(data[i]["id"].asString());
                }

                reader->asyncRequest(reqGrpc,
                    [resp](const ::axxonsoft::bl::archive::ChangeBookmarksResponse &r, grpc::Status status) mutable
                {
                    if (status.ok())
                        npu::SendText(resp, std::string(""), true);
                    else
                        return NPluginUtility::SendGRPCError(resp, status);
                }
                );


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

    void CArchiveContentImpl::procCreateBookmarks(const PRequest req, PResponse resp)
    {
        m_rightsChecker->IsCommentAllowed(req->GetAuthSession(),
            [this, req, resp](int valid) mutable
        {
            if (valid < axxonsoft::bl::security::BOOKMARK_ACCESS_CREATE_PROTECT_EDIT_DELETE)
            {
                Error(resp, IResponse::Forbidden);
                return;
            }

            try
            {
                std::vector<uint8_t> body(req->GetBody());
                std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

                Json::Value data;
                Json::CharReaderBuilder jReader;
                std::string err;
                std::istringstream is(bodyContent);
                if (!bodyContent.empty() && !Json::parseFromStream(jReader, is, &data, &err))
                {
                    _err_ << "Error occured ( " << err << " ) during parsing body content: " << bodyContent;
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                if (!data.isArray())
                    return Error(resp, IResponse::BadRequest);

                if (data.size() <= 0)
                    return npu::SendText(resp, std::string(""), true);

                const IRequest::AuthSession& as = req->GetAuthSession();
                NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

                PBookmarkReader_t reader(new BookmarkReader_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::archive::ArchiveService::Stub::AsyncChangeBookmarks));

                auto group_id = Notification::GuidToString(Notification::GenerateUUID());
                auto text = data[0]["comment"].asString();
                auto begins_at = data[0]["begins_at"].asString();
                auto ends_at = data[0]["ends_at"].asString();
                bool isProtected = data[0]["is_protected"].asBool();

                ::axxonsoft::bl::archive::ChangeBookmarksRequest reqGrpc;

                for (unsigned i = 0; i < data.size(); ++i)
                {
                    ::axxonsoft::bl::archive::ChangeBookmarksRequest_BookmarkToAdd *b = reqGrpc.add_added();

                    ::axxonsoft::bl::archive::ChangeBookmarksRequest_BookmarkInternal *p
                        = new  ::axxonsoft::bl::archive::ChangeBookmarksRequest_BookmarkInternal();

                    p->set_message(text);

                    auto endpoint = data[i]["endpoint"].asString();
                    p->set_archive_ap(data[i]["storage_id"].asString());
                    p->set_camera_ap(endpoint);

                    p->set_group_id(group_id);

                    std::string hostName;
                    NPluginUtility::ParseHostname(endpoint, hostName);
                    p->set_node_name(hostName);

                    p->set_is_protected(isProtected);

                    ::axxonsoft::bl::primitive::TimeRange *time = new ::axxonsoft::bl::primitive::TimeRange();

                    time->set_begin_time(begins_at);
                    time->set_end_time(ends_at);

                    p->set_allocated_range(time);
                    b->set_allocated_bookmark_internal(p);
                }

                reader->asyncRequest(reqGrpc,
                    [resp](const ::axxonsoft::bl::archive::ChangeBookmarksResponse &r, grpc::Status status) mutable
                {
                    if (status.ok())
                        npu::SendText(resp, std::string(""), true);
                    else
                        return NPluginUtility::SendGRPCError(resp, status);
                }
                );


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

    void CArchiveContentImpl::ProcessBookmarksRequest(const PRequest req, PArchiveRequestContext arc)
    {
        m_rightsChecker->IsCommentAllowed(req->GetAuthSession(),
            [this, req, arc](int valid)
        {
            using namespace npu;
            std::string response;

            CManagerResourceHandle manager(boost::bind(&CArchiveContentImpl::RequestDone, this, arc));

            if (valid <= axxonsoft::bl::security::BOOKMARK_ACCESS_NO)
            {
                Error(arc->m_response, IResponse::Forbidden);
                return;
            }

            try
            {
                std::uint32_t offset = 0;
                GetParam<std::uint32_t>(arc->params, OFFSET_MASK, offset, 0);

                std::uint32_t limit = BOOKMARKS_LIMIT;
                GetParam<std::uint32_t>(arc->params, LIMIT_MASK, limit, BOOKMARKS_LIMIT);

                bl::events::ReadBookmarksRequest creq;

                if (arc->m_posixStartTime > arc->m_posixEndTime)
                    std::swap(arc->m_posixStartTime, arc->m_posixEndTime);

                creq.mutable_range()->set_begin_time(boost::posix_time::to_iso_string(arc->m_posixStartTime));
                creq.mutable_range()->set_end_time(boost::posix_time::to_iso_string(arc->m_posixEndTime));

                google::protobuf::RepeatedPtrField<bl::events::NodeDescription>* desc = creq.mutable_node_descriptions();
                bl::events::NodeDescription* nd = desc->Add();
                nd->set_node_name(arc->m_hostname);

                creq.set_limit(limit);
                creq.set_offset(offset);
                creq.set_descending(false);

                PBookmarkQuery_t reader(new BookmarkQuery_t
                    (GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, &bl::events::EventHistoryService::Stub::AsyncReadBookmarks));

                std::shared_ptr<Json::Value> ctx = std::make_shared<Json::Value>(Json::arrayValue);            
                auto sharedObj = shared_from_base<CArchiveContentImpl>();
                reader->asyncRequest(creq, [this, sharedObj, arc, ctx, limit](const bl::events::ReadBookmarksResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus)
                {
                    if (!grpcStatus.ok())
                    {
                        return NPluginUtility::SendGRPCError(arc->m_response, grpcStatus);
                    }
                    
                    int eventCount = res.items_size();
                    for (int i = 0; i < eventCount; ++i)
                    {
                        const bl::events::Event& bm = res.items(i);
                        bl::events::Bookmark ev;
                        if (bm.body().UnpackTo(&ev))
                        {
                            Json::Value event{ Json::objectValue };

                            event["endpoint"] = ev.camera().access_point();
                            event["timestamp"] = ev.timestamp();
                            event["id"] = ev.guid();

                            event["storage_id"] = ev.archive().access_point();
                            if (!ev.alert_id().empty())
                                event["alert_id"] = ev.alert_id();
                            if (!ev.group_id().empty())
                                event["group_id"] = ev.group_id();
                            event["is_protected"] = ev.is_protected();
                            if (!ev.user().empty())
                                event["user_id"] = ev.user();
                            event["begins_at"] = ev.range().begin_time();
                            event["ends_at"] = ev.range().end_time();
                            event["comment"] = ev.message();

                            event["primitives"] = { Json::objectValue }; // TOOD: feel free to change the name before release AN 4.5

                            google::protobuf::util::JsonOptions jOpt;
                            jOpt.always_print_enums_as_ints = false;
                            jOpt.always_print_primitive_fields = true;
                            jOpt.preserve_proto_field_names = true;
                            std::string json;
                            if (google::protobuf::util::MessageToJsonString(ev.boundary(), &json, jOpt).ok())
                            {
                                event["primitives"]["boundary"] = json;
                            }
                            if (google::protobuf::util::MessageToJsonString(ev.geometry(), &json, jOpt).ok())
                            {
                                event["primitives"]["geometry"] = json;
                            }

                            // TODO: better way is 'recompile' this parameters again from primitive values
                            event["boundary"] = ev.boundary_deprecated();
                            event["geometry"] = ev.geometry_deprecated();
                            //////////////////////////////////////////////////////////////////////////////

                            ctx->append(event);
                        }
                    }

                    if (status == NWebGrpc::_FINISH)
                    {
                        Json::Value responseObject;
                        responseObject["events"] = *ctx;
                        responseObject["more"] = static_cast<std::uint32_t>(ctx->size()) == limit;

                        auto ctxIn = std::make_shared<CamContextIn>(*ctx, "");

                        bl::domain::BatchGetCamerasRequest breq;
                        for (const auto& t : ctxIn->m_cameras)
                        {
                            bl::domain::ResourceLocator* rl = breq.add_items();
                            rl->set_access_point(t);
                        }

                        PBatchCameraReader_t grpcReader(new BatchCameraReader_t
                            (GET_LOGGER_PTR, m_grpcManager, arc->m_credentials, &bl::domain::DomainService::Stub::AsyncBatchGetCameras));

                        auto ctxOut = std::make_shared<CamContextOut>();
                        grpcReader->asyncRequest(breq, [ctxIn, ctxOut, arc, responseObject](const bl::domain::BatchGetCamerasResponse& res,
                            NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus) mutable
                            {
                                if (!grpcStatus.ok())
                                {
                                    return NPluginUtility::SendGRPCError(arc->m_response, grpcStatus);
                                }

                                processCameras(ctxIn, ctxOut, res.items());

                                if (NWebGrpc::_FINISH == status)
                                {
                                    Json::Value json = ctxOut->GetJson();
                                    responseObject["cameras"] = json["cameras"];
                                    responseObject["archives"] = json["archives"];
                                    SendText(arc->m_response, Json::FastWriter().write(responseObject).c_str(), true);
                                }
                            }
                        );
                    }
                });
            }
            catch (const CORBA::Exception &)
            {
                Error(arc->m_response, IResponse::InternalServerError);
                return;
            }

        }
        );

    }
}
