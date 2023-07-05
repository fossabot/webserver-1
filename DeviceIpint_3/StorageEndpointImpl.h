#ifndef DEVICEIPINT3_STORAGEENDPOINTIMPL_H
#define DEVICEIPINT3_STORAGEENDPOINTIMPL_H

#include <boost/asio/io_service.hpp>

#include "RecordingPlaybackFactory.h"
#include "PullToPushAdapter.h"
#include "PlaybackControl.h"
#include "IObjectsGroupHolder.h"

#include "../MediaType.h"

#include <Logging/log2.h>

namespace IPINT30
{
typedef boost::shared_ptr<ITV8::MFF::IMultimediaBuffer> IMultimediaBufferSP;

class BufferWrapper
{
public:
    IMultimediaBufferSP Buffer;
    ITV8::hresult_t Error{};
};

typedef PushToPullAdapter<BufferWrapper> PushToPullAdapter_t;
typedef boost::shared_ptr<PushToPullAdapter_t> PushToPullAdapterSP;
typedef boost::shared_ptr<ITV8::MFF::IMultimediaFrameFactory> IMultimediaFrameFactorySP;

using errorNotifyHandler_t = boost::function<void(ITV8::hresult_t)>;

class StorageEndpointImpl
{
    typedef boost::shared_ptr<PlaybackControl> PPlaybackControl;
public:
    typedef boost::function<void(ITV8::hresult_t)> errorCallback_t;
    typedef boost::function<void(BufferWrapper)> videoFrameCallback_t;
    typedef boost::function<ITV8::Analytics::IDetectorEventRaiser*(bool, ITV8::IContract* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)> eventCallback_t;

    StorageEndpointImpl(int playbackSpeed, DECLARE_LOGGER_ARG);

    ~StorageEndpointImpl();

    void start(IObjectsGroupHolderSP holder,
        RecordingPlaybackFactory playbackFactory,
        IMultimediaFrameFactorySP frameFactory,
        ITV8::timestamp_t timestamp,
        const std::string& readerName,
        bool isReverse,
        bool withAudio,
        videoFrameCallback_t frameCallback,
        eventCallback_t eventCallback,
        errorNotifyHandler_t errorNotifyHandler);
    
    int reset();
    int detach();
    void setWithAudio(bool);

    void request(int count, int farmeType = NMMSS::NMediaType::Video::ID);

private:
    DECLARE_LOGGER_HOLDER;
    NCorbaHelpers::PReactor m_reactor;
    boost::asio::io_service& m_service;
    const int m_playbackSpeed;

    PPlaybackControl m_playbackControl;
    PushToPullAdapterSP m_adapterQueue;
    PushToPullAdapterSP m_audioAdapterQueue;
    boost::mutex m_guard;
    std::atomic<bool> m_withAudio;
};

}

#endif

