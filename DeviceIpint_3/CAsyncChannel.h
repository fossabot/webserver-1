#ifndef DEVICEIPINT3_ISYNCDEVICECHANNELHANDLERIMPL_H
#define DEVICEIPINT3_ISYNCDEVICECHANNELHANDLERIMPL_H

#include "sdkHelpers.h"
#include "CChannel.h"

namespace IPINT30
{

template<class T>
class CAsyncChannel : public CChannel, public T
{
protected:
    CAsyncChannel(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec,
        boost::shared_ptr<IPINT30::IIpintDevice> parent, INotifyState* notifier);

    // Performs additional, custom processing when signal lost. The driver methods calling  
    // from this one is not safe. You should use another thread to call driver method.
    virtual void OnSignalLost()
    {
    }

//ITV8::GDRV::IAsyncDeviceChannelHandler
private:
    virtual void Started    (ITV8::IContract* source);
    virtual void Stopped    (ITV8::IContract* source);
    virtual void SignalLost (ITV8::IContract* source, ITV8::hresult_t errorCode);
};

template<class T>
CAsyncChannel<T>::CAsyncChannel(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec,
                                boost::shared_ptr<IPINT30::IIpintDevice> parent, INotifyState* notifier)
                                : WithLogger(GET_LOGGER_PTR)
                                , CChannel(GET_LOGGER_PTR, dynExec, parent, notifier)
{
}

//ITV8::GDRV::IAsyncDeviceChannelHandler implementation
template<class T>
void CAsyncChannel<T>::Started(ITV8::IContract* source)
{
    _log_ << ToString() << " Started.";
    OnStarted();

    SetFlag(cfStarted, true);

    this->Notify(NMMSS::IPDS_SignalRestoredOnStart);

    CChannel::RaiseChannelStarted();
}

//ITV8::GDRV::IAsyncDeviceChannelHandler implementation
template<class T>
void CAsyncChannel<T>::Stopped(ITV8::IContract* source)
{
    _log_ << ToString() << " Stopped.";
    OnStopped();

    this->Notify(NMMSS::IPDS_SignalLostOnStop);
    SetFlag(cfSignal, false);

    // Notify thread which called Stop (see CChannel::onDoStop())
    CChannel::RaiseChannelStopped();
}

//ITV8::GDRV::IVideoSourceHandler implementation
template<class T>
void CAsyncChannel<T>::SignalLost(ITV8::IContract* source, ITV8::hresult_t error)
{
    this->Notify(NMMSS::IPDS_SignalLost);

    std::string message = get_last_error_message(source, error);
    _log_ << ToString() << " Signal lost, err:" << message << std::endl;
    SetFlag(cfSignal, false);

    OnSignalLost();
}

}//namespace IPINT30

#endif// DEVICEIPINT3_ISYNCDEVICECHANNELHANDLERIMPL_H
