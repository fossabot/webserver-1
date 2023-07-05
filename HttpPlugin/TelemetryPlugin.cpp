#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/foreach.hpp>
#include <boost/function.hpp>

#include <CorbaHelpers/ResolveServant.h>
#include <CorbaHelpers/ObjectName.h>
#include <CorbaHelpers/Unicode.h>
#include <HttpServer/json_oarchive.h>
#include <HttpServer/BasicServletImpl.h>
#include <DeviceIpint_3/DeviceSettings.h>
#include <ItvDeviceSdk/include/infoWriters.h>

#include <MMIDL/TelemetryC.h>

#include "HttpPlugin.h"
#include "Constants.h"
#include "RegexUtility.h"
#include "CommonUtility.h"
#include "ConfigHelper.h"
#include "Tokens.h"
#include "BLQueryHelper.h"
#include "CommonUtility.h"

#include <axxonsoft/bl/ptz/Telemetry.grpc.pb.h>

#include <json/json.h>

namespace nis = InfraServer::New;
using namespace NHttp;

namespace bl = axxonsoft::bl;
using PtzReader_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::CommonRequest, google::protobuf::Empty>;
using PPtzReader_t = std::shared_ptr < PtzReader_t >;

using PtzReaderMove_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::MoveRequest, google::protobuf::Empty>;
using PPtzReaderMove_t = std::shared_ptr < PtzReaderMove_t >;

using PtzReaderAbsoluteMove_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::AbsoluteMoveNormalizedRequest, google::protobuf::Empty>;
using PPtzReaderAbsoluteMove_t = std::shared_ptr<PtzReaderAbsoluteMove_t>;

using PtzReaderAcquireSessionId_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::AcquireSessionRequest, bl::ptz::AcquireSessionResponse>;
using PPtzReaderAcquireSessionId_t = std::shared_ptr < PtzReaderAcquireSessionId_t >;

using PtzReaderKeepAlive_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::SessionRequest, bl::ptz::KeepAliveResponse>;
using PPtzReaderKeepAlive_t = std::shared_ptr < PtzReaderKeepAlive_t >;

using PtzReaderReleaseSessioneId_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::SessionRequest, google::protobuf::Empty>;
using PPtzReaderReleaseSessioneId_t = std::shared_ptr < PtzReaderReleaseSessioneId_t >;

using PtzReaderGetPresetsInfo_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::GetPresetsInfoRequest, bl::ptz::PresetCollectionResponse>;
using PPtzReaderGetPresetsInfo_t = std::shared_ptr < PtzReaderGetPresetsInfo_t >;

using PtzReaderPresetGo_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::GoPresetRequest, google::protobuf::Empty>;
using PPtzReaderPresetGo_t = std::shared_ptr < PtzReaderPresetGo_t >;

using PtzReaderPresetRemove_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::RemovePresetRequest, google::protobuf::Empty>;
using PPtzReaderPresetRemove_t = std::shared_ptr < PtzReaderPresetRemove_t >;

using PtzReaderPresetSet_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::SetPresetRequest, google::protobuf::Empty>;
using PPtzReaderPresetSet_t = std::shared_ptr < PtzReaderPresetSet_t >;

using PtzReaderPosition_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::GetPositionInformationRequest, bl::ptz::GetPositionInformationNormalizedResponse>;
using PPtzReaderPosition_t = std::shared_ptr < PtzReaderPosition_t >;

using PtzReaderAreaZoom_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::AreaZoomRequest, google::protobuf::Empty>;
using PPtzReaderAreaZoom_t = std::shared_ptr < PtzReaderAreaZoom_t >;

using PtzReaderPointMove_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::PointMoveRequest, google::protobuf::Empty>;
using PPtzReaderPointMove_t = std::shared_ptr < PtzReaderPointMove_t >;

using PtzReaderSessionId_t = NWebGrpc::AsyncResultReader < bl::ptz::TelemetryService, bl::ptz::SessionID, google::protobuf::Empty>;
using PPtzReaderSessionId_t = std::shared_ptr < PtzReaderSessionId_t >;

using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
    bl::domain::BatchGetCamerasResponse >;
using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

namespace
{
    const char* const AUTO_FOCUS_FEATURE = "autoFocus";
    const char* const AUTO_IRIS_FEATURE = "autoIris";
    const char* const AREA_ZOOM_FEATURE = "areaZoom";
    const char* const POINT_MOVE_FEATURE = "pointMove";

    const char* const LIST       = "list";
    const char* const INFO       = "info";
    const char* const MOVE       = "move";
    const char* const MOVE_POINT = "move/point";
    const char* const FOCUS      = "focus";
    const char* const IRIS       = "iris";
    const char* const ZOOM       = "zoom";
    const char* const ZOOM_AREA  = "zoom/area";
    const char* const AUTO       = "auto";
    const char* const POSITION   = "position";

