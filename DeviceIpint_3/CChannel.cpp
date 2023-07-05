#include <ItvSdkWrapper.h>
#include "CChannel.h"
#include "sdkHelpers.h"
#include "CIpInt30.h"
// for ITVSDKUTILES::CreateFrameFactory
#include "../ItvSdkUtil/ItvSdkUtil.h"
// for NMMSS::SAllocatorRequirements
#include "../ConnectionBroker.h"

#include <boost/foreach.hpp>
#include <boost/interprocess/detail/os_thread_functions.hpp>

namespace IPINT30
{

const std::string VIDEO_SOURCE_ENDPOINT("SourceEndpoint.video:%d:%s");

std::string ToVideoAccessPoint(int channelId, const std::string& streamId, const std::string& objectContext)
{
    std::string accessPoint = (boost::format(VIDEO_SOURCE_ENDPOINT) % channelId % streamId).str();
    return objectContext.empty() ? accessPoint : objectContext + "/" + accessPoint;
}

std::string ToVideoAccessPoint(int channelId, int streamId, const std::string& objectContext)
{
    return ToVideoAccessPoint(channelId, std::to_string(streamId), objectContext);
}

CChannel::CChannel(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent, INotifyState* notifier):
    NLogging::WithLogger(GET_LOGGER_PTR),
	m_flags(cfApplicationActive),
	m_dynExec(dynExec),
	m_parent(parent),
    m_stopping(false),
    m_starting(false),
    m_notifier(0)
{
    _dbg_ << " CChannel() [0x" << std::hex << this << "]" << std::endl;
}

CChannel::~CChannel()
{
    _dbg_ << " ~CChannel() [0x" << std::hex << this << "]" << std::endl;
}

// Begin asynchronous operation of stopping the channel work. 
void CChannel::BeginFinalization()
{
    _log_ << ToString() <<  " Begin finalization."<<std::endl;
    SetFlag(cfApplicationActive, false);
}

// Completes asynchronous operation of stopping the channel work. 
void CChannel::EndFinalization()
{
    _log_ << ToString() <<  " End finalization."<<std::endl;

    {
        boost::recursive_mutex::scoped_lock locker(m_flagsMutex);

        if(GetFlag(cfStarted))
        {
            _log_ << ToString() <<  " It waits Stopped()..."<<std::endl;
            
            if( !m_condition.timed_wait(locker, 
                boost::posix_time::seconds(CChannel::DRIVER_OPERATION_CONFIRM_TIMEOUT+2)))
            {
                _err_ << ToString() <<  " The timeout period of completing DoStop() operation elapsed. "\
                    "The IAsyncDeviceChannelHandler::Stopped(...) wasn't called by driver."<<std::endl;

                if(GetFlag(cfStarted))
                    m_condition.wait(locker);
            }
        }
    }

    WaitForStop();

    // Now IPINT object can be safe destroyed.
    OnFinalized();
    _log_ << ToString() <<  " End finalization - Ok"<<std::endl;
    m_parent.reset();
}

void CChannel::SetFlag(EChannelFlags flag, bool value)
{
    boost::recursive_mutex::scoped_lock lock(m_flagsMutex);
    unsigned int oldFlags = m_flags;
    if (value)
    {
        m_flags |= flag;
    }
    else
    {
        m_flags &= ~flag;
    }
    lock.unlock();

	OnFlagsChanged(oldFlags, m_flags);
}

bool CChannel::GetFlag(EChannelFlags flag)const
{
	boost::recursive_mutex::scoped_lock lock(m_flagsMutex);
	return (m_flags & flag) == flag;
}

void CChannel::OnFlagsChanged(unsigned int oldValue, unsigned int newValue)
{
    if ((oldValue&cfEnabled) != cfEnabled && (newValue&cfEnabled) == cfEnabled)
    {
        _inf_ << ToString() << " OnEnabled()..." << std::endl;
        OnEnabled();
        _inf_ << ToString() << " OnEnabled() completed." << std::endl;
    }
    else if ((oldValue&cfEnabled) == cfEnabled && (newValue&cfEnabled) != cfEnabled)
    {
        PostStop();

        WaitForStop();

        _inf_ << ToString() << " OnDisabled()..." << std::endl;
        OnDisabled();
        _inf_ << ToString() << " OnDisabled() completed." << std::endl;

        return;
    }

    //Если после изменения флага окружение (соединения с устройством и получателями аудио)
    //готово к активации канала.
    if((oldValue&cfEnvironmentReady)!=cfEnvironmentReady && 
	    (newValue&cfEnvironmentReady)==cfEnvironmentReady)
    {
	    PostStart();
    }
    //Если после изменения флага канал не может продолжать работу
    else if((oldValue&cfEnvironmentReady)==cfEnvironmentReady && 
        (newValue&cfEnvironmentReady)!=cfEnvironmentReady)
    {
        PostStop();
    }
}

void CChannel::PostStart()
{
    // Start channel in separate thread.
    _log_ << ToString() << " post DoStart() " << std::endl;

    {
        boost::recursive_mutex::scoped_lock lock(m_startingMutex);
        m_starting = true;
    }

    WaitForStop();
    AddRef();
    if (!m_dynExec->Post(boost::bind(&CChannel::DoStart, this)))
        _err_ << "Can't post DoStart() to thread pool!";
}

void CChannel::PostStop()
{
    // Stop channel in separate thread.
    _log_ << ToString() << " post onDoStop() " << std::endl;

    // Because we cannot cancel async operation Start, so waiting for Started signal here
    if (!GetFlag(cfStarted))
    {
        WaitForStart();
    }

    {
        boost::recursive_mutex::scoped_lock lock(m_stoppingMutex);
        while (m_stopping)
        {
            m_channelStoppingCondition.wait(lock);
        }
        m_stopping = true;
    }
    AddRef();
    if (!m_dynExec->Post(boost::bind(&CChannel::onDoStop, this)))
        _err_ << "Can't post onDoStop() to thread pool!";
}

void CChannel::WaitForStop()
{
    boost::recursive_mutex::scoped_lock lock(m_stoppingMutex);
    while (m_stopping)
    {
        m_channelStoppingCondition.wait(lock);
    }
}

void CChannel::WaitForStart()
{
    boost::recursive_mutex::scoped_lock lock(m_startingMutex);
    while (m_starting)
    {
        m_channelStartingCondition.wait(lock);
    }
}

void CChannel::RaiseChannelStopped()
{
    // Т.к. синхонные каналы не требуют остановки  сразу шлем сигнал, что канал остановлен.
    boost::recursive_mutex::scoped_lock locker(m_stoppingMutex);
    m_stopping = false;
    m_channelStoppingCondition.notify_all();
}

void CChannel::RaiseChannelStarted()
{
    boost::recursive_mutex::scoped_lock lock(m_startingMutex);
    m_starting = false;
    m_channelStartingCondition.notify_one();
}

// Do stop for channels not implemented  IAsyncDeviceChannelHandler interface like ITelemetry
void CChannel::DoStop()
{
    RaiseChannelStopped();
}

void CChannel::onDoStop()
{
    DoStop();
    {
        boost::recursive_mutex::scoped_lock locker(m_stoppingMutex);
        if(m_stopping)
        {
            if( !m_channelStoppingCondition.timed_wait(locker, 
                boost::posix_time::seconds(int(CChannel::DRIVER_OPERATION_CONFIRM_TIMEOUT))))
            {
                _err_ << ToString() <<  " The timeout period of waiting Stopped() event elapsed. "\
                    "The IAsyncDeviceChannelHandler::Stopped(...) wasn't called by driver."<<std::endl;

                while(m_stopping)
                    m_channelStoppingCondition.wait(locker);
            }
        }
    }

    boost::recursive_mutex::scoped_lock locker(m_flagsMutex);
    SetFlag(cfStarted, false);
    m_condition.notify_one();
    locker.unlock();

    Release();
}

ITV8::GDRV::IDevice* CChannel::getDevice() const
{
	if(!m_parent || !m_parent->getDevice() )
	{
		throw std::runtime_error("CAudioSource::getDevice() IDevice instance not initialized.");
	}

	return m_parent->getDevice();
}

void CChannel::SetValues(ITV8::IAsyncAdjuster *adjuster, 
                         const NStructuredConfig::TCustomParameters& params)
{
    BOOST_FOREACH(const NStructuredConfig::SCustomParameter& param, params)
    {
        std::string name = param.name;
        //Чиним расхождения в XSD и реализации драйверов IPINT
#ifndef PROBLEM_WITH_TAG_NAMES_IN_REP_FILES_RESOLVED
        if(name == "audioCodec")
        {
            name = "codec";
        }
        else if(name == "FPS")
        {
            name = "fps";
        }
        else if(name == "TVStandard")
        {
            name = "tvStandard";
        }
        else if(name == "videoCodec")
        {
            name = "codec";
        }
#endif
        SetValue(adjuster, param.Type(), name, param.ValueUtf8());
    }
}

void CChannel::SetValue(ITV8::IAsyncAdjuster *adjuster, const std::string& type,
    const std::string& name, const std::string& value)
{
    try
    {
        _inf_ << ToString()
            << (boost::format(" SetValue(\"%1%\", %2%:%3%)") % name % value % type)
            << std::endl;

        ITV8::set_value(adjuster, type, name, value);
    }
    catch (const std::runtime_error& e)
    {
        _wrn_ << ToString() << " ITV8::set_value threw runtime_error exception :"
            << e.what() << std::endl;
    }
    catch (const boost::bad_lexical_cast& e)
    {
        _wrn_ << ToString() << " ITV8::set_value threw bad_lexical_cast exception :"
            << e.what() << std::endl;
    }
}
void CChannel::SetNotifier(INotifyState* notifier)
{
    boost::mutex::scoped_lock lock(m_mutex);
    m_notifier.reset(notifier);
}

void CChannel::Notify(NMMSS::EIpDeviceState state, Json::Value&& data)
{
    boost::mutex::scoped_lock lock(m_mutex);
    if (m_notifier.get() != 0)
        m_notifier->Notify(state, std::move(data));
}

}
