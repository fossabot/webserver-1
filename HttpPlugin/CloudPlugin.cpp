#include <mutex>
#include <queue>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/format.hpp>

#include "HttpPlugin.h"
#include "CommonUtility.h"

#include <HttpServer/BasicServletImpl.h>

#include <CommonNotificationCpp/Connector.h>

#include <InfraServer_IDL/HostAgentC.h>
#include <InfraServer_IDL/CloudClientC.h>

#include <Notification_IDL/Notification.h>
#include <NotificationService_IDL/RemoteReportingS.h>
#include <InfraServer_IDL/EventTypeTraits.h>
#include <ORM_IDL/EventTypeTraits.h>

#include <mmss/Grabber/Grabber.h>

#include <CorbaHelpers/Envar.h>
#include <CorbaHelpers/Reactor.h>

#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/ResolveServant.h>
#include <CorbaHelpers/RefcountedServant.h>
#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/Uuid.h>

#include <ORM_IDL/ORM.h>
#include <ORM_IDL/ORMC.h>

#include <json/json.h>

#include <Crypto/Crypto.h>

#include <CloudUtils/ApiClient.h>
#include <NativeBLClient/EventSubscription.h>
#include <Primitives/HttpClient/CloudClient.h>

#include <SecurityManager/SecurityManager.h>
#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

#include <axxonsoft/bl/realtimeRecognizer/RealtimeRecognizer.grpc.pb.h>
#include <axxonsoft/bl/events/Notification.grpc.pb.h>
#include <axxonsoft/bl/security/SecurityService.grpc.pb.h>

using namespace NHttp;
using namespace NPluginUtility;
using namespace NCloudClient;
namespace bl = axxonsoft::bl;

using BatchCameraReader_t = NWebGrpc::AsyncStreamReader < bl::domain::DomainService, bl::domain::BatchGetCamerasRequest,
    bl::domain::BatchGetCamerasResponse >;
using PBatchCameraReader_t = std::shared_ptr < BatchCameraReader_t >;

using GetLists_t = NWebGrpc::AsyncResultReader < bl::realtimerecognizer::RealtimeRecognizerService, bl::realtimerecognizer::GetListsRequest,
    bl::realtimerecognizer::GetListsResponse>;
using PGetLists_t = std::shared_ptr < GetLists_t >;

using CameraEventReader_t = NWebGrpc::AsyncStreamReader < bl::events::DomainNotifier, bl::events::PullEventsRequest,
    bl::events::Events >;
using PCameraEventReader_t = std::shared_ptr < CameraEventReader_t >;
using CameraEventCallback_t = std::function<void(const bl::events::Events&, NWebGrpc::STREAM_ANSWER)>;

using ListConfig_t = NWebGrpc::AsyncResultReader < bl::security::SecurityService, bl::security::ListConfigRequest,
    bl::security::ListConfigResponse>;
using PListConfig_t = std::shared_ptr<ListConfig_t>;
using ListConfigCallback_t = std::function<void(const bl::security::ListConfigResponse&, bool)>;

namespace
{
    const char* const CLOUD_CLIENT_REFERENCE = "CloudClient.0";
    const char* const CONFIG_EVENT_RECEIVER = "ConfigEventReceiver";
    const char* const CLOUD_SERVLET = "CloudServlet";
    const char* const PUSH_AGENT = "PushAgent";

    const char* const SUBSCRIPTION_PATH = "/api/v3/ac-backend/devices";
    const char* const TEST_PATH         = "/api/v3/ac-backend/notifications/test";
    const char* const EVENT_PATH        = "/api/v3/ac-backend/notifications/event";
    const char* const SNOOZE_PATH       = "/api/v3/ac-backend/notifications/snooze";
    const char* const CLEAR_PATH       = "/api/v3/ac-backend/notifications/clear";

    long INVALID_RESPONSE_CODE = -1;

    class EventsAndMobileApiClient : public ApiClientBase
    {
    public:
        EventsAndMobileApiClient(ApiClientConfig const& config) 
            : ApiClientBase(config), 
            m_timer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
        {
            NCorbaHelpers::PContainer container = config.container;

            auto n_cores = std::max(NCorbaHelpers::CEnvar::NgpHardwareConcurrency(), 1U);
            m_rpcPool = NExecutors::CreateDynamicThreadPool(container->GetLogger(), "MobileApiCl",
                n_cores * 16 /* max queue length */,
                0 /* min idle threads */,
                n_cores /* max threads */
            );
        }

