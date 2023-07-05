#include "StorageEndpoint.h"
#include "StorageSource.h"

#include <boost/make_shared.hpp>

#include <CorbaHelpers/GccUtils.h>
GCC_SUPPRESS_WARNING_BEGIN((-Wunused-local-typedefs))
#include <boost/icl/open_interval.hpp>
GCC_SUPPRESS_WARNING_END()

#include <fstream>

#include <MMCoding/FrameBuilder.h>
#include <ItvSdkUtil/ItvSdkUtil.h>
#include <ItvFramework/TimeConverter.h>
#include <PtimeFromQword.h>
#include <CorbaHelpers/Reactor.h>
#include "TimeStampHelpers.h"

#include "../MediaType.h"
#include "../EndOfStreamSample.h"

namespace IPINT30
{

namespace
{

const int SEEK_THRESHOLD_MS = 2000;
const int ONE_FRAME_BACK_MS = 100;
const boost::icl::open_interval<ITV8::timestamp_t> BEGIN_TIME_BORDERS(
    std::numeric_limits<ITV8::timestamp_t>::min(), std::numeric_limits<ITV8::timestamp_t>::max());

bool IsEOSSample(NMMSS::PSample sample)
{
    return NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&sample->Header());
}

bool IsVideoSample(NMMSS::PSample sample)
{
    return sample->Header().nMajor == NMMSS::NMediaType::Video::ID;
}

bool IsAudioSample(NMMSS::PSample sample)
{
    return sample->Header().nMajor == NMMSS::NMediaType::Audio::ID;
}

}

StorageEndpoint::StorageEndpoint(DECLARE_LOGGER_ARG, 
        const RecordingPlaybackFactory& factory,
        ITV8::timestamp_t begintime,
        std::string readerName,
        IObjectsGroupHolderSP groupHolder,
        int playbackSpeed,
        NMMSS::EPlayModeFlags mode,
        NMMSS::EEndpointStartPosition startPos,
        boost::shared_ptr<ITimedEventFactory> eventFactory,
        deactivateHandler_t handler,
        errorNotifyHandler_t errHandler) :
    m_deactivateEndpointHandler(handler),
    m_errorNotifyHandler(errHandler),
    m_playbackFactory(factory),
    m_beginTime(begintime),
    m_lastStartTime(0),
    m_mode(mode),
    m_startPos(startPos),
    m_sessionId(0),
    m_readerName(readerName),
    m_groupHolder(groupHolder),
    m_sampleCount(0),
    m_sourceConnection(nullptr),
    m_endpointImpl(playbackSpeed, GET_LOGGER_PTR),
    m_lastTs(0),
    m_eventFactory(eventFactory),
    m_measurer(NMMSS::CreateStreamQualityMeasurer()),
    m_needMarkFirstVideoFrameAsDiscontinuity(false)
{
    INIT_LOGGER_HOLDER;
    _dbg_ << ToString() << "created: begintime=" << ipintTimestampToIsoString(m_beginTime)
        << ", mode=" << mode << ", startPos=" << startPos;
}

StorageEndpoint::~StorageEndpoint()
{
    _dbg_ << ToString() << "destroyed";
}

void StorageEndpoint::SetAudioEndpointProxy(NCorbaHelpers::CWeakPtr<AudioStorageEndpointProxy> audioSource)
{
    m_audioEndpointProxy = audioSource;
}

void StorageEndpoint::SetWithAudio(bool state)
{
    m_endpointImpl.setWithAudio(state);
}

const NMMSS::SAllocatorRequirements StorageEndpoint::m_nullAllocatorRequirements(
    NMMSS::GetNullAllocatorFactory());

void StorageEndpoint::OnConnected(TConnection* connection)
{
    _dbg_ << ToString() << "OnConnected: connection=" << connection;
    if (!connection)
    {
        return;
    }

    {
        boost::mutex::scoped_lock lock(m_sourceMutex);
        if (!m_frameFactory)
        {

            NMMSS::PAllocator pAllocator(connection->GetAllocator(this));
            m_frameFactory = ITVSDKUTILES::CreateFrameFactory(GET_LOGGER_PTR, pAllocator.Get(), "StorageEndpointFrameFactory");
        }

        m_sourceConnection = connection;
    }

    start();
}

