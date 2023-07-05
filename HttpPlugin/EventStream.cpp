#include <mutex>
#include <set>

#include "EventStream.h"

#include <json/json.h>

namespace
{
    const char CRLF[] = { '\r', '\n' };

    class CEventStream : public NHttp::IEventStream
    {
        struct SEvent
        {
            SEvent()
                : type("event: ")
                , data("data: ")
            {}

            std::string type;
            std::string data;
        };
        typedef std::shared_ptr<SEvent> PEvent;

    public:
        CEventStream(DECLARE_LOGGER_ARG)
            : m_processing(false)
        {
            INIT_LOGGER_HOLDER;
        }

        ~CEventStream()
        {
            _log_ << "CEventStream dtor";
        }

        void AddClient(NHttp::PResponse r) override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_responses.insert(r);
        }

        void RemoveClient(NHttp::PResponse r) override
        {
        }

        bool HasClients() override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return !m_responses.empty();
        }

        void SendEvent(const char* const type, const Json::Value& value) override
        {
            PEvent ev = std::make_shared<SEvent>();
            ev->type.append("timestamp");
            ev->data.append(m_writer.write(value));

            std::unique_lock<std::mutex> lock(m_dataMutex);
            if (!m_processing)
            {
                m_processing = true;
                lock.unlock();

                sendData(ev);
            }
            else
            {
                m_eventsToSend.push_back(ev);
            }            
        }

    private:
        void sendData(PEvent ev)
        {
            std::vector<boost::asio::const_buffer> buffers;
            buffers.push_back(boost::asio::buffer(ev->type));
            buffers.push_back(boost::asio::buffer(CRLF));
            buffers.push_back(boost::asio::buffer(ev->data));
            buffers.push_back(boost::asio::buffer(CRLF));
            buffers.push_back(boost::asio::buffer(CRLF));

            std::unique_lock<std::mutex> lock(m_mutex);
            TResponses::const_iterator it1 = m_responses.begin(), it2 = m_responses.end();
            for (; it1 != it2; ++it1)
            {
                try
                {
                    (*it1)->AsyncWrite(buffers, std::bind(&CEventStream::handle_send,
                        shared_from_base<CEventStream>(),
                        ev, *it1, std::placeholders::_1));
                }
                catch (...)
                {
                    _wrn_ << "Failed to send timestamp info";
                }
            }
        }

        void processNextEvent()
        {
            std::unique_lock<std::mutex> lock(m_dataMutex);
            if (m_eventsToSend.empty())
            {
                m_processing = false;
                return;

            }

            PEvent ev = m_eventsToSend.front();
            m_eventsToSend.pop_front();

            if (ev)
                sendData(ev);
        }

        void handle_send(PEvent, NHttp::PResponse, boost::system::error_code ec)
        {
            if (!ec)
            {
                processNextEvent();
            }
        }

        DECLARE_LOGGER_HOLDER;

        std::mutex m_mutex;
        typedef std::set<NHttp::PResponse> TResponses;
        TResponses m_responses;

        Json::FastWriter m_writer;

        std::mutex m_dataMutex;
        bool m_processing;
        std::deque<PEvent> m_eventsToSend;
    };
}

namespace NHttp
{
    PEventStream CreateEventStream(DECLARE_LOGGER_ARG)
    {
        return PEventStream(new CEventStream(GET_LOGGER_PTR));
    }
}
