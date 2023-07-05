#include "CAudioBuffer.h"
#include "../Sample.h"
#include "../MediaType.h"
#include "../MMCoding/FrameBuilder.h"
#include <cassert>

using namespace std;
using namespace ITV8;

//////////////////////////////////////////////////////////////////////////
CAudioBuffer::CAudioBuffer(NMMSS::ISample const* sample, char const* name)
:TBase(sample, name)
{
	using namespace NMMSS::NMediaType;

	switch (sample->Header().nSubtype)
	{
	case Audio::AAC::ID:
	case Audio::GSM::ID:
	case Audio::VORBIS::ID:
    case Audio::MP2::ID:
#ifdef _DEBUG
		TBase::DebugViewSubheader(Audio::ID, sample->Header().nSubtype);
#endif
		break;
	default:
		throw runtime_error("Can't create AudioBuffer: Incorrect or unknown format");
	}
}

CAudioBuffer::CAudioBuffer(NMMSS::ISample* sample, ::uint32_t minorID, const char* name,
	ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp)
    : TBase(sample, name, timestamp, bufferSize)
{
    using namespace NMMSS::NMediaType;
    switch(minorID)
    {
	case Audio::AAC::ID:
		MakeMediaTypeStruct<Audio::AAC>(sample->GetHeader());
		break;
	case Audio::GSM::ID:
		MakeMediaTypeStruct<Audio::GSM>(sample->GetHeader());
		break;
	case Audio::VORBIS::ID:
		MakeMediaTypeStruct<Audio::VORBIS>(sample->GetHeader());
		break;
    case Audio::MP2::ID:
        MakeMediaTypeStruct<Audio::MP2>(sample->GetHeader());
        break;
    default:
        throw runtime_error("Can't create AudioBuffer: Incorrect or unknown format");
    }
}



CAudioBuffer::~CAudioBuffer()
{
}

ITV8::uint8_t* CAudioBuffer::GetBuffer()
{
    return GetSample()->GetBody();
}

ITV8::uint32_t CAudioBuffer::GetBufferSize()
{
    return GetSample()->Header().nBodySize;
}
