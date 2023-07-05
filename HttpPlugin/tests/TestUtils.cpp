#include <boost/test/unit_test.hpp>

#include "../RegexUtility.h"

BOOST_AUTO_TEST_SUITE(HttpPlugin)

BOOST_AUTO_TEST_CASE(Utils_EmptyQuery)
{
    using namespace NPluginUtility;

    TParams params;
    BOOST_CHECK(ParseParams("", params));
    BOOST_CHECK(params.empty());
}

BOOST_AUTO_TEST_CASE(Utils_PostCondition)
{
    using namespace NPluginUtility;

    TParams params;
    params["trash"] = "poo";

    BOOST_CHECK(ParseParams("", params));
    BOOST_CHECK(params.empty());
}

BOOST_AUTO_TEST_CASE(Utils_Lowercase)
{
    using namespace NPluginUtility;

    TParams params;
    BOOST_CHECK(ParseParams("NAME=VALUE", params));
    BOOST_CHECK(!params.empty());
    BOOST_CHECK(params["name"] == "VALUE");
}

BOOST_AUTO_TEST_CASE(Utils_ValidQuery)
{
    using namespace NPluginUtility;

    TParams orig;
    orig["speed"] = "1";
    orig["guid"] = "70da1101-a0fc-41ba-aa26-2f77ed356029";
    orig["fraction"] = "1.2";

    std::stringstream query;
    for(TParams::const_iterator i = orig.begin(); i != orig.end(); ++i)
        query << i->first << "=" << i->second << "&";

    TParams params;
    BOOST_CHECK(ParseParams(query.str(), params));
    BOOST_CHECK(!params.empty());
    BOOST_CHECK(params == orig);
}

BOOST_AUTO_TEST_CASE(Utils_InvalidQuery)
{
    using namespace NPluginUtility;

    TParams params;
    //BOOST_CHECK(!ParseParams("first=1&second=", params));
    //BOOST_CHECK(params.empty());

    //BOOST_CHECK(!ParseParams("first=1&second", params));
    //BOOST_CHECK(params.empty());

    BOOST_CHECK(!ParseParams("=1&second=", params));
    BOOST_CHECK(params.empty());
}

BOOST_AUTO_TEST_SUITE_END()