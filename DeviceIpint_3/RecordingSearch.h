#ifndef DEVICEIPINT3_RECORDINGSEARCH_H
#define DEVICEIPINT3_RECORDINGSEARCH_H

#include <vector>

#include <boost/make_shared.hpp>
#include <boost/system/system_error.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/thread.hpp>
#include <boost/signals2.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_context_strand.hpp>

#include <ItvDeviceSdk/include/IStorageDevice.h>
#include "StorageDataTypes.h"
#include "DeviceIpint_Exports.h"
#include <Executors/DynamicThreadPool.h>
#include <Primitives/CorbaHelpers/Reactor.h>

namespace IPINT30
{
typedef boost::shared_ptr<ITV8::GDRV::IRecordingSearch> IRecordingSearchSP;
typedef boost::signals2::signal<void()> stopSignal_t;

#ifdef _MSC_VER
#pragma warning(push)
// warning C4251: <data member>: <type> needs to have dll-interface to
#pragma warning(disable : 4251)
// warning C4275: non dll-interface struct <typeA> used as base for dll-interface class <typeB>
#pragma warning(disable : 4275)
#endif

class DEVICEIPINT_TESTABLE_DECLSPEC RecordingSearch : public ITV8::GDRV::IRecordingSearchHandler
                                                    , public ITV8::GDRV::ICalendarHandler
                                                    , public NLogging::WithLogger
{
public:
    RecordingSearch(DECLARE_LOGGER_ARG, ITV8::GDRV::IStorageDevice* device,
        const std::string& recordingId, NExecutors::PDynamicThreadPool dynExec,
        const boost::posix_time::time_duration& timeOut = boost::posix_time::minutes(1));

    typedef boost::shared_ptr<ITV8::Utility::RecordingRange> RecordingRangeSP;
    typedef std::vector<RecordingRangeSP> rangeList_t;

    rangeList_t findRecordings(const ITV8::GDRV::DateTimeRange& timeBounds, stopSignal_t& stopSignal);

    ITV8::Utility::calendarList_t getCalendar(const ITV8::GDRV::DateTimeRange& timeBounds, stopSignal_t& stopSignal);

    bool isFinished() const;

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IRecordingSearchHandler)
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::ICalendarHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingSearchHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::ICalendarHandler)
    ITV8_END_CONTRACT_MAP()

private:
    void Finished(ITV8::IContract*, ITV8::hresult_t code) override;
    void RangeFound(ITV8::GDRV::IRecordingSearch*,
        ITV8::GDRV::Storage::IRecordingRange& recordingRange) override;
    void DateFound(ITV8::GDRV::ICalendarSearch*, ITV8::timestamp_t date) override;

    void doFindRecordings();
    void cancelOperation();
    void postTaskAndWait();
    void setStopSignalConnection(stopSignal_t& stopSignal);

private:
    ITV8::GDRV::IStorageDevice* const   m_device;
    const std::string                   m_recordingId;
    NExecutors::PDynamicThreadPool      m_dynExec;
    boost::posix_time::time_duration    m_waitTimeOut;
    ITV8::hresult_t                     m_error;
    IRecordingSearchSP                  m_search;
    bool                                m_finished;
    ITV8::GDRV::DateTimeRange           m_timeBounds;
    uint32_t                            m_failedAttempts;
    bool                                m_isCalendarRequestSupported;
    bool                                m_isCalendarRequest;
    std::atomic_bool                    m_internalStopSearch;
    boost::mutex                        m_mutex;
    boost::condition_variable           m_finishedCondition;
    rangeList_t                         m_rangeList;
    ITV8::Utility::calendarList_t       m_calendarList;
    boost::signals2::connection         m_stopSignalConnection;
};

typedef boost::shared_ptr<ITV8::Utility::RecordingRange> RecordingRangeSP;
typedef std::function<void(RecordingRangeSP)> rangeHandler_t;
typedef std::function<void(ITV8::timestamp_t)> calendarHandler_t;
typedef std::function<void(ITV8::hresult_t)> finishedHandler_t;

class DEVICEIPINT_TESTABLE_DECLSPEC AsyncRecordingSearch : 
        public ITV8::GDRV::IRecordingSearchHandler,
        public ITV8::GDRV::ICalendarHandler,
        public NLogging::WithLogger
{
public:
    AsyncRecordingSearch(DECLARE_LOGGER_ARG, ITV8::GDRV::IStorageDevice* device,
        const std::string& recordingId, const boost::posix_time::time_duration& timeOut = boost::posix_time::minutes(5));

    void asyncFindRecordings(const ITV8::GDRV::DateTimeRange& timeBounds, rangeHandler_t rangeHandler, 
        finishedHandler_t finishedHandler, stopSignal_t& stopSignal);

    void asyncGetCalendar(const ITV8::GDRV::DateTimeRange& timeBounds, calendarHandler_t calendarHandler, 
        finishedHandler_t finishedHandler, stopSignal_t& stopSignal);

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IRecordingSearchHandler)
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::ICalendarHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingSearchHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::ICalendarHandler)
    ITV8_END_CONTRACT_MAP()

private:
    void Finished(ITV8::IContract*, ITV8::hresult_t code) override;
    void RangeFound(ITV8::GDRV::IRecordingSearch*,
        ITV8::GDRV::Storage::IRecordingRange& recordingRange) override;
    void DateFound(ITV8::GDRV::ICalendarSearch*, ITV8::timestamp_t date) override;

    void doFindRecordings();
    void cancelOperation(const std::string& reason);
    void postAsyncTask(stopSignal_t& stopSignal);

private:
    ITV8::GDRV::IStorageDevice* const    m_device;
    const std::string                    m_recordingId;
    NCorbaHelpers::PReactor              m_reactor;
    boost::asio::io_service::strand      m_strand;
    boost::asio::deadline_timer          m_cancelTimer;
    boost::posix_time::time_duration     m_waitTimeOut;
    bool                                 m_cancelled;   
    std::mutex                           m_cancelledMutex;

    IRecordingSearchSP                   m_search;
    ITV8::GDRV::DateTimeRange            m_timeBounds;
    bool                                 m_isCalendarRequestSupported;
    bool                                 m_isCalendarRequest;

    rangeHandler_t                       m_rangeHandler;
    calendarHandler_t                    m_calendarHandler;
    finishedHandler_t                    m_finishedHandler;
    boost::signals2::connection          m_stopSignalConnection;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}

#endif