void StorageEndpoint::OnDisconnected(TConnection* connection)
{
    _dbg_ << ToString() << "OnDisconnected: connection=" << connection
        << ", lastTs=" << ipintTimestampToIsoString(m_lastTs)
        << ", sampleCount=" << m_sampleCount << ", sessionId=" << m_sessionId;

    if (!connection)
    {
        return;
    }
    {
        boost::mutex::scoped_lock lock(m_sourceMutex);
        m_sourceConnection = nullptr;
    }

    m_eventFactory.reset();
    boost::mutex::scoped_lock lock(m_endpointMutex);
    m_endpointImpl.reset();
}

void StorageEndpoint::Seek(boost::posix_time::ptime const& startTime, NMMSS::EEndpointStartPosition startPos, NMMSS::EPlayModeFlags mode, std::uint32_t sessionId)
{
    ITV8::timestamp_t beginTime = toIpintTime(startTime);
    m_sessionId = sessionId;
    const auto preventDestroyPlayback = m_mode == mode &&
        m_startPos == startPos &&
        m_beginTime - beginTime < ONE_FRAME_BACK_MS && ipintNow() - m_lastStartTime < SEEK_THRESHOLD_MS;

    _dbg_ << ToString() << "Seek to " << boost::posix_time::to_iso_string(startTime) << ", startPos="
        << startPos << ", mode=" << mode << ", sessionId=" << sessionId << ", currentTime="
        << ipintTimestampToIsoString(m_beginTime) << ", resetPlayback=" << !preventDestroyPlayback;

    if (preventDestroyPlayback)
    {
        return;
    }

    m_mode = mode;
    m_startPos = startPos;
    int dept = 0;
    {
        boost::mutex::scoped_lock lock(m_endpointMutex);
        dept = m_endpointImpl.reset();
    }

    m_beginTime = beginTime;
    {
        boost::mutex::scoped_lock lock(m_sourceMutex);
        m_sampleCount = 0;
        if (!m_sourceConnection)
            return;
    }

    start();
    m_endpointImpl.request(dept);
}

void StorageEndpoint::Request(unsigned int count /*= 1*/)
{
    m_endpointImpl.request(count);
}

void StorageEndpoint::RequestAudio(unsigned int count /*= 1*/)
{
    m_endpointImpl.request(count, NMMSS::NMediaType::Audio::ID);
}

NMMSS::SAllocatorRequirements StorageEndpoint::GetAllocatorRequirements()
{
    return m_nullAllocatorRequirements;
}

bool isSampleDiscontinuity(NMMSS::PSample sample, ITV8::timestamp_t& lastTs)
{
    if (!sample)
        return false;

    static const boost::posix_time::seconds MAX_STREAM_GAP(3);
    bool isDiscontinuity = abs(static_cast<int64_t>(ITV8::PtimeToTimestamp(NMMSS::PtimeFromQword(sample->Header().dtTimeBegin)))
        - static_cast<int64_t>(lastTs)) > MAX_STREAM_GAP.total_milliseconds();
    lastTs = ITV8::PtimeToTimestamp(NMMSS::PtimeFromQword(sample->Header().dtTimeBegin));

    return isDiscontinuity;
}

ITV8::Analytics::IDetectorEventRaiser* StorageEndpoint::handleOccasionalEventRaising(ITV8::IContract* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)
{
    auto pSender = ITV8::contract_cast<ITV8::Analytics::IDetector>(sender);
    if (m_eventFactory && pSender)
         return m_eventFactory->BeginOccasionalEventRaising(pSender, name, time, phase);
    return nullptr;
}

ITV8::Analytics::IDetectorEventRaiser* StorageEndpoint::handlePeriodicalEventRaising(ITV8::IContract* sender, const char* metadataType, ITV8::timestamp_t time)
{
    auto pSender = ITV8::contract_cast<ITV8::Analytics::IDetector>(sender);
    if (m_eventFactory && pSender)
        return m_eventFactory->BeginPeriodicalEventRaising(pSender, metadataType, time);
    return nullptr;
}