    const char* const PRESET_INFO   = "preset/info";
    const char* const PRESET_SET    = "preset/set";
    const char* const PRESET_REMOVE = "preset/remove";
    const char* const PRESET_GO     = "preset/go";

    const char* const SESSION_ACQUIRE    = "session/acquire";
    const char* const SESSION_RELEASE    = "session/release";
    const char* const SESSION_KEEP_ALIVE = "session/keepalive";

    const char* const TELEMETRY_CONTROL = "TelemetryControl";

    const char* const MODE_PARAMETER = "mode";
    const char* const DEGREE_PARAMETER = "degree";
    const char* const DEGREES_PARAMETER = "degrees";
    const char* const FEATURE_PARAMETER = "feature";

    const char* const ABSOLUTE_MODE = "absolute";
    const char* const RELATIVE_MODE = "relative";
    const char* const CONTINUOUS_MODE = "continuous";

    const char* const TILT_PARAMETER = "tilt";
    const char* const PAN_PARAMETER = "pan";
    const char* const FOCUS_PARAMETER = "focus";
    const char* const ZOOM_PARAMETER = "zoom";
    const char* const IRIS_PARAMETER = "iris";
    const char* const VALUE_PARAMETER = "value";
    const char* const PRESET_SPEED_PARAMETER = "presetSpeed";
    const char* const MASK_PARAMETER = "mask";

    const char* const POS_PARAMETER        = "pos";
    const char* const LABEL_PARAMETER      = "label";

    const char* const X_PARAMETER = "x";
    const char* const Y_PARAMETER = "y";

    const char* const SESSION_PRIORITY_PARAMETER = "session_priority";
    const char* const SESSION_ID_PARAMETER       = "session_id";
    const char* const USER_NAME_PARAMETER        = "user_name";
    const char* const HOST_NAME_PARAMETER        = "host_name";

    const char ERROR_CODE_PARAMETER[] = "error_code";

    const int ERROR_GENERAL = 1;
    const int ERROR_WRONG_PARAMETERS = 2;
    const int ERROR_SESSION_NOT_AVAILABLE = 3;
    const int ERROR_PRESET_OPERATION = 4;
    const int ERROR_POSITION_OPERATION = 5;

    const int DEGREE_DEFAULT = 0;
    const int POS_DEFAULT = 0;

    const int MAX_PRESET_SPEED = 100;

    const std::string SESSION_HOST_NAME("web-client");

    void sendResponseWithErrorCode(PResponse resp, int errorCode)
    {
        std::stringstream ss;
        {
            boost::archive::json_oarchive ar(ss);
            ar << boost::serialization::make_nvp(ERROR_CODE_PARAMETER, errorCode);
        }
        NPluginUtility::SendText(resp, IResponse::BadRequest, ss.str(), true);
    }

    void checkAndSendResponse(PResponse resp, CORBA::Long commandResult)
    {
        if (commandResult == Equipment::Telemetry::ENotError)
        {
            Error(resp, IResponse::OK);
            return;
        }

        int result = ERROR_GENERAL;
        switch (commandResult)
        {
        case Equipment::Telemetry::ESessionUnavailable:
            result = ERROR_SESSION_NOT_AVAILABLE;
            break;
        case Equipment::Telemetry::EPresetError:
            result = ERROR_PRESET_OPERATION;
        }
        sendResponseWithErrorCode(resp, result);
    }

    class CTelemetryContentImpl : public NHttpImpl::CBasicServletImpl
    {
    private:
        typedef CConfigHelper::TRevision TRevision;
        typedef IPINT30::SDeviceSettings TConfig;
        typedef IPINT30::STelemetryParam TConfigTelemetry;
        typedef IPINT30::STelemetryParam::TPresets TPresets;
        typedef CConfigHelper::PIpintConfig PConfig;
        typedef CConfigHelper::TIpintConfigSnapshot TSnapshot;
        typedef CConfigHelper::XFailed XFailed;
        typedef CConfigHelper::XChanged XChanged;
        typedef CConfigHelper::XRetry XRetry;
        typedef std::map<std::string, TSnapshot> TSnapshots;
        typedef boost::function1<void, CORBA::ULong> TPresetFunction;
        typedef boost::function1<void, TPresets &> TPresetChanger;

    private:
        enum ESendMode { SM_Headers, SM_Full };

        enum EOperationMode {
            EABSOLUTE,
            ERELATIVE,
            ECONTINUOUS,
            EAUTO_OP
        };

        enum ETelemetryMode {
            EUNKNOWN,
            ELIST,
            EINFO,
            EMOVE,
            EMOVEPOINT,
            EFOCUS,
            EIRIS,
            EZOOM,
            EZOOMAREA,
            EAUTO,
            EPRESET_INFO,
            EPRESET_SET,
            EPRESET_REMOVE,
            EPRESET_GO,
            ESESSION_ACQUIRE,
            ESESSION_RELEASE,
            ESESSION_KEEP_ALIVE,
            EPOSITION
        };