        ~EventsAndMobileApiClient()
        {
            if (m_rpcPool)
            {
                m_rpcPool->Shutdown();
                m_rpcPool.reset();
            }

            m_timer.cancel();
        }

        long Subscribe(Json::Value const& e)
        {
            long code = INVALID_RESPONSE_CODE;
            auto r = post(SUBSCRIPTION_PATH, e, code);
            _log_ << "response 'subscription' from cloud: status :" << code << " body :"<< (r ? r.get().toStyledString() : "");
            return code;
        }

        long Unsubscribe(std::string const& device, const Json::Value &body)
        {
            std::ostringstream query;
            query << SUBSCRIPTION_PATH << '/' << device;

            long code = INVALID_RESPONSE_CODE;
            deleteRequest(query.str(), body, code);
            return code;
        }

        long SendTestMessage(Json::Value const& message)
        {
            long code = INVALID_RESPONSE_CODE;
            auto r = post(TEST_PATH, message, code);
            _log_ << "response 'test' from cloud: status :" << code << " body :" << (r ? r.get().toStyledString() : "");
            return code;
        }

        long SendSnoozeMessage(const Json::Value& e)
        {
            long code = INVALID_RESPONSE_CODE;
            auto r = post(SNOOZE_PATH, e, code);
            _log_ << "response 'snooze' from cloud: status :" << code << " body :" << (r ? r.get().toStyledString() : "");
            return code;
        }

        long SendClearMessage(const Json::Value& e)
        {
            long code = INVALID_RESPONSE_CODE;
            auto r = post(CLEAR_PATH, e, code);
            _log_ << "response 'clear' from cloud: status :" << code << " body :" << (r ? r.get().toStyledString() : "");
            return code;
        }

        void SendEvent(Json::Value const& e)
        {
            addEvent(e);
            schedule(boost::posix_time::neg_infin);
        }
    private:
        void schedule(const boost::posix_time::time_duration& delay)
        {
            m_timer.expires_from_now(delay);
            m_timer.async_wait(boost::bind(&EventsAndMobileApiClient::onTimer, this, _1));
        }

        void onTimer(const boost::system::error_code& error)
        {
            if (error)
                return;

            sendEvent();
        }

        void sendEvent()
        {
            std::lock_guard<std::mutex> lock(m_eventsGuard);
            while (m_events.size() > 0)
            {
                Json::Value e = m_events.front();
                if (!m_rpcPool->Post([this, e]()
                     {
                        NCloudClient::optionalJson_t r;
                        long code = INVALID_RESPONSE_CODE;
                        try {
                            r = this->post(EVENT_PATH, e, code);
                        }
                        catch (...)
                        {
                            _err_ << "DynamicThreadPool error: post event to cloud failed";
                        }
                        _log_ << "response 'event' from cloud: status :" << code << " body :" << (r ? r.get().toStyledString() : "");
                     }))
                {
                    _dbg_ << "Event post failed, will re-try later.";
                    schedule(boost::posix_time::time_duration(boost::posix_time::seconds(1)));
                    break;
                }

                m_events.pop_front();
            }
        }
        
        void addEvent(Json::Value const& ev)
        {
            std::lock_guard<std::mutex> lock(m_eventsGuard);
            if (m_events.size() > m_limit)
            {
                _err_ << "Event queue is full, dropping the oldest event.";
                m_events.pop_front();
            }

            m_events.push_back(ev);
        }

    private:
        NExecutors::PDynamicThreadPool m_rpcPool;
        boost::asio::deadline_timer m_timer;
        std::mutex m_eventsGuard;
        std::deque<Json::Value> m_events;
        const size_t m_limit = 1024;
    };

    struct IEventListener
    {
        virtual void OnConfigChanged() = 0;
    protected:
        ~IEventListener() {}
    };

    class CEventConsumerSink : public NCommonNotification::IEventConsumerSink
        , public NCorbaHelpers::CWeakReferableImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CEventConsumerSink(DECLARE_LOGGER_ARG, IEventListener* l, NCorbaHelpers::IContainer* c)
            : m_listener(l)
            , m_container(c)
        {
            INIT_LOGGER_HOLDER;
            _log_ << "Cloud client: event consumer sink created";
        }