ITV8::Analytics::IDetectorEventRaiser* StorageEndpoint::handleEventRaising(bool isOccasional, ITV8::IContract* sender, const char* name, ITV8::timestamp_t time, ITV8::uint32_t phase)
{
    return isOccasional ? handleOccasionalEventRaising(sender, name, time, phase) : handlePeriodicalEventRaising(sender, name, time);
}

std::string StorageEndpoint::ToString() const
{
    std::ostringstream str;
    str << "StorageEndpoint." << this << "[" << m_readerName << "] ";
    return str.str();
}

void StorageEndpoint::handleFrame(const BufferWrapper& buffer, bool firstFrame)
{
    try
    {
        if (!buffer.Buffer)
        {
            const auto eosPtime = firstFrame
                ? ipintTimestampToPtime(m_beginTime)
                : ipintTimestampToPtime(m_lastTs);
            // Handle end of stream frame
            _inf_ << ToString() << "Reached end of recording. lastTs="
                << boost::posix_time::to_iso_string(eosPtime)
                << ", sampleCount=" << m_sampleCount << ", sessionId=" << m_sessionId;

            NMMSS::PSample eos(new NMMSS::EndOfStreamSample(NMMSS::PtimeToQword(eosPtime)));
            NMMSS::SetSampleSessionId(eos.Get(), m_sessionId);
            push(eos, firstFrame);
            if (buffer.Error && !m_deactivateEndpointHandler.empty())
            {
                _dbg_ << ToString() << "Starting endpoint deactivation process.";
                m_deactivateEndpointHandler();
            }

            return;
        }

        ITVSDKUTILES::ISampleContainer* pSampleContainer =  
            ITV8::contract_cast<ITVSDKUTILES::ISampleContainer>(buffer.Buffer.get());
        if (!pSampleContainer)
            return;

        NMMSS::PSample sample(pSampleContainer->Detach());
        NMMSS::SetSampleSessionId(sample.Get(), m_sessionId);

        bool isDiscontinuity = false;
        if (IsVideoSample(sample))
            isDiscontinuity = isSampleDiscontinuity(sample, m_lastTs);

        push(sample, isDiscontinuity || firstFrame);
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << "std::exception in handleFrame: " << e.what();
    }
}

void StorageEndpoint::start()
{
    m_lastStartTime = ipintNow();
    NCorbaHelpers::CAutoPtr<StorageEndpoint> thisSP(this, NCorbaHelpers::ShareOwnership());
    const bool isReverse = m_mode & NMMSS::PMF_REVERSE;
    if (m_startPos == NMMSS::espOneFrameBack)
    {
        if (boost::icl::contains(BEGIN_TIME_BORDERS, m_beginTime))
        {
            m_beginTime += (isReverse ? ONE_FRAME_BACK_MS : 0 - ONE_FRAME_BACK_MS);
            _inf_ << ToString() << "Handling one frame back beginTime="
                << ipintTimestampToIsoString(m_beginTime) << ", sessionId=" << m_sessionId;
        }
    }
    auto firstFrame = std::make_shared<bool>(true);
    try
    {
        boost::mutex::scoped_lock lock(m_endpointMutex);
        m_endpointImpl.start(m_groupHolder, m_playbackFactory, m_frameFactory, m_beginTime,
            m_readerName, m_mode & NMMSS::PMF_REVERSE, m_audioEndpointProxy != nullptr,
            [firstFrame, thisSP](BufferWrapper buffer)
            {
                thisSP->handleFrame(buffer, *firstFrame);
                *firstFrame = false;
            },
            boost::bind(&StorageEndpoint::handleEventRaising, thisSP, _1, _2, _3, _4, _5),
            m_errorNotifyHandler);

        _log_ << ToString() << "Reset to time " << ipintTimestampToIsoString(m_beginTime)
            << ", sessionId=" << m_sessionId;
    }
    catch (const std::exception& e)
    {
        _err_ << ToString() << "std::exception on storage endpoint start: " << e.what();
    }
}

