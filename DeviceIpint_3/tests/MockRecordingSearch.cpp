#include <functional>
#include <boost/asio/io_service.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/thread.hpp>

#include "TestUtils.h"
#include "MockRecordingSearch.h"

#include "../StorageDataTypes.h"

#include <ItvSdk/include/IErrorService.h>
#include <Primitives/Logging/WithLogger.h>
#include <Logging/Log3.h>

#include "../TimeStampHelpers.h"

namespace
{
using namespace DeviceIpint_3::UnitTesting;

std::string rangeToString(const ITV8::GDRV::DateTimeRange& timeRange)
{
    return fmt::format(FMT_STRING("[{}, {}]"), IPINT30::ipintTimestampToIsoString(timeRange.rangeBegin), IPINT30::ipintTimestampToIsoString(timeRange.rangeEnd));
}

class RecordingSearch :
    public ITV8::GDRV::IRecordingSearch,
    public NLogging::WithLogger
{
public:
    RecordingSearch(NLogging::ILogger* logger, const char* recordingId, MockRecordingSearch* mock):
        NLogging::WithLogger(logger, fmt::format(FMT_STRING("/RecordingSearch{{{}}} [{}]"), recordingId, static_cast<void*>(this))),
        m_timer(m_ioService),
        m_stopped(true),
        m_recordingId(recordingId),
        m_mock(mock)
    {
        _dbg_ << "Created";
        m_reportingTracks = m_mock->getReportingTracks();
        m_searchFail = m_mock->getSearchWillFail();
    }

    ~RecordingSearch()
    {
        m_stopped = true;
        m_timer.cancel();
        m_work.reset();
        
        try
        {
            if (m_thread.joinable())
                m_thread.join();
        }
        catch (const std::exception& e)
        {
            _err_ << "Error during join thread " << e.what();
        }

        _dbg_ << "Destroyed";
    }

    void FindRecordings(const ITV8::GDRV::DateTimeRange& timeRange, ITV8::GDRV::IRecordingSearchHandler* handler) override
    {
        if (!handler)
            return;

        if (!m_thread.joinable())
        {
            m_work.reset(new boost::asio::io_service::work(m_ioService));
            m_thread = std::thread(boost::bind(&boost::asio::io_service::run, &m_ioService));
        }

        _inf_ << "FindRecordings for range " << rangeToString(timeRange);

        m_stopped = false;
        m_handler = handler;
        m_timeRange = timeRange;

        m_current = m_reportingTracks.begin();
        m_end = m_reportingTracks.end();
        scheduleTimer(m_current->first);
    }

    void CancelFindRecordings() override
    {
        _inf_ << "CancelFindRecordings for range " << rangeToString(m_timeRange);

        m_ioService.post([this]() 
            {
                m_stopped = true;

                boost::system::error_code ec;
                m_timer.cancel(ec);

                m_current = m_end;
            });

    }

    void Destroy() override
    {
        delete this;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IRecordingSearch)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingSearch)
    ITV8_END_CONTRACT_MAP()

private:
    void scheduleTimer(uint32_t milliseconds)
    {
        if (m_stopped)
            return;

        m_timer.expires_at(boost::posix_time::second_clock::universal_time() + boost::posix_time::milliseconds(milliseconds));
        m_timer.async_wait(boost::bind(&RecordingSearch::timerHandler, this, _1));
    }

    void timerHandler(const boost::system::error_code& ec)
    {
        if (ec)
            return reportFinished();

        if (m_searchFail)
        {
            return reportFinished(ITV8::EGeneralConnectionError);
        }

        if (m_current == m_end)
            return reportFinished();

        ITV8::Utility::RecordingRange recordingRange(m_recordingId, ITV8::GDRV::Storage::ERecordStatusStoped);

        std::ostringstream rangeStr;
        rangeStr << " id(" << m_recordingId << ")";
        for (auto& it : m_current->second)
        {
            const auto left = it.range.rangeBegin;
            const auto right = it.range.rangeEnd;

            if ((m_timeRange.rangeBegin <= left && right < m_timeRange.rangeEnd) || // contains full 
                (m_timeRange.rangeBegin <= left  && left < m_timeRange.rangeEnd) || // contains left range
                (m_timeRange.rangeBegin <= right && right < m_timeRange.rangeEnd)) // contains right range
            {
                rangeStr << "\n\t range " << rangeToString(it.range);
                recordingRange.tracksRange.push_back(it);

            }

        }

        _inf_ << "RangeFound for " << rangeStr.str();
        m_handler->RangeFound(static_cast<ITV8::GDRV::IRecordingSearch*>(this), recordingRange);

        ++m_current;
        if (m_current == m_end)    
            return reportFinished();

        scheduleTimer(m_current->first);
    }

    void reportFinished(ITV8::hresult_t result = ITV8::ENotError)
    {
        if (m_handler)
        {
            m_handler->Finished(static_cast<ITV8::GDRV::IRecordingSearch*>(this), result);
            _inf_ << "Report Finished result=" << result;
        }

        m_handler = nullptr;
    }

private:
    std::thread m_thread;
    boost::asio::io_service m_ioService;
    boost::scoped_ptr<boost::asio::io_service::work> m_work;
    boost::asio::deadline_timer m_timer;
    std::atomic<bool> m_stopped;

    const std::string m_recordingId;
    MockRecordingSearch* m_mock;

    ITV8::GDRV::IRecordingSearchHandler* m_handler;
    ITV8::GDRV::DateTimeRange m_timeRange;

    reportingTracks_t                 m_reportingTracks;
    bool                              m_searchFail;
    reportingTracks_t::const_iterator m_current;
    reportingTracks_t::const_iterator m_end;
};
}

namespace DeviceIpint_3 { namespace UnitTesting {

MockRecordingSearch::MockRecordingSearch(DECLARE_LOGGER_ARG) :
    m_searchFail(false)
{
    INIT_LOGGER_HOLDER;
}

ITV8::GDRV::IRecordingSearch* MockRecordingSearch::createRecordingsSearch(const char* recordingId)
{
    return new RecordingSearch(GET_LOGGER_PTR, recordingId, this);
}


}

}

