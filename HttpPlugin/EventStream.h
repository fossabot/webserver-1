#ifndef EVENT_STREAM_H__
#define EVENT_STREAM_H__

#include <cstdint>
#include <memory>

#include <Logging/log2.h>
#include <HttpServer/HttpResponse.h>

namespace Json
{
    class Value;
}

namespace NHttp
{
    struct IEventStream : public std::enable_shared_from_this < IEventStream >
    {
        virtual ~IEventStream() {}

        virtual void AddClient(NHttp::PResponse) = 0;
        virtual void RemoveClient(NHttp::PResponse) = 0;
        virtual void SendEvent(const char* const, const Json::Value&) = 0;
        virtual bool HasClients() = 0;

        template <typename Derived>
        std::shared_ptr<Derived> shared_from_base()
        {
            return std::dynamic_pointer_cast<Derived>(shared_from_this());
        }
    };
    typedef std::shared_ptr<IEventStream> PEventStream;

    PEventStream CreateEventStream(DECLARE_LOGGER_ARG);
}

#endif // EVENT_STREAM_H__
