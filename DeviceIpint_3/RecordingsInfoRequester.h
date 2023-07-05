#ifndef DEVICEIPINT3_RECORDINGSINFOREQUESTER_H
#define DEVICEIPINT3_RECORDINGSINFOREQUESTER_H

#include <boost/asio/io_service.hpp>
#include <boost/function.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/thread/mutex.hpp>

#include <Logging/log2.h>
#include <ItvDeviceSdk/include/IStorageDevice.h>

#include "StorageDataTypes.h"
#include "DeviceIpint_Exports.h"
#include <Executors/DynamicThreadPool.h>

namespace IPINT30
{

typedef boost::shared_ptr<void> StartedStateHolderSP;
using ITV8::Utility::recordingInfoList;

#ifdef _MSC_VER
#pragma warning(push)
// warning C4251: <data member>: <type> needs to have dll-interface to
#pragma warning(disable : 4251)
#endif

// Continuously tries obtain recordings information from storage device.
// Class fully thread-safe. 
// Callback will be posted to io_service provided to the object.
// Ipint objects will be used only from the thread where io_service is executed.
class DEVICEIPINT_TESTABLE_DECLSPEC RecordingsInfoRequester : boost::noncopyable
{
public:
    typedef boost::function<void(const recordingInfoList&, StartedStateHolderSP)> recordingsCallback_t;

    RecordingsInfoRequester(DECLARE_LOGGER_ARG, ITV8::GDRV::IStorageDevice* storageDevice, int esId,
            recordingsCallback_t callback, StartedStateHolderSP stateHolder,
            boost::asio::io_service& service,
            NExecutors::PDynamicThreadPool dynExec,
            const boost::posix_time::time_duration& retryTimeout = boost::posix_time::seconds(20));

public:
    void cancel();

private:
    void scheduleRequest();

    void doRequest(const boost::system::error_code& error,
        StartedStateHolderSP startedState);

    void handleRecordingsInfo(ITV8::hresult_t error, 
        const ITV8::Utility::recordingInfoList& recordingsInfo,
        StartedStateHolderSP startedState);

private:
    DECLARE_LOGGER_HOLDER;

    ITV8::GDRV::IStorageDevice* const m_storageDevice;
    recordingsCallback_t m_callback;
    StartedStateHolderSP m_startedState;
    NExecutors::PDynamicThreadPool m_dynExec;
    boost::asio::deadline_timer m_timer;
    boost::mutex m_timerGuard;
    bool m_canelled;
    const boost::posix_time::time_duration m_retryTimeout;
    const int m_embeddedStorageId;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}

#endif

