#include "CAudioG7xxBuffer.h"
#include "../Sample.h"
#include "../MediaType.h"
#include "../MMCoding/FrameBuilder.h"
#include <cassert>

using namespace std;
using namespace ITV8;
namespace
{
    const int DEFAULT_BITS_PER_SAMPLE = 8;
}
//////////////////////////////////////////////////////////////////////////
CAudioG7xxBuffer::CAudioG7xxBuffer(NMMSS::ISample const* sample, char const* name)
: TBase(sample, name)
, m_bitsPerSample(DEFAULT_BITS_PER_SAMPLE)
{
    using namespace NMMSS::NMediaType;

    switch (sample->Header().nSubtype)
    {
    case Audio::G711::ID:
    case Audio::G726::ID:
#ifdef _DEBUG
        TBase::DebugViewSubheader(Audio::ID, sample->Header().nSubtype);
#endif
        break;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}

CAudioG7xxBuffer::CAudioG7xxBuffer(NMMSS::ISample* sample, ::uint32_t minorID, char const* name,
    ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t bitRate, ITV8::uint32_t encoding)
: TBase(sample, name, timestamp, bufferSize)
, m_bitsPerSample(DEFAULT_BITS_PER_SAMPLE)
{
    using namespace NMMSS::NMediaType;
    switch(minorID)
    {
    case Audio::G711::ID:
    {
        Audio::G711::SubtypeHeader* subheader = 0;
        MakeMediaTypeStruct<Audio::G711>(sample->GetHeader(), &subheader);
        switch(encoding)
        {
        case ITV8::MFF::AUDIO_G7XX_ENCODING_ULAW:
            subheader->eCodingLaw = NMMSS::NMediaType::Audio::G711::U_LAW;
            break;
        case ITV8::MFF::AUDIO_G7XX_ENCODING_ALAW:
            subheader->eCodingLaw = NMMSS::NMediaType::Audio::G711::A_LAW;
            break;
        case ITV8::MFF::AUDIO_G7XX_ENCODING_LINEAR:
        default:
            throw std::logic_error("not supported");
        }

        subheader->nSampleRate = bitRate / DEFAULT_BITS_PER_SAMPLE ;
        subheader->nChannelsCount = 1;
    }
    break;
    case Audio::G726::ID:
    {
        Audio::G726::SubtypeHeader* subheader = 0;
        MakeMediaTypeStruct<Audio::G726>(sample->GetHeader(), &subheader);
        subheader->nBitRate = bitRate;
        subheader->nSampleRate = 8000; // bitRate / bitsPerSample 
        subheader->nChannelsCount = 1;
    }
    break;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}

CAudioG7xxBuffer::CAudioG7xxBuffer(NMMSS::ISample* sample, ::uint32_t minorID, char const* name,
    ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp,
    ITV8::uint32_t bitRate, ITV8::uint32_t encoding,
    ITV8::uint32_t channelsNum, ITV8::uint32_t sampleRate,
    ITV8::uint32_t bitsPerSample)
: TBase(sample, name, timestamp, bufferSize)
, m_bitsPerSample(bitsPerSample)
{
    using namespace NMMSS::NMediaType;
    switch (minorID)
    {
    case Audio::G711::ID:
    {
        Audio::G711::SubtypeHeader* subheader = 0;
        MakeMediaTypeStruct<Audio::G711>(sample->GetHeader(), &subheader);
        switch (encoding)
        {
        case ITV8::MFF::AUDIO_G7XX_ENCODING_ULAW:
            subheader->eCodingLaw = NMMSS::NMediaType::Audio::G711::U_LAW;
            break;
        case ITV8::MFF::AUDIO_G7XX_ENCODING_ALAW:
            subheader->eCodingLaw = NMMSS::NMediaType::Audio::G711::A_LAW;
            break;
        case ITV8::MFF::AUDIO_G7XX_ENCODING_LINEAR:
        default:
            throw std::logic_error("not supported");
        }

        subheader->nSampleRate = sampleRate;
        subheader->nChannelsCount = channelsNum;
    }
    break;
    case Audio::G726::ID:
    {
        Audio::G726::SubtypeHeader* subheader = 0;
        MakeMediaTypeStruct<Audio::G726>(sample->GetHeader(), &subheader);
        subheader->nBitRate = bitRate;
        subheader->nSampleRate = sampleRate;
        subheader->nChannelsCount = channelsNum;
    }
    break;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}

CAudioG7xxBuffer::~CAudioG7xxBuffer()
{
}

ITV8::uint8_t* CAudioG7xxBuffer::GetBuffer()
{
    return GetSample()->GetBody();
}

ITV8::uint32_t CAudioG7xxBuffer::GetBufferSize()
{
    return GetSample()->Header().nBodySize;
}

ITV8::uint32_t CAudioG7xxBuffer::GetBitRate()
{
    using namespace NMMSS::NMediaType;

    switch (GetSample()->Header().nSubtype)
    {
    case Audio::G711::ID:
        return GetSample()->SubHeader<Audio::G711>().nSampleRate * DEFAULT_BITS_PER_SAMPLE;
    case Audio::G726::ID:
        return GetSample()->SubHeader<Audio::G726>().nBitRate;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}

ITV8::uint32_t TranslateNmmssG711CodingLaw(NMMSS::NMediaType::Audio::G711::ECodingLaw e)
{
    switch(e)
    {
    case NMMSS::NMediaType::Audio::G711::U_LAW:
        return ITV8::MFF::AUDIO_G7XX_ENCODING_ULAW;
    case NMMSS::NMediaType::Audio::G711::A_LAW:
        return ITV8::MFF::AUDIO_G7XX_ENCODING_ALAW;
    default:
        //к сожалению это всё что мы можем сделать по этому поводу.
        //кто-то изменил структуру енумов не уведомив об этом её клиентов.
        assert(false);
    }

    return ITV8::MFF::AUDIO_G7XX_ENCODING_LINEAR;
}

ITV8::uint32_t CAudioG7xxBuffer::GetEncoding()
{
    using namespace NMMSS::NMediaType;

    switch (GetSample()->Header().nSubtype)
    {
    case Audio::G711::ID:
        return TranslateNmmssG711CodingLaw(GetSample()->SubHeader<Audio::G711>().eCodingLaw);
    case Audio::G726::ID:
        return ITV8::MFF::AUDIO_G7XX_ENCODING_LINEAR;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}

ITV8::uint32_t CAudioG7xxBuffer::GetNumChannels()
{
    using namespace NMMSS::NMediaType;

    switch (GetSample()->Header().nSubtype)
    {
    case Audio::G711::ID:
        return GetSample()->SubHeader<Audio::G711>().nChannelsCount;
    case Audio::G726::ID:
        return GetSample()->SubHeader<Audio::G726>().nChannelsCount;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}
ITV8::uint32_t CAudioG7xxBuffer::GetSampleRate()
{
    using namespace NMMSS::NMediaType;

    switch (GetSample()->Header().nSubtype)
    {
    case Audio::G711::ID:
        return GetSample()->SubHeader<Audio::G711>().nSampleRate;
    case Audio::G726::ID:
        return GetSample()->SubHeader<Audio::G726>().nSampleRate;
    default:
        throw runtime_error("Can't create AudioG7xxBuffer: Incorrect or unknown format");
    }
}
ITV8::uint32_t CAudioG7xxBuffer::GetBitsPerSample()
{
    return m_bitsPerSample;
}

void CAudioG7xxBuffer::ConvertToHostBitOrder()
{
    this->ConvertBitOrder<CAudioG7xxBuffer::TO_HOST>();
}

void CAudioG7xxBuffer::ConvertToNetworkBitOrder()
{
    this->ConvertBitOrder<CAudioG7xxBuffer::TO_NETWORK>();
}

template<CAudioG7xxBuffer::EDirection direction>
void CAudioG7xxBuffer::ConvertBitOrder()
{
    if(this->GetEncoding() != ITV8::MFF::AUDIO_G7XX_ENCODING_LINEAR) return;

    const int bitrate = this->GetBitRate();
    ITV8::uint8_t* data = this->GetBuffer();
    const ITV8::uint32_t size = this->GetBufferSize();

    ITV8::uint32_t used_bytes = 0;
    while(used_bytes < size)
    {
        switch(bitrate)
        {
        case 16000: used_bytes += ConvertBytesGroup<16000,direction>(data + used_bytes); break;
        case 24000: used_bytes += ConvertBytesGroup<24000,direction>(data + used_bytes); break;
        case 32000: used_bytes += ConvertBytesGroup<32000,direction>(data + used_bytes); break;
        case 40000: used_bytes += ConvertBytesGroup<40000,direction>(data + used_bytes); break;
        default:
            throw runtime_error("Unsupported bit rate for G726");
        }
    }
}


template<>
int CAudioG7xxBuffer::ConvertBytesGroup<16000,CAudioG7xxBuffer::TO_HOST>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0];

    ITV8::uint8_t y0 = ((x0 & 0x03) << 6) | ((x0 & 0x0C) << 2) | ((x0 & 0x30) >> 2) | ((x0 & 0xC0) >> 6);

    ptr[0] = y0;
    return 1;
}

template<>
int CAudioG7xxBuffer::ConvertBytesGroup<16000,CAudioG7xxBuffer::TO_NETWORK>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0];

    ITV8::uint8_t y0 = ((x0 & 0x03) << 6) | ((x0 & 0x0C) << 2) | ((x0 & 0x30) >> 2) | ((x0 & 0xC0) >> 6);

    ptr[0] = y0;
    return 1;
}


template<>
int CAudioG7xxBuffer::ConvertBytesGroup<24000,CAudioG7xxBuffer::TO_HOST>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0], x1 = ptr[1], x2 = ptr[2];

    ITV8::uint8_t y0 = ((x0 & 0x07) << 5) | ((x0 & 0x38) >> 1) | ((x1 & 0x01) << 1) | ((x0 & 0x80) >> 7);
    ITV8::uint8_t y1 = ((x0 & 0x40) << 1) | ((x1 & 0x0E) << 3) | ((x1 & 0x70) >> 3) | ((x2 & 0x02) >> 1);
    ITV8::uint8_t y2 = ((x2 & 0x01) << 7) | ((x1 & 0x80) >> 1) | ((x2 & 0x1C) << 1) | ((x2 & 0xE0) >> 5);

    ptr[0] = y0; ptr[1] = y1; ptr[2] = y2;
    return 3;
}


