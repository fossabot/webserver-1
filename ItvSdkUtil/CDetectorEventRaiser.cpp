#include "CDetectorEventRaiser.h"

#include <boost/bind.hpp>
#include <ItvFramework/TimeConverter.h>

#include "AppDataArgsAdjuster.h"
#include "BoneDetectorArgsAdjuster.h"
#include "GlobalTrackerArgsAdjuster.h"
#include "MaskEventDataAdjuster.h"
#include "TemperatureDetectorArgsAdjuster.h"
#include "DepthDetectorArgsAdjuster.h"
#include "EventArgsAdjuster.h"

namespace
{
NMMSS::EAlertState convertPhase(DECLARE_LOGGER_ARG, ITV8::Analytics::EEventPhase phase)
{
    switch (phase)
    {
        case ITV8::Analytics::esBegin:
            return NMMSS::AS_Began;
        case ITV8::Analytics::esEnd:
            return NMMSS::AS_Ended;
        case ITV8::Analytics::esMomentary:
            return NMMSS::AS_Happened;
        default:
            _err_ << "ITV8::Analytics::EEventPhase doesn't support value=" << phase << "." << std::endl;
            return NMMSS::AS_Happened;
    }
}

const char* convertMetadataType(DECLARE_LOGGER_ARG, const char* metadataType)
{
    // From mmss/MMIDL/MMVideo.idl
    static const char SCHEMA_VMDA[] = "VMDA";
    static const char SCHEMA_AUDIO_VOLUME[] = "AudioVolume";
    static const char SCHEMA_MASK[] = "Mask";
    static const char SCHEMA_HUMAN_BONE[] = "HumanBone";
    static const char SCHEMA_GLOBAL_TRACK[] = "GlobalTrack";
    static const char SCHEMA_TEMP_MATRIX[] = ITV8_METADATA_TYPE_TEMPERATURE_MATRIX;
    static const char SCHEMA_DEPTH_MASK[] = ITV8_METADATA_TYPE_DEPTH_MASK;

    if (!strcmp(metadataType, ITV8_METADATA_TYPE_TARGET_LIST))
    {
        return SCHEMA_VMDA;
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_VOLUME_LEVEL))
    {
        return SCHEMA_AUDIO_VOLUME;
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_HUMAN_BONE_TARGET_LIST))
    {
        return SCHEMA_HUMAN_BONE;
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_GLOBAL_TRACK_LIST))
    {
        return SCHEMA_GLOBAL_TRACK;
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_DEPTH_MASK))
    {
        return SCHEMA_DEPTH_MASK;
    }
    else if (strstr(metadataType, SCHEMA_MASK))
    {
        return SCHEMA_MASK;
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_TEMPERATURE_MATRIX))
    {
        return SCHEMA_TEMP_MATRIX;
    }
    else
    {
        _err_ << "metadataType \"" << metadataType << "\" - not supported." << std::endl;
        return metadataType;
    }
}

boost::posix_time::ptime getPtimeFromTimestamp(ITV8::timestamp_t timestamp)
{
    if (ITV8::testTimeStampFlag(timestamp, ITV8::ERelativeTimeFlag))
    {
        return boost::posix_time::microsec_clock::universal_time();
    }
    return ITV8::PtimeFromTimestamp(timestamp);
}

const boost::posix_time::time_duration FAKE_EVENT_DURATION = boost::posix_time::hours(120);

}

///////////////////////////////////////////////////////////////////////////////////////////////////

CBaseDetectorEventRaiser::CBaseDetectorEventRaiser(DECLARE_LOGGER_ARG, ITV8::timestamp_t time)
    : m_timestamp(time)
{
    INIT_LOGGER_HOLDER;
}

ITV8::timestamp_t CBaseDetectorEventRaiser::GetTime() const
{
    return m_timestamp;
}

void CBaseDetectorEventRaiser::Cancel()
{
    delete this;
}

