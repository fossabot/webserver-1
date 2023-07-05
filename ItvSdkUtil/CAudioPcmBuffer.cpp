#include "CAudioPcmBuffer.h"
#include "../Sample.h"
#include "../MediaType.h"
#include "../MMCoding/FrameBuilder.h"
#include <cassert>

using namespace std;
using namespace ITV8;

//////////////////////////////////////////////////////////////////////////
CAudioPcmBuffer::CAudioPcmBuffer(NMMSS::ISample const* sample, char const* name)
: TBase(sample, name)
{
	using namespace NMMSS::NMediaType;

	switch (sample->Header().nSubtype)
	{
	case Audio::PCM::ID:
#ifdef _DEBUG
		TBase::DebugViewSubheader(Audio::ID, sample->Header().nSubtype);
#endif
		break;
	default:
		throw runtime_error("Can't create AudioPcmBuffer: Incorrect or unknown format");
	}
}

static NMMSS::NMediaType::Audio::ESampleType GetSampleTypeFromSize(int bitPerSample)
{
	switch (bitPerSample)
	{
	case 8:
		return NMMSS::NMediaType::Audio::ST_UINT8;
	case 16:
		return NMMSS::NMediaType::Audio::ST_INT16;
	case 32:
		//тут преобразование неоднозначно, однако вроде как не используется нигде.
		return NMMSS::NMediaType::Audio::ST_INT32;
		return NMMSS::NMediaType::Audio::ST_FLOAT32;
	default: 
		return NMMSS::NMediaType::Audio::ST_INT16;
	}
};

CAudioPcmBuffer::CAudioPcmBuffer(NMMSS::ISample* sample, ::uint32_t minorID, char const* name, 
	ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, 
	ITV8::uint32_t sampleRate, ITV8::uint32_t bitPerSample, ITV8::uint32_t channels)
: TBase(sample, name, timestamp, bufferSize)
{
	using namespace NMMSS::NMediaType;
	switch (minorID)
	{
	case Audio::PCM::ID:
		{
			Audio::PCM::SubtypeHeader *subheader = 0;
			MakeMediaTypeStruct<Audio::PCM>(sample->GetHeader(), &subheader);
			
			subheader->nChannelsCount = channels;
			subheader->nSampleRate = sampleRate;
			subheader->nSampleType = GetSampleTypeFromSize(bitPerSample);
		}
		break;

	default:
		throw runtime_error("Can't create AudioBuffer: Incorrect or unknown format");
	}
}

CAudioPcmBuffer::~CAudioPcmBuffer()
{
}

ITV8::uint8_t* CAudioPcmBuffer::GetBuffer()
{
	return GetSample()->GetBody();
}

ITV8::uint32_t CAudioPcmBuffer::GetBufferSize()
{
	return GetSample()->Header().nBodySize;
}

ITV8::uint32_t	CAudioPcmBuffer::GetNumChannels()
{
	return GetSample()->SubHeader<NMMSS::NMediaType::Audio::PCM>().nChannelsCount;
}

ITV8::uint32_t	CAudioPcmBuffer::GetSampleRate()
{
	return GetSample()->SubHeader<NMMSS::NMediaType::Audio::PCM>().nSampleRate;
}

ITV8::uint32_t	CAudioPcmBuffer::GetBitsPerSample()
{
	using namespace NMMSS::NMediaType;
	return Audio::GetTypeSize(GetSample()->SubHeader<Audio::PCM>().nSampleType);
}
