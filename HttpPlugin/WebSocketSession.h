#ifndef WEBSOCKET_SESSION_H__
#define WEBSOCKET_SESSION_H__

#include <memory>
#include <boost/enable_shared_from_this.hpp>

#include <Logging/log2.h>
#include <HttpServer/HttpRequest.h>
#include <HttpServer/HttpResponse.h>

namespace NWebWS
{
    using OnWriteCallback_t = std::function < void() > ;
    using OnMessageCallback_t = std::function<void(std::string&&)>;
    using OnCloseCallback_t = std::function<void()>;

    struct IWebSocketClient : public boost::enable_shared_from_this < IWebSocketClient >
    {
        virtual ~IWebSocketClient() {}

        virtual void Init() {}
        virtual void OnMessage(const std::string&) = 0;
        virtual void Stop() {}

        template <typename Derived>
        boost::shared_ptr<Derived> shared_from_base()
        {
            return boost::dynamic_pointer_cast<Derived>(shared_from_this());
        }
    };
    typedef boost::shared_ptr<IWebSocketClient> PWebSocketClient;
    typedef boost::weak_ptr<IWebSocketClient> WWebSocketClient;

    typedef std::vector<std::uint8_t> WSDataPresentation;
    typedef std::shared_ptr<WSDataPresentation> WSData;

    enum EOpCode
    {
        EContinuation = 0x0,
        ETextFrame = 0x1,
        EBinaryFrame = 0x2,
        EClose = 0x8,
        EPing = 0x9,
        EPong = 0xA,
        EUnknown = 0xFF
    };

    struct IWebSocketSession : public boost::enable_shared_from_this < IWebSocketSession >
    {
        virtual ~IWebSocketSession() {}

        virtual void Start(PWebSocketClient) = 0;
        virtual void SendText(const std::string&) = 0;
        virtual void SendBinary(std::uint8_t*, std::size_t) = 0;
        virtual void SendData(WSData) = 0;
        virtual void SendError() = 0;

        virtual void SetWriteCallback(OnWriteCallback_t) {}

        template <typename Derived>
        boost::shared_ptr<Derived> shared_from_base()
        {
            return boost::dynamic_pointer_cast<Derived>(shared_from_this());
        }
    };
    typedef boost::shared_ptr<IWebSocketSession> PWebSocketSession;
    typedef boost::weak_ptr<IWebSocketSession> WWebSocketSession;

    PWebSocketSession CreateWebSocketSession(DECLARE_LOGGER_ARG, NHttp::PRequest, NHttp::PResponse, OnCloseCallback_t);

    uint64_t ntoh64(uint64_t input);
    uint64_t hton64(uint64_t input);
}

#endif // WEBSOCKET_SESSION_H__
