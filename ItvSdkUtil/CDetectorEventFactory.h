#ifndef ITVSDKUTIL_DETECTOREVENTFACTORY_H
#define ITVSDKUTIL_DETECTOREVENTFACTORY_H

#include <boost/enable_shared_from_this.hpp>

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include <Logging/log2.h>

#include "../MMClient/DetectorEventFactory.h"
#include <boost/asio/io_service.hpp>

class CFaceTrackerWrap;

typedef boost::shared_ptr<CFaceTrackerWrap> PFaceTrackerWrap;

class ITimedEventRaiser;
class ITimedEventFactory : public ITV8::Analytics::IEventFactory
                         , public boost::enable_shared_from_this<ITimedEventFactory>
{
public:
    virtual ITimedEventRaiser* BeginTimedEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase, boost::asio::io_service& io) = 0;
    virtual ITV8::Analytics::IDetectorEventRaiser* BeginNoOpEventRaising(
        const char* name, ITV8::timestamp_t time) = 0;
};

class CDetectorEventFactory : public ITimedEventFactory
{
    DECLARE_LOGGER_HOLDER;

protected:
    // ITV8::IContract implementation
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventFactory)
    ITV8_END_CONTRACT_MAP()

public:
    CDetectorEventFactory(DECLARE_LOGGER_ARG,
        NMMSS::PDetectorEventFactory factory, const char* prefix);

public:
    // ITV8::Analytics::IEventFactory implementation
    virtual ITV8::Analytics::IDetectorEventRaiser* BeginOccasionalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase);

    virtual ITV8::Analytics::IDetectorEventRaiser* BeginPeriodicalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time);

    virtual ITimedEventRaiser* BeginTimedEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase, boost::asio::io_service& io);

    virtual ITV8::Analytics::IDetectorEventRaiser* BeginNoOpEventRaising(
        const char* name, ITV8::timestamp_t time);

private:
    const NMMSS::PDetectorEventFactory m_factory;
    const boost::shared_ptr<CFaceTrackerWrap> m_faceTrackerWrap;
};

class IDeviceNodeEventFactory : public ITimedEventFactory
{
public:
    virtual void NotifyDeviceNodeOperation(const char* operation, const char* uuid, int32_t code) = 0;
};

class CDeviceNodeEventFactory : public IDeviceNodeEventFactory
{
private:
    // ITV8::IContract implementation
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventFactory)
        ITV8_CONTRACT_ENTRY(IDeviceNodeEventFactory)
        ITV8_END_CONTRACT_MAP()

    DECLARE_LOGGER_HOLDER;
public:
    CDeviceNodeEventFactory(DECLARE_LOGGER_ARG,
        NMMSS::PDetectorEventFactory factory, const char* prefix);

public:
    // ITV8::Analytics::IEventFactory implementation
    ITV8::Analytics::IDetectorEventRaiser* BeginOccasionalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase) override;

    ITV8::Analytics::IDetectorEventRaiser* BeginPeriodicalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time) override;

    ITimedEventRaiser* BeginTimedEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase, boost::asio::io_service& io) override;

    ITV8::Analytics::IDetectorEventRaiser* BeginNoOpEventRaising(
        const char* name, ITV8::timestamp_t time) override;

    // IDeviceNodeEventFactory
    void NotifyDeviceNodeOperation(const char* operation, const char* uuid, int32_t code) override;
private:
    const NMMSS::PDetectorEventFactory m_factory;
    const boost::shared_ptr<CFaceTrackerWrap> m_faceTrackerWrap;
};

#endif //ITVSDKUTIL_DETECTOREVENTFACTORY_H

