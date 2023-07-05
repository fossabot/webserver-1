#include "Utility.h"
#include "TimeStampHelpers.h"

#include <boost/make_shared.hpp>

namespace IPINT30
{

// Class is used to wrap two handler into one. It is useful when we must wait while all settings are applied.
class AsyncActionHandlerWrapper : public ITV8::IAsyncActionHandler
{
public:
    AsyncActionHandlerWrapper(ITV8::IAsyncActionHandler* handler, ITV8::IAsyncActionHandler* waitHandler);

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IAsyncActionHandler)
    ITV8_END_CONTRACT_MAP()

private:
    virtual void Finished(IContract* source, ITV8::hresult_t code);

private:
    ITV8::IAsyncActionHandler*  m_handler;
    ITV8::IAsyncActionHandler*  m_waitHandler;
};

ITV8::IAsyncActionHandler* WrapAsyncActionHandler(ITV8::IAsyncActionHandler* handler, ITV8::IAsyncActionHandler* waitHandler)
{
    if (!waitHandler)
        return handler;

    SettingsHandler* settingshandler = ITV8::contract_cast<SettingsHandler>(waitHandler);
    return settingshandler ? settingshandler->WrapHandler(handler) : handler;
}

AsyncActionHandlerWrapper::AsyncActionHandlerWrapper(ITV8::IAsyncActionHandler* handler, ITV8::IAsyncActionHandler* waitHandler)
    : m_handler(handler)
    , m_waitHandler(waitHandler)
{
}

void AsyncActionHandlerWrapper::Finished(IContract* source, ITV8::hresult_t code)
{
    if (m_handler)
    {
        m_handler->Finished(source, code);
    }

    if (m_waitHandler)
    {
        m_waitHandler->Finished(source, code);
    }
}

SettingsHandler::SettingsHandler()
	: m_threadCount(0)
{
}

void SettingsHandler::Wait()
{
    boost::mutex::scoped_lock lock(m_conditionMutex);
    while (m_threadCount > 0)
    {
        m_condition.wait(lock);
    }
}

void SettingsHandler::Finished(ITV8::IContract* source, ITV8::hresult_t code)
{
    boost::mutex::scoped_lock lock(m_conditionMutex);
    --m_threadCount;
    m_condition.notify_all();
}

ITV8::IAsyncActionHandler* SettingsHandler::WrapHandler(ITV8::IAsyncActionHandler* handler)
{
    {
        boost::mutex::scoped_lock lock(m_conditionMutex);
        ++m_threadCount;
        m_condition.notify_all(); 
    }

    auto wrapper = boost::make_shared<AsyncActionHandlerWrapper>(handler, this);
    m_wrapperList.push_back(wrapper);
    return wrapper.get();
}

void CBaseBlockingHandler::WaitForResult()
{
    boost::mutex::scoped_lock lock(m_conditionMutex);
    while (!m_finished)
    {
        m_condition.wait(lock);
    }
}

void CBaseBlockingHandler::ReleaseCondition()
{
    boost::mutex::scoped_lock lock(m_conditionMutex);
    m_finished = true;
    m_condition.notify_all();
}

void CDynamicParametersHandler::Failed(IContract*, ITV8::hresult_t code)
{
    m_result = code;

    CBaseBlockingHandler::ReleaseCondition();
}

void CDynamicParametersHandler::Finished(IContract*, const char* parameters, uint32_t size)
{
    m_jsonData = std::string(parameters, size);

    CBaseBlockingHandler::ReleaseCondition();
}

CSampleSequenceChecker::CSampleSequenceChecker(NLogging::ILogger* parent, const std::string& prefix, uint32_t period) :
    NLogging::WithLogger(parent, prefix),
    m_logPeriod(period),
    m_framesCount(0),
    m_seqViolationsCount(0),
    m_lastTimestamp(0)
{
}

uint32_t CSampleSequenceChecker::Update(uint64_t currentTimestamp)
{
    m_framesCount++;
    bool firstFrame = false;
    if (m_lastTimestamp == 0)
    {
        m_lastTimestamp = currentTimestamp;
        firstFrame = true;
    }

    if (currentTimestamp <= m_lastTimestamp && !firstFrame)
    {
        m_seqViolationsCount++;
        m_lastTimestamp = 0;
        LogStatistics();
        return m_framesCount;
    }

    m_lastTimestamp = currentTimestamp;
    LogStatistics();
    return m_framesCount;
}

void CSampleSequenceChecker::Reset()
{
    LogStatistics(true);
    m_framesCount = 0;
    m_seqViolationsCount = 0;
    m_lastTimestamp = 0;
}

void CSampleSequenceChecker::LogStatistics(bool onReset)
{
    const auto emptySession = m_framesCount == 0 && m_lastTimestamp == 0;
    if ((onReset && !emptySession) || m_framesCount % m_logPeriod == 1)
    {
        _log_ << (onReset ? "Reset session " : "") << "framesCount=" << m_framesCount << ", seqViolationsCount="
            << m_seqViolationsCount << ", lastTimestamp=" << ipintTimestampToIsoString(m_lastTimestamp);
    }
}

}
