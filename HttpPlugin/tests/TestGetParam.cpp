#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/test/unit_test.hpp>

#include "../RegexUtility.h"

namespace npu = NPluginUtility;

template<class T>
void Setter(T &dest, const T &value)
{
    dest = value;
}

BOOST_AUTO_TEST_SUITE(GetParamTest)

BOOST_AUTO_TEST_CASE(Empty)
{
    npu::TParams empty;
    int value;
    BOOST_REQUIRE_THROW(npu::GetParam(empty, "absent", value), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(EmptySetter)
{
    npu::TParams empty;
    int value;
    BOOST_REQUIRE_THROW(npu::GetParam<int>(empty, "absent", boost::bind(Setter<int>, boost::ref(value), _1)), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(EmptyName)
{
    npu::TParams valid;
    valid["name"] = "value";
    int value;
    BOOST_REQUIRE_THROW(npu::GetParam(valid, "", value), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(BadCast)
{
    npu::TParams invalid;
    invalid["name"] = "value";
    int value;
    BOOST_REQUIRE_THROW(npu::GetParam(invalid, "name", value), boost::bad_lexical_cast);
}

BOOST_AUTO_TEST_CASE(BadCastSetter)
{
    npu::TParams invalid;
    invalid["name"] = "value";
    int value;
    BOOST_REQUIRE_THROW(npu::GetParam<int>(invalid, "name", boost::bind(Setter<int>, boost::ref(value), _1)), boost::bad_lexical_cast);
}

BOOST_AUTO_TEST_CASE(Untouched)
{
    int DEFAULT_VALUE = 100;
    npu::TParams invalid;
    invalid["name"] = "value";
    int value = DEFAULT_VALUE;

    BOOST_REQUIRE_THROW(npu::GetParam(invalid, "name", value), boost::bad_lexical_cast);
    BOOST_REQUIRE(value == DEFAULT_VALUE);
}

BOOST_AUTO_TEST_CASE(UntouchedSetter)
{
    int DEFAULT_VALUE = 100;
    npu::TParams invalid;
    invalid["name"] = "value";
    int value = DEFAULT_VALUE;

    BOOST_REQUIRE_THROW(npu::GetParam<int>(invalid, "name", boost::bind(Setter<int>, boost::ref(value), _1)), boost::bad_lexical_cast);
    BOOST_REQUIRE(value == DEFAULT_VALUE);
}

BOOST_AUTO_TEST_CASE(SetDefault)
{
    int DEFAULT_VALUE = 100;
    npu::TParams invalid;
    invalid["name"] = "value";
    int value = 0;

    BOOST_REQUIRE(value != DEFAULT_VALUE);
    BOOST_REQUIRE_NO_THROW(npu::GetParam(invalid, "name", value, DEFAULT_VALUE));
    BOOST_REQUIRE(value == DEFAULT_VALUE);
}

BOOST_AUTO_TEST_CASE(SetDefaultSetter)
{
    int DEFAULT_VALUE = 100;
    npu::TParams invalid;
    invalid["name"] = "value";
    int value = 0;

    BOOST_REQUIRE(value != DEFAULT_VALUE);
    BOOST_REQUIRE_NO_THROW(
        npu::GetParam<int>(invalid, "name", boost::bind(Setter<int>, boost::ref(value), _1), DEFAULT_VALUE));
    BOOST_REQUIRE(value == DEFAULT_VALUE);
}

BOOST_AUTO_TEST_CASE(SetDefaultIfNotFound)
{
    int DEFAULT_VALUE = 100;
    npu::TParams empty;
    int value = 0;

    BOOST_REQUIRE(value != DEFAULT_VALUE);
    BOOST_REQUIRE_NO_THROW(npu::GetParam(empty, "", value, DEFAULT_VALUE));
    BOOST_REQUIRE(value == DEFAULT_VALUE);
}

BOOST_AUTO_TEST_SUITE_END()