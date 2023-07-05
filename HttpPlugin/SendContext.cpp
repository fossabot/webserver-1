#include <fstream>

#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/system/system_error.hpp>
#include <boost/date_time/posix_time/ptime.hpp>

#include <HttpServer/HttpServer.h>
#include <ConnectionBroker.h>
#include "SendContext.h"
#include "Constants.h"
#include "../PtimeFromQword.h"

#include <CorbaHelpers/Unicode.h>

using namespace NHttp;
using namespace NContext;

namespace
{
    const char* const CONTENT_TYPE = "Content-Type: image/jpeg";
    const char *const SAMPLE_TIMESTAMP = "X-Video-Original-Time: ";

    class CStringContext : public ISendContext
    {
        NHttp::PResponse            m_response;
        std::string                 m_stream;
        FDoneCallback               m_cb;

    public:
        CStringContext(NHttp::PResponse resp, const std::string& in, FDoneCallback cb)
            : m_response(resp)
            , m_stream(in)
            , m_cb(cb)
        {
        }

        void ScheduleWrite()
        {
            try
            {
                m_response->AsyncWrite(
                    &m_stream[0], m_stream.size()
                    , boost::bind(
                    &ISendContext::WriteHandler, shared_from_this(), _1)
                    );
            }
            catch (const boost::system::system_error& e)
            {
                WriteHandler(e.code());
            }
        }

    private:
        void WriteHandler(boost::system::error_code ec)
        {
            m_cb(ec);
        }
    };

    class CSampleContext : public ISendContext
    {
        PResponse                   m_response;
        NMMSS::PSample              m_sample;
        FDoneCallback               m_cb;

    public:
        CSampleContext(PResponse resp, NMMSS::ISample* s, FDoneCallback cb)
            : m_response(resp)
            , m_sample(s, NCorbaHelpers::ShareOwnership())
            , m_cb(cb)
        { }

        void ScheduleWrite()
        {
            IResponse::TConstBufferSeq buffs;
            buffs.push_back(boost::asio::buffer(m_sample->GetBody(), m_sample->Header().nBodySize));

            try
            {
                m_response->AsyncWrite(
                    buffs
                    , boost::bind(
                        &ISendContext::WriteHandler, shared_from_this(), _1)
                );
            }
            catch (const boost::system::system_error& e)
            {
                WriteHandler(e.code());
            }
        }

    private:
        void WriteHandler(boost::system::error_code ec)
        {
            m_cb(ec);
        }
    };

    class CMultipartContext: public ISendContext
    {
        PResponse                   m_response;
        NMMSS::PSample              m_sample;
        FDoneCallback               m_cb;

    public:
        CMultipartContext(PResponse resp, NMMSS::ISample* s, FDoneCallback cb)
            : m_response(resp)
            , m_sample(s, NCorbaHelpers::ShareOwnership())
            , m_cb(cb)
        { }

        void ScheduleWrite()
        {
            size_t bodylen = m_sample->Header().nBodySize;
            const std::string ts = boost::posix_time::to_iso_string(
                NMMSS::PtimeFromQword(m_sample->Header().dtTimeBegin));

            std::stringstream s;
            s                                << CR << LF 
                << BOUNDARY                  << CR << LF 
                << CONTENT_TYPE              << CR << LF 
                << CONTENT_LENGTH << bodylen << CR << LF
                << SAMPLE_TIMESTAMP << ts    << CR << LF 
                                             << CR << LF;

            IResponse::TConstBufferSeq buffs;
            m_headerBuffer.assign(s.str());
            buffs.push_back(boost::asio::buffer(m_headerBuffer.c_str(), m_headerBuffer.size()));
            buffs.push_back(boost::asio::buffer(m_sample->GetBody(), bodylen));

            try
            {
                m_response->AsyncWrite(
                    buffs
                    ,boost::bind(
                        &ISendContext::WriteHandler, shared_from_this(), _1)
                );
            }
            catch(const boost::system::system_error& e)
            {
                WriteHandler(e.code());
            }
        }

    private:
        void WriteHandler(boost::system::error_code ec)
        {
            m_cb(ec);
        }

        std::string m_headerBuffer;
    };

    class CFileContext : public ISendContext
    {
        DECLARE_LOGGER_HOLDER;

        static std::uint32_t        CHUNK_SIZE;
        PResponse                   m_response;
        const std::string           m_presentationName;
        const boost::filesystem::path m_filePath;
        FDoneCallback               m_cb;
        std::vector < char >        m_buffer;
        std::ifstream               m_fs;

    public:
        CFileContext(DECLARE_LOGGER_ARG, PResponse resp, const char* const presentationName,
            const boost::filesystem::path& filePath, FDoneCallback cb)
            : m_response(resp)
            , m_presentationName(presentationName)
            , m_filePath(filePath)
            , m_cb(cb)
        {
            INIT_LOGGER_HOLDER;
            m_buffer.reserve(CHUNK_SIZE);
        }

        void ScheduleWrite()
        {
            m_fs.open(m_filePath.c_str(), std::ios::binary | std::ios::in);
            if (!m_fs.good())
            {
                Error(m_response, IResponse::NotFound);
                return;
            }

            std::string contentDisposition("attachment; filename=");
            contentDisposition.append(m_presentationName);
            NHttp::SHttpHeader contentDispositionHeader("Content-Disposition", contentDisposition);
            NHttp::SHttpHeader acceptRangeHeader("Accept-Range", "bytes");

            std::string extension(m_presentationName.substr(m_presentationName.find_last_of('.') + 1));
            
            m_response->SetStatus(IResponse::OK);
            m_response << ContentLength(static_cast<std::size_t>(boost::filesystem::file_size(m_filePath)))
                << ContentType(NHttp::GetMIMETypeByExt(extension.c_str()))
                << CacheControlNoCache()
                << contentDispositionHeader
                << acceptRangeHeader;

            m_response->FlushHeaders();

            ReadChunk();
        }

    private:
        void ReadChunk()
        {
            m_fs.read(&m_buffer[0], CHUNK_SIZE);
            size_t count = static_cast<std::size_t>(m_fs.gcount());
            m_response->AsyncWrite(
                &m_buffer[0], count,
                boost::bind(
                &ISendContext::WriteHandler, shared_from_this(), _1)
            );
        }
        void WriteHandler(boost::system::error_code ec)
        {
            if (!m_fs)
                m_cb(ec);
            else
                ReadChunk();
        }
    };
    std::uint32_t CFileContext::CHUNK_SIZE = 65536;
}

namespace NContext
{
    ISendContext* CreateStringContext(NHttp::PResponse response, const std::string& data, FDoneCallback cb)
    {
        return new CStringContext(response, data, cb);
    }

    ISendContext* CreateSampleContext(NHttp::PResponse response, NMMSS::ISample* sample, FDoneCallback cb)
    {
        return new CSampleContext(response, sample, cb);
    }

    ISendContext* CreateMultipartContext(NHttp::PResponse response, NMMSS::ISample* sample, FDoneCallback cb)
    {
        return new CMultipartContext(response, sample, cb);
    }

    ISendContext* CreateFileContext(DECLARE_LOGGER_ARG, NHttp::PResponse response, const char* const presentationName,
        const boost::filesystem::path& filePath, FDoneCallback cb)
    {
        return new CFileContext(GET_LOGGER_PTR, response, presentationName, filePath, cb);
    }
}
