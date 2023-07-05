#include <mutex>
#include <functional>
#include <boost/asio.hpp>

#include "WebSocketSession.h"

using namespace NHttp;

namespace
{
    const uint8_t FIRST_BIT_MASK = 0x80;
    const uint8_t OPCODE_MASK = 0x0F;
    const uint8_t PAYLOAD_MASK = 0x7F;

    const uint8_t TWO_BYTE_LEN = 126;
    const uint8_t EIGHT_BYTE_LEN = 127;

    const uint8_t MASK_SIZE = 4;

    const uint8_t CLOSE_CODE_LENGTH = 0x2;

    const uint64_t LOW_DATA_THRESHOLD = 125;
    const uint64_t HIGH_DATA_THRESHOLD = 65536;

    const uint16_t SOURCE_NAME_LEN = 2;

    const uint32_t PING_TIMEOUT = 30;

    enum class EWebSocketStatusCode
    {
        ENormal = 1000,
        EGoAway,
        EProtocolError,
        EWrongType,
        EReserved,
        ENoCode,
        ENoCodeAbnormally,
        EWrongData,
        EWrongPolicy,
        EMessageTooBig,
        EExtensionNegatiationError,
        EUnexpected,
        ETlsHandshakeFailure = 1015
    };

    struct SFragmentLength
    {
        uint8_t len;
        union
        {
            uint16_t len2;
            uint64_t len8;
        } extLen;
    };

#pragma pack(push, 1)
    struct SFragmentHeader
    {
        uint8_t code;
        SFragmentLength len;
        uint32_t mask;
    };
#pragma pack(pop)

    class CWebSocketSession : public NWebWS::IWebSocketSession
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CWebSocketSession(DECLARE_LOGGER_ARG, const PRequest req, PResponse resp, NWebWS::OnCloseCallback_t ccb)
            : m_req(req)
            , m_resp(resp)
            , m_onClose(ccb)
            , m_codeRead(false)
            , m_lenRead(false)
            , m_lenBytesLeftToRead(0)
            , m_maskBytesLeftToRead(MASK_SIZE)
            , m_payloadBytesLeftToRead(0)
            //, m_pingTimer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
        {
            INIT_LOGGER_HOLDER;
        }

        ~CWebSocketSession()
        {
            _log_ << "CWebSocketSession dtor";
            if (m_client)
                m_client->Stop();
        }

        void Start(NWebWS::PWebSocketClient client) override
        {
            m_client = client;
            if (m_client)
                m_client->Init();
            //SetPingTimer();
            ReadMoreData();
        }

        void SendText(const std::string& msg) override
        {
            sendMessage(msg.begin(), msg.end(), NWebWS::ETextFrame);
        }

        void SendBinary(std::uint8_t* data, std::size_t dataSize) override
        {
            sendMessage(data, data + dataSize, NWebWS::EBinaryFrame);
        }

        void SendData(NWebWS::WSData data) override
        {
            sendData(data);
        }

        void SendError() override
        {
            SendCloseMessage((uint16_t)EWebSocketStatusCode::EWrongData);
        }

        void SetWriteCallback(NWebWS::OnWriteCallback_t wc) override
        {
            m_onWrite = wc;
        }
        
    private:
        template <typename TIterator>
        void sendMessage(TIterator b, TIterator e, NWebWS::EOpCode frameType)
        {
            NWebWS::WSData message = std::make_shared<NWebWS::WSDataPresentation>();

            message->push_back(FIRST_BIT_MASK | frameType);

            uint8_t* dataPtr = nullptr;
            std::size_t size = std::distance(b, e);
          
            if (size <= LOW_DATA_THRESHOLD)
            {
                message->push_back((uint8_t)size);
            }
            else if (size <= HIGH_DATA_THRESHOLD)
            {
                message->push_back((uint8_t)126);
                uint16_t dataSize = htons((uint16_t)size);
                dataPtr = (uint8_t*)&dataSize;
                message->insert(message->end(), dataPtr, dataPtr + sizeof(uint16_t));
            }
            else
            {
                message->push_back((uint8_t)127);
                uint64_t dataSize = NWebWS::hton64(size);
                dataPtr = (uint8_t*)&dataSize;
                message->insert(message->end(), dataPtr, dataPtr + sizeof(uint64_t));
            }

            message->insert(message->end(), b, e);

            sendData(message);
        }