        ~CEventConsumerSink()
        {
            _log_ << "Cloud client: event consumer sink destroyed";
        }

        void SetCloudReference(ApiClientConfig const& config)
        {
            m_client.reset(new EventsAndMobileApiClient(config));
        }

        long SendSubscriptionMessage(Json::Value& message, const std::string &vmsLogin)
        {
            message["vmsLogin"] = vmsLogin;
            _log_ << "to_cloud_subscr: " << message.toStyledString();

            long statusCode = INVALID_RESPONSE_CODE;
            if (m_client)
                statusCode = m_client->Subscribe(message);
            return statusCode;
        }

        long Unsubscribe(const std::string& device, const std::string& vmsLogin)
        {
            Json::Value body(Json::objectValue);
            body["vmsLogin"] = vmsLogin;

            _log_ << "to_cloud_unsubscr: " << body.toStyledString();

            long statusCode = INVALID_RESPONSE_CODE;
            if (m_client)
                statusCode = m_client->Unsubscribe(device, body);
            return statusCode;
        }

        long SendTestMessage(Json::Value const& message)
        {
            long statusCode = INVALID_RESPONSE_CODE;
            if (m_client)
                statusCode = m_client->SendTestMessage(message);
            return statusCode;
        }

        long SendSnoozeMessage(const Json::Value& message)
        {
            _log_ << "to_cloud_snooze: " << message.toStyledString();

            long statusCode = INVALID_RESPONSE_CODE;
            if (m_client)
                statusCode = m_client->SendSnoozeMessage(message);
            return statusCode;
        }

        long SendClearMessage(const Json::Value& message)
        {
            _log_ << "to_cloud_clear: " << message.toStyledString();

            long statusCode = INVALID_RESPONSE_CODE;
            if (m_client)
                statusCode = m_client->SendClearMessage(message);
            return statusCode;
        }

        void SendEvent(Json::Value const& message)
        {
            if (m_client)
                m_client->SendEvent(message);
        }

        virtual void ProcessEvents(const Notification::Events& events)
        {
            if (!m_client)
            {
                _dbg_ << "Unable to send event: domain detached from cloud";
                return;
            }

            const CORBA::ULong length = events.length();
            for (CORBA::ULong i = 0; i < length; ++i)
            {
                Notification::Event ev = events[i];

                const InfraServer::SItemStatus* stat = nullptr;
                ev.Body >>= stat;

                if (stat && stat->Status == InfraServer::S_Changed)
                {
                    std::string serviceName(stat->ItemName.in());
                    if (std::string::npos != serviceName.find(CLOUD_CLIENT_REFERENCE))
                    {
                        m_listener->OnConfigChanged();
                    }
                }
            }
        }

    private:

        IEventListener* m_listener;
        NCorbaHelpers::WPContainer m_container;

