#include <string>
#include <boost/format.hpp>

#include "ItvSdkUtil.h"
#include "CLogger.h"

CLogger::CLogger(DECLARE_LOGGER_ARG, const char* name)
{
    if(0 != name)
    {
        m_name = name;
    }

    INIT_LOGGER_HOLDER;
}

// Returns current logging level.
ITV8::uint32_t CLogger::GetLogLevel() const
{
    int ngpMaxLevel = ngp_Logger_Ptr_->GetMaxLevel();

    if(ngpMaxLevel <=10)
        return ITV8::LOG_ERROR;
    if(ngpMaxLevel <=30)
        return ITV8::LOG_WARNING;
    if(ngpMaxLevel <=40)
        return ITV8::LOG_INFO;

    return ITV8::LOG_DEBUG;
}

// Writes simple message to log.
void CLogger::Log(ITV8::uint32_t level, const char* message)
{
    int ngpLevel = ConvertToNgpLevel(level);

    if( !ngp_Logger_Ptr_->CheckLevel(ngpLevel) )
    {
        ngp_Logger_Ptr_->WriteLine(ngpLevel, message, false);
    }
}

int CLogger::ConvertToNgpLevel(ITV8::uint32_t level)
{
    switch(level)
    {
    case ITV8::LOG_DEBUG:
        return NLogging::LEVEL_DEBUG;
    case ITV8::LOG_INFO:
        return NLogging::LEVEL_INFO;
    case ITV8::LOG_WARNING:
        return NLogging::LEVEL_WARNING;
    case ITV8::LOG_ERROR:
        return NLogging::LEVEL_ERROR;
    default:
        return NLogging::LEVEL_NORMAL;
    }
}

// Writes message to log with extended information about place where
// error occurred.
void CLogger::Log(ITV8::uint32_t level, const char* file, ITV8::uint32_t line,
                 const char* function, const char* message)
{

    int ngpLevel = ConvertToNgpLevel(level);
    if( !ngp_Logger_Ptr_->CheckLevel(ngpLevel) )
    {
        std::string msg = boost::str((boost::format("%1%(%2%): %3% %4% : %5%") 
            %(!file?"":file)%line%((ngpLevel==NLogging::LEVEL_ERROR)?"ERROR:":"")
			%(!function?"":function)%(!message?"":message)));

        ngp_Logger_Ptr_->WriteLine(ngpLevel, msg, false);
    }
}

namespace ITVSDKUTILES
{

    ITVSDKUTILES_API ILoggerPtr CreateLogger(DECLARE_LOGGER_ARG, const char* name)
    {
        ILoggerPtr logger(new CLogger(GET_LOGGER_PTR, name));
        return logger;
    }
}

