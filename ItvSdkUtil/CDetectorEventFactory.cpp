#include "CDetectorEventFactory.h"

#include <boost/make_shared.hpp>

#include "CDetectorEventRaiser.h"
#include "CFaceTrackerWrap.h"
#include "EventArgsAdjuster.h"

#include <MMIDL/DeviceNodeS.h>
#include <MMIDL/MMVideoC.h>

CDetectorEventFactory::CDetectorEventFactory(DECLARE_LOGGER_ARG,
    NMMSS::PDetectorEventFactory factory,
    const char* prefix)
    : m_factory(factory)
    , m_faceTrackerWrap(boost::make_shared<CFaceTrackerWrap>())
{
    CLONE_LOGGER;
    ADD_LOG_PREFIX(prefix);
    INIT_LOGGER_HOLDER;
}

ITV8::Analytics::IDetectorEventRaiser* CDetectorEventFactory::BeginOccasionalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)
{
    return new CDetectorEventRaiser(GET_LOGGER_PTR, m_factory, name, time, (ITV8::Analytics::EEventPhase)phase);
}

ITV8::Analytics::IDetectorEventRaiser* CDetectorEventFactory::BeginPeriodicalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time)
{
    return new CDetectorEventRaiser(GET_LOGGER_PTR, m_factory, metadataType, time, m_faceTrackerWrap);
}

ITimedEventRaiser* CDetectorEventFactory::BeginTimedEventRaising(
    ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
    ITV8::uint32_t phase, boost::asio::io_service& io)
{
    return new CProlongatedDetectorEventRaiser(GET_LOGGER_PTR, m_factory, name, time,
        (ITV8::Analytics::EEventPhase)phase, io, shared_from_this());
}

ITV8::Analytics::IDetectorEventRaiser* CDetectorEventFactory::BeginNoOpEventRaising(
    const char* name, ITV8::timestamp_t time)
{
    return new CNoOpDetectorEventRaiser(GET_LOGGER_PTR, name, time);
}

CDeviceNodeEventFactory::CDeviceNodeEventFactory(DECLARE_LOGGER_ARG,
                                                NMMSS::PDetectorEventFactory factory,
                                                const char* prefix)
    : m_factory(factory)
    , m_faceTrackerWrap(boost::make_shared<CFaceTrackerWrap>())
{
    CLONE_LOGGER;
    ADD_LOG_PREFIX(prefix);
    INIT_LOGGER_HOLDER;
}

ITV8::Analytics::IDetectorEventRaiser* CDeviceNodeEventFactory::BeginOccasionalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
    ITV8::uint32_t phase)
{
    return new CDetectorEventRaiser(GET_LOGGER_PTR, m_factory, name, time,
        (ITV8::Analytics::EEventPhase)phase);
}

ITV8::Analytics::IDetectorEventRaiser* CDeviceNodeEventFactory::BeginPeriodicalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time)
{
    return new CDetectorEventRaiser(GET_LOGGER_PTR, m_factory, metadataType, time, m_faceTrackerWrap);
}

ITimedEventRaiser* CDeviceNodeEventFactory::BeginTimedEventRaising(
    ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
    ITV8::uint32_t phase, boost::asio::io_service& io)
{
    return new CProlongatedDetectorEventRaiser(GET_LOGGER_PTR, m_factory, name, time,
        (ITV8::Analytics::EEventPhase)phase, io, shared_from_this());
}

ITV8::Analytics::IDetectorEventRaiser* CDeviceNodeEventFactory::BeginNoOpEventRaising(
    const char* name, ITV8::timestamp_t time)
{
    return new CNoOpDetectorEventRaiser(GET_LOGGER_PTR, name, time);
}

void CDeviceNodeEventFactory::NotifyDeviceNodeOperation(const char* operation, const char* uuid, int32_t code)
{
    auto event = m_factory->CreateEvent(MMSS::DetectorEventTypes::DET_DeviceNodeOperation, NMMSS::AS_Happened, boost::posix_time::microsec_clock::universal_time());
    std::shared_ptr<ITV8::Analytics::IEventArgsAdjuster> args(new EventArgsAdjuster(GET_LOGGER_PTR, MMSS::DetectorEventTypes::DET_DeviceNodeOperation, event));

    if (event && args)
    {
        args->SetValue("operation", operation);
        args->SetValue("id", uuid);
        args->SetValue("result", code);
        event->Commit();
    }
}
