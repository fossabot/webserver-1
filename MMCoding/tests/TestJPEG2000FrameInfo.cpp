#include <boost/test/unit_test.hpp>

#include "../FrameInfo.h"

namespace
{
#include "./Jpeg2000TestData.h"
}

BOOST_AUTO_TEST_SUITE(MMCoding)

BOOST_AUTO_TEST_CASE(Jpeg2000FrameInfoJ2C)
{
    NMMSS::CFrameInfo info = { 0 };
    uint8_t resolutionLevels;

    BOOST_REQUIRE(
        NMMSS::FindFrameInfoJPEG2000(info, resolutionLevels, jpeg2000_codestream, sizeof(jpeg2000_codestream)) == true
    );

    BOOST_CHECK(info.type == 0);
    BOOST_CHECK(info.width == 6144);
    BOOST_CHECK(info.height == 4096);
    BOOST_CHECK(resolutionLevels == 7);
}

BOOST_AUTO_TEST_CASE(Jpeg2000FrameInfoJP2)
{
    NMMSS::CFrameInfo info = { 0 };
    uint8_t resolutionLevels;

    BOOST_REQUIRE(
        NMMSS::FindFrameInfoJPEG2000(info, resolutionLevels, jpeg2000_jp2, sizeof(jpeg2000_jp2)) == true
    );

    BOOST_CHECK(info.type == 0);
    BOOST_CHECK(info.width == 5120);
    BOOST_CHECK(info.height == 3840);
    BOOST_CHECK(resolutionLevels == 5);
}

BOOST_AUTO_TEST_SUITE_END()
