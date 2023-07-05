#include <ItvSdkWrapper.h>
#include "CTextEventSource.h"
#include "CIpInt30.h"
#include "Notify.h"
#include "Utility.h"
#include "ParamContext.h"
#include "TimeStampHelpers.h"

#include <ItvDeviceSdk/include/ITextEventDevice.h>
#include <ItvDeviceSdk/include/infoWriters.h>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/iostreams/stream.hpp>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) //'conversion' conversion from 'type1' to 'type2', possible loss of data
#endif // _MSC_VER

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/device/array.hpp>

#ifdef _MSC_VER
#pragma warning(pop) 
#endif // _MSC_VER


#include <CorbaHelpers/Unicode.h>
#include <CorbaHelpers/CorbaStl.h>
#include <ItvFramework/TimeConverter.h>

#include <ORM_IDL/ORMS.h>
#include "ORM_IDL/ORM.h"
#include <Notification_IDL/Notification.h>
#include "../../mmss/DeviceInfo/include/PropertyContainers.h"
#include "../../mmss/FilterImpl.h"
#include "../../mmss/MakeFourCC.h"
#include "../mIntTypes.h"
#include "../MMClient/MMClient.h"
#include "../MMTransport/SourceFactory.h"
#include "../MMTransport/MMTransport.h"
#include <MMIDL/MMVideoC.h>

