#ifndef DEVICEIPINT3_PLAYBACKCONTROL_INL
#define DEVICEIPINT3_PLAYBACKCONTROL_INL


namespace IPINT30
{
struct PlaybackControl::IWrappedPlayback
{
    virtual ~IWrappedPlayback() {}
    
    typedef boost::function<void (ITV8::hresult_t)> errorCallback_t;
    virtual void play(errorCallback_t callback) = 0;
    virtual void pause(errorCallback_t callback) = 0;
    virtual void seek(ITV8::timestamp_t timestamp,
        errorCallback_t callback) = 0;
    virtual void teardown(errorCallback_t callback) = 0;

};

namespace detail
{
template<typename TPlayback>
class WrappedPlayback : public PlaybackControl::IWrappedPlayback
{
public:
    explicit WrappedPlayback(TPlayback playback) : 
        m_playback(playback)
    {}

    virtual void play(errorCallback_t callback)
    {
        m_playback->play(callback);
    }
    virtual void pause(errorCallback_t callback)
    {
        m_playback->pause(callback);
    }

    virtual void seek(ITV8::timestamp_t timestamp,
        errorCallback_t callback)
    {
        m_playback->seek(timestamp, callback);
    }

    virtual void teardown(errorCallback_t callback)
    {
        m_playback->teardown(callback);
    }

private:
    TPlayback m_playback;
};

}

template<typename TRecordingPlayback, typename TObservableQueue> void PlaybackControl::start(
    ITV8::timestamp_t seekTime, const std::string& readerName, TRecordingPlayback recordingPlayback, 
    TObservableQueue& observableQueue, IObjectsGroupHolderSP stateHolder, DECLARE_LOGGER_ARG)
{
    IWrappedPlaybackSP playback = boost::make_shared<
        detail::WrappedPlayback<TRecordingPlayback>>(recordingPlayback);

    StateMachineSP machine = createStateMachine(stateHolder, playback, readerName, GET_LOGGER_PTR);
    observableQueue.atachObserver(getQueueObserver(machine));
    registerAndStart(machine, seekTime);
}


}

#endif