template<>
int CAudioG7xxBuffer::ConvertBytesGroup<24000,CAudioG7xxBuffer::TO_NETWORK>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0], x1 = ptr[1], x2 = ptr[2];

    ITV8::uint8_t y0 = ((x0 & 0xE0) >> 5) | ((x0 & 0x1C) << 1) | ((x1 & 0x80) >> 1) | ((x0 & 0x01) << 7);
    ITV8::uint8_t y1 = ((x0 & 0x02) >> 1) | ((x1 & 0x70) >> 3) | ((x1 & 0x0E) << 3) | ((x2 & 0x40) << 1);
    ITV8::uint8_t y2 = ((x2 & 0x80) >> 7) | ((x1 & 0x01) << 1) | ((x2 & 0x38) >> 1) | ((x2 & 0x07) << 5);

    ptr[0] = y0; ptr[1] = y1; ptr[2] = y2;
    return 3;
}


template<>
int CAudioG7xxBuffer::ConvertBytesGroup<32000,CAudioG7xxBuffer::TO_HOST>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0];

    ITV8::uint8_t y0 = ((x0 & 0x0F) << 4) | ((x0 & 0xF0) >> 4);

    ptr[0] = y0;
    return 1;
}

template<>
int CAudioG7xxBuffer::ConvertBytesGroup<32000,CAudioG7xxBuffer::TO_NETWORK>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0];

    ITV8::uint8_t y0 = ((x0 & 0x0F) << 4) | ((x0 & 0xF0) >> 4);

    ptr[0] = y0;
    return 1;
}