namespace
{
const std::string TEXT_EVENT_SOURCE_ENDPOINT("SourceEndpoint.textEvent:%d");
const char TEXT_EVENT_SOURCE_STREAM_ENDPOINT[] = "SourceEndpoint.textEvent:%1%:%2%";
const char TEXT_EVENT_CHANNEL_FORMAT[] = "TextEvent.%d";
const char BILLS_ONLY_META_PROPERTY[] = "billsOnly";
const char SAMPLE_DURATION_PROPERTY[] = "sampleDuration";
const char SAMPLE_OFFSET_PROPERTY[] = "sampleOffset";

const unsigned int MAX_TEXT_SAMPLE_STRING_COUNT = 500;
const unsigned int MAX_TEXT_EVENT_STRING_COUNT = 2000;

class CSubtitleFormatter
{
private:
    DECLARE_LOGGER_HOLDER;
    uint32_t m_formatSize;
    std::unique_ptr<uint8_t[]> m_format;
private:
    void addToBuffer(uint8_t* dest, const void* source, const uint16_t& sourceLength, uint32_t& offset)
    {
        memcpy(dest + offset, source, sourceLength);
        offset += sourceLength;
    }
public:
    CSubtitleFormatter(DECLARE_LOGGER_ARG, const IPINT30::STextFormat& format = IPINT30::STextFormat())
        : m_formatSize(0)
    {
        INIT_LOGGER_HOLDER;
            
        std::unique_ptr<uint8_t[]> formatBuf(new uint8_t[10000]);

        addToBuffer(formatBuf.get(), &format.position, sizeof(format.position), m_formatSize);

        double_t opacity(format.opacity);
        addToBuffer(formatBuf.get(), &opacity, sizeof(opacity), m_formatSize);

        uint32_t color(-format.color);
        addToBuffer(formatBuf.get(), &color, sizeof(color), m_formatSize);

        uint16_t fontLength(strlen(format.font.c_str()));
        addToBuffer(formatBuf.get(), &fontLength, sizeof(fontLength), m_formatSize);
        addToBuffer(formatBuf.get(), format.font.c_str(), fontLength, m_formatSize);

        uint32_t fontStyle(format.fontStyle);
        addToBuffer(formatBuf.get(), &fontStyle, sizeof(fontStyle), m_formatSize);

        double_t fontSize(format.fontSize);
        addToBuffer(formatBuf.get(), &fontSize, sizeof(fontSize), m_formatSize);

        uint16_t keyWordsLength = format.keyWords.size();
        addToBuffer(formatBuf.get(), &keyWordsLength, sizeof(keyWordsLength), m_formatSize);
            
        for (IPINT30::TKeyWord::const_iterator it = format.keyWords.begin(); it != format.keyWords.end(); ++it)
        {
            std::string text(NCorbaHelpers::ToUtf8(it->text));
            uint16_t textLength(strlen(text.c_str()));
            addToBuffer(formatBuf.get(), &textLength, sizeof(textLength), m_formatSize);
            addToBuffer(formatBuf.get(), text.c_str(), textLength, m_formatSize);
                
            bool isCaseSensitive(it->isCaseSensitive);
            addToBuffer(formatBuf.get(), &isCaseSensitive, sizeof(isCaseSensitive), m_formatSize);

            bool isForFullString(it->isForFullString);
            addToBuffer(formatBuf.get(), &isForFullString, sizeof(isForFullString), m_formatSize);

            uint32_t wordColor(-it->color);
            addToBuffer(formatBuf.get(), &wordColor, sizeof(wordColor), m_formatSize);
        }

        for (IPINT30::TKeyWord::const_iterator it = format.keyWords.begin(); it != format.keyWords.end(); ++it)
        {
            uint32_t colorBackground(-it->colorBackground);
            addToBuffer(formatBuf.get(), &colorBackground, sizeof(colorBackground), m_formatSize);

            bool colorBackgroundEnabled(it->colorBackgroundEnabled);
            addToBuffer(formatBuf.get(), &colorBackgroundEnabled, sizeof(colorBackgroundEnabled), m_formatSize);
        }

        m_format = std::unique_ptr<uint8_t[]>(new uint8_t[m_formatSize]);
        memcpy(&m_format[0], &formatBuf[0], m_formatSize);
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        if (sample->Header().nMajor != NMMSS::NMediaType::Text::ID)
            return NMMSS::EFAILED;

        uint32_t sampleSize(m_formatSize + sample->Header().nBodySize + sizeof(sample->Header().nBodySize));
        if (holder.Alloc(sampleSize))
        {
            NMMSS::NMediaType::Application::TypedOctetStream::SubtypeHeader* subHeader = nullptr;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Application::TypedOctetStream>(holder->GetHeader(), &subHeader);
            NMMSS::SMediaSampleHeader& header = holder->Header();
            header.nBodySize = sampleSize;
            header.eFlags = sample->Header().eFlags;
            header.dtTimeBegin = sample->Header().dtTimeBegin;
            header.dtTimeEnd = sample->Header().dtTimeEnd;
                
            uint32_t offset = 0;
            addToBuffer(holder->GetBody(), &sample->Header().nBodySize, sizeof(sample->Header().nBodySize), offset);
            addToBuffer(holder->GetBody(), sample->GetBody(), sample->Header().nBodySize, offset);
            addToBuffer(holder->GetBody(), &m_format[0], m_formatSize, offset);
            subHeader->nSize = offset;
            subHeader->nTypeMagic = MMSS_MAKEFOURCC('S', 'T', 'T', 'U'); // utf-8 subtitles subtype

            return NMMSS::ETRANSFORMED;
        }
        return NMMSS::EFAILED;
    };
};

bool IsBillsOnlyModeEnabled(const IPINT30::STextEventSourceParam& settings)
{
    for (auto param : settings.metaParams)
        if (param.name == BILLS_ONLY_META_PROPERTY)
        {
            return (0 == param.ValueUtf8().compare("1"));
        }
    return false;
}
}

