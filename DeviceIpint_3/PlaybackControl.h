#ifndef DEVICEIPINT3_PLAYBACKCONTROL_H
#define DEVICEIPINT3_PLAYBACKCONTROL_H

#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/strand.hpp>
#include <boost/function.hpp>
#include <boost/thread/mutex.hpp>

#include <ItvSdk/include/baseTypes.h>

#include "IObjectsGroupHolder.h"

#include <Logging/log2.h>

namespace IPINT30
{
namespace detail
{
    class PlaybackStateMachine;
}

class PlaybackControl
{
public:
    typedef boost::function<void (ITV8::hresult_t)> callback_t;
    explicit PlaybackControl(boost::asio::io_service& service, 
        callback_t errorCallback, 
        int underflowThreshold = 100,
        int overflowThreshold = 300);

    template<typename TRecordingPlayback, typename TObservableQueue>
    void start(ITV8::timestamp_t time,
        const std::string& readerName,
        TRecordingPlayback recordingPlayback,
        TObservableQueue& observableQueue,
        IObjectsGroupHolderSP stateHolder,
        DECLARE_LOGGER_ARG);

    struct IWrappedPlayback;
    typedef boost::shared_ptr<IWrappedPlayback> IWrappedPlaybackSP;

    void terminate();

private:
    typedef boost::shared_ptr<detail::PlaybackStateMachine> StateMachineSP;
    StateMachineSP createStateMachine(IObjectsGroupHolderSP stateHolder,
        IWrappedPlaybackSP playback, const std::string& readerName, DECLARE_LOGGER_ARG);
    boost::function<void(int)> getQueueObserver(StateMachineSP machine);
    void registerAndStart(StateMachineSP machine, ITV8::timestamp_t timestamp);

    template<typename TEvent>
    void postEvent();

private:
    typedef boost::weak_ptr<detail::PlaybackStateMachine> StateMachineWP;
    StateMachineSP getStateMachine();

private:
    boost::asio::io_service&             m_service;
    callback_t                            m_errorCallback;
    boost::mutex                        m_stateMachineGuard;
    StateMachineWP                        m_stateMachine;
    const int                            m_underflowThreshold;
    const int                            m_overflowThreshold;
};
}

#endif