        void sendData(NWebWS::WSData message)
        {
            try
            {
                m_resp->AsyncWrite(&message->operator[](0), message->size(),
                    std::bind(&CWebSocketSession::write_handler, shared_from_base<CWebSocketSession>(),
                    message, std::placeholders::_1));
            }
            catch (...)
            {
            }
        }

        void read_handler(boost::system::error_code ec, const char* data, size_t dataSize)
        {
            if (!ec)
            {

                while (ProcessFrame(data, dataSize) && (0 < dataSize))
                {
                    //_dbg_ << "Left " << dataSize << " bytes to process";
                }
                ReadMoreData();
            }
            else
            {
                _err_ << "Cannot read data from websocket. Error code: " << ec.value() << ". Error message: " << ec.message();
                SendCloseMessage((uint16_t)EWebSocketStatusCode::EProtocolError);
            }
        }

        void write_handler(NWebWS::WSData data, boost::system::error_code ec)
        {
            if (ec)
            {
                _wrn_ << "Cannot write data to websocket. Error code: " << ec.value() << ". Error message: " << ec.message();
            }
            else
            {
                if (m_onWrite)
                    m_onWrite();
            }
        }

        bool ReadCode(const char*& data, size_t& dataSize)
        {
            //_dbg_ << "Read code process " << dataSize << " bytes";
            if (0 == dataSize)
                return false;

            if (!m_codeRead)
            {
                m_header.code = *data++;

                //_dbg_ << "code = " << (int)m_header.code;
                --dataSize;
                m_codeRead = true;
            }
            return true;
        }

        bool ReadLength(const char*& data, size_t& dataSize)
        {
            //_dbg_ << "Read length process " << dataSize << " bytes";
            if (0 == dataSize)
                return false;

            if (!m_lenRead)
            {
                m_header.len.len = *data++;
                --dataSize;

                uint8_t frameLen = m_header.len.len & PAYLOAD_MASK;

                if (frameLen > TWO_BYTE_LEN)
                {
                    m_lenBytesLeftToRead = 8;
                }
                else if (frameLen == TWO_BYTE_LEN)
                {
                    m_lenBytesLeftToRead = 2;
                }

                m_lenRead = true;
            }

            if (0 != m_lenBytesLeftToRead && 0 != dataSize)
            {
                uint8_t frameLen = m_header.len.len & PAYLOAD_MASK;

                size_t needWrite = std::min<size_t>(m_lenBytesLeftToRead, dataSize);
                uint8_t lenOffset = (frameLen == TWO_BYTE_LEN ? 2 : 8) - m_lenBytesLeftToRead;

                //_dbg_ << "Need write " << needWrite << " bytes at offset " << lenOffset;

                memcpy(&m_header.len.extLen + lenOffset, data, needWrite);
                m_lenBytesLeftToRead -= needWrite;
                dataSize -= needWrite;
                data += needWrite;
            }
            return true;
        }

        bool ReadMask(const char*& data, size_t& dataSize)
        {
            //_dbg_ << "Read mask process " << dataSize << " bytes";
            if (0 == dataSize)
                return false;

            if (0 != m_maskBytesLeftToRead)
            {
                size_t needWrite = std::min<size_t>(m_maskBytesLeftToRead, dataSize);
                uint8_t offset = MASK_SIZE - m_maskBytesLeftToRead;

                //_dbg_ << "Need write " << needWrite << " bytes at offset " << offset;

                memcpy(&m_header.mask + offset, data, needWrite);
                m_maskBytesLeftToRead -= needWrite;
                dataSize -= needWrite;
                data += needWrite;
            }
            return true;
        }

        bool ReadPayload(const char*& data, size_t& dataSize)
        {
            //_dbg_ << "Read payload process " << dataSize << " bytes";
            if (0 == dataSize)
                return false;

            if (0 != m_payloadBytesLeftToRead)
            {
                size_t needWrite = std::min<size_t>((size_t)m_payloadBytesLeftToRead, dataSize);

                m_maskedMessage.insert(m_maskedMessage.end(), data, data + needWrite);
                m_payloadBytesLeftToRead -= needWrite;
                dataSize -= needWrite;
                data += needWrite;
            }
            return 0 == m_payloadBytesLeftToRead ? true : false;
        }