namespace IPINT30
{

CTextEventSource::CTextEventSource(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec,
                                    boost::shared_ptr<IPINT30::IIpintDevice> parent, const STextEventSourceParam& settings,
                                    boost::shared_ptr<NMMSS::IGrabberCallback> callback, const char* objectContext, const char* eventChannel,
                                    NCorbaHelpers::IContainerNamed* container)
    : WithLogger(GET_LOGGER_PTR)
    , CAsyncChannelHandlerImpl<ITV8::GDRV::ITextEventSourceHandler>(GET_LOGGER_PTR, dynExec, parent, 0)
    , CSourceImpl(GET_LOGGER_PTR, boost::str(boost::format(objectContext + ("/" + TEXT_EVENT_SOURCE_ENDPOINT)) % settings.id), container)
    , m_settings(settings)
    , m_callback(callback)
    , m_objectContext(objectContext)
    , m_container(container)
    , m_channel(boost::str(boost::format(TEXT_EVENT_CHANNEL_FORMAT) % settings.id))
    , m_accessPoint(std::string(m_objectContext + "/" + boost::str(boost::format(TEXT_EVENT_SOURCE_ENDPOINT) % m_settings.id)))
    , m_context(new CParamContext)
    , m_billsOnlyMode(IsBillsOnlyModeEnabled(settings))
    , m_billStarted(false)
    , m_timestampCounter()
    , m_pushSampleService(std::make_shared<boost::asio::io_context>())
    , m_pushSampleTimer(*m_pushSampleService)
{
    TCustomProperties properties;
    RegisterProperty<bool>(properties, BILLS_ONLY_META_PROPERTY, "bool",
        [this]() -> bool { boost::mutex::scoped_lock locker(m_mutex); return this->m_billsOnlyMode; },
        [this](bool value) { boost::mutex::scoped_lock locker(m_mutex); this->m_billsOnlyMode = value; });
    RegisterProperty<int>(properties, SAMPLE_DURATION_PROPERTY, "int",
        [this]() -> int { boost::mutex::scoped_lock locker(m_mutex); return this->m_settings.sampleDuration; },
        [this](int value) { boost::mutex::scoped_lock locker(m_mutex); this->m_settings.sampleDuration = value; });
    RegisterProperty<int>(properties, SAMPLE_OFFSET_PROPERTY, "int",
        [this]() -> int { boost::mutex::scoped_lock locker(m_mutex); return this->m_settings.sampleOffset; },
        [this](int value) { boost::mutex::scoped_lock locker(m_mutex); this->m_settings.sampleOffset = value; });
    m_context->AddContext(m_channel.c_str(), MakeContext(m_settings, properties));

    m_connector = NCommonNotification::CreateEventSupplierServant(container, eventChannel, NCommonNotification::RefreshCachedEvents);
    container->ActivateServant(m_connector.Dup(), m_channel.c_str());

    m_groupUuid = boost::uuids::nil_uuid();
    m_groupTuples = Json::Value{ Json::arrayValue };
}

void CTextEventSource::Failed(ITV8::IContract* source, ITV8::hresult_t error)
{
    std::string message = get_last_error_message(source, error);
    _log_ << ToString() << " Failed. It's unexpected event, err:" << message;

    CChannel::RaiseChannelStarted();
    CChannel::RaiseChannelStopped();

    NMMSS::EIpDeviceState st;
    switch(error)
    {
    case ITV8::EAuthorizationFailed:
        st = NMMSS::IPDS_AuthorizationFailed;
        break;
    case ITV8::EDeviceReboot:
        st = NMMSS::IPDS_Rebooted;
        break;
    case ITV8::EHostUnreachable:
    case ITV8::ENetworkDown:
        st = NMMSS::IPDS_NetworkFailure;
        break;
    case ITV8::EGeneralConnectionError:
        st = NMMSS::IPDS_ConnectionError;
        break;
    default:
        st = NMMSS::IPDS_IpintInternalFailure;
        break;
    }
    this->Notify(st);
}

void CTextEventSource::EventGroupBegan()
{
    boost::mutex::scoped_lock lock(m_mutex);
    m_billStarted = true;
    eventGroupBegan();
}

void CTextEventSource::eventGroupBegan()
{
    if (m_groupTuples.size() > 0) // begin without end
    {
        eventGroupEnded();
    }
    m_groupUuid = NCorbaHelpers::GenerateUUID();
    m_groupBegins = boost::posix_time::second_clock::universal_time();
    resetCounter();
}

void CTextEventSource::pushSamples(const std::vector<NMMSS::PSample>& samples)
{
    if (!samples.empty())
    {
        NMMSS::PSample first = samples.front();
        NMMSS::PSample last = samples.back();

        if (first == last)
            Push(first);
        else
        {
            last->Header().dtTimeBegin = first->Header().dtTimeBegin;
            last->Header().eFlags = first->Header().eFlags;
            Push(last);
        }
    }
}
void CTextEventSource::pushSampleDelayed(NMMSS::PSample sample)
{
    static const auto FPS30_TIMEOUT = boost::posix_time::milliseconds(33);
    {
        std::lock_guard<std::mutex> g(m_sampleQueueGuard);
        m_sampleQueue.push_back(sample);

        if (m_sampleQueue.size() > 100) // To prevent endless accumulation
        {
            m_pushSampleTimer.cancel();

            std::vector<NMMSS::PSample> sampleQueue;
            sampleQueue.swap(m_sampleQueue);
            m_pushSampleService->post([this, sampleQueue]() { pushSamples(sampleQueue); });
            return;
        }
    }

    m_pushSampleTimer.expires_from_now(FPS30_TIMEOUT);
    m_pushSampleTimer.async_wait([sample, this](const boost::system::error_code& err)
    {
        if (err) // Canceled by stop or new 'too fast' sample
            return;

        std::vector<NMMSS::PSample> sampleQueue;
        {
            std::lock_guard<std::mutex> g(m_sampleQueueGuard);
            sampleQueue.swap(m_sampleQueue);
        }
        pushSamples(sampleQueue);
    });
}

bool CTextEventSource::emitTextEvent(ORM::JsonEvent& je, const char* payload, size_t size, Json::Value anyValues)
{
    using namespace std::literals;
    anyValues["entirety"s] = (int)ORM::TET_SingleTuple;
    anyValues["group_id"s] = boost::uuids::to_string(m_groupUuid);
    if (nullptr != payload && 0u != size)
    {
        auto const payloadEnd = payload + size;
        anyValues["text"s] = Json::Value(payload, payloadEnd);
    }
    je.Type = ORM::JE_TextEvent;

    Json::Value links(Json::ValueType::arrayValue);
    for (auto& l : m_settings.links)
        links.append(l.source);
    anyValues["links"s] = links;
    
    try
    {
        je.AnyValues = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(Json::FastWriter().write(anyValues)).c_str());
        NCommonNotification::SendOneShotEvent(m_connector.Get(),je);
        return true;
    }
    catch (NCorbaHelpers::XFailed const& e)
    {
        std::stringstream fullSampleBytes {};
        for (auto const payloadEnd = payload + size; payload != payloadEnd; ++payload)
        {
            fullSampleBytes << std::hex << static_cast<int>(*payload) << ' ';
        }
        
        _err_ << "FromUtf8 thrown an exception, details: " << e.what()
              << ", full sample bytes: " << fullSampleBytes.str();
    }
    return false;
}