void StorageEndpoint::push(NMMSS::PSample sample, bool isDiscontinuity)
{
    if (sample)
    {
        if (isDiscontinuity)
            m_needMarkFirstVideoFrameAsDiscontinuity = true;

        if (isDiscontinuity && !sample->Header().IsKeySample())
        {
            _wrn_ << ToString() << "First frame should be key frame! It seems that ipint does not filtered out frames after seek.";
        }

        boost::mutex::scoped_lock lock(m_sourceMutex);
        if (nullptr != m_sourceConnection && (IsVideoSample(sample) || IsEOSSample(sample)))
        {
            if (m_needMarkFirstVideoFrameAsDiscontinuity)
            {
                sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
                m_needMarkFirstVideoFrameAsDiscontinuity = false;
            }
            m_measurer->Update(sample.Get());

            static constexpr uint64_t FRAMES_LOG_PERIOD = 1000ull;
            const auto firstFrame = m_sampleCount == 0;
            _dbg_if_(firstFrame || m_sampleCount % FRAMES_LOG_PERIOD == 0)
                << ToString() << "Receive " << (firstFrame ? 1ull : m_sampleCount)
                << " video frames, currentTime=" << boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(sample->Header().dtTimeBegin));

            auto const sink = NCorbaHelpers::ShareRefcounted(m_sourceConnection->GetOtherSide());
            ++m_sampleCount;
            lock.unlock();
            sink->Receive(sample.Get());
        }

        if (IsAudioSample(sample) || IsEOSSample(sample))
        {
            if (NCorbaHelpers::CAutoPtr<AudioStorageEndpointProxy> p = m_audioEndpointProxy)
            {
                p.Get()->Push(sample);
            }
        }
    }
}


AudioStorageEndpointProxy::AudioStorageEndpointProxy(PStorageSource parent)
    : m_parent(parent)
    , m_measurer(NMMSS::CreateStreamQualityMeasurer())
    , m_sourceConnection(nullptr)
    , m_isFirstFrame(true)
    , m_lastTs(0)
{
}

NMMSS::SAllocatorRequirements AudioStorageEndpointProxy::GetAllocatorRequirements()
{
    return StorageEndpoint::m_nullAllocatorRequirements;
}
void AudioStorageEndpointProxy::OnConnected(TConnection* connection)
{
    if (!connection)
    {
        return;
    }

    boost::mutex::scoped_lock lock(m_sourceMutex);
    m_sourceConnection = connection;
}
void AudioStorageEndpointProxy::OnDisconnected(TConnection* connection)
{
    if (!connection)
    {
        return;
    }

    boost::mutex::scoped_lock lock(m_sourceMutex);
    m_sourceConnection = nullptr;
}
void AudioStorageEndpointProxy::Request(unsigned int count)
{
    PStorageSource parent = m_parent;
    if (parent.Get())
    {
        NMMSS::PSeekableSource vs = parent->GetVideoSource();
        if (auto vse = dynamic_cast<StorageEndpoint*>(vs.Get()))
        {
            std::call_once(m_proxySetted, [&] ()
                {
                    NCorbaHelpers::CWeakPtr<AudioStorageEndpointProxy> weak(this);
                    vse->SetAudioEndpointProxy(weak);
                    vse->SetWithAudio(true);
                });

            vse->RequestAudio(count);
        }
    }
}
void AudioStorageEndpointProxy::Push(NMMSS::PSample sample)
{
    if (m_isFirstFrame || isSampleDiscontinuity(sample, m_lastTs))
        sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
    m_isFirstFrame = false;

    boost::mutex::scoped_lock lock(m_sourceMutex);
    if (nullptr != m_sourceConnection)
    {
        m_measurer->Update(sample.Get());

        auto const sink = NCorbaHelpers::ShareRefcounted(m_sourceConnection->GetOtherSide());
        lock.unlock();
        if (sink)
            sink->Receive(sample.Get());
    }
}

void AudioStorageEndpointProxy::Seek(boost::posix_time::ptime const&, NMMSS::EEndpointStartPosition,
    NMMSS::EPlayModeFlags, std::uint32_t)
{
    // do nothing
}

}