    public:
        CTelemetryContentImpl(NCorbaHelpers::IContainer *c, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rightsChecker)
            : m_grpcManager(grpcManager)
            , m_rightsChecker(rightsChecker)
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        virtual void Head(const PRequest req, PResponse resp)
        {
            Send(req, resp, SM_Headers);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            Send(req, resp, SM_Full);
        }

    private:
        static bool ParsePath(const std::string &path, ETelemetryMode &mode, NCorbaHelpers::CObjectName &name)
        {
            using namespace NPluginUtility;
            PObjName service = ObjName(name, 2, "hosts/");
            PObjName telemetry = ObjName(name, 3, "hosts/");

            if(Match(path, Mask(LIST) / service))                 mode = ELIST;
            else if (Match(path, Mask(INFO) / telemetry))         mode = EINFO;
            else if(Match(path, Mask(MOVE) / telemetry))          mode = EMOVE;
            else if(Match(path, Mask(MOVE_POINT) / telemetry))    mode = EMOVEPOINT;
            else if(Match(path, Mask(ZOOM) / telemetry))          mode = EZOOM;
            else if(Match(path, Mask(FOCUS) / telemetry))         mode = EFOCUS;
            else if(Match(path, Mask(IRIS) / telemetry))          mode = EIRIS;
            else if(Match(path, Mask(ZOOM_AREA) / telemetry))     mode = EZOOMAREA;
            else if(Match(path, Mask(AUTO) / telemetry))          mode = EAUTO;
            else if(Match(path, Mask(PRESET_INFO) / telemetry))   mode = EPRESET_INFO;
            else if(Match(path, Mask(PRESET_SET) / telemetry))    mode = EPRESET_SET;
            else if(Match(path, Mask(PRESET_REMOVE) / telemetry)) mode = EPRESET_REMOVE;
            else if(Match(path, Mask(PRESET_GO) / telemetry))     mode = EPRESET_GO;
            else if (Match(path, Mask(SESSION_ACQUIRE) / telemetry)) mode = ESESSION_ACQUIRE;
            else if (Match(path, Mask(SESSION_RELEASE) / telemetry)) mode = ESESSION_RELEASE;
            else if (Match(path, Mask(SESSION_KEEP_ALIVE) / telemetry)) mode = ESESSION_KEEP_ALIVE;
            else if (Match(path, Mask(POSITION) / telemetry))     mode = EPOSITION;

            return mode != EUNKNOWN;
        }

        void Send(const PRequest& req, PResponse& resp, ESendMode sm)
        {
            ETelemetryMode mode = EUNKNOWN;
            NCorbaHelpers::CObjectName name;
            try
            {
                if (!ParsePath(req->GetPathInfo(), mode, name))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                if (ESESSION_ACQUIRE == mode || ESESSION_RELEASE == mode || ESESSION_KEEP_ALIVE == mode
                    || EPRESET_INFO == mode || EPRESET_GO == mode || EPRESET_REMOVE == mode
                    || EPRESET_SET == mode || EPOSITION == mode
                    || EZOOMAREA == mode || EMOVEPOINT == mode || EAUTO == mode)
                {
                    return doTelemetryOperationSimple(req, resp, mode, name.ToString());
                }

                const std::string service = name.GetObjectParent() + "/" + name.GetObjectTypeId();

                if (ELIST == mode)
                    return telemetryList(req, resp, name, service);

                if (EINFO == mode)
                    return telemetryInfo(req, resp, service);

                if (EMOVE == mode || EFOCUS == mode || EZOOM == mode || EIRIS == mode)
                    return doTelemetryOperation(req, resp, mode, service, name.ToString());
            }
            catch (const std::invalid_argument& e)
            {
                _err_ << "Parameter parse error: " << e.what();
            }
            catch (const boost::bad_lexical_cast& e)
            {
                _err_ << "Parameter parse error: " << e.what();
            }
            Error(resp, IResponse::BadRequest);
        }