void CTextEventSource::emitTextSample( const char* data, size_t dataSize
    , ITV8::timestamp_t timestampInt, std::string const& timestampStr
    , ITV8::timestamp_t timestampLast )
{
    if (nullptr == data || 0 == dataSize)
        return;

    checkCounter();

    ITV8::timestamp_t timestampEnd = 0 == m_settings.sampleDuration ? timestampInt + 1 : timestampInt + m_settings.sampleDuration * 1000;
    if ((m_settings.sampleDuration > 0) && ((timestampInt - timestampLast) >= (timestampEnd - timestampInt)))
        resetCounter();

    if (!m_sampleData.empty())
        m_sampleData.append("\n");
    m_sampleData.append(data, dataSize);

    NMMSS::PSample sample;
    {
        boost::mutex::scoped_lock lock(mutex());
        sample = NMMSS::PSample(allocate(lock, m_sampleData.size()));
    }
    NMMSS::NMediaType::Text::Plain::SubtypeHeader* subHeader = nullptr;
    NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Text::Plain>(sample->GetHeader(), &subHeader);

    sample->Header().dtTimeBegin = timestampInt;
    sample->Header().dtTimeEnd = timestampInt + 1;
    sample->Header().nBodySize = m_sampleData.size();
    if (m_settings.sampleDuration > 0 && uint64_t(m_settings.sampleDuration * 1000) < timestampInt - timestampLast)
        sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
    else if (m_settings.sampleDuration == 0 && 10000u < timestampInt - timestampLast)
        sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
    subHeader->eEncoding = NMMSS::NMediaType::Text::STextBlockHeader::UTF8;
    subHeader->nLength = 0; // TODO: 'm_sampleData.size()' isn't really utf-8 string length
    memcpy(sample->GetBody(), m_sampleData.data(), m_sampleData.size());

    if (!m_billsOnlyMode || m_billStarted)
    {
        pushSampleDelayed(sample);
    }
    _dbg_ << "EventData new time: " << timestampStr << ", data: " << data << ", full sample: " << m_sampleData;
    
    ++m_subtitlesCounter;
}