void CBaseDetectorEventRaiser::Commit()
{
    delete this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CNoOpDetectorEventRaiser::CNoOpDetectorEventRaiser(DECLARE_LOGGER_ARG,
    const char* name, ITV8::timestamp_t time)
    : CBaseDetectorEventRaiser(GET_LOGGER_PTR, time)
{
    INIT_LOGGER_HOLDER;
    m_adjuster.reset(new EventArgsAdjuster(GET_LOGGER_PTR, name, 0));
}

ITV8::Analytics::IEventArgsAdjuster* CNoOpDetectorEventRaiser::GetEventArgsAdjuster()
{
    return m_adjuster.get();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CDetectorEventRaiser::CDetectorEventRaiser(DECLARE_LOGGER_ARG, 
    NMMSS::PDetectorEventFactory factory,
    const char* name, ITV8::timestamp_t time, ITV8::Analytics::EEventPhase phase)
    : CBaseDetectorEventRaiser(GET_LOGGER_PTR, time)
{
    INIT_LOGGER_HOLDER;

    // TODO: Normalize timestamp, as we do for video frames.
    m_event = factory->CreateEvent(name, convertPhase(GET_LOGGER_PTR, phase), getPtimeFromTimestamp(time));
    m_adjuster.reset(new EventArgsAdjuster(GET_LOGGER_PTR, name, m_event));
}

CDetectorEventRaiser::CDetectorEventRaiser(DECLARE_LOGGER_ARG,
    NMMSS::PDetectorEventFactory factory, 
    const char* metadataType, ITV8::timestamp_t time,
    boost::shared_ptr<CFaceTrackerWrap> faceTrackerWrap)
    : CBaseDetectorEventRaiser(GET_LOGGER_PTR, time)
{
    INIT_LOGGER_HOLDER;

    // TODO: Normalize timestamp, as we do for video frames.
    const char* schemaName = convertMetadataType(GET_LOGGER_PTR, metadataType);
    m_event = factory->CreateAppData(schemaName, getPtimeFromTimestamp(time));

    if (!strcmp(metadataType, ITV8_METADATA_TYPE_HUMAN_BONE_TARGET_LIST))
    {
        m_adjuster.reset(new BoneDetectorArgsAdjuster(GET_LOGGER_PTR, metadataType, m_event, factory, time));
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_GLOBAL_TRACK_LIST))
    {
        m_adjuster.reset(new GlobalTrackerArgsAdjuster(GET_LOGGER_PTR, metadataType, m_event));
    }
    else if (!strcmp(schemaName, "Mask")) // no 'vmda' functioality just mmevent
    {
        m_adjuster.reset(new CMaskEventDataAdjuster(GET_LOGGER_PTR, metadataType, m_event));
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_TEMPERATURE_MATRIX))
    {
        m_adjuster.reset(new TemperatureDetectorArgsAdjuster(GET_LOGGER_PTR, metadataType, m_event));
    }
    else if (!strcmp(metadataType, ITV8_METADATA_TYPE_DEPTH_MASK))
    {
        m_adjuster.reset(new DepthDetectorArgsAdjuster(GET_LOGGER_PTR, metadataType, m_event));
    }
    else
    {
        m_adjuster.reset(new AppDataArgsAdjuster(GET_LOGGER_PTR, metadataType, m_event, factory, time, faceTrackerWrap));
    }
}

ITV8::Analytics::IEventArgsAdjuster* CDetectorEventRaiser::GetEventArgsAdjuster()
{
    return m_adjuster.get();
}

void CDetectorEventRaiser::Commit()
{
    if (m_event != 0)
        m_event->Commit();
    delete this;
}

void CDetectorEventRaiser::Cancel()
{
    if (m_event != 0)
        m_event->Cancel();
    delete this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CProlongatedDetectorEventRaiser::CProlongatedDetectorEventRaiser(DECLARE_LOGGER_ARG,
    NMMSS::PDetectorEventFactory factory, const char* name,
    ITV8::timestamp_t time, ITV8::Analytics::EEventPhase phase,
    boost::asio::io_service& io, IEventFactoryPtr eventFactory)
    : CBaseDetectorEventRaiser(GET_LOGGER_PTR, time)
    , m_timer(io)
    , m_duration(boost::posix_time::max_date_time)
    , m_eventFactory(eventFactory)
{
    m_scheduleTime = boost::posix_time::microsec_clock::universal_time();
    m_eventTime = getPtimeFromTimestamp(time);
    if (m_eventTime.is_special())
        m_eventTime = boost::posix_time::microsec_clock::universal_time();

    m_event = factory->CreateEvent(name, convertPhase(GET_LOGGER_PTR, phase), m_eventTime);
    m_adjuster.reset(new EventArgsAdjuster(GET_LOGGER_PTR, name, m_event));
}

ITV8::Analytics::IEventArgsAdjuster* CProlongatedDetectorEventRaiser::GetEventArgsAdjuster()
{
    return m_adjuster.get();
}

void CProlongatedDetectorEventRaiser::Commit()
{
    m_timer.expires_from_now(m_duration);
    m_timer.async_wait(boost::bind(&ITimedEventRaiser::handle_timeout,
        shared_from_this(), _1));
}

void CProlongatedDetectorEventRaiser::Cancel()
{
    if (m_event != 0)
        m_event->Cancel();
    m_adjuster.reset();
}

bool CProlongatedDetectorEventRaiser::Prolongate()
{
    // Начало нового события пытается продлить время жизни на достаточно длительное время
    bool res = m_timer.expires_from_now(FAKE_EVENT_DURATION) > 0;
    if (res)
    {
        m_timer.async_wait(boost::bind(&ITimedEventRaiser::handle_timeout,
            shared_from_this(), _1));
    }
    return res;
}

bool CProlongatedDetectorEventRaiser::DelayCommit(boost::posix_time::time_duration td)
{
    m_duration = td;
    if (m_timer.expires_from_now(td) > 0)
    {
        m_timer.async_wait(boost::bind(&ITimedEventRaiser::handle_timeout,
            shared_from_this(), _1));
        return true;
    }
    return false;
}

void CProlongatedDetectorEventRaiser::SetCommitTime(boost::posix_time::time_duration td)
{
    m_duration = td;
}

void CProlongatedDetectorEventRaiser::Stop()
{
    if (m_timer.cancel() > 0)
        commitEvent();
}

void CProlongatedDetectorEventRaiser::handle_timeout(const boost::system::error_code& error)
{
    if (!error)
        commitEvent();
}

void CProlongatedDetectorEventRaiser::commitEvent()
{
    if (m_event != 0)
    {
        boost::posix_time::time_duration td = boost::posix_time::microsec_clock::universal_time() - m_scheduleTime;
        m_event->SetTimestamp(m_eventTime + td);
        m_event->Commit();
    }
    m_adjuster.reset();
}
