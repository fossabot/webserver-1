#ifndef ITVSDKUTIL_DETECTOREVENTRAISER_H
#define ITVSDKUTIL_DETECTOREVENTRAISER_H

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/enable_shared_from_this.hpp>

// Привет славной компании Microsoft
#ifdef CreateEvent
#   undef CreateEvent
#endif

#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

#include <Logging/log2.h>

#include "../MMClient/DetectorEventFactory.h"

#include <itv-sdk/ItvSdk/include/VisualPrimitives.h>

class PanasonicVMDMask : public ITV8::IMask
{
public:
    typedef std::vector<char> maskBuffer_t;

    PanasonicVMDMask(const ITV8::Size& size, const maskBuffer_t& buffer) :
        m_buffer(buffer),
        m_size(size)
    {
        assert(m_buffer.size());
    }

    virtual const ITV8::uint8_t* GetMask() const
    {
        return reinterpret_cast<const ITV8::uint8_t*>(&m_buffer.front());
    }

    virtual ITV8::Size GetSize() const
    {
        return m_size;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IMask)
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_END_CONTRACT_MAP()

private:
    maskBuffer_t		m_buffer;
    ITV8::Size			m_size;
};


class CFaceTrackerWrap;

class CBaseDetectorEventRaiser : public virtual ITV8::Analytics::IDetectorEventRaiser
{
protected:
    // ITV8::IContract implementation
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::IEventRaiser)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IDetectorEventRaiser)
        ITV8_END_CONTRACT_MAP()

        DECLARE_LOGGER_HOLDER;

public:
    CBaseDetectorEventRaiser(DECLARE_LOGGER_ARG, ITV8::timestamp_t time);

    virtual ITV8::timestamp_t GetTime() const;
    virtual void Commit();
    virtual void Cancel();

protected:
    ITV8::timestamp_t m_timestamp;
};

class CNoOpDetectorEventRaiser : public CBaseDetectorEventRaiser
{
public:
    CNoOpDetectorEventRaiser(DECLARE_LOGGER_ARG, const char* name, ITV8::timestamp_t time);

    virtual ITV8::Analytics::IEventArgsAdjuster* GetEventArgsAdjuster();
private:
    boost::shared_ptr<ITV8::Analytics::IEventArgsAdjuster> m_adjuster;
};

// Translates interface of event riser from IPINT style to ngp style.
class CDetectorEventRaiser : public CBaseDetectorEventRaiser
{
private:
    // ITV8::IContract implementation
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::IEventRaiser)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IDetectorEventRaiser)
    ITV8_END_CONTRACT_MAP()

    DECLARE_LOGGER_HOLDER;

public:
    CDetectorEventRaiser(DECLARE_LOGGER_ARG, 
        NMMSS::PDetectorEventFactory m_factory, 
        const char* name, ITV8::timestamp_t time, ITV8::Analytics::EEventPhase phase);

    CDetectorEventRaiser(DECLARE_LOGGER_ARG, 
        NMMSS::PDetectorEventFactory m_factory,
        const char* metadataType, ITV8::timestamp_t time, boost::shared_ptr<CFaceTrackerWrap> faceTrackerWrap);

public:
    // ITV8::Analytics::IDetectorEventRaiser implementation
    virtual ITV8::Analytics::IEventArgsAdjuster* GetEventArgsAdjuster();

    //virtual ITV8::timestamp_t GetTime() const;

    virtual void Commit();
    virtual void Cancel();

protected:
    // Use Commit or Cancel for object deleting.
    ~CDetectorEventRaiser() {}

private:
    NMMSS::IDetectorEvent* m_event;
    //const ITV8::timestamp_t m_timestamp;
    boost::shared_ptr<ITV8::Analytics::IEventArgsAdjuster> m_adjuster;
};

class ITimedEventRaiser : public virtual ITV8::Analytics::IDetectorEventRaiser
    , public boost::enable_shared_from_this<ITimedEventRaiser>
{
public:
    virtual bool Prolongate() = 0;
    virtual bool DelayCommit(boost::posix_time::time_duration td) = 0;
    virtual void SetCommitTime(boost::posix_time::time_duration) = 0;
    virtual void Stop() = 0;

    virtual void handle_timeout(const boost::system::error_code&) = 0;
};

class ITimedEventFactory;
class IDeviceNodeEventFactory;

typedef boost::shared_ptr<ITimedEventFactory> IEventFactoryPtr;
typedef boost::shared_ptr<IDeviceNodeEventFactory> IDeviceNodeEventFactoryPtr;

class CProlongatedDetectorEventRaiser : public virtual CBaseDetectorEventRaiser
    , public virtual ITimedEventRaiser
{
public:
    CProlongatedDetectorEventRaiser(DECLARE_LOGGER_ARG,
        NMMSS::PDetectorEventFactory m_factory, const char* name,
        ITV8::timestamp_t time, ITV8::Analytics::EEventPhase phase,
        boost::asio::io_service& io, IEventFactoryPtr eventFactory);

    virtual ITV8::Analytics::IEventArgsAdjuster* GetEventArgsAdjuster();
    virtual void Commit();
    virtual void Cancel();

    virtual bool Prolongate();
    virtual bool DelayCommit(boost::posix_time::time_duration td);
    virtual void SetCommitTime(boost::posix_time::time_duration td);
    virtual void Stop();

protected:
    virtual void handle_timeout(const boost::system::error_code& error);
    void commitEvent();

private:
    NMMSS::IDetectorEvent* m_event;
    boost::shared_ptr<ITV8::Analytics::IEventArgsAdjuster> m_adjuster;

    boost::asio::deadline_timer m_timer;
    boost::posix_time::time_duration m_duration;

    boost::posix_time::ptime m_eventTime;
    boost::posix_time::ptime m_scheduleTime;

    IEventFactoryPtr m_eventFactory;
};

#endif // ITVSDKUTIL_DETECTOREVENTRAISER_H