template<>
int CAudioG7xxBuffer::ConvertBytesGroup<40000,CAudioG7xxBuffer::TO_HOST>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0], x1 = ptr[1], x2 = ptr[2], x3 = ptr[3], x4 = ptr[4];

    ITV8::uint8_t y0 = ((x0 & 0x1F) << 3) | ((x1 & 0x03) << 1) | ((x0 & 0x80) >> 7);
    ITV8::uint8_t y1 = ((x0 & 0x60) << 1) | ((x1 & 0x7C) >> 1) | ((x2 & 0x08) >> 3);
    ITV8::uint8_t y2 = ((x2 & 0x07) << 5) | ((x1 & 0x80) >> 3) | ((x3 & 0x01) << 3) | ((x2 & 0xE0) >> 5);
    ITV8::uint8_t y3 = ((x2 & 0x10) << 3) | ((x3 & 0x3E) << 1) | ((x4 & 0x06) >> 1);
    ITV8::uint8_t y4 = ((x4 & 0x01) << 7) | ((x3 & 0xC0) >> 1) | ((x4 & 0xF8) >> 3);

    ptr[0] = y0; ptr[1] = y1; ptr[2] = y2; ptr[3] = y3; ptr[4] = y4;
    return 5;
}

template<>
int CAudioG7xxBuffer::ConvertBytesGroup<40000,CAudioG7xxBuffer::TO_NETWORK>(ITV8::uint8_t* ptr)
{
    const ITV8::uint8_t x0 = ptr[0], x1 = ptr[1], x2 = ptr[2], x3 = ptr[3], x4 = ptr[4];

    ITV8::uint8_t y0 = ((x0 & 0xF8) >> 3) | ((x1 & 0xC0) >> 1) | ((x0 & 0x01) << 7);
    ITV8::uint8_t y1 = ((x0 & 0x06) >> 1) | ((x1 & 0x3E) << 1) | ((x2 & 0x10) << 3);
    ITV8::uint8_t y2 = ((x2 & 0xE0) >> 5) | ((x1 & 0x01) << 3) | ((x3 & 0x80) >> 3) | ((x2 & 0x07) << 5);
    ITV8::uint8_t y3 = ((x2 & 0x08) >> 3) | ((x3 & 0x7C) >> 1) | ((x4 & 0x60) << 1);
    ITV8::uint8_t y4 = ((x4 & 0x80) >> 7) | ((x3 & 0x03) << 1) | ((x4 & 0x1F) << 3);

    ptr[0] = y0; ptr[1] = y1; ptr[2] = y2; ptr[3] = y3; ptr[4] = y4;
    return 5;
}