void CTextEventSource::EventData(ITV8::timestamp_t timestamp, const char* data)
{
    _dbg_ << "EventData: " << data << " with time: " << ipintTimestampToIsoString(timestamp);

    boost::mutex::scoped_lock locker(m_mutex);
    size_t dataSize = strlen(data);
    if (dataSize == 0)
        return;

    if (m_groupUuid.is_nil())
    {
        _wrn_ << "Group wasn't began. Force begin for group. Possible reasons are groups weren't configured or previous group was ended by force";
        eventGroupBegan();
    }

    ITV8::timestamp_t timestampLast(ipintTimeToQword(m_timestampCounter.TimestampLast()) + m_settings.sampleOffset * 1000);
    ITV8::timestamp_t timestampInt(ipintTimeToQword(m_timestampCounter.Timestamp(timestamp)) + m_settings.sampleOffset * 1000);

    if (timestampLast > timestampInt)
        timestampInt = timestampLast + 1;

    std::string timestampStr(boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(timestampInt)));

    ORM::JsonEvent je = ORM::CreateEmptyJsonEvent(); // Id can be nil
    je.ObjectId = CORBA::string_dup(m_accessPoint.c_str()); // TODO: EventSupplier.TextEvent.id should be used for events actually
    je.Timestamp.value = CORBA::string_dup(timestampStr.c_str());

    Json::Value json;
    Json::CharReaderBuilder reader;
    std::string err;
    boost::iostreams::stream<boost::iostreams::array_source> ss(data, dataSize);
    const bool isJSON = Json::parseFromStream(reader, ss, &json, &err);

    using namespace std::literals;
    // Send specify text event as JSON event with according type
    const auto sendDetectorEventsMode = isJSON
        && json.isObject()
        && static_cast<Json::Value const&>(json)["detector_type"s].asString() == "Ray"s
        && json.isMember("vendor"s);
    if (sendDetectorEventsMode)
    {
        json["phase"s] = MMSS::AS_Happened;
        if (m_settings.links.size())
        {
            json["origin_id"s] = m_settings.links.front().source;
        }

        Notification::StringToGuid(NCorbaHelpers::StringifyUUID(m_groupUuid), je.Id);
        je.Type = ORM::JE_Detector;
        je.AnyValues = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(Json::FastWriter().write(json)).c_str());

        NCommonNotification::SendOneShotEvent(m_connector.Get(), je);
        m_groupUuid = boost::uuids::nil_uuid();
        return;
    }

    static const auto s_titles = "titles"s;
    static const auto s_text_message = "text_message"s;
    static const auto s_event = "event"s;
    static const auto s_version = "version"s;
    std::string textBuffer;
    if (!isJSON || !json.isObject())
    {
        if (!emitTextEvent(je, data, dataSize))
            return;

        json = Json::Value();
    }
    else
    {
        auto & posEvent = json[s_event];
        if (posEvent.isNull())
        {
            _err_ << "Got text event JSON data with unrecognized structure " << data;
            return;
        }
        auto const& titles = static_cast<Json::Value const&>(json)[s_titles];
        if (!titles.isNull())
        {
            constexpr auto MAX_RECOGNIZED_VERSION = 1u;
            constexpr auto MIN_RECOGNIZED_VERSION = 1u;
            const auto version = titles.get(s_version, Json::Value(1u)).asUInt();
            if (version > MAX_RECOGNIZED_VERSION || version < MIN_RECOGNIZED_VERSION)
                _wrn_ << "'titles' object version " << version << " does not belong to the currently supported versions range ["
                << MIN_RECOGNIZED_VERSION << ", " << MAX_RECOGNIZED_VERSION << "]";
            else
            {
                auto const& text_message = titles[s_text_message];
                if (!text_message.isNull())
                    textBuffer = text_message.asString();
            }
            json.removeMember(s_titles);
        }
        data = textBuffer.data();
        dataSize = textBuffer.size();
        if (!emitTextEvent(je, textBuffer.data(), textBuffer.size(), json))
            return;
    }

    json["timestamp"s] = timestampStr;
    if (0u != dataSize)
        json["text"s] = Json::Value(data, data + dataSize);
    m_groupTuples.resize(m_groupTuples.size() + 1);
    m_groupTuples[m_groupTuples.size() - 1] = json;

    if (m_groupTuples.size() == MAX_TEXT_EVENT_STRING_COUNT)
    {
        _wrn_ << "Max size for group has been reached. Force end for group";
        eventGroupEnded();
    }

    emitTextSample(data, dataSize, timestampInt, timestampStr, timestampLast);
}

