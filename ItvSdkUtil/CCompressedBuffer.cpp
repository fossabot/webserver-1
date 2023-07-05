#include "CCompressedBuffer.h"
#include "../Sample.h"
#include "../MediaType.h"
#include "../MMCoding/FrameBuilder.h"
#include <ItvMediaSdk/include/codecConstants.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <vector>
#include <string>

using namespace std;
using namespace ITV8;

CCompressedBuffer::CCompressedBuffer(NMMSS::ISample const* sample, char const* name)
    : TBase(sample, name)
{
    using namespace NMMSS::NMediaType;

    switch (sample->Header().nSubtype)
    {
    case Video::fccJPEG::ID:
    case Video::fccJPEG2000::ID:
    case Video::fccMXPEG::ID:
    case Video::fccH264::ID:
    case Video::fccH264SVC::ID:
    case Video::fccH264SVCT::ID:
    case Video::fccH265::ID:
    case Video::fccVP8::ID:
    case Video::fccVP9::ID:
    case Video::fccMPEG4::ID:
    case Video::fccMPEG2::ID:
    case Video::fccWXWL::ID:
    case Video::fccVendor::ID:
#ifdef _DEBUG
        TBase::DebugViewSubheader(Video::ID, sample->Header().nSubtype);
#endif
        break;
    default:
            throw runtime_error("Can't create CompressedBuffer: Incorrect or unknown format");
    }
}

template <typename X> inline void UNUSED_VARIABLE(X&& x) {}

CCompressedBuffer::CCompressedBuffer(NMMSS::IFrameBuilder* frameBuilder, NMMSS::ISample* sample,
    ::uint32_t minorID, ::uint16_t vendor, ::uint16_t codec, char const* name, ITV8::uint32_t bufferSize, ITV8::timestamp_t timestamp,
    ITV8::bool_t isKeyFrame)
    : TBase(sample, name, timestamp, bufferSize)
    , m_frameBuilder(frameBuilder, NCorbaHelpers::ShareOwnership())
{
    using namespace NMMSS::NMediaType;
    NMMSS::SMediaSampleHeader& header = sample->Header();
    header.eFlags = 0;
    switch(minorID)
    {
        case Video::fccJPEG::ID:
        {
            UNUSED_VARIABLE(isKeyFrame);
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccJPEG>(sample->GetHeader());
        }
        break;
        case Video::fccJPEG2000::ID:
        {
            UNUSED_VARIABLE(isKeyFrame);
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccJPEG2000>(sample->GetHeader());
        }
        break;
        case Video::fccMXPEG::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccMXPEG>(sample->GetHeader());
            if(!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccH264::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccH264>(sample->GetHeader());
            if(!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccH264SVC::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccH264SVC>(sample->GetHeader());
            if(!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccH264SVCT::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccH264SVCT>(sample->GetHeader());
            if(!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccH265::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccH265>(sample->GetHeader());
            if (!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame |
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccVP8::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccVP8>(sample->GetHeader());
            if (!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame | 
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccVP9::ID:
        {
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccVP9>(sample->GetHeader());
            if (!isKeyFrame)
            {
                header.eFlags = NMMSS::SMediaSampleHeader::EFNeedKeyFrame | 
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccMPEG4::ID:
        {
            UNUSED_VARIABLE(isKeyFrame);
            Video::fccMPEG4::SubtypeHeader *pSubHeader=0;
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccMPEG4>(sample->GetHeader(),
                                                                    &pSubHeader);

            //    TODO: See using of mpeg4_cookies_info
            pSubHeader->nCodedWidth = 0;
            pSubHeader->nCodedHeight = 0;
            if(!isKeyFrame)
            {
                header.eFlags |= NMMSS::SMediaSampleHeader::EFNeedKeyFrame|
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccMPEG2::ID:
        {
            Video::fccMPEG2::SubtypeHeader *pSubHeader=0;
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccMPEG2>(sample->GetHeader(),
                                                                    &pSubHeader);
            pSubHeader->nCodedWidth = 0;
            pSubHeader->nCodedHeight = 0;
            if(!isKeyFrame)
            {
                header.eFlags |= NMMSS::SMediaSampleHeader::EFNeedKeyFrame|
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
        }
        break;
        case Video::fccWXWL::ID:
        {
            Video::fccWXWL::SubtypeHeader *pSubHeader=0;
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccWXWL>(sample->GetHeader(),
                                                                  &pSubHeader);
            if(!isKeyFrame)
            {
                header.eFlags |= NMMSS::SMediaSampleHeader::EFNeedKeyFrame;
            }
        }
        break;
        case Video::fccVendor::ID:
        {
            Video::fccVendor::SubtypeHeader *pSubHeader=0;
            NMMSS::NMediaType::MakeMediaTypeStruct<Video::fccVendor>(sample->GetHeader(), &pSubHeader);
            //≈сли кадр не ключевой, то помечаем, что ему требуютс€ ключивой дл€ распаковки.
            if(!isKeyFrame)
            {
                //TODO: Ќадо расшир€ть isKeyFrame - здесь приходитс€ считать, что это MPEG подобный поток.
                header.eFlags |= NMMSS::SMediaSampleHeader::EFNeedKeyFrame|
                    NMMSS::SMediaSampleHeader::EFNeedPreviousFrame;
            }
            //TODO: можно выставить еще  EFDiscontinuity, если получить от фабрики признак, что это первый кадр.
            pSubHeader->nVendor = vendor;
            pSubHeader->nCodec  = codec;
            pSubHeader->nCodedHeight = 0;
            pSubHeader->nCodedWidth  = 0;
            pSubHeader->nTailSize = 0;
        }
        break;
    default:
        throw runtime_error("Can't create CompressedBuffer: Incorrect or unknown format");
    }
}

CCompressedBuffer::~CCompressedBuffer()
{
}

NMMSS::ISample *CCompressedBuffer::Detach()
{
    using namespace NMMSS::NMediaType;
    NMMSS::ISample* sample = GetSample();

    boost::uint32_t eFlags = sample->Header().eFlags;
    bool res = m_frameBuilder->InitializeSubheader(sample);
    if (!res)
        sample->Header().eFlags = eFlags;

    return TBase::Detach();
}

ITV8::uint8_t * CCompressedBuffer::GetBuffer()
{
    return GetSample()->GetBody();
}

ITV8::uint32_t CCompressedBuffer::GetBufferSize()
{
    return GetSample()->Header().nBodySize;
}

ITV8::bool_t CCompressedBuffer::IsKeyFrame()
{
        using namespace NMMSS;
        const SMediaSampleHeader& h = GetSample()->Header();
        bool isKey = ( (h.eFlags & SMediaSampleHeader::EFPreroll)!=0 ||
                                   (h.eFlags & SMediaSampleHeader::EFNeedKeyFrame)==0   );

    return isKey;
}
