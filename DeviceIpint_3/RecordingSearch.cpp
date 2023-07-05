#include "RecordingSearch.h"

#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>

#include <fmt/format.h>

#include <ItvSdk/include/IErrorService.h>
#include <ItvDeviceSdk/include/IRecordingSearch.h>

#include "IpintDestroyer.h"
#include "TimeStampHelpers.h"

namespace IPINT30
{

namespace
{

const uint32_t MAX_FAILED_SEARCH_ATTEMPTS = 5;

ITV8::Utility::tracksRangeList_t enumerateTrackRanges(
    ITV8::GDRV::Storage::ITracksRangeEnumerator* trackRangeEnum)
{
    ITV8::Utility::tracksRangeList_t result;
    trackRangeEnum->Reset();
    while (trackRangeEnum->MoveNext())
    {
        ITV8::GDRV::Storage::ITrackRange* iTrackRange = trackRangeEnum->GetCurrent();
        result.push_back(ITV8::Utility::TrackRange(iTrackRange->GetId(),
            iTrackRange->GetRange()));
    }
    return result;
}

}

RecordingSearch::RecordingSearch(DECLARE_LOGGER_ARG, ITV8::GDRV::IStorageDevice* device,
    const std::string& recordingId, NExecutors::PDynamicThreadPool dynExec,
    const boost::posix_time::time_duration& timeOut) :
    NLogging::WithLogger(GET_LOGGER_PTR, fmt::format(FMT_STRING("RecordingSearch.{}[{}]"), static_cast<void*>(this), recordingId)),
    m_device(device),
    m_recordingId(recordingId),
    m_dynExec(dynExec),
    m_waitTimeOut(timeOut),
    m_error(ITV8::ENotError),
    m_finished(false),
    m_failedAttempts(0),
    m_isCalendarRequestSupported(false),
    m_isCalendarRequest(false),
    m_internalStopSearch(false)
{
}

RecordingSearch::rangeList_t RecordingSearch::findRecordings(const ITV8::GDRV::DateTimeRange& timeBounds,
    stopSignal_t& stopSignal)
{
    m_timeBounds = timeBounds;
    setStopSignalConnection(stopSignal);
    postTaskAndWait();

    return m_rangeList;
}

void RecordingSearch::cancelOperation()
{
    if (!m_search)
    {
        return;
    }

    // Run cancel operation from service thread.
    // We have to wait while handler executed to prevent dangling handlers.
    // It is a bit overkill though. Just calling a function from current thread should be
    // safe with 99.(9)% cases.
    typedef boost::packaged_task<void> task_t;
    if (m_isCalendarRequestSupported)
    {
        auto* calendarSearch = ITV8::contract_cast<ITV8::GDRV::ICalendarSearch>(m_search.get());
        if (!calendarSearch)
        {
            throw std::logic_error("Can not call CancelCalendar from not ICalendarSearch");
        }

        task_t cancelTask(boost::bind(&ITV8::GDRV::ICalendarSearch::CancelCalendar, calendarSearch));
        if (!m_dynExec->Post(boost::bind(&task_t::operator(), &cancelTask)))
            throw std::runtime_error("Can't post cancelOperation() to thread pool!");
        auto cancelFuture = cancelTask.get_future();
        cancelFuture.wait();
    }
    else
    {
        task_t cancelTask(boost::bind(&ITV8::GDRV::IRecordingSearch::CancelFindRecordings, m_search.get()));
        if (!m_dynExec->Post(boost::bind(&task_t::operator(), &cancelTask)))
            throw std::runtime_error("Can't post cancelOperation() to thread pool!");
        auto cancelFuture = cancelTask.get_future();
        cancelFuture.wait();
    }

    // Wait while handler will be called
    boost::mutex::scoped_lock lock(m_mutex);
    m_finishedCondition.wait(lock, boost::bind(&RecordingSearch::isFinished, this));
}

void RecordingSearch::postTaskAndWait()
{
    if (m_internalStopSearch)
        return;

    m_finished = false;

    if (!m_dynExec->Post(boost::bind(&RecordingSearch::doFindRecordings, this)))
        throw std::runtime_error("Can't post doFindRecordings() to thread pool!");

    // Wait for search completion with timeout.
    bool finished = false;
    {
        boost::mutex::scoped_lock lock(m_mutex);
        finished = m_finishedCondition.timed_wait(lock, m_waitTimeOut,
            boost::bind(&RecordingSearch::isFinished, this));
    }

    if (!finished || m_internalStopSearch)
    {
        _wrn_ << "Canceling recording search process, the reason is "
            << (m_internalStopSearch ? "internal stop" : "timeout");
        cancelOperation();
    }

    if (m_error)
    {
        if (m_isCalendarRequest)
        {
            throw std::runtime_error("Calendar request finished with error: " +
                boost::lexical_cast<std::string>(m_error));
        }

        if (m_error == ITV8::EOperationCancelled)
        {
            throw std::runtime_error("Waiting timeout is reached");
        }

        // It means that Ipint does not report any range MAX_FAILED_SEARCH_ATTEMPTS times in succession
        if (++m_failedAttempts > MAX_FAILED_SEARCH_ATTEMPTS)
        {
            throw std::runtime_error("Max failed search attempts reached. Drop results");
        }

        _wrn_ << "Request recordings error=" << m_error
            << ". Retry with interval [" << ipintTimestampToIsoString(m_timeBounds.rangeBegin) << ", "
            << ipintTimestampToIsoString(m_timeBounds.rangeEnd) << "]. Current recordings count="
            << m_rangeList.size() << ". Failed attempts:" << m_failedAttempts << "/" << MAX_FAILED_SEARCH_ATTEMPTS;

        return postTaskAndWait();
    }

    if (m_isCalendarRequest)
        _dbg_ << "ICalendarSearch." << m_search.get() << " finished";
    else
        _dbg_ << "IRecordingSearch." << m_search.get() << " finished";
}

void RecordingSearch::setStopSignalConnection(stopSignal_t& stopSignal)
{
    m_stopSignalConnection = stopSignal.connect(
        [&]()
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_internalStopSearch = true;
            m_finishedCondition.notify_one();
        });
}

