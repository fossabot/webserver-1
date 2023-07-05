#include "../RegexUtility.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(TestRegexUtility)

BOOST_AUTO_TEST_CASE(TestParseParameters)
{
    const auto& test = [](const bool expectedParsingResult, const std::string& query, const NPluginUtility::TParams& expectedParameters)
    {
        NPluginUtility::TParams parameters;
        BOOST_CHECK_MESSAGE(expectedParsingResult == NPluginUtility::ParseParams(query, parameters), "Failed for query: '" << query << "'");
        for (const auto& expectation : expectedParameters)
        {
            BOOST_CHECK_MESSAGE(parameters.at(expectation.first) == expectation.second,
                                "Failed expectation: '" << expectation.first << "':'" << expectation.second << "'");
        }
    };

    // trailing dot supported
    test(true, "field.subfield=value&second=another", {{"field.subfield", "value"}, {"second", "another"}});
    test(true, "field.subfield=value&second=another&", {{"field.subfield", "value"}, {"second", "another"}});
    test(true, "field.subfield.=value&second=another", {{"field.subfield", "value"}, {"second", "another"}});
    test(true, "field.subfield.=value&second.=another&", {{"field.subfield", "value"}, {"second", "another"}});
    test(true, "field.=value&second=another&", {{"field", "value"}, {"second", "another"}});
    test(true, "field.=value&second=another", {{"field", "value"}, {"second", "another"}});
    test(true, "field=value&second=another&", {{"field", "value"}, {"second", "another"}});
    test(true, "field=value&second=another", {{"field", "value"}, {"second", "another"}});
    test(true, "a=b&second=another&", {{"a", "b"}, {"second", "another"}});
    test(true, "a.=b&second=another", {{"a", "b"}, {"second", "another"}});
    test(true, "abc=qwe_aBc__ZXC==", {{"abc", "qwe_aBc__ZXC=="}});
    test(true, "a.=b&", {{"a", "b"}});
    test(true, "a.=b", {{"a", "b"}});
    test(true, "a=b&", {{"a", "b"}});
    test(true, "a=b", {{"a", "b"}});
    test(true, "a.=", {{"a", ""}});
    test(true, "a=", {{"a", ""}});
    test(true, "a_b=c", {{"a_b", "c"}});

    // any other trailing character is not supported after which there is a value definition
    test(false, "a-b=c", {});
    test(false, "a~=b&", {});
    test(false, "a-=b", {});
    test(false, "a+=b&", {});
    test(false, "a;=b", {});

    // these should be failing, but since the regex needs to support legacy API, this is omitted for now
    test(true, "a", {{"a", ""}});
    test(true, "a.", {{"a", ""}});
    test(true, "a,", {{"a", ","}});
    test(true, "a:", {{"a", ":"}});
    test(true, "a++", {{"a", "++"}});
    test(true, "abc-def", {{"abc", "-def"}});
    test(true, "abc_def-asd&", {{"abc_def", "-asd"}});
    test(true, "a==", {{"a", "="}});
    test(true, "a======", {{"a", "====="}});
    test(true, "a|=", {{"a", "|="}});
}

BOOST_AUTO_TEST_SUITE_END()
