#include "../ItvSdkUtil.h"
#include <ItvMediaSdk/include/codecConstants.h>
#include "../../ConnectionBroker.h"
#include "../../MediaType.h"

//#define BOOST_TEST_MODULE ItvSdkUtil
#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <limits>

template<typename TMediaType>
NMMSS::PSample MakeSample()
{
    static NMMSS::PAllocatorFactory allocatorFacory(NMMSS::GetLocalAllocatorFactory());
    BOOST_REQUIRE(!!allocatorFacory);
    static NMMSS::PAllocator allocator(
        allocatorFacory->CreateAllocator(NMMSS::SAllocatorRequirements(10, 1024, 0), nullptr));
    BOOST_REQUIRE(!!allocator);
    NMMSS::PSample sample(allocator->Alloc(10));
    BOOST_REQUIRE(!!sample);
    NMMSS::NMediaType::MakeMediaTypeStruct<TMediaType>(sample->GetHeader());
    return sample;
}

void CheckDescriptor(const ITVSDKUTILES::IMediaFormatDictionary& dictionary,
                     NMMSS::ISample* sample, const std::string& name,
                     ::uint32_t major, ::uint32_t subtype,
                     ::uint16_t vendor=std::numeric_limits<::uint16_t>::max(),
                     ::uint16_t codec = std::numeric_limits<::uint16_t>::max())
{
    BOOST_TEST_MESSAGE(boost::str(boost::format("CheckDescriptor(..., \"%1%\", %2%, %3%, %4%, %5%)...")
        %name%major%std::string(reinterpret_cast<const char*>(&subtype),4)%vendor%codec));

    BOOST_REQUIRE(sample != 0);

    bool hasVendor = vendor != std::numeric_limits<::uint16_t>::max();

    std::vector<ITVSDKUTILES::IMediaFormatDescriptorPtr> descriptors;
    // Checks mapping itv format name to Ngp identifiers.
    descriptors.push_back(dictionary.Find(name.c_str()));
    // Checks mapping Ngp codec identifiers to itv format name.
    descriptors.push_back(dictionary.Find(*sample));
    BOOST_FOREACH(ITVSDKUTILES::IMediaFormatDescriptorPtr desc, descriptors)
    {
        BOOST_REQUIRE(desc.get() != 0);
        BOOST_CHECK(desc->IsValid());
        BOOST_CHECK(major == desc->GetMajorID());
        BOOST_CHECK(subtype == desc->GetSubtype());
        BOOST_CHECK(name == desc->GetName());
        BOOST_CHECK(hasVendor == desc->HasVendor());
        if(hasVendor)
        {
            BOOST_CHECK(vendor == desc->GetVendor());
            BOOST_CHECK(codec == desc->GetCodec());
        }
    }
}

template<typename TMediaType>
inline void CheckDescriptor(const ITVSDKUTILES::IMediaFormatDictionary& dictionary,
                            const std::string& name,
                            ::uint16_t vendor=std::numeric_limits<::uint16_t>::max(),
                            ::uint16_t codec = std::numeric_limits<::uint16_t>::max())
{
    NMMSS::PSample sample = MakeSample<TMediaType>();
    CheckDescriptor(dictionary, sample.Get(), name, TMediaType::Major::ID, TMediaType::ID, vendor, codec);
}

template<>
inline void CheckDescriptor<NMMSS::NMediaType::Video::fccVendor>(const ITVSDKUTILES::IMediaFormatDictionary& dictionary,
                                              const std::string& name,
                                              ::uint16_t vendor,
                                              ::uint16_t codec)
{
    NMMSS::PSample sample = MakeSample<NMMSS::NMediaType::Video::fccVendor>();
    NMMSS::NMediaType::Video::fccVendor::SubtypeHeader& subHeader =
        sample->SubHeader<NMMSS::NMediaType::Video::fccVendor>();
    subHeader.nVendor = vendor;
    subHeader.nCodec = codec;

    CheckDescriptor(dictionary, sample.Get(), name, NMMSS::NMediaType::Video::fccVendor::Major::ID,
        NMMSS::NMediaType::Video::fccVendor::ID, vendor, codec);
}

BOOST_AUTO_TEST_SUITE(MediaFormatDictionaryTest)

BOOST_AUTO_TEST_CASE(Creation)
{
    using namespace NMMSS::NMediaType;
    using namespace ITV8::CSDK;

    BOOST_REQUIRE(NLogging::GetDefaultLogger()!=0);
    const ITVSDKUTILES::IMediaFormatDictionary& dictionary = ITVSDKUTILES::GetMediaFormatDictionary(NLogging::GetDefaultLogger());

    CheckDescriptor<Video::fccJPEG>(dictionary,   ITV8_VIDEO_CODEC_MJPEG);
    CheckDescriptor<Video::fccJPEG2000>(dictionary, ITV8_VIDEO_CODEC_JP2K);
    CheckDescriptor<Video::fccMXPEG>(dictionary,   ITV8_VIDEO_CODEC_MXPEG);
    CheckDescriptor<Video::fccMPEG4>(dictionary,  ITV8_VIDEO_CODEC_MPEG4);
    CheckDescriptor<Video::fccMPEG2>(dictionary,  ITV8_VIDEO_CODEC_MPEG2);
    CheckDescriptor<Video::fccH264>(dictionary,   ITV8_VIDEO_CODEC_H264);
    CheckDescriptor<Video::fccH265>(dictionary, ITV8_VIDEO_CODEC_H265);
    CheckDescriptor<Video::fccVP8>(dictionary, ITV8_VIDEO_CODEC_VP8);
    CheckDescriptor<Video::fccVP9>(dictionary, ITV8_VIDEO_CODEC_VP9);
#ifdef _WIN32
    CheckDescriptor<Video::fccWXWL>(dictionary, ITV8_VIDEO_CODEC_WXWL);
#endif
    CheckDescriptor<Video::fccVendor>(dictionary, ITV8_VIDEO_CODEC_H264_INTERLACED, AXXON_SOFT, ASC_H264_INTERLACED);
    CheckDescriptor<Video::fccVendor>(dictionary, ITV8_VIDEO_CODEC_H263,            AXXON_SOFT, ASC_H263);
    CheckDescriptor<Video::fccVendor>(dictionary, ITV8_VIDEO_CODEC_VWV,             AXXON_SOFT, ASC_VWV);
    CheckDescriptor<Video::fccVendor>(dictionary, ITV8_VIDEO_CODEC_BOSCH,           AXXON_SOFT, ASC_BOSCH);
    CheckDescriptor<Video::fccVendor>(dictionary, ITV8_MAKE_CODEC_ID(1,1),           1, 1);
    CheckDescriptor<Video::fccVendor>(dictionary, ITV8_MAKE_CODEC_ID(2,2),           2, 2);

    ITVSDKUTILES::IMediaFormatDescriptorPtr desc;
    desc = dictionary.Find("Unsupported codec name");
    BOOST_REQUIRE(desc.get() != 0);
    BOOST_REQUIRE(!desc->IsValid());

    desc = dictionary.Find(*MakeSample<Video::fccMSMPEG4V3>().Get());
    BOOST_REQUIRE(desc.get() != 0);
    BOOST_REQUIRE(!desc->HasName());
    BOOST_REQUIRE(!desc->HasVendor());

}

BOOST_AUTO_TEST_SUITE_END()