        std::unique_ptr<EventsAndMobileApiClient> m_client;
    };

    typedef NCorbaHelpers::CWeakPtr<CEventConsumerSink> PWeakEventConsumerSink;
    typedef NCorbaHelpers::CAutoPtr<CEventConsumerSink> PEventConsumerSink;

    class CPushContentImpl
        : public NHttpImpl::CBasicServletImpl
        , public IEventListener
    {
        DECLARE_LOGGER_HOLDER;

    public:
        CPushContentImpl(NCorbaHelpers::IContainer* c, const char* globalObjectName, const NWebGrpc::PGrpcManager grpcManager)
            : m_container(c)
            , m_contentContainer(c->CreateContainer(CLOUD_SERVLET, NCorbaHelpers::IContainer::EA_Advertise))
            , m_sink(new CEventConsumerSink(c->GetLogger(), this, c))
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);

            NNativeBL::NGrpcNotification::FilterBuilder filter;
            filter.Include("", axxonsoft::bl::events::ET_CloudBindingChanged);
            m_nodeEventSubscription.reset(new NNativeBL::CNodeEventSubscription(GET_LOGGER_PTR, filter, std::bind(&CPushContentImpl::processEvents, this, std::placeholders::_1)));

            InitConfigChannel(globalObjectName);
            InitCloud(c);
        }

        virtual void Get(const PRequest req, PResponse resp)
        {
            try
            {
                NCorbaHelpers::PContainer cont = m_container;
                if (!cont)
                {
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                if (!InitCloud(cont.Get()))
                {
                    _wrn_ << "Currently cloud info is not available";
                    Error(resp, IResponse::NotFound);
                    return;
                }

                std::string url(m_rh->GetCloudUrl());
                CloudClient::ConnectionInfo_var ci = m_rh->GetConnectionInfo();
                _log_ << "Cloud URL: " << url;

                Json::Value responseObject(Json::objectValue);
                responseObject["url"] = ci->CloudUrl.in();
                responseObject["domainId"] = ci->Id.in();

                NPluginUtility::SendText(req, resp, responseObject.toStyledString());
            }
            catch (const std::exception& e)
            {
                _err_ << "Std exception: " << e.what();
                Error(resp, IResponse::InternalServerError);
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "CORBA exception: " << e._info();
                Error(resp, IResponse::InternalServerError);
            }
        }

        virtual void Post(const PRequest req, PResponse resp)
        {
            std::vector<uint8_t> body(req->GetBody());
            std::string bodyContent(reinterpret_cast<const char*>(&body[0]), body.size());

            std::ostringstream ss;

            ss << "Cloud::Post():\n";
            ss << req->GetQuery() << "\n";

            for (auto header : req->GetHeaders())
            {
                ss << header.second.Name << ":" << header.second.Value << "\n";
            }

            ss << "\n";
            ss << bodyContent;

            _dbg_ << ss.str(); 

            ECluodMode mode = EUNKNOWN;

            if (Match(req->GetPathInfo(), Mask("subscription")))
                mode = ESUBSCRIBTION;
            else if (Match(req->GetPathInfo(), Mask("test")))
                mode = ETEST;
            else if (Match(req->GetPathInfo(), Mask("snooze")))
                mode = ESNOOZE;
            else if (Match(req->GetPathInfo(), Mask("clear")))
                mode = ECLEAR;

            long code = INVALID_RESPONSE_CODE;
            try
            {
                NCorbaHelpers::PContainer cont = m_container;
                if (!cont)
                {
                    Error(resp, IResponse::InternalServerError);
                    return;
                }

                if (!InitCloud(cont.Get()))
                {
                    _wrn_ << "Currently cloud info is not available";
                    Error(resp, IResponse::NotFound);
                    return;
                }

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

                switch (mode)
                {
                case ESUBSCRIBTION:
                {
                    _log_ << "CloudPlugin: subscription json :" << json;
                    const IRequest::AuthSession& as = req->GetAuthSession();
                    if (!as.data.first)
                    {
                        Error(resp, IResponse::InternalServerError);
                        return;
                    }
                    code = procSubscription(json, as.user, req);
                    break;
                }
                case ETEST:
                    _log_ << "CloudPlugin: test json :" << json;
                    code = m_sink->SendTestMessage(json);
                    break;
                case ESNOOZE:
                {
                    _log_ << "CloudPlugin: snooze json :" << json;
                    const IRequest::AuthSession& as = req->GetAuthSession();
                    if (!as.data.first)
                    {
                        Error(resp, IResponse::InternalServerError);
                        return;
                    }

                    json["vmsLogin"] = as.user;
                    code = m_sink->SendSnoozeMessage(json);
                    break;
                }
                case ECLEAR:
                {
                    _log_ << "CloudPlugin: clear json :" << json;
                    const IRequest::AuthSession& as = req->GetAuthSession();
                    if (!as.data.first)
                    {
                        Error(resp, IResponse::InternalServerError);
                        return;
                    }

                    json["vmsLogin"] = as.user;
                    code = m_sink->SendClearMessage(json);
                    break;
                }
                default:
                    break;
                };
                
            }
            catch (const Json::LogicError& e)
            {
                _err_ << e.what();
                Error(resp, IResponse::BadRequest);
                return;
            }

            Error(resp, (NHttp::IResponse::EStatus)code);
        }

        virtual void Delete(const PRequest req, PResponse resp)
        {
            std::string device;

            if (!Match(req->GetPathInfo(), Mask("subscription") / Token(device)))
            {
                _err_ << "Failed to unsubscribe from push notifications";
                Error(resp, IResponse::BadRequest);
                return;
            }

            const IRequest::AuthSession& as = req->GetAuthSession();
            long statusCode = m_sink->Unsubscribe(device, as.user);
            Error(resp, (INVALID_RESPONSE_CODE == statusCode) ? IResponse::InternalServerError : IResponse::OK);
        }

        virtual void OnConfigChanged()
        {
            NCorbaHelpers::PContainer cont = m_container;
            if (cont)
            {
                if (CORBA::is_nil(m_rh))
                {
                    _log_ << "Cloud reference is empty. Try to get new reference";
                    m_rh = NCorbaHelpers::ResolveServant<CloudClient::RegHelper>(cont.Get(), "CloudClient.0/RegHelper");
                }
                if (UpdateCloudReference(cont.Get(), m_rh))
                    return;
            }

            _log_ << "Cloud is unavailable";
        }

    private:
        enum ECluodMode {
            EUNKNOWN,
            ESUBSCRIBTION,
            ETEST,
            ESNOOZE,
            ECLEAR
        };

        void InitConfigChannel(const char* globalObjectName)
        {
            try
            {
                auto id = NCommonNotification::MakeEventConsumerAuxUid(globalObjectName, CONFIG_EVENT_RECEIVER);
                std::unique_ptr<NCommonNotification::CEventConsumer> configConsumer(
                    NCommonNotification::CreateEventConsumerServantNamed(m_contentContainer.Get(), id.c_str(), m_sink.Get()));
                m_contentContainer->ActivateServant(configConsumer.get(), CONFIG_EVENT_RECEIVER);
                // Fine-graned subscription
                NCommonNotification::FilterBuilder builder;
                builder.Include<InfraServer::SItemStatus>(CLOUD_CLIENT_REFERENCE); // Current config change state
                configConsumer->SetFilter(builder.Get());
                configConsumer->SetSubscriptionAddress("ExecutionManager/EventSupplier");
                configConsumer.release();
            }
            catch (const CORBA::Exception&)
            {
                _err_ << "Unable to activate event channel";
            }
        }

        bool InitCloud(NCorbaHelpers::IContainer *c)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            try
            {
                if (CORBA::is_nil(m_rh))
                {
                    CloudClient::RegHelper_var rh = NCorbaHelpers::ResolveServant<CloudClient::RegHelper>(c, "CloudClient.0/RegHelper");
                    if (UpdateCloudReference(c, rh))
                    {
                        m_rh = rh;
                        return true;
                    }
                }
                else
                    return true;
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "InitCloud failed. Reason: " << e._info();
            }
            return false;
        }

        bool UpdateCloudReference(NCorbaHelpers::IContainer* c, CloudClient::RegHelper_ptr rh)
        {
            try
            {
                if (!CORBA::is_nil(rh))
                {
                    auto config = GetDefaultApiClientConfig(c);
                    m_sink->SetCloudReference(config);
                    return true;
                }
            }
            catch (const CORBA::Exception& e)
            {
                _err_ << "UpdateCloudReference failed: CORBA::Exception - " << e._info();
            }
            catch (const std::exception& e)
            {
                _err_ << "UpdateCloudReference failed: std::exception - " << e.what();
            }
            return false;
        }

        std::int32_t procSubscription(Json::Value& message, const std::string &vmsLogin, const PRequest req)
        {
            return m_sink->SendSubscriptionMessage(message, vmsLogin);
        }

        void processEvents(const axxonsoft::bl::events::Events& events)
        {
            using namespace axxonsoft::bl::events;
            for (const auto& event : events.items())
            {
                if (event.event_type() == ET_CloudBindingChanged)
                    return OnConfigChanged();
            }
        }

        NCorbaHelpers::WPContainer m_container;
        NCorbaHelpers::PContainerNamed m_contentContainer;
        PEventConsumerSink m_sink;
        std::unique_ptr<NNativeBL::CNodeEventSubscription> m_nodeEventSubscription;
        CloudClient::RegHelper_var m_rh;

        std::mutex m_mutex;
    };
}

namespace NHttp
{
IServlet* CreateCloudServlet(NCorbaHelpers::IContainer* c, const char* globalObjectName, const NWebGrpc::PGrpcManager grpcManager)
{
    return new CPushContentImpl(c, globalObjectName, grpcManager);
}
}
