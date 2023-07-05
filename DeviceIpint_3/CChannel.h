#ifndef DEVICEIPINT3_CCHANNEL_H
#define DEVICEIPINT3_CCHANNEL_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <vector>

#include <ItvDeviceSdk/include/ItvDeviceSdk.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include <Executors/DynamicThreadPool.h>
#include <ItvSdk/include/baseTypes.h>

#include "IIPManager3.h"
#include "../MMSS.h"
#include "IpintDestroyer.h"


namespace NStructuredConfig
{
    struct SCustomParameter;
    typedef std::vector<SCustomParameter> TCustomParameters;
}

namespace IPINT30
{
class CDevice;

std::string ToVideoAccessPoint(int channelId, const std::string& streamId, const std::string& objectContext = "");
std::string ToVideoAccessPoint(int channelId, int streamId, const std::string& objectContext = "");

class IChannel : public virtual NCorbaHelpers::IRefcounted
{
};

class CChannel : public virtual IChannel
    , public virtual NCorbaHelpers::CRefcountedImpl
    , public virtual NLogging::WithLogger
{

public:
    enum ETimeouts
    {
        // The count of seconds for driver operation confirmation.
        DRIVER_OPERATION_CONFIRM_TIMEOUT=300
    };

public:
    CChannel(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent, INotifyState* notifier);

    // The distructor should be virtual !!!
    virtual ~CChannel();

protected:
    //Starts ipint channel in separate thread
    virtual void DoStart()
    {
        Release();
    }

    //Stops ipint channel in separate thread
    virtual void DoStop();

    // Occurs when flag cfEnabled flag changed to true
    virtual void OnEnabled()
    {
        // Do nothing.
    };

    // Occurs when flag cfEnabled flag changed to false
    virtual void OnDisabled()
    {
        // Do nothing.
    }

    // Raises conditinal variable "channel stopped".
    void RaiseChannelStopped();
    // Raises conditinal variable "channel started".
    void RaiseChannelStarted();


protected:
    enum EChannelFlags
    {
        // К объекту подключились получатели данных
        cfSinkConnected = 0x01,
        // Драйвер устройства перешел в режим установления соединения (можно создавать каналы)
        cfDeviceConnected = 0x02,
        // Данный канал включен пользователем
        cfEnabled = 0x04, 
        // Канал запущен
        cfStarted = 0x08,
        // Канал приступил к получению данных
        cfSignal = 0x10,
        // Приложение DeviceIpint не находится в стадии завершения.
        cfApplicationActive = 0x20,
        //окружение подготовлено для активации канала
        cfEnvironmentReady = cfSinkConnected|cfDeviceConnected|cfEnabled|cfApplicationActive
    };

public:
    //Sets flags indicating whether the device connected.
    void SetDeviceConnected(bool value)
    {
        SetFlag(cfDeviceConnected, value);
    }

    //Sets temporary device states.
    virtual void SetDeviceStateChanged(NMMSS::EIpDeviceState state)
    {
    }

    //Sets flags indicating whether the channel enabled.
    void SetEnabled(bool value)
    {
        SetFlag(cfEnabled, value);
    }

    // Gets human readable name of class instance which can use in log.
    virtual std::string ToString() const = 0;

    //Sets flags indicating whether the sink connected to the video/audio sink.
    void SetSinkConnected(bool value)
    {
        SetFlag(cfSinkConnected, value);
    }

    // Begin asynchronous operation of channel finalization.
    virtual void BeginFinalization();

    // Completes asynchronous operation of channel finalization. 
    void EndFinalization();

    NLogging::PLogger GetLogger() const
    {
        return ngp_Logger_Ptr_;
    }

    // Sets to adjuster new the name-value pares from params.
    void SetValues(ITV8::IAsyncAdjuster *adjuster, const NStructuredConfig::TCustomParameters& params);

    void SetValue(ITV8::IAsyncAdjuster *adjuster, const std::string& type,
        const std::string& name, const std::string& value);

    // Base method return invalid id
    virtual int GetChannelId() const { return -1; }
    virtual bool IsChannelEnabled() const { return false; }

protected:

    // Performs additional, custom processing when driver channel started. The driver methods calling 
    // from this one is not safe. You should use another thread to call driver method.
    virtual void OnStarted()
    {
    }

    // Performs additional, custom processing when driver channel started. The driver methods calling  
    // from this one is not safe. You should use another thread to call driver method.
    virtual void OnStopped()
    {
    }

    // Performs additional, custom processing when EndFinalization called and channel finalized.
    virtual void OnFinalized()
    {
    }
protected:

    void SetFlag(EChannelFlags flag, bool value);
    bool GetFlag(EChannelFlags flag)const;
    void OnFlagsChanged(unsigned int oldValue, unsigned int newValue);

    void PostStart();
    void PostStop();
    void WaitForStop();
    void WaitForStart();

    //The mutex locks state flags.
    mutable boost::recursive_mutex m_flagsMutex;
    boost::condition_variable_any m_condition;
    unsigned int m_flags;

    ITV8::GDRV::IDevice* getDevice() const;

    NExecutors::PDynamicThreadPool m_dynExec;
    
    // The owner of this tis class instance.
    boost::shared_ptr<IPINT30::IIpintDevice> m_parent;

    boost::mutex m_switchMutex;

    void SetNotifier(INotifyState* notifier);
    void Notify(NMMSS::EIpDeviceState state, Json::Value&& data = Json::Value());
private:

    template<class> friend class CAsyncChannel;

    mutable boost::recursive_mutex m_stoppingMutex;
    boost::condition_variable_any m_channelStoppingCondition;
    bool m_stopping;

    mutable boost::recursive_mutex m_startingMutex;
    boost::condition_variable_any m_channelStartingCondition;
    bool m_starting;

    void onDoStop();

    boost::mutex m_mutex;
    std::auto_ptr<INotifyState> m_notifier;
};

}
#endif // DEVICEIPINT3_CCHANNEL_H