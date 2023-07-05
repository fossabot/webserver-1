#include "RecordingsInfoRequester.h"

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <ItvSdk/include/IErrorService.h>

namespace IPINT30
{
namespace
{

recordingInfoList enumerateRecordings(
        ITV8::GDRV::Storage::IRecordingInfoEnumerator* recordingEnumerator)
{
    recordingInfoList result;
    recordingEnumerator->Reset();
    while (recordingEnumerator->MoveNext())
    {
        result.push_back(boost::make_shared<ITV8::Utility::RecordingInfo>(
            recordingEnumerator->GetCurrent()));
    }
    return result;
}

// Performs single request for recordings, 
// adapts plain IPINT interface data to more handy ones.
class RecordingsRequestAdapter : public ITV8::GDRV::IRecordingsInfoHandler2
{
public:
    typedef boost::function<void (ITV8::hresult_t, const recordingInfoList&, StartedStateHolderSP)> recordingsCallback_t;

    static void asyncRequest(ITV8::GDRV::IStorageDevice* storageDevice, int embeddedStorageId,
                             recordingsCallback_t callback, StartedStateHolderSP stateHolder)
    {
        // It is Safe.
        // Object will destroy itself just after calling the callback.
        // StartedStateHolderSP copy will preserve object from being abandoned.
        new RecordingsRequestAdapter(storageDevice, callback, stateHolder, embeddedStorageId);
    }


protected:
    virtual void Failed(ITV8::IContract* source, ITV8::hresult_t error)
    {
        reportResult(error, recordingInfoList());
    }

    virtual void RequestRecordingsDone(ITV8::GDRV::IStorageDevice* source, 
        ITV8::GDRV::Storage::IRecordingInfoEnumerator& recordingsEnumerator)
    {
        recordingInfoList recordings = enumerateRecordings(&recordingsEnumerator);
        reportResult(ITV8::ENotError, recordings);
    }

    virtual int GetStorageIndex() const
    {
        return m_embeddedStorageId;
    }

private:
    RecordingsRequestAdapter(ITV8::GDRV::IStorageDevice* storageDevice,
                             recordingsCallback_t callback,
                             StartedStateHolderSP stateHolder,
                             int embeddedStorageId)
        : m_callback(callback)
        , m_startedState(stateHolder)
        , m_embeddedStorageId(embeddedStorageId)
    {
        storageDevice->RequestRecordingsInfo(this);
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IRecordingsInfoHandler)
        ITV8_CONTRACT_ENTRY(ITV8::IEventHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingsInfoHandler2)
    ITV8_END_CONTRACT_MAP()


private:
    void reportResult(ITV8::hresult_t errorCode, const recordingInfoList& recordings)
    {
        m_callback(errorCode, boost::ref(recordings), m_startedState);
        // Destroy instance just after reporting callback
        delete this;
    }

private:
    recordingsCallback_t m_callback;
    StartedStateHolderSP m_startedState;
    int                  m_embeddedStorageId;
};
}

RecordingsInfoRequester::RecordingsInfoRequester(DECLARE_LOGGER_ARG, 
                                                 ITV8::GDRV::IStorageDevice* storageDevice, 
                                                 int esId,
                                                 recordingsCallback_t callback,
                                                 StartedStateHolderSP stateHolder,
                                                 boost::asio::io_service& service,
                                                 NExecutors::PDynamicThreadPool dynExec,
                                                 const boost::posix_time::time_duration& retryTimeout):
    m_storageDevice(storageDevice),
    m_callback(callback),
    m_startedState(stateHolder),
    m_dynExec(dynExec),
    m_timer(service),
    m_canelled(false),
    m_retryTimeout(retryTimeout),
    m_embeddedStorageId(esId)
{
    INIT_LOGGER_HOLDER;
    if (!m_dynExec->Post(boost::bind(&RecordingsInfoRequester::doRequest,
        this, boost::system::error_code(), m_startedState)))
    {
        _err_ << "Can't post recording info requester to dynamic thread poll!";
    }

}

void RecordingsInfoRequester::cancel()
{
    _dbg_ << "Cancel recording info requesting.";
    StartedStateHolderSP localCopy = m_startedState;
    boost::mutex::scoped_lock lock(m_timerGuard);
    m_canelled = true;
    m_timer.cancel();
    m_startedState.reset();
}

void RecordingsInfoRequester::scheduleRequest()
{
    _dbg_ << "Make a new recording info request after " << m_retryTimeout.total_seconds() << " seconds.";
    boost::mutex::scoped_lock lock(m_timerGuard);
    if (m_canelled)
    {
        return;
    }
    m_timer.expires_from_now(m_retryTimeout);
    m_timer.async_wait(boost::bind(&RecordingsInfoRequester::doRequest,
        this, _1, m_startedState));
}

void RecordingsInfoRequester::doRequest(const boost::system::error_code& error, StartedStateHolderSP startedState)
{
    _dbg_ << "Request recording info. error = " << error << ", is cancelled = " << m_canelled;
    if (error || m_canelled)
        return;

    RecordingsRequestAdapter::asyncRequest(m_storageDevice, m_embeddedStorageId,
        boost::bind(&RecordingsInfoRequester::handleRecordingsInfo, this, _1, _2, _3),
        startedState);
}


void RecordingsInfoRequester::handleRecordingsInfo(ITV8::hresult_t error, 
                                                   const ITV8::Utility::recordingInfoList& recordingsInfo,
                                                   StartedStateHolderSP startedState)
{
    if (m_canelled)
        return;

    if (error && error != ITV8::EUnsupportedCommand)
    {
        _wrn_ << "Device RequestRecordingsInfo return error: " << error;
        return scheduleRequest();
    }
    if (error == ITV8::EUnsupportedCommand)
        _wrn_ << "Device doesn't support embedded storage!";

    m_startedState.reset();
    if (!m_dynExec->Post(boost::bind(m_callback, recordingsInfo, startedState)))
    {
        _err_ << "Can't post recording info requester to dynamic thread poll!";
    }
}

}