void RecordingSearch::Finished(ITV8::IContract*, ITV8::hresult_t code)
{
    m_error = code;
    m_stopSignalConnection.disconnect();
    boost::mutex::scoped_lock lock(m_mutex);
    m_finished = true;
    m_finishedCondition.notify_one();
}

void RecordingSearch::RangeFound(ITV8::GDRV::IRecordingSearch*,
    ITV8::GDRV::Storage::IRecordingRange& recordingRange)
{
    if (recordingRange.GetId() != m_recordingId)
    {
        _dbg_ << "Skip recording with wrong id=" << recordingRange.GetId();
        return;
    }

    // In case when contract_cast to ICalendarSearch failed we have to convert m_rangeList to
    // m_calendarList and return
    if (m_isCalendarRequest && !m_isCalendarRequestSupported)
    {
        auto enumerator = recordingRange.GetTracksRangeEnumerator();
        enumerator->Reset();
        boost::gregorian::date currentDate;
        while (enumerator->MoveNext())
        {
            const auto range = enumerator->GetCurrent()->GetRange();
            const auto ptimeBegin = ipintTimestampToPtime(range.rangeBegin);
            const auto ptimeEnd = ipintTimestampToPtime(range.rangeEnd);

            if (currentDate != ptimeBegin.date())
            {
                currentDate = ptimeBegin.date();
                m_calendarList.push_back(range.rangeBegin);
            }
            if (currentDate != ptimeEnd.date())
            {
                currentDate = ptimeEnd.date();
                m_calendarList.push_back(range.rangeEnd);
            }
        }

        return;
    }

    RecordingRangeSP range = boost::make_shared<ITV8::Utility::RecordingRange>(
        recordingRange.GetId(), recordingRange.GetStatus());
    range->tracksRange = enumerateTrackRanges(recordingRange.GetTracksRangeEnumerator());
    if (range->tracksRange.empty())
        return;

    m_timeBounds.rangeBegin = range->tracksRange.back().range.rangeEnd;
    m_failedAttempts = 0;
    m_rangeList.push_back(range);
}

void RecordingSearch::doFindRecordings()
{
    m_search.reset(m_device->CreateRecordingSearch(m_recordingId.c_str()),
        ipint_destroyer<ITV8::GDRV::IRecordingSearch>());
    if (!m_search)
    {
        return Finished(static_cast<IRecordingSearchHandler*>(this), ITV8::EInvalidOperation);
    }

    if (m_isCalendarRequest)
    {
        auto* calendarSearch = ITV8::contract_cast<ITV8::GDRV::ICalendarSearch>(m_search.get());
        if (calendarSearch)
        {
            m_isCalendarRequestSupported = true;
            _dbg_ << "ICalendarSearch." << calendarSearch << " started";
            return calendarSearch->GetCalendar(m_timeBounds, this);
        }
    }

    _dbg_ << "IRecordingSearch." << m_search.get() << " started";
    m_search->FindRecordings(m_timeBounds, this);
}

bool RecordingSearch::isFinished() const
{
    return m_finished;
}