void CTextEventSource::EventGroupEnded()
{
    boost::mutex::scoped_lock lock(m_mutex);
    m_billStarted = false;
    if (!m_groupUuid.is_nil()) // nil if group already ended force and next call is EventGroupEnded(), so nothing end
    {
        eventGroupEnded();
    }
}

std::string CTextEventSource::GetDynamicParamsContextName() const
{
    std::ostringstream stream;
    stream << "textEventSource:" << m_settings.id;
    return stream.str();
}

void CTextEventSource::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (!handler)
    {
        _log_ << ToString() << " IDynamicParametersHandler doesn't exist";
        return;
    }

    // If source is not active than we should not do any call to driver.
    if (!GetFlag(cfStarted))
    {
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    if (!m_eventSource)
    {
        _log_ << ToString() << " text event source doesn't exist";
        handler->Failed(0, ITV8::EGeneralError);
        return;
    }

    ITV8::IDynamicParametersProvider* provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(
        static_cast<ITV8::GDRV::IDeviceChannel*>(m_eventSource.get()));
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(static_cast<ITV8::GDRV::IDeviceChannel*>(m_eventSource.get()), ITV8::EUnsupportedCommand);
}

void CTextEventSource::SwitchEnabled()
{
    SetEnabled(m_settings.enabled);
}

void CTextEventSource::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    if (GetFlag(cfEnvironmentReady) && m_eventSource)
    {
        ITV8::IAsyncAdjuster* const asyncAdj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_eventSource.get());
        if(asyncAdj)
        {
            //Sets properties of textEvent source.
            SetValues(asyncAdj, m_settings.publicParams);
            SetValues(asyncAdj, m_settings.privateParams);

            //Applies new settings.
            ApplySettings(asyncAdj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() <<" couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped." <<std::endl;
        }
    }
}

IParamContextIterator* CTextEventSource::GetParamIterator()
{
    return m_context.get();
}

std::string CTextEventSource::ToString() const
{
    std::ostringstream str;
    if(!m_parent)
    {
        str << "DeviceIpint.Unknown";
    }
    else
    {
        str << m_parent->ToString();
    }
    str << "\\" << ".textEvent:" << m_settings.id;
    return str.str();
}

void CTextEventSource::DoStart()
{
    if (!m_eventSource)
    {
        ITV8::GDRV::ITextEventDevice* device = 0;
        try
        {
            device = ITV8::contract_cast<ITV8::GDRV::ITextEventDevice>(getDevice());
        }
        catch (const std::runtime_error& e)
        {
            _err_ << ToString() << " Exception: Couldn't getDevice(). msg=" << e.what() << std::endl;
            return;
        }
        if (!device)
        {
            _err_ << ToString() << " Couldn't cast ITV8::GDRV::IDevice to ITV8::GDRV::ITextEventDevice" << std::endl;
            return;
        }
        m_eventSource = ITextEventSourcePtr(
            device->CreateTextEventSource(this, m_settings.id),
            ipint_destroyer<ITV8::GDRV::ITextEventSource>());

        if(!m_eventSource)
        {
            std::string message = get_last_error_message(device);
            _err_ << ToString() <<  "Exception: TextEvent source was not created. msg="
                << message << std::endl;
            return;
        }
    }

    ApplyChanges(0);

    m_pushSampleExecutor = NExecutors::CreateReactor("TextEventSrc", GET_LOGGER_PTR, 1, m_pushSampleService);
    m_eventSource->Start();

    CChannel::DoStart();
}