        template<typename TReq, typename TReader>
        void handleReq(TReq &req, const  EOperationMode &flag, const NPluginUtility::TParams &params, const std::string& telemetryName
                       , TReader& reader, PResponse &resp)
        {
            setSessionId(req, params, SESSION_ID_PARAMETER);

            axxonsoft::bl::ptz::Capabilities *pc = new axxonsoft::bl::ptz::Capabilities();

            switch (flag)
            {
            case ECONTINUOUS:
                pc->set_is_continuous(true);
                break;
            case ERELATIVE:
                pc->set_is_relative(true);
                break;
            case EAUTO_OP:
                pc->set_is_auto(true);
                break;
            default:
                break;
            };          

            req.set_allocated_mode(pc);
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                    Equipment::Telemetry::EGeneralError);
            }
            );
        }

        void handleCommonReq(PtzReader_t::AsyncRpcMethod_t method,
            const  NGrpcHelpers::PCredentials &metaCredentials,
            const  EOperationMode &flag, const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            PPtzReader_t reader(new PtzReader_t
                (GET_LOGGER_PTR, m_grpcManager, metaCredentials, method));

            bl::ptz::CommonRequest req;
            req.set_value(NPluginUtility::GetParam<double>(params, VALUE_PARAMETER));

            handleReq(req, flag, params, telemetryName, reader, resp);
        }

        void handleAcquireSessionReq(PtzReaderAcquireSessionId_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            std::string user, const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            PPtzReaderAcquireSessionId_t reader(new PtzReaderAcquireSessionId_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method));

            bl::ptz::AcquireSessionRequest req;
            req.set_host_name(SESSION_HOST_NAME);
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const bl::ptz::AcquireSessionResponse &r, grpc::Status status) mutable
                {
                    if (status.ok() && (r.error_code() == bl::ptz::AcquireSessionResponse::NotError))
                    {
                        std::stringstream ss;
                        {
                            boost::archive::json_oarchive ar(ss);
                            int sessionId = r.session_id();
                            ar << boost::serialization::make_nvp(SESSION_ID_PARAMETER, sessionId);
                        }

                        NPluginUtility::SendText(resp, IResponse::OK, ss.str(), true);
                    }
                    else if (status.ok() && (r.error_code() == bl::ptz::AcquireSessionResponse::SessionUnavailable))
                    {
                        std::stringstream ss;
                        {
                            boost::archive::json_oarchive ar(ss);
                            int ec = r.error_code();
                            ar << boost::serialization::make_nvp(ERROR_CODE_PARAMETER, ec);
                            ar << boost::serialization::make_nvp(USER_NAME_PARAMETER, r.user_session_info().user_name());
                            ar << boost::serialization::make_nvp(HOST_NAME_PARAMETER, r.user_session_info().host_name());
                        }

                        NPluginUtility::SendText(resp, IResponse::BadRequest, ss.str(), true);
                    }
                    else
                        sendResponseWithErrorCode(resp, status.ok() ? bl::ptz::AcquireSessionResponse::NotError : bl::ptz::AcquireSessionResponse::GeneralError);
                }
            );
        }

        void handleKeepAliveReq(PtzReaderKeepAlive_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            PPtzReaderKeepAlive_t reader(new PtzReaderKeepAlive_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method));

            bl::ptz::SessionRequest req;
            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const bl::ptz::KeepAliveResponse &r, grpc::Status status) mutable
                {
                    if (status.ok() && !r.result())
                        Error(resp, IResponse::NotFound);
                    else
                        checkAndSendResponse(resp, status.ok() ? Equipment::Telemetry::ENotError : Equipment::Telemetry::EGeneralError);
                }
            );
        }

        void handleReleaseSessionReq(PtzReaderReleaseSessioneId_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            PPtzReaderReleaseSessioneId_t reader(new PtzReaderReleaseSessioneId_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method));

            bl::ptz::SessionRequest req;
            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError : Equipment::Telemetry::EGeneralError);
            }
            );
        }

        void handlePresetsInfoReq(PtzReaderGetPresetsInfo_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            PPtzReaderGetPresetsInfo_t reader(new PtzReaderGetPresetsInfo_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method));

            bl::ptz::GetPresetsInfoRequest req;
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const bl::ptz::PresetCollectionResponse &r, grpc::Status valid) mutable
            {
                if (!valid.ok())
                    return sendResponseWithErrorCode(resp, ERROR_GENERAL);

                using mapPresetCollection = std::map<CORBA::ULong, std::wstring>;
                mapPresetCollection presetCollection;
                for (const auto& info : r.preset_info())
                {
                    presetCollection.insert(std::make_pair(info.position(), NCorbaHelpers::FromUtf8(info.label())));
                }

                Json::Value presets(Json::objectValue);
                mapPresetCollection::iterator it1 =
                    presetCollection.begin(), it2 = presetCollection.end();
                for (; it1 != it2; ++it1)
                    presets[((boost::format("%d") % it1->first).str())] = NCorbaHelpers::ToUtf8(it1->second);

                NPluginUtility::SendText(resp, presets.toStyledString(), true);
            }
            );
        }

        void handlePresetsGoReq(PtzReaderPresetGo_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            using namespace NPluginUtility;

            PPtzReaderPresetGo_t reader(new PtzReaderPresetGo_t(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method));

            bl::ptz::GoPresetRequest req;
            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);
            req.set_position(GetParam<int>(params, POS_PARAMETER));
            req.set_speed(GetParam<int>(params, PRESET_SPEED_PARAMETER, MAX_PRESET_SPEED) / static_cast<double>(MAX_PRESET_SPEED));

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                    Equipment::Telemetry::EGeneralError);
            }
            );
        }

        void handlePresetRemoveReq(PtzReaderPresetRemove_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse resp, const IRequest::AuthSession& as)
        {
            using namespace NPluginUtility;

            m_rightsChecker->HasGlobalPermissions({ axxonsoft::bl::security::FEATURE_ACCESS_EDIT_PTZ_PRESETS }, as,
                [=](bool hasPermissions)
            {
                if (!hasPermissions)
                {
                    Error(resp, IResponse::Forbidden);
                    return;
                }

                auto reader = std::make_shared<PtzReaderPresetRemove_t>(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method);

                bl::ptz::RemovePresetRequest req;

                setSessionId(req, params, SESSION_ID_PARAMETER);
                req.set_access_point(telemetryName);
                req.set_position(GetParam<int>(params, POS_PARAMETER));

                reader->asyncRequest(req,
                    [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
                {
                    checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                        Equipment::Telemetry::EGeneralError);
                }
                );
            });
        }

        void handlePresetSetReq(PtzReaderPresetSet_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const std::string &query, const std::string& telemetryName, PResponse resp, const IRequest::AuthSession& as)
        {
            using namespace NPluginUtility;

            m_rightsChecker->HasGlobalPermissions({ axxonsoft::bl::security::FEATURE_ACCESS_EDIT_PTZ_PRESETS }, as,
                [=](bool hasPermissions)
            {
                if (!hasPermissions)
                {
                    Error(resp, IResponse::Forbidden);
                    return;
                }

                TParams params;
                if (!ParseParams(query, params))
                {
                    Error(resp, IResponse::BadRequest);
                    return;
                }

                auto reader = std::make_shared<PtzReaderPresetSet_t>(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method);

                bl::ptz::SetPresetRequest req;

                setSessionId(req, params, SESSION_ID_PARAMETER);
                req.set_access_point(telemetryName);
                req.set_position(GetParam<int>(params, POS_PARAMETER));

                const std::string utfLabel = GetParam<std::string>(params, LABEL_PARAMETER);

                req.set_label(utfLabel);

                reader->asyncRequest(req,
                    [resp](const google::protobuf::Empty &r, grpc::Status valid)
                {
                    checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                        Equipment::Telemetry::EGeneralError);
                }
                );
            });
        }

        void handlePositionReq(PtzReaderPosition_t::AsyncRpcMethod_t method, const NGrpcHelpers::PCredentials &metaCredentials,
            const std::string& telemetryName, PResponse &resp)
        {
            auto reader = std::make_shared<PtzReaderPosition_t>(GET_LOGGER_PTR, m_grpcManager, metaCredentials, method);

            bl::ptz::GetPositionInformationRequest req;

            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const bl::ptz::GetPositionInformationNormalizedResponse &r, grpc::Status valid) mutable
            {
                if (!valid.ok())
                    return sendResponseWithErrorCode(resp, ERROR_POSITION_OPERATION);

                const auto& pos = r.absolute_position();

                std::stringstream ss;
                {
                    boost::archive::json_oarchive ar(ss);

                    auto pan = pos.pan();
                    auto tilt = pos.tilt();
                    auto zoom = pos.zoom();
                    auto mask = pos.mask();

                    ar << boost::serialization::make_nvp(PAN_PARAMETER, pan);
                    ar << boost::serialization::make_nvp(TILT_PARAMETER, tilt);
                    ar << boost::serialization::make_nvp(ZOOM_PARAMETER, zoom);
                    ar << boost::serialization::make_nvp(MASK_PARAMETER, mask);
                }

                NPluginUtility::SendText(resp, IResponse::OK, ss.str(), true);
            }
            );
        }

        void handleAreaZoomReq(const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            long sessionId = 0;
            double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
            NPluginUtility::GetParam(params, SESSION_ID_PARAMETER, sessionId);
            NPluginUtility::GetParam(params, X_PARAMETER, x);
            NPluginUtility::GetParam(params, Y_PARAMETER, y);
            NPluginUtility::GetParam(params, WIDTH_PARAMETER, width);
            NPluginUtility::GetParam(params, HEIGHT_PARAMETER, height);

            PPtzReaderAreaZoom_t reader(new PtzReaderAreaZoom_t
                (GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::ptz::TelemetryService::Stub::AsyncAreaZoom)
                );
            bl::ptz::AreaZoomRequest req;

            bl::primitive::Rectangle *pRect = new bl::primitive::Rectangle();
            pRect->set_x(x);
            pRect->set_y(y);

            pRect->set_w(width);
            pRect->set_h(height);

            req.set_allocated_rectangle(pRect);
            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                    Equipment::Telemetry::EGeneralError);
            }
            );
        }

        void handlePointMoveReq(const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            double x = 0.0, y = 0.0;
            NPluginUtility::GetParam(params, X_PARAMETER, x);
            NPluginUtility::GetParam(params, Y_PARAMETER, y);

            auto reader = std::make_shared<PtzReaderPointMove_t>(GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, &bl::ptz::TelemetryService::Stub::AsyncPointMove);
            bl::ptz::PointMoveRequest req;

            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);

            bl::primitive::Point *pPt = new bl::primitive::Point();
            pPt->set_x(x);
            pPt->set_y(y);

            req.set_allocated_point(pPt);

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                    Equipment::Telemetry::EGeneralError);
            }
            );

        }

        void handleSessionIdReq(const NGrpcHelpers::PCredentials &metaCredentials,
            const NPluginUtility::TParams &params, const std::string& telemetryName, PResponse &resp)
        {
            using namespace NPluginUtility;

            std::string degree;
            TParams::const_iterator it = params.find(DEGREE_PARAMETER);
            if (it != params.end())
                degree = it->second;

            auto reader = std::make_shared<PtzReaderSessionId_t>(GET_LOGGER_PTR, m_grpcManager,
                metaCredentials, (FOCUS_PARAMETER == degree) ? &bl::ptz::TelemetryService::Stub::AsyncFocusAuto :
                &bl::ptz::TelemetryService::Stub::AsyncIrisAuto);

            bl::ptz::SessionID req;

            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                    Equipment::Telemetry::EGeneralError);
            }
            );
        }

        void telemetryList(const PRequest& req, PResponse &resp, const NCorbaHelpers::CObjectName &name, const std::string &service)
        {
            bl::domain::BatchGetCamerasRequest creq;

            bl::domain::ResourceLocator* rl = creq.add_items();
            rl->set_access_point("hosts/" + service + "/SourceEndpoint.video:0:0");

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            PBatchCameraReader_t grpcReader(new BatchCameraReader_t
                (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::domain::DomainService::Stub::AsyncBatchGetCameras));

            auto arr = std::make_shared<Json::Value>(Json::arrayValue);

            grpcReader->asyncRequest(creq, [this, resp, arr](const bl::domain::BatchGetCamerasResponse& res, NWebGrpc::STREAM_ANSWER status, grpc::Status grpcStatus) mutable
            {
                if (!grpcStatus.ok())
                {
                    return NPluginUtility::SendGRPCError(resp, grpcStatus);
                }

                processCamerasTelemetryList(arr, res.items());

                if (NWebGrpc::_FINISH == status)
                {
                    NPluginUtility::SendText(resp,arr->toStyledString(), true);
                }             
            });
        }

        void telemetryInfo(const PRequest& req, PResponse &resp, const std::string &service)
        {
            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            auto ctxOut = std::make_shared<Json::Value>();
            NWebBL::TEndpoints eps{ "hosts/" + service + "/SourceEndpoint.video:0:0" };
            NWebBL::FAction action = boost::bind(&CTelemetryContentImpl::onCameraInfo, shared_from_base<CTelemetryContentImpl>(),
                req, resp, ctxOut, _1, _2, _3);
            NWebBL::QueryBLComponent(GET_LOGGER_PTR, m_grpcManager, metaCredentials, eps, action);
        }

        void onCameraInfo(const PRequest req, PResponse resp, std::shared_ptr<Json::Value> ctxOut,
            const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams, NWebGrpc::STREAM_ANSWER valid, grpc::Status grpcStatus)
        {
            if (!grpcStatus.ok())
            {
                _err_ << "/telemetry: GetCamerasByComponents method failed";
                return NPluginUtility::SendGRPCError(resp, grpcStatus);
            }

            getCamerasTelemetryInfo(ctxOut, cams);

            if (valid == NWebGrpc::_FINISH)
            {
                NPluginUtility::SendText(req, resp, ctxOut->toStyledString());
            }
        }

        void doTelemetryOperationSimple(const PRequest& req, PResponse &resp, ETelemetryMode mode,
            const std::string& telemetryName)
        {
            std::string query(req->GetQuery());

            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            NPluginUtility::TParams params;
            if (!NPluginUtility::ParseParams(query, params))
            {
                return Error(resp, IResponse::BadRequest);
            }

            switch (mode)
            {
            case ESESSION_ACQUIRE:
                handleAcquireSessionReq(&bl::ptz::TelemetryService::Stub::AsyncAcquireSessionId, metaCredentials, as.user, params, telemetryName, resp);
                break;
            case ESESSION_KEEP_ALIVE:
                handleKeepAliveReq(&bl::ptz::TelemetryService::Stub::AsyncKeepAlive, metaCredentials, params, telemetryName, resp);
                break;
            case ESESSION_RELEASE:
                handleReleaseSessionReq(&bl::ptz::TelemetryService::Stub::AsyncReleaseSessionId, metaCredentials, params, telemetryName, resp);
                break;
            case EPRESET_INFO:
                handlePresetsInfoReq(&bl::ptz::TelemetryService::Stub::AsyncGetPresetsInfo, metaCredentials, params, telemetryName, resp);
                break;
            case EPRESET_GO:
                handlePresetsGoReq(&bl::ptz::TelemetryService::Stub::AsyncGoPreset, metaCredentials, params, telemetryName, resp);
                break;
            case EPRESET_REMOVE:
                handlePresetRemoveReq(&bl::ptz::TelemetryService::Stub::AsyncRemovePreset, metaCredentials, params, telemetryName, resp, as);
                break;
            case EPRESET_SET:
                handlePresetSetReq(&bl::ptz::TelemetryService::Stub::AsyncSetPreset, metaCredentials, req->GetQuery(), telemetryName, resp, as);
                break;
            case EPOSITION:
                handlePositionReq(&bl::ptz::TelemetryService::Stub::AsyncGetPositionInformationNormalized, metaCredentials, telemetryName, resp);
                break;
            case EZOOMAREA:
                handleAreaZoomReq(metaCredentials, params, telemetryName, resp);
                break;
            case EMOVEPOINT:
                handlePointMoveReq(metaCredentials, params, telemetryName, resp);
                break;
            case EAUTO:
                handleSessionIdReq(metaCredentials, params, telemetryName, resp);
                break;

            default:
                break;
            }

            return;
        }

        void doTelemetryOperation(const PRequest& req, PResponse &resp, ETelemetryMode mode,
            const std::string& service, const std::string& telemetryName)
        {
            const IRequest::AuthSession& as = req->GetAuthSession();
            NGrpcHelpers::PCredentials metaCredentials = NPluginUtility::GetCommonCredentials(GET_LOGGER_PTR, as);

            std::string query(req->GetQuery());
            NPluginUtility::TParams params;
            if (!NPluginUtility::ParseParams(query, params))
            {
                return Error(resp, IResponse::BadRequest);
            }

            execDoTelemetryOperation(resp, mode, service, telemetryName, params, metaCredentials, getFlag(NPluginUtility::GetParam<std::string>(params, MODE_PARAMETER)));
        }

        void execDoTelemetryOperation(PResponse resp, ETelemetryMode mode,
            const std::string& service, const std::string& telemetryName, NPluginUtility::TParams params, NGrpcHelpers::PCredentials metaCredentials, EOperationMode flag)
        {
            switch (mode)
            {
            case EMOVE:
            {
                if (EABSOLUTE == flag)
                    return absoluteMove(resp, service, telemetryName, params, metaCredentials);

                PPtzReaderMove_t reader(new PtzReaderMove_t
                    (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::ptz::TelemetryService::Stub::AsyncMove));

                bl::ptz::MoveRequest req;

                req.set_val_pan(NPluginUtility::GetParam<double>(params, PAN_PARAMETER));
                req.set_val_tilt(NPluginUtility::GetParam<double>(params, TILT_PARAMETER));

                handleReq(req, flag, params, telemetryName, reader, resp);
            }
            break;
            case EFOCUS:
                handleCommonReq(&bl::ptz::TelemetryService::Stub::AsyncFocus, metaCredentials, flag, params, telemetryName, resp);
                break;
            case EZOOM:
            {
                if (EABSOLUTE == flag)
                    return absoluteMove(resp, service, telemetryName, params, metaCredentials);

                handleCommonReq(&bl::ptz::TelemetryService::Stub::AsyncZoom, metaCredentials, flag, params, telemetryName, resp);                
            }
            break;
            case EIRIS:
                handleCommonReq(&bl::ptz::TelemetryService::Stub::AsyncIris, metaCredentials, flag, params, telemetryName, resp);
                break;

            default:
                break;
            }
        }

        void absoluteMove(PResponse resp,
            const std::string& service, const std::string& telemetryName, NPluginUtility::TParams params, NGrpcHelpers::PCredentials metaCredentials)
        {
            PPtzReaderAbsoluteMove_t reader(new PtzReaderAbsoluteMove_t
            (GET_LOGGER_PTR, m_grpcManager, metaCredentials, &bl::ptz::TelemetryService::Stub::AsyncAbsoluteMoveNormalized));
            
            //first bit - zoom; second - tilt, third - pan
            int mask = 0;

            const double def_val = -2.;
            const double val_in_range = 0.5;  //гарантированно попал в диапазон [0;1]

            auto pan = NPluginUtility::GetParam<double>(params, PAN_PARAMETER, def_val);
            auto tilt = NPluginUtility::GetParam<double>(params, TILT_PARAMETER, def_val);
            auto zoom = NPluginUtility::GetParam<double>(params, VALUE_PARAMETER, def_val);

            if (NPluginUtility::eq(zoom, def_val))
                zoom = val_in_range;
            else
                mask |= 1;

            if (NPluginUtility::eq(tilt, def_val))
                tilt = val_in_range;
            else
                mask |= 2;

            if (NPluginUtility::eq(pan, def_val))
                pan = val_in_range;
            else
                mask |= 4;
            

            bl::ptz::AbsoluteMoveNormalizedRequest req;

            setSessionId(req, params, SESSION_ID_PARAMETER);
            req.set_access_point(telemetryName);

            auto *p = new  ::axxonsoft::bl::ptz::AbsolutePositionNormalized();

            p->set_mask(mask);
            p->set_zoom(zoom);
            p->set_tilt(tilt);
            p->set_pan(pan);

            req.set_allocated_absolute_position(p);

            reader->asyncRequest(req,
                [resp](const google::protobuf::Empty &r, grpc::Status valid) mutable
            {
                checkAndSendResponse(resp, valid.ok() ? Equipment::Telemetry::ENotError :
                    Equipment::Telemetry::EGeneralError);
            }
            );

        }

        static std::string delHosts(const std::string &telemetry)
        {
            std::string pattern = "hosts/";
            if (telemetry.substr(0, pattern.size()) == pattern)
                return telemetry.substr(pattern.size());

            return telemetry;
        }

        EOperationMode getFlag(const std::string &mode)
        {
            if (CONTINUOUS_MODE == mode)
                return ECONTINUOUS;

            if (ABSOLUTE_MODE == mode)
                return EABSOLUTE;

            if (RELATIVE_MODE == mode)
                return ERELATIVE;

            if (AUTO == mode)
                return EAUTO_OP;

            return ECONTINUOUS;
        }

        static void processCamerasTelemetryList(std::shared_ptr<Json::Value> ctxOut, const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);
                int ptzCount = c.ptzs_size();
                for (int j = 0; j < ptzCount; ++j)
                {
                    const bl::domain::Telemetry& tel = c.ptzs(j);
                    ctxOut->append(delHosts(tel.access_point()));
                }
            }
        }

        static std::string convertToEFreedom(int freedom)
        {
            switch (freedom)
            {
            case 0:
                return "pan";
            case 1:
                return "tilt";
            case 2:
                return "focus";
            case 3:
                return "zoom";
            case 4:
                return "iris";
            default:
                return "";
            };

            return "";
        }

        static Json::Value jsonRange(int _min, int _max)
        {
            Json::Value res;
            res["min"] = std::to_string(_min);
            res["max"] = std::to_string(_max);

            return res;
        }

        static Json::Value convert(const bl::ptz::Limits &limits)
        {
            Json::Value res;

            if (limits.relative_step().min() != 0 || limits.relative_step().max() != 0)
                res["relative"] = jsonRange(limits.relative_step().min(), limits.relative_step().max());

            if (limits.absolute_position().min() != 0 || limits.absolute_position().max() != 0)
                res["absolute"] = jsonRange(limits.absolute_position().min(), limits.absolute_position().max());

            if (limits.continuous_speed().min() != 0 || limits.continuous_speed().max() != 0)                   
                res["continuous"] = jsonRange(limits.continuous_speed().min(), limits.continuous_speed().max());

            return res;
        }

        static void getCamerasTelemetryInfo(std::shared_ptr<Json::Value> ctxOut
                                        , const ::google::protobuf::RepeatedPtrField< ::axxonsoft::bl::domain::Camera >& cams)
        {
            int itemCount = cams.size();
            for (int i = 0; i < itemCount; ++i)
            {
                const bl::domain::Camera& c = cams.Get(i);
                int ptzCount = c.ptzs_size();
                for (int j = 0; j < ptzCount; ++j)
                {
                    const bl::domain::Telemetry& tel = c.ptzs(j);
                    const auto &limits = tel.capabilities().limits();

                    for (auto &limit : limits)
                    {
                        auto freedom = convertToEFreedom(limit.first);

                        if (!freedom.empty())
                            (*ctxOut)[DEGREES_PARAMETER][freedom] = convert(limit.second);
                    }

                    if (tel.capabilities().is_point_move_supported())
                        (*ctxOut)[FEATURE_PARAMETER].append("pointMove");

                    if (tel.capabilities().is_area_zoom_supported())
                        (*ctxOut)[FEATURE_PARAMETER].append("areaZoom");
                }
            }
        }

        template<class TReq, class TPs>
        void setSessionId(TReq &req, const TPs &params, const typename TPs::key_type &paramName)
        {
            try
            {
                req.set_session_id(NPluginUtility::GetParam<long>(params, paramName));
            }
            catch (const std::invalid_argument&)
            {
                throw std::invalid_argument("Parameter session_id is not found");
            }
        }

    private:
        const NWebGrpc::PGrpcManager m_grpcManager;
        const NPluginUtility::PRigthsChecker m_rightsChecker;

        DECLARE_LOGGER_HOLDER;
    };
}

namespace NHttp
{
    IServlet* CreateTelemetryServlet(NCorbaHelpers::IContainer* c, const NWebGrpc::PGrpcManager grpcManager, const NPluginUtility::PRigthsChecker rightsChecker)
    {
        return new CTelemetryContentImpl(c, grpcManager, rightsChecker);
    }
}
