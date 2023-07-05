#ifndef ASYNC__ACTION__HANDLER__H__
#define ASYNC__ACTION__HANDLER__H__

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

#include <ItvSdk/include/baseTypes.h>

#include "IIPManager3.h"
#include "sdkHelpers.h"
#include "CChannel.h"
#include "CAsyncChannel.h"

namespace IPINT30
{

class CAsyncHandlerBase : public ITV8::IAsyncActionHandler, public virtual NLogging::WithLogger
{
public:
    CAsyncHandlerBase(DECLARE_LOGGER_ARG)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , m_applyAttemptCount(0)
    {
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IAsyncActionHandler)
    ITV8_END_CONTRACT_MAP()

protected:
    void WaitForApply()
    {
        boost::mutex::scoped_lock lock(m_applyMutex);
        if (m_applyAttemptCount > 0)
        {
            _log_ << "Wait " << m_applyAttemptCount << " for apply: " << this;
            m_applyCondition.wait(lock, [this] { return m_applyAttemptCount == 0; });
            _log_ << "Awaiting for apply is over: " << this;
        }
    }

    virtual void Finished(ITV8::IContract* source, ITV8::hresult_t code)
    {
        _dbg_ << "ApplySettings end:" << this;
        ApplyCompleted(source, code);

        boost::mutex::scoped_lock lock(m_applyMutex);
        if (m_applyAttemptCount == 0)
        {
            _err_ << "ApplySettings finish-handler has been called second time!";
            return; //To prevent counter overflow and hanging inside WaitForReply.
        }

        --m_applyAttemptCount;
        m_applyCondition.notify_all();
    }

    virtual void ApplySettings(ITV8::IAsyncAdjuster* adjuster, ITV8::IAsyncActionHandler* handler)
    {
        boost::uint32_t cnt = 0;
        {
            boost::mutex::scoped_lock lock(m_applyMutex);
            cnt = ++m_applyAttemptCount;
        }
        _dbg_ << "ApplySettings begin " << cnt << ":" << handler;
        adjuster->ApplySettings(handler);
    }
protected:
    virtual void ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code)
    {
    }

    boost::mutex m_applyMutex;
    boost::uint32_t m_applyAttemptCount;
    boost::condition_variable_any m_applyCondition;
};

class CChannelHandlerImpl : public CChannel
                          , public CAsyncHandlerBase
{
public:
    CChannelHandlerImpl(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec,
        boost::shared_ptr<IPINT30::IIpintDevice> parent, INotifyState* notifier)
        : WithLogger(GET_LOGGER_PTR)
        , CChannel(GET_LOGGER_PTR, dynExec, parent, notifier)
        , CAsyncHandlerBase(GET_LOGGER_PTR)
    {
    }

protected:
    virtual void ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code)
    {
        if (code != ITV8::ENotError)
        {
            _err_ << ToString() <<" ITV8::IAsyncAdjuster::ApplySettings(); return err:"
                  << code <<"; msg: "<<get_last_error_message(source, code)<<std::endl;
        }
        else
        {
            _inf_ << ToString() <<" Async operation completed." << std::endl;
        }
    }
};

template <typename T>
class CAsyncChannelHandlerImpl : public CAsyncChannel<T>
                               , public CAsyncHandlerBase
{
    typedef CAsyncChannel<T> TBase;
public:
    CAsyncChannelHandlerImpl(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec,
        boost::shared_ptr<IPINT30::IIpintDevice> parent, IPINT30::INotifyState* notifier)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , TBase(GET_LOGGER_PTR, dynExec, parent, notifier)
        , CAsyncHandlerBase(GET_LOGGER_PTR)
    {
    }

protected:
    virtual void ApplyCompleted(ITV8::IContract* source, ITV8::hresult_t code)
    {
        if (code == ITV8::ENotError || code == ITV8::EBlockingConfiguration)
        {
            _this_inf_ << this->ToString() << " Async operation completed." << std::endl;
            return;
        }

        _this_err_ << this->ToString() << " ITV8::IAsyncAdjuster::ApplySettings(); return err:"
                   << code << "; msg: " << get_last_error_message(source, code) << std::endl;

        switch (code)
        {
            case ITV8::EAuthorizationFailed:
                this->Notify(NMMSS::IPDS_AuthorizationFailed);
                break;
            default:
                this->Notify(NMMSS::IPDS_AcceptSettingsFailure);
        }
    }
};
}

#endif // ASYNC__ACTION__HANDLER__H__
