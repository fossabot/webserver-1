#ifndef DEVICEIPINT3_RECORDINGPLAYBACK_H
#define DEVICEIPINT3_RECORDINGPLAYBACK_H

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <ItvDeviceSdk/include/IRecordingPlayback.h>
#include <ItvDeviceSdk/include/IStorageDevice.h>
#include <ItvDetectorSdk/include/ItvDetectorsSdk2.h>

namespace IPINT30
{

typedef boost::shared_ptr<ITV8::GDRV::IRecordingPlayback> IRecordingPlaybackSP;
typedef boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> IMultimediaBufferSP;
typedef boost::shared_ptr<ITV8::MFF::IMultimediaFrameFactory> IMultimediaFrameFactorySP;
typedef boost::function<ITV8::Analytics::IDetectorEventRaiser*(bool, ITV8::IContract* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)> eventCallback_t;
// TODO: handle max speed with extending IPINT interface

class RecordingPlayback : public ITV8::GDRV::IRecordingPlaybackHandler
{
public: 
    typedef boost::function<void(IMultimediaBufferSP)> videoFrameCallback_t;
    typedef boost::function<void (ITV8::hresult_t)> errorCallback_t;
    typedef boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> IMultimediaBufferSP;

    template<typename TPlaybackFactory>
    RecordingPlayback(videoFrameCallback_t videoFrameCallback,
        eventCallback_t eventCallback,
        errorCallback_t endOfStream,
        TPlaybackFactory& factory,
        IMultimediaFrameFactorySP frameFactory,
        int playbackSpeed,
        bool isReverse);

    ~RecordingPlayback();

    void play(errorCallback_t callback);
    void pause(errorCallback_t callback);
    void seek(ITV8::timestamp_t timestamp, errorCallback_t callback);
    void teardown(errorCallback_t callback);

private:
    virtual void MultimediaFrameReceived(ITV8::IContract* source,
        const char* trackId, ITV8::MFF::IMultimediaBuffer* buffer);

    virtual ITV8::Analytics::IDetectorEventRaiser* BeginOccasionalEventRaising(
        ITV8::IContract* sender,
        const char* trackId,
        const char* name,
        ITV8::timestamp_t time,
        ITV8::uint32_t phase);

    virtual ITV8::Analytics::IDetectorEventRaiser* BeginPeriodicalEventRaising(
        ITV8::IContract* sender,
        const char* trackId,
        const char* metadataType,
        ITV8::timestamp_t time);

    virtual void EndOfStream(ITV8::hresult_t errorCode);
    virtual void Torndown();

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingPlaybackHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingMediaHandler)
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, IRecordingMediaHandler)
    ITV8_END_CONTRACT_MAP()

private:
    videoFrameCallback_t        m_videoFrameCallback;
    eventCallback_t             m_eventCallback;
    errorCallback_t             m_endOfStream;
    bool                        m_shutdownNotified;
    IRecordingPlaybackSP        m_playback;
    errorCallback_t             m_shutdownCallback;
    boost::mutex                m_shutdownGuard;
    IMultimediaFrameFactorySP   m_frameFactory;
    const int                   m_playbackSpeed;
    const bool                  m_isReverse;
};

struct RecordingPlaybackStub : public ITV8::GDRV::IRecordingPlayback
{
    RecordingPlaybackStub(ITV8::GDRV::IRecordingPlaybackHandler* handler)
        : m_handler(handler)
    {
    }
    void Destroy() override
    {
        delete this;
    }
    void Teardown() override
    {
        if (m_handler)
            m_handler->Torndown();
    }
    void Play(ITV8::double_t speed, ITV8::IAsyncActionHandler* handler) override
    {
        handler->Finished(this, ITV8::EUnsupportedCommand);
    }
    void Seek(ITV8::timestamp_t time, ITV8::IAsyncActionHandler* handler) override
    {
        handler->Finished(this, ITV8::EUnsupportedCommand);
    }
    void Step(ITV8::int32_t direction, ITV8::IAsyncActionHandler* handler) override
    {
        handler->Finished(this, ITV8::EUnsupportedCommand);
    }
    ~RecordingPlaybackStub()
    {
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::IRecordingPlayback)
        ITV8_CONTRACT_ENTRY(ITV8::IDestroyable)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IRecordingPlayback)
    ITV8_END_CONTRACT_MAP()

    ITV8::GDRV::IRecordingPlaybackHandler* m_handler;
};

template<typename TPlaybackFactory>
RecordingPlayback::RecordingPlayback(videoFrameCallback_t videoFrameCallback,
        eventCallback_t eventCallback,
        errorCallback_t endOfStream,
        TPlaybackFactory& factory,
        IMultimediaFrameFactorySP frameFactory,
        int playbackSpeed,
        bool isRevrse) :
    m_videoFrameCallback(videoFrameCallback),
    m_eventCallback(eventCallback),
    m_endOfStream(endOfStream),
    m_shutdownNotified(false),
    m_frameFactory(frameFactory),
    m_playbackSpeed(playbackSpeed),
    m_isReverse(isRevrse)
{
    m_playback = factory.create(frameFactory.get(), this);
    if (!m_playback)
        m_playback.reset(new RecordingPlaybackStub(this));
}


}

#endif

