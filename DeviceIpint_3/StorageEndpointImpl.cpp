#include "StorageEndpointImpl.h"

#include "RecordingPlayback.h"
#include "PlaybackControl.inl"


namespace IPINT30
{
namespace
{
bool frameFilter(BufferWrapper buffer)
{
    // Empty frame is used for EOS signals.
    if (!buffer.Buffer)
    {
        return true;
    }
    using namespace ITV8::MFF;
    const BufferTypes frameType = buffer.Buffer->GetBufferType();
    return frameType == Compressed || frameType == Composite || frameType == Planar;
}

bool frameFilterAudio(BufferWrapper buffer)
{
    // Empty frame is used for EOS signals.
    if (!buffer.Buffer)
    {
        return true;
    }
    using namespace ITV8::MFF;
    const BufferTypes frameType = buffer.Buffer->GetBufferType();
    return frameType == AudioPcm || frameType == AudioG7xx || frameType == AudioCompressed;
}
}

StorageEndpointImpl::StorageEndpointImpl(int playbackSpeed, DECLARE_LOGGER_ARG) :
    m_reactor(NCorbaHelpers::GetReactorInstanceShared()),
    m_service(m_reactor->GetIO()),
    m_playbackSpeed(playbackSpeed),
    m_withAudio(false)
{
    INIT_LOGGER_HOLDER;
    _dbg_ << "Created StorageEndpointImpl = " << this; 
}


StorageEndpointImpl::~StorageEndpointImpl()
{
    _dbg_ << "Destroyed StorageEndpointImpl = " << this;
}

void StorageEndpointImpl::start(IObjectsGroupHolderSP holder,
    RecordingPlaybackFactory playbackFactory,
    IMultimediaFrameFactorySP frameFactory,
    ITV8::timestamp_t timestamp,
    const std::string& readerName,
    bool isReverse,
    bool withAudio,
    videoFrameCallback_t frameCallback,
    eventCallback_t eventCallback,
    errorNotifyHandler_t errorNotifyHandler)
{
    // Creating adapter
    PushToPullAdapterSP adapterCopy;
    PushToPullAdapterSP audioAdapterCopy;
    {
        boost::mutex::scoped_lock lock(m_guard);
        m_adapterQueue = boost::make_shared<PushToPullAdapter_t>(
            boost::bind(&frameFilter, _1),
            frameCallback,
            boost::ref(m_service),
            holder,
            GET_LOGGER_PTR);

        adapterCopy = m_adapterQueue;

        m_audioAdapterQueue = boost::make_shared<PushToPullAdapter_t>(
            boost::bind(&frameFilterAudio, _1),
            frameCallback,
            boost::ref(m_service),
            holder,
            GET_LOGGER_PTR);

        audioAdapterCopy = m_audioAdapterQueue;
    }
    m_withAudio = withAudio;
    // Send empty frame to signal EOS on errors.
    auto errorHandler = [errorNotifyHandler, adapterCopy, this](ITV8::hresult_t e)
    {
        _err_ << "StorageEndpointImpl = " << this << " error occurred: " << e << ". Send empty frame to signal EOS.";
        errorNotifyHandler(e);
        adapterCopy->receiveFrame({ IMultimediaBufferSP(), e });
    };

    _dbg_ << "Start StorageEndpointImpl = " << this << ", readerName = " << readerName
        << ", adapter(v) = " << adapterCopy.get() << ", adapter(a) = " << audioAdapterCopy.get();
    // Creating ipint object.
    boost::shared_ptr<RecordingPlayback>  recordingPlayback(new RecordingPlayback(
            [this, adapterCopy, audioAdapterCopy](IMultimediaBufferSP frame)
            {
                adapterCopy->receiveFrame({ frame });

                if (this->m_withAudio)
                    audioAdapterCopy->receiveFrame({ frame });
            },
            eventCallback,
            errorHandler,
            playbackFactory,
            frameFactory,
            m_playbackSpeed,
            isReverse));

    m_playbackControl = boost::make_shared<PlaybackControl>(boost::ref(m_service), errorHandler);

    m_playbackControl->start(timestamp, readerName, recordingPlayback, 
        *adapterCopy, holder, GET_LOGGER_PTR);
}

int StorageEndpointImpl::reset()
{
    _dbg_ << "Reset StorageEndpointImpl = " << this;
    return detach();
}

int StorageEndpointImpl::detach()
{
    _dbg_ << "Detach StorageEndpointImpl = " << this;

    // Finalizing playback objects
    PPlaybackControl control;
    PushToPullAdapterSP adapter;
    PushToPullAdapterSP audioAdapter;
    {
        boost::mutex::scoped_lock lock(m_guard);
        m_playbackControl.swap(control);
        m_audioAdapterQueue.swap(audioAdapter);
        m_adapterQueue.swap(adapter);
    }
    if (control)
        control->terminate();

    if (audioAdapter)
        audioAdapter->detach();

    int dept = 0;
    if (adapter)
        dept = adapter->detach();
    return dept;
}

void StorageEndpointImpl::setWithAudio(bool state)
{
    m_withAudio = state;
}

void StorageEndpointImpl::request(int count, int farmeType)
{
    PushToPullAdapterSP adapter;
    {
        boost::mutex::scoped_lock lock(m_guard);
        adapter = farmeType == NMMSS::NMediaType::Audio::ID ? m_audioAdapterQueue :m_adapterQueue;
    }
    if (adapter)
    {
        adapter->request(count);
    }
}

}
