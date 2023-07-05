#include "CCompositeBuffer.h"
#include "../Sample.h"
#include "../MediaType.h"

using namespace std;
using namespace ITV8;

CCompositeBuffer::CCompositeBuffer(NMMSS::ISample const* sample, char const* name)
    :TBase(sample, name)
{
	throw runtime_error("Can't create CompositeBuffer: not implemented!");
/*
	using namespace NMMSS::NMediaType;

	switch (sample->Header().nSubtype)
	{
	case Video::fccRGB::ID:
	case Video::fccRGBA::ID:
#ifdef _DEBUG
		TBase::DebugViewSubheader(Video::ID, sample->Header().nSubtype);
#endif
		break;
	default:
		throw runtime_error("Can't create CompositeBuffer: Incorrect or unknown format");
	}
*/
}

CCompositeBuffer::CCompositeBuffer(NMMSS::ISample* sample, ::uint32_t minorID, 
	char const* name,ITV8::uint32_t bufferSize, ITV8::int32_t stride, ITV8::timestamp_t timestamp, 
    ITV8::uint32_t width, ITV8::uint32_t height)
: TBase(sample, name, timestamp, bufferSize)
{
	throw runtime_error("Can't create CompositeBuffer: not implemented!");
/*
	using namespace NMMSS::NMediaType;
	NMMSS::SMediaSampleHeader& header = sample->Header();
	switch(minorID)
	{
	case Video::fccRGB::ID:
		NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccRGB>(sample->GetHeader());
		break;
	case Video::fccRGBA::ID:
		NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccRGBA>(sample->GetHeader());
		break;
	default:
		throw runtime_error("Can't create CompositeBuffer: Incorrect or unknown format");
	}
*/
}

CCompositeBuffer::~CCompositeBuffer()
{
}

ITV8::uint8_t * CCompositeBuffer::GetBuffer()
{
    return GetSample()->GetBody();
}

ITV8::uint32_t CCompositeBuffer::GetBufferSize()
{
    return GetSample()->Header().nBodySize;
}

ITV8::uint32_t CCompositeBuffer::GetWidth()
{
	throw runtime_error("Can't create CompositeBuffer: not implemented!");
}

ITV8::uint32_t CCompositeBuffer::GetHeight()
{
	throw runtime_error("Can't create CompositeBuffer: not implemented!");
}
