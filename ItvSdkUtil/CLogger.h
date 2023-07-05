#if !defined(ITVSDKUTIL_CLOGGER_H)
#define ITVSDKUTIL_CLOGGER_H

#include <ItvSdk/include/IErrorService.h>
#include <Logging/log2.h>

class CLogger : public ITV8::ILogger
{
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::ILogger)
    ITV8_END_CONTRACT_MAP()

public:
    CLogger(DECLARE_LOGGER_ARG, const char* name);

// ITV8::ILogger implementation
public:
    // Returns current logging level.
    virtual ITV8::uint32_t GetLogLevel() const;

    // Writes simple message to log.
    virtual void Log(ITV8::uint32_t level, const char* message);

    // Writes message to log with extended information about place where
    // error occurred.
    virtual void Log(ITV8::uint32_t level, const char* file, ITV8::uint32_t line,
        const char* function, const char* message);

private:
    static int ConvertToNgpLevel(ITV8::uint32_t level);

private:
    std::string m_name;
    DECLARE_LOGGER_HOLDER;
};
#endif // ITVSDKUTIL_CLOGGER_H