void CTextEventSource::DoStop()
{
    if (m_eventSource)
    {
        WaitForApply();
        m_eventSource->Stop();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

void CTextEventSource::OnStopped()
{
    m_pushSampleTimer.cancel();
    m_pushSampleExecutor->Shutdown();
    m_pushSampleExecutor.reset();
    m_eventSource.reset();
}

void CTextEventSource::OnFinalized()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if (cont)
        cont->DeactivateServant(m_channel.c_str());
    SetEnabled(false);
}

void CTextEventSource::OnEnabled()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if(!cont)
        return;

    boost::mutex::scoped_lock lock(m_switchMutex);

    m_distributor = NMMSS::CreateDistributor(GET_LOGGER_PTR, NMMSS::NAugment::UnbufferedDistributor{});
    m_distributorConnection = NMMSS::CConnectionResource(this, m_distributor->GetSink(), GET_LOGGER_PTR);

    for (TFormatLink::const_iterator it = m_settings.links.begin(); it != m_settings.links.end(); ++it)
    {
        NMMSS::PPullFilter filter(CreateSubtitleFormatter(GET_LOGGER_PTR, it->format));
        NMMSS::PPullStyleSource src (m_distributor->CreateSource());
        m_connections.insert(NMMSS::GetConnectionBroker()->SetConnection(src.Get(), filter->GetSink(), GET_LOGGER_PTR));
        NMMSS::PSourceFactory factory(NMMSS::CreateDefaultSourceFactory(GET_LOGGER_PTR, filter->GetSource()));
        m_filters.insert(filter);
        PortableServer::Servant eventServant(NMMSS::CreatePullSourceEndpoint(GET_LOGGER_PTR, cont.Get(), factory.Get()));
        std::string accessPoint(boost::str(boost::format(TEXT_EVENT_SOURCE_STREAM_ENDPOINT) % m_settings.id % it->id));
        PServant servant(NCorbaHelpers::ActivateServant(cont.Get(), eventServant, accessPoint.c_str()));
        m_servants.insert(servant);
    }

    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback, 
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        m_accessPoint));
}

void CTextEventSource::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    m_servants.clear();

    for (TConnections::iterator it = m_connections.begin(); it != m_connections.end(); ++it)
        NMMSS::GetConnectionBroker()->DestroyConnection(*it);
    
    m_filters.clear();

    m_distributorConnection = NMMSS::CConnectionResource();
    m_distributor.Reset();

    this->SetNotifier(0);
}

void CTextEventSource::checkCounter()
{
    if (m_subtitlesCounter > MAX_TEXT_SAMPLE_STRING_COUNT)
        resetCounter();
}

void CTextEventSource::resetCounter()
{
    m_sampleData = "";
    m_subtitlesCounter = 0;

    {
        std::lock_guard<std::mutex> g(m_sampleQueueGuard);
        m_pushSampleTimer.cancel();

        std::vector<NMMSS::PSample> sampleQueue;
        sampleQueue.swap(m_sampleQueue);
        m_pushSampleService->post([this, sampleQueue]() { pushSamples(sampleQueue); });
    }
}

void CTextEventSource::eventGroupEnded()
{
    if (m_groupTuples.size() > 0)
    {
        ORM::JsonEvent je = ORM::CreateEmptyJsonEvent();

        Notification::StringToGuid(NCorbaHelpers::StringifyUUID(m_groupUuid), je.Id);
        je.Type = ORM::JE_TextEvent;
        je.ObjectId = CORBA::string_dup(m_accessPoint.c_str()); // TODO: EventSupplier.TextEvent.id should be used for events actually
        je.Timestamp.value = CORBA::string_dup(boost::posix_time::to_iso_string(m_groupBegins).c_str());

        Json::Value anyValues;
        anyValues["entirety"] = (int)ORM::TET_FullTuple;
        anyValues["data"] = m_groupTuples;

        Json::Value links(Json::ValueType::arrayValue);
        for (auto& l : m_settings.links)
            links.append(l.source);
        anyValues["links"] = links;
        
        try
        {
            je.AnyValues = CORBA::wstring_dup(NCorbaHelpers::FromUtf8(Json::FastWriter().write(anyValues)).c_str());
        }
        catch (NCorbaHelpers::XFailed &e)
        {            
            _err_ << "FromUtf8 thrown an exception, details: " << e.what();
            return;
        }
        

        m_connector->SendEvents(NCommonNotification::WrapSingleEvent(je));
        _dbg_ << "Full tuple text event has been sended. Timestamp: " << je.Timestamp.value;

        m_groupUuid = boost::uuids::nil_uuid();
        m_groupTuples.resize(0);
    }
    else
        _wrn_ << "Ended sended for empty group. Do nothing";
}

NMMSS::IFilter* CreateSubtitleFormatter(DECLARE_LOGGER_ARG, const STextFormat& format)
{
    return new NMMSS::CPullFilterImpl<CSubtitleFormatter>(
        GET_LOGGER_PTR, NMMSS::SAllocatorRequirements(0), NMMSS::SAllocatorRequirements(0), new CSubtitleFormatter(GET_LOGGER_PTR, format)
        );
}

}