void RecordingSearch::DateFound(ITV8::GDRV::ICalendarSearch*, ITV8::timestamp_t date)
{
    m_calendarList.push_back(date);
}

ITV8::Utility::calendarList_t RecordingSearch::getCalendar(const ITV8::GDRV::DateTimeRange& timeBounds,
    stopSignal_t& stopSignal)
{
    m_timeBounds = timeBounds;
    m_isCalendarRequest = true;
    setStopSignalConnection(stopSignal);
    postTaskAndWait();

    return m_calendarList;
}

AsyncRecordingSearch::AsyncRecordingSearch(DECLARE_LOGGER_ARG, ITV8::GDRV::IStorageDevice* device,
    const std::string& recordingId, const boost::posix_time::time_duration& timeOut) :
    NLogging::WithLogger(GET_LOGGER_PTR, fmt::format(FMT_STRING("AsyncRecordingSearch.{}[{}]"), static_cast<void*>(this), recordingId)),
    m_device(device),
    m_recordingId(recordingId),
    m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
    m_strand(m_reactor->GetIO()),
    m_cancelTimer(m_reactor->GetIO()),
    m_waitTimeOut(timeOut),
    m_cancelled(false),
    m_isCalendarRequestSupported(false),
    m_isCalendarRequest(false)
{
}

void AsyncRecordingSearch::asyncFindRecordings(const ITV8::GDRV::DateTimeRange& timeBounds, rangeHandler_t rangeHandler, 
    finishedHandler_t finishedHandler, stopSignal_t& stopSignal)
{   
    m_rangeHandler = rangeHandler;
    m_timeBounds = timeBounds;
    m_finishedHandler = finishedHandler;

    postAsyncTask(stopSignal);
}

void AsyncRecordingSearch::postAsyncTask(stopSignal_t& stopSignal)
{
    m_strand.post(boost::bind(&AsyncRecordingSearch::doFindRecordings, this));

    auto cancelLambda = [this](const std::string& reason) { m_strand.post(boost::bind(&AsyncRecordingSearch::cancelOperation, this, reason)); };

    m_stopSignalConnection = stopSignal.connect([this, cancelLambda]()
        {
            boost::system::error_code e;
            m_cancelTimer.cancel(e);

            cancelLambda("cancelled by request");
        });

    const auto cancelTime = boost::posix_time::second_clock::universal_time() + m_waitTimeOut;
    m_cancelTimer.expires_at(cancelTime);
    m_cancelTimer.async_wait([this, cancelLambda](const boost::system::error_code& e)
        {
            if (e)
                return;

            cancelLambda("cancelled by timeout");
        });
}

void AsyncRecordingSearch::cancelOperation(const std::string& reason)
{
    if (m_cancelled)
        return;

    m_cancelled = true;
    
    if (!m_search)
        return;
    
    if (m_isCalendarRequestSupported)
    {
        auto* calendarSearch = ITV8::contract_cast<ITV8::GDRV::ICalendarSearch>(m_search.get());
        if (!calendarSearch)
        {
            _err_ << "Can not call CancelCalendar from not ICalendarSearch " << m_search.get();
            return;
        }

        _dbg_ << "Call ICalendarSearch->CancelCalendar " << m_search.get() << ", " << reason;        
        m_strand.post(boost::bind(&ITV8::GDRV::ICalendarSearch::CancelCalendar, calendarSearch));
    }
    else
    {
        _dbg_ << "Call IRecordingSearch->CancelFindRecordings " << m_search.get() << ", " << reason;  
        m_strand.post(boost::bind(&ITV8::GDRV::IRecordingSearch::CancelFindRecordings, m_search.get()));

    }
}

void AsyncRecordingSearch::Finished(ITV8::IContract*, ITV8::hresult_t code)
{
    if (code)
    {
        if (code == ITV8::EOperationCancelled)
        {
            _dbg_ << "Operation cancelled, requested interval [" << ipintTimestampToIsoString(m_timeBounds.rangeBegin) << ", "
                << ipintTimestampToIsoString(m_timeBounds.rangeEnd) << "].";
        }
        else if (m_isCalendarRequest)
        {
            _wrn_ << "Calendar request finished with error: " << std::to_string(code);
        }
        else
        {
            _wrn_ << "Request finished with error: " << std::to_string(code) << ", requested interval [" << ipintTimestampToIsoString(m_timeBounds.rangeBegin) << ", "
                << ipintTimestampToIsoString(m_timeBounds.rangeEnd) << "].";
        }
    }
    
    if (m_isCalendarRequestSupported)
    {
        _dbg_ << "Finished ICalendarSearch->GetCalendar " << m_search.get();        
    }
    else
    {
        _dbg_ << "Finished IRecordingSearch->FindRecordings " << m_search.get();
    }

    m_stopSignalConnection.disconnect();

    m_strand.post([this, code]()
        {
            if (!m_finishedHandler)
                return;

            finishedHandler_t finishedHandler;
            finishedHandler.swap(m_finishedHandler);
            finishedHandler(code);
        });
}