        bool SelectProcessingMode(NWebWS::EOpCode opCode)
        {
            if (opCode == NWebWS::EContinuation && m_currentOpCode == NWebWS::EUnknown)
            {
                _err_ << "Received frame has wrong type";
                Reset();
                ResetFrame();
                SendCloseMessage((uint16_t)EWebSocketStatusCode::EWrongType);
                return false;
            }
            else if (opCode == NWebWS::EContinuation)
            {
                //_dbg_ << "Continuation frame";
                return SelectProcessingMode(m_currentOpCode);
            }

            m_currentOpCode = opCode;

            if (FIRST_BIT_MASK & m_header.code)
            {
                switch (opCode)
                {
                case NWebWS::ETextFrame:
                    //_dbg_ << "Text frame";
                    ProcessTextFrame();
                    break;
                case NWebWS::EBinaryFrame:
                    _wrn_ << "Currently we dont work with binary frames. Silently swallow it";
                    break;
                case NWebWS::EClose:
                    _dbg_ << "Close frame";
                    ProcessCloseFrame();
                    break;
                case NWebWS::EPing:
                    _dbg_ << "Ping frame";
                    SendPongMessage();
                    break;
                case NWebWS::EPong:
                    _wrn_ << "Currently we dont work with pong frames. Silently swallow it";
                    break;
                default:
                    break;
                }

                Reset();
            }
            else
            {
                // https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API/Writing_WebSocket_servers
                // The FIN bit tells whether this is the last message in a series. If it's 0, then the server keeps listening for more parts of the message; otherwise, the server should consider the message delivered..

                //_dbg_ << "SelectProcessingMode::F == 0";
                UnmaskFrame();
            }

            ResetFrame();

            return true;
        }

        void ProcessTextFrame()
        {
            //_dbg_ << "Process text frame";

            UnmaskFrame();

            std::string content(reinterpret_cast<const char*>(&m_fragmentedMessage[0]), m_fragmentedMessage.size());
            //_dbg_ << "JSON content: " << content;

            Reset();
            ResetFrame();

            if (m_client)
            {
                m_client->OnMessage(std::move(content));
            }
        }

        void ProcessCloseFrame()
        {
            UnmaskFrame();

            uint16_t statusCode = (m_fragmentedMessage[0] << 8) | m_fragmentedMessage[1];
            //_dbg_ << "Close status code: " << statusCode;

            SendCloseMessage(statusCode);
        }

        void SendCloseMessage(uint16_t statusCode)
        {
            //_dbg_ << "Send close frame";

            NWebWS::WSData message = std::make_shared<NWebWS::WSDataPresentation>();
            message->push_back(FIRST_BIT_MASK | NWebWS::EClose);
            message->push_back(CLOSE_CODE_LENGTH);

            uint16_t netCode = htons(statusCode);
            uint8_t* codePtr = (uint8_t*)&netCode;

            message->insert(message->end(), codePtr, codePtr + sizeof(uint16_t));

            sendData(message);

            if (m_onClose)
                m_onClose();
        }

        void SendPingMessage()
        {
            //_dbg_ << "Send ping frame";

            NWebWS::WSData message = std::make_shared<NWebWS::WSDataPresentation>();
            message->push_back(FIRST_BIT_MASK | NWebWS::EPing);
            message->push_back(0x00);

            sendData(message);
        }

        void SendPongMessage()
        {
            //_dbg_ << "Send pong frame";

            UnmaskFrame();

            NWebWS::WSData message = std::make_shared<NWebWS::WSDataPresentation>();
            message->push_back(FIRST_BIT_MASK | NWebWS::EPong);
            CopyMessageLength(message);
            message->insert(message->end(), m_fragmentedMessage.begin(), m_fragmentedMessage.end());

            sendData(message);
        }

        bool ProcessFrame(const char*& data, size_t& dataSize)
        {
            if (!ReadCode(data, dataSize) ||
                !ReadLength(data, dataSize) ||
                !ReadMask(data, dataSize))
            {
                return false;
            }

            CalculatePayloadLen();
            //_dbg_ << "Payload length: " << GetPayloadLen();

            if (!ReadPayload(data, dataSize))
            {
                _wrn_ << "ReadPayload: false";
                return false;
            }

            NWebWS::EOpCode opCode = static_cast<NWebWS::EOpCode>(m_header.code & OPCODE_MASK);
            
            return SelectProcessingMode(opCode);
        }

        void UnmaskFrame()
        {
            size_t len = m_maskedMessage.size();
            for (size_t i = 0; i < len; ++i)
                m_fragmentedMessage.push_back(m_maskedMessage[i] ^ ((char*)&(m_header.mask))[i % 4]);
        }

