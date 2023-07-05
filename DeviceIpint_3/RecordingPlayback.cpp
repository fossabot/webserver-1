#include "RecordingPlayback.h"

#include <string>

#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <ItvSdk/include/IErrorService.h>

namespace IPINT30
{
namespace
{

class HandlerAdapter;
typedef boost::shared_ptr<HandlerAdapter> HandlerAdapterSP;

class HandlerAdapter : public boost::enable_shared_from_this<HandlerAdapter>,
    private ITV8::IAsyncActionHandler
{
public:
    typedef RecordingPlayback::errorCallback_t callback_t;
    explicit HandlerAdapter(callback_t callback) : 
        m_callback(callback)
    {
    }

    ITV8::IAsyncActionHandler* prepareHandler()
    {
        m_selfReference = shared_from_this();
        return this;
    }

    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::IContract)
        ITV8_CONTRACT_ENTRY(ITV8::IAsyncActionHandler)
    ITV8_END_CONTRACT_MAP()

private:
    virtual void Finished(ITV8::IContract* source, ITV8::hresult_t code)
    {
        m_callback(code);
        m_selfReference.reset();
    }

private:
    boost::shared_ptr<HandlerAdapter>    m_selfReference;
    const callback_t                    m_callback;
};

}

void RecordingPlayback::play(errorCallback_t callback)
{
    m_playback->Play(m_playbackSpeed * (m_isReverse ? -1 : 1),
         boost::make_shared<HandlerAdapter>(callback)->prepareHandler());
}

void RecordingPlayback::pause(errorCallback_t callback)
{
    m_playback->Play(0, 
        boost::make_shared<HandlerAdapter>(callback)->prepareHandler());
}

void RecordingPlayback::seek(ITV8::timestamp_t timestamp, 
                             errorCallback_t callback)
{
    m_playback->Seek(timestamp, 
        boost::make_shared<HandlerAdapter>(callback)->prepareHandler());
}

void RecordingPlayback::MultimediaFrameReceived(ITV8::IContract* source,
                            const char* trackId, ITV8::MFF::IMultimediaBuffer* buffer)
{
    IMultimediaBufferSP wrappedBuffer(buffer, 
        boost::bind(&ITV8::MFF::IMultimediaBuffer::Destroy, _1));
    m_videoFrameCallback(wrappedBuffer);
}

void RecordingPlayback::EndOfStream(ITV8::hresult_t errorCode)
{
    if (!errorCode)
    {
        // Report end of stream sample
        m_videoFrameCallback(IMultimediaBufferSP());
    }
    else
    {
        m_endOfStream(errorCode);
    }
}

RecordingPlayback::~RecordingPlayback()
{
    boost::mutex::scoped_lock lock(m_shutdownGuard);
    if (!m_shutdownNotified)
    {
        // Fail immediately, it is best you can do here.
        abort();
    }
}

void RecordingPlayback::Torndown()
{
    {
        boost::mutex::scoped_lock lock(m_shutdownGuard);
        m_shutdownNotified = true;
    }
    if (m_shutdownCallback)
    {
        m_shutdownCallback(ITV8::ENotError);
    }
    else
    {
        // Fail immediately, it is best you can do here.
        throw std::runtime_error("deleted before torn down!");
    }
}

ITV8::Analytics::IDetectorEventRaiser* RecordingPlayback::BeginOccasionalEventRaising(ITV8::IContract* sender,
    const char* trackId, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)
{
    return m_eventCallback(true, sender, name, time, phase);
}

ITV8::Analytics::IDetectorEventRaiser* RecordingPlayback::BeginPeriodicalEventRaising(ITV8::IContract* sender,
    const char* trackId, const char* metadataType, ITV8::timestamp_t time)
{
    return m_eventCallback(false, sender, metadataType, time, 0);
}

void RecordingPlayback::teardown(errorCallback_t callback)
{
    m_shutdownCallback = callback;
    m_playback->Teardown();
}

}
