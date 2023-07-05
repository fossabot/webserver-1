#ifndef STRING_CONTEXT_H__
#define STRING_CONTEXT_H__

#include <boost/shared_ptr.hpp>
#include <boost/optional/optional.hpp>
#include <boost/smart_ptr/enable_shared_from_this.hpp>

#include <HttpServer/HttpResponse.h>

namespace boost { namespace filesystem { class path; } }

namespace NMMSS
{
    class ISample;
}

namespace NContext
{
    class ISendContext : public boost::enable_shared_from_this<ISendContext>
    {
    public:
        virtual ~ISendContext() {}

        virtual void ScheduleWrite() = 0;
        virtual void WriteHandler(boost::system::error_code) = 0;
    };

    typedef boost::shared_ptr<ISendContext> PSendContext;

    typedef boost::function1<void, boost::system::error_code> FDoneCallback;

    ISendContext* CreateStringContext(NHttp::PResponse, const std::string&, FDoneCallback = [](boost::system::error_code) {});
    ISendContext* CreateSampleContext(NHttp::PResponse, NMMSS::ISample*, FDoneCallback);
    ISendContext* CreateMultipartContext(NHttp::PResponse, NMMSS::ISample*, FDoneCallback);
    ISendContext* CreateFileContext(DECLARE_LOGGER_ARG, NHttp::PResponse, const char* const /*presentationName*/,
        const boost::filesystem::path& /*filePath*/, FDoneCallback);
}

#endif // STRING_CONTEXT_H__