void AsyncRecordingSearch::RangeFound(ITV8::GDRV::IRecordingSearch*,
    ITV8::GDRV::Storage::IRecordingRange& recordingRange)
{
    if (recordingRange.GetId() != m_recordingId)
    {
        _dbg_ << "Skip recording with wrong id=" << recordingRange.GetId();
        return;
    }

    // In case when contract_cast to ICalendarSearch failed we have to convert m_rangeList to
    // m_calendarList and return
    if (m_isCalendarRequest && !m_isCalendarRequestSupported)
    {
        auto enumerator = recordingRange.GetTracksRangeEnumerator();
        enumerator->Reset();
        boost::gregorian::date currentDate;
        while (enumerator->MoveNext())
        {
            const auto range = enumerator->GetCurrent()->GetRange();
            const auto ptimeBegin = ipintTimestampToPtime(range.rangeBegin);
            const auto ptimeEnd = ipintTimestampToPtime(range.rangeEnd);

            if (currentDate != ptimeBegin.date())
            {
                currentDate = ptimeBegin.date();
                m_calendarHandler(range.rangeBegin);
            }
            if (currentDate != ptimeEnd.date())
            {
                currentDate = ptimeEnd.date();
                m_calendarHandler(range.rangeEnd);
            }
        }

        return;
    }

    RecordingRangeSP range = boost::make_shared<ITV8::Utility::RecordingRange>(
        recordingRange.GetId(), recordingRange.GetStatus());
    range->tracksRange = enumerateTrackRanges(recordingRange.GetTracksRangeEnumerator());

    if (range->tracksRange.empty())
        return;

    m_timeBounds.rangeBegin = range->tracksRange.back().range.rangeEnd;

    std::ostringstream oss;
    for (const auto& t : range->tracksRange)
    {
        oss << "id{" << t.id << " [" << ipintTimestampToIsoString(t.range.rangeBegin) << ", "
            << ipintTimestampToIsoString(t.range.rangeEnd) << "]} ";
    }
    _dbg_ << "IRecordingSearch->RangeFound => " << oss.str();

    m_rangeHandler(range);
}

void AsyncRecordingSearch::doFindRecordings()
{
    if (m_cancelled)
        return;

    m_search.reset(m_device->CreateRecordingSearch(m_recordingId.c_str()),
        ipint_destroyer<ITV8::GDRV::IRecordingSearch>());
    
    if (!m_search)
    {
        return Finished(static_cast<IRecordingSearchHandler*>(this), ITV8::EInvalidOperation);
    }

    _dbg_ << "Created IRecordingSearch " << m_search.get();

    if (m_isCalendarRequest)
    {
        auto* calendarSearch = ITV8::contract_cast<ITV8::GDRV::ICalendarSearch>(m_search.get());
        if (calendarSearch)
        {
            m_isCalendarRequestSupported = true;

            _dbg_ << "Started ICalendarSearch->GetCalendar" << m_search.get()
                << ", requested interval[" << ipintTimestampToIsoString(m_timeBounds.rangeBegin) << ", "
                << ipintTimestampToIsoString(m_timeBounds.rangeEnd) << "].";

            return calendarSearch->GetCalendar(m_timeBounds, this);
        }
    }

    _dbg_ << "Started IRecordingSearch->FindRecordings " << m_search.get() 
        << ", requested interval[" << ipintTimestampToIsoString(m_timeBounds.rangeBegin) << ", "
        << ipintTimestampToIsoString(m_timeBounds.rangeEnd) << "].";

    m_search->FindRecordings(m_timeBounds, this);
}

void AsyncRecordingSearch::DateFound(ITV8::GDRV::ICalendarSearch*, ITV8::timestamp_t date)
{
    m_calendarHandler(date);
}

void AsyncRecordingSearch::asyncGetCalendar(const ITV8::GDRV::DateTimeRange& timeBounds, calendarHandler_t calendarListHandler,
    finishedHandler_t finishedHandler, stopSignal_t& stopSignal)
{
    m_isCalendarRequest = true;
    m_calendarHandler = calendarListHandler;
    m_finishedHandler = finishedHandler;

    postAsyncTask(stopSignal);
}

}