#include "CAudioBufferMpeg.h"
#include "../Sample.h"
#include "../MediaType.h"
#include "../ConnectionBroker.h"
#include "../MMCoding/FrameBuilder.h"

#include <cassert>

using namespace std;
using namespace ITV8;

CAudioBufferMpeg::CAudioBufferMpeg(NMMSS::IAllocator* allocator, NMMSS::ISample* sample, ::uint32_t minorID, const char* name,
        ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp, ITV8::uint32_t extraSize, IExtraDataHolder* extraDataHolder)
                : TBase(sample, name, timestamp, bufferSize)
                , m_allocator(allocator, NCorbaHelpers::ShareOwnership())
                , m_extraDataHolder(extraDataHolder)
{
    assert(m_extraDataHolder);
    using namespace NMMSS::NMediaType;
    switch(minorID)
    {
    case Audio::AAC::ID:
        {
            NMMSS::NMediaType::Audio::AAC::SubtypeHeader* pSubtypeHeader;
            MakeMediaTypeStruct<Audio::AAC>(sample->GetHeader(), &pSubtypeHeader);
            m_extraBuffer.resize(extraSize);
            break;
        }
    default:
        throw runtime_error("Can't create AudioBuffer: Incorrect or unknown format");
    }
}

ITV8::uint8_t* CAudioBufferMpeg::GetBuffer()
{
	return GetSample()->GetBody();
}

ITV8::uint32_t CAudioBufferMpeg::GetBufferSize()
{
	return GetSample()->Header().nBodySize;
}

ITV8::uint8_t*	CAudioBufferMpeg::GetExtraData()
{
	return &m_extraBuffer[0];
}

ITV8::uint32_t	CAudioBufferMpeg::GetExtraSize()
{
	using namespace NMMSS::NMediaType;

	switch (GetSample()->Header().nSubtype)
	{
	case Audio::AAC::ID:
		return (ITV8::uint32_t)m_extraBuffer.size();
	default:
		throw runtime_error("Can't get CAudioBufferMpeg::GetExtraSize: Incorrect or unknown format");
	}
}

NMMSS::ISample* CAudioBufferMpeg::Detach()
{
    using namespace NMMSS::NMediaType;
    NMMSS::ISample* sample = TBase::Detach();

    
    if (!m_extraBuffer.empty())
    {
        m_extraDataHolder->SetExtraData(m_extraBuffer.data(), m_extraBuffer.size());
    }
    
    const size_t extraDataSize = m_extraDataHolder->GetExtraDataSize();

    if (extraDataSize)
    {
        const ITV8::uint8_t* extraData = m_extraDataHolder->GetExtraData();
        const size_t extraDataOffset = sample->Header().nBodySize - extraDataSize;
        std::copy(extraData, extraData + extraDataSize, sample->GetBody() + extraDataOffset);
    }

    if (sample->Header().nSubtype == NMMSS::NMediaType::Audio::AAC::ID)
    {
        NMMSS::NMediaType::Audio::AAC::SubtypeHeader& pSubtypeHeader = 
            sample->SubHeader<NMMSS::NMediaType::Audio::AAC>();
        Audio::AAC::ParseExtradata(&m_extraBuffer[0], AAC_EXTRA_LENGTH,
            pSubtypeHeader.nSampleRate, pSubtypeHeader.nChannelsCount);
        pSubtypeHeader.nExtraDataSize = extraDataSize;
    }

    return sample;
}
