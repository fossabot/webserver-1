#ifndef DEVICEIPINT3_UTILITY_H
#define DEVICEIPINT3_UTILITY_H

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include "IIPManager3.h"

namespace IPINT30
{

class AsyncActionHandlerWrapper;

class SettingsHandler : public ITV8::IAsyncActionHandler
{
public:
    SettingsHandler();

    void Wait();
    ITV8::IAsyncActionHandler* WrapHandler(ITV8::IAsyncActionHandler* handler);

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IAsyncActionHandler)
        ITV8_CONTRACT_ENTRY(SettingsHandler)
    ITV8_END_CONTRACT_MAP()

private:
    virtual void Finished(ITV8::IContract* source, ITV8::hresult_t code);

private:
    typedef boost::shared_ptr<AsyncActionHandlerWrapper> AsyncActionHandlerWrapperSP;
    typedef std::vector<AsyncActionHandlerWrapperSP> wrapperList_t;

private:
    int                             m_threadCount;

    boost::mutex                    m_conditionMutex;
    boost::condition_variable_any   m_condition;

    wrapperList_t                   m_wrapperList;
};

ITV8::IAsyncActionHandler* WrapAsyncActionHandler(ITV8::IAsyncActionHandler* handler, ITV8::IAsyncActionHandler* waitHandler);

class CBaseBlockingHandler
{
public:
    CBaseBlockingHandler() :
        m_finished(false),
        m_result(ITV8::ENotError)
    {
    }

    virtual ITV8::hresult_t GetResult() const
    {
        return m_result;
    }

    virtual void WaitForResult();
protected:
    virtual void ReleaseCondition();
protected:
    bool                            m_finished;
    ITV8::hresult_t                 m_result;
    boost::mutex                    m_conditionMutex;
    boost::condition_variable_any   m_condition;
};

class CDynamicParametersHandler : public CBaseBlockingHandler
                                , public ITV8::IDynamicParametersHandler
{
public:
    CDynamicParametersHandler() :
        CBaseBlockingHandler()
    {
    }

    std::string GetJsonData() const
    {
        return m_jsonData;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IDynamicParametersHandler)
    ITV8_END_CONTRACT_MAP()
private:
    void Failed(IContract*, ITV8::hresult_t code) override;

    void Finished(IContract*, const char* parameters, uint32_t size) override;
private:
    std::string                     m_jsonData;
};

class CSampleSequenceChecker : public NLogging::WithLogger
{
public:
    CSampleSequenceChecker(NLogging::ILogger* parent, const std::string& prefix, uint32_t period = 1000);

    uint32_t Update(uint64_t currentTimestamp);

    void Reset();

private:
    void LogStatistics(bool onReset = false);

private:
    const uint32_t m_logPeriod;
    uint32_t m_framesCount;
    uint32_t m_seqViolationsCount;
    uint64_t m_lastTimestamp;
};

}

#endif