        void CopyMessageLength(NWebWS::WSData message)
        {
            uint8_t frameLen = m_header.len.len & PAYLOAD_MASK;
            message->push_back(frameLen);

            if (frameLen == TWO_BYTE_LEN)
            {
                uint16_t msgLen = m_header.len.extLen.len2;
                uint8_t* mgsPtr = (uint8_t*)&msgLen;
                message->insert(message->end(), mgsPtr, mgsPtr + sizeof(uint16_t));
            }
            else if (frameLen == EIGHT_BYTE_LEN)
            {
                uint64_t msgLen = m_header.len.extLen.len8;
                uint8_t* mgsPtr = (uint8_t*)&msgLen;
                message->insert(message->end(), mgsPtr, mgsPtr + sizeof(uint64_t));
            }
        }

        void ReadMoreData()
        {
            m_req->AsyncRead(std::bind(&CWebSocketSession::read_handler, shared_from_base<CWebSocketSession>(),
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        void ResetFrame()
        {
            m_maskedMessage.clear();
            m_codeRead = m_lenRead = false;
            m_lenBytesLeftToRead = 0;
            m_payloadBytesLeftToRead = 0;
            m_maskBytesLeftToRead = MASK_SIZE;
        }

        void Reset()
        {
            m_fragmentedMessage.clear();
            m_currentOpCode = NWebWS::EUnknown;
        }

        void CalculatePayloadLen()
        {
            if (0 == m_payloadBytesLeftToRead)
            {
                uint8_t frameLen = m_header.len.len & PAYLOAD_MASK;
                if (frameLen < TWO_BYTE_LEN)
                    m_dataLen = frameLen;
                else if (frameLen < EIGHT_BYTE_LEN)
                    m_dataLen = ntohs(m_header.len.extLen.len2);
                else
                    m_dataLen = NWebWS::ntoh64(m_header.len.extLen.len8);

                m_payloadBytesLeftToRead = m_dataLen;
            }
        }

        uint64_t GetPayloadLen() const
        {
            return m_dataLen;
        }

        /*void SetPingTimer()
        {
            m_pingTimer.expires_from_now(boost::posix_time::seconds(PING_TIMEOUT));
            m_pingTimer.async_wait(std::bind(&CWebSocketSession::handle_timeout,
                std::weak_ptr<CWebSocketSession>(shared_from_this()),
                std::placeholders::_1));
        }

        static void handle_timeout(std::weak_ptr<CWebSocketSession> c, const boost::system::error_code& error)
        {
            if (!error)
            {
                std::shared_ptr<CWebSocketSession> wss = c.lock();
                if (wss)
                {
                    wss->SendPingMessage();
                    wss->SetPingTimer();
                }
            }
        }*/

        const PRequest m_req;
        PResponse m_resp;

        NWebWS::OnCloseCallback_t m_onClose;
        NWebWS::OnWriteCallback_t m_onWrite;

        NWebWS::PWebSocketClient m_client;

        bool m_codeRead;
        bool m_lenRead;

        uint8_t m_lenBytesLeftToRead;
        uint8_t m_maskBytesLeftToRead;
        uint64_t m_payloadBytesLeftToRead;

        SFragmentHeader m_header;
        uint64_t m_dataLen;
        NWebWS::EOpCode m_currentOpCode;

        std::vector<uint8_t> m_maskedMessage;
        std::vector<uint8_t> m_fragmentedMessage;

        //boost::asio::deadline_timer m_pingTimer;
    };
}

namespace NWebWS
{
    PWebSocketSession CreateWebSocketSession(DECLARE_LOGGER_ARG, NHttp::PRequest req, NHttp::PResponse resp, OnCloseCallback_t ccb)
    {
        return PWebSocketSession(new CWebSocketSession(GET_LOGGER_PTR, req, resp, ccb));
    }

    uint64_t ntoh64(uint64_t input)
    {
        uint64_t rval;
        uint8_t *data = (uint8_t *)&rval;

        data[0] = (uint8_t)(input >> 56);
        data[1] = (uint8_t)(input >> 48);
        data[2] = (uint8_t)(input >> 40);
        data[3] = (uint8_t)(input >> 32);
        data[4] = (uint8_t)(input >> 24);
        data[5] = (uint8_t)(input >> 16);
        data[6] = (uint8_t)(input >> 8);
        data[7] = (uint8_t)(input >> 0);

        return rval;
    }

    uint64_t hton64(uint64_t input)
    {
        return (ntoh64(input));
    }
}
