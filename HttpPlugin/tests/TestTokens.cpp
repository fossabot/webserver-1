
#include <boost/test/unit_test.hpp>

#include "../Tokens.h"

namespace npu = NPluginUtility;
namespace bpt = boost::posix_time;

const boost::posix_time::ptime NONE   = boost::date_time::not_a_date_time;
const boost::posix_time::ptime FUTURE = boost::date_time::max_date_time;
const boost::posix_time::ptime PAST   = boost::date_time::min_date_time;

BOOST_AUTO_TEST_SUITE(TestTokens)

BOOST_AUTO_TEST_CASE(InvalidTokens)
{
    using namespace npu;
    BOOST_CHECK_THROW(PToken() / PToken(), XInvalidToken);
}

BOOST_AUTO_TEST_CASE(EmptyPath)
{
    using namespace npu;
    const std::string invalidPath;
    PToken validToken = Token();
    BOOST_CHECK_THROW(Parse(invalidPath, validToken), XNotMatch);
}

BOOST_AUTO_TEST_CASE(HeadSlash)
{
    using namespace npu;
    const std::string value("something");
    PToken smt = Token();

    BOOST_CHECK_NO_THROW(Parse("/" + value, smt));
    BOOST_CHECK(smt->GetValue() == value);

    BOOST_CHECK_NO_THROW(Parse(value, smt));
    BOOST_CHECK(smt->GetValue() == value);
}

BOOST_AUTO_TEST_CASE(TailSlash)
{
    using namespace npu;
    const std::string value("something");
    PToken smt = Token();

    BOOST_CHECK_NO_THROW(Parse(value + "/", smt));
    BOOST_CHECK(smt->GetValue() == value);

    BOOST_CHECK_NO_THROW(Parse(value, smt));
    BOOST_CHECK(smt->GetValue() == value);
}

BOOST_AUTO_TEST_CASE(Tokens)
{
    using namespace npu;
    PToken one   = Token();
    PToken two   = Token(2);
    PToken three = Token(3);
    PToken four  = Token(4);
    PToken five  = Token(5);
    PMask  tail  = Tail();

    const std::string valid = "/1/2/3/4/5/";
    BOOST_CHECK_NO_THROW(Parse(valid, one / tail));
    BOOST_CHECK(one->GetValue() == "1");

    BOOST_CHECK_NO_THROW(Parse(valid, two / tail));
    BOOST_CHECK(two->GetValue() == "1/2");

    BOOST_CHECK_NO_THROW(Parse(valid, three / tail));
    BOOST_CHECK(three->GetValue() == "1/2/3");

    BOOST_CHECK_NO_THROW(Parse(valid, four / tail));
    BOOST_CHECK(four->GetValue() == "1/2/3/4");

    BOOST_CHECK_NO_THROW(Parse(valid, five));
    BOOST_CHECK(five->GetValue() == "1/2/3/4/5");
}

BOOST_AUTO_TEST_CASE(Reset)
{
    using namespace npu;
    PToken first = Token();
    PToken second = Token();

    BOOST_CHECK_NO_THROW(Parse("/1/2/", first / second));
    BOOST_CHECK(first->GetValue() == "1");
    BOOST_CHECK(second->GetValue() == "2");

    BOOST_CHECK_THROW(Parse("", first / second), XFailed);
    BOOST_CHECK(first->GetValue().empty());
    BOOST_CHECK(second->GetValue().empty());
}

BOOST_AUTO_TEST_CASE(Anchor)
{
    using namespace npu;
    PToken pref = Token();
    PToken suff = Token();
    PMask must = Mask("must");

    BOOST_CHECK_THROW(Parse("", must), XNotMatch);
    BOOST_CHECK_THROW(Parse("/pref/must", must), XNotMatch);
    BOOST_CHECK_THROW(Parse("/pref/suff", must / suff), XNotMatch);

    BOOST_CHECK_NO_THROW(Parse("/must/suff", must / suff));
    BOOST_CHECK(suff->GetValue() == "suff");

    BOOST_CHECK_NO_THROW(Parse("/pref/must/suff", pref / must / suff));
    BOOST_CHECK(pref->GetValue() == "pref");
    BOOST_CHECK(suff->GetValue() == "suff");

    BOOST_CHECK_NO_THROW(Parse("/pref/must/", pref / must));
    BOOST_CHECK(pref->GetValue() == "pref");
}

BOOST_AUTO_TEST_CASE(Masks)
{
    using namespace npu;
    PMask mask = Mask("[A|a]pples?");

    BOOST_CHECK_NO_THROW(Parse("/apple/", mask));
    BOOST_CHECK(mask->GetValue() == "apple");

    BOOST_CHECK_NO_THROW(Parse("/Apples/", mask));
    BOOST_CHECK(mask->GetValue() == "Apples");

    BOOST_CHECK_THROW(Parse("/applet/", mask), XNotMatch);
    BOOST_CHECK(mask->GetValue().empty());
}

BOOST_AUTO_TEST_CASE(EmptyMask)
{
    using namespace npu;
    PMask empty = Empty();

    BOOST_CHECK_THROW(Parse("/not/empty/", empty), XNotMatch);
    BOOST_CHECK(!Match("/not/empty/", empty));

    BOOST_CHECK_NO_THROW(Parse("/", empty));
    BOOST_CHECK(Match("/", empty));

    BOOST_CHECK_NO_THROW(Parse("", empty));
    BOOST_CHECK(Match("", empty));
}

BOOST_AUTO_TEST_CASE(MaskWithSlash)
{
    using namespace npu;
    const std::string orig = "one/two";
    PMask mask = Mask(orig);

    BOOST_CHECK_NO_THROW(Parse(orig, mask));
    BOOST_CHECK(Match(orig, mask));
}

BOOST_AUTO_TEST_CASE(Alternative)
{
    using namespace npu;
    PMask fruit = Mask("(?:apple|peach)");
    PToken pref = Token();
    PToken suff = Token();

    BOOST_CHECK_NO_THROW(Parse("/pref/peach/suff/", pref / fruit / suff));
    BOOST_CHECK(pref->GetValue() == "pref");
    BOOST_CHECK(fruit->GetValue() == "peach");
    BOOST_CHECK(suff->GetValue() == "suff");
}

BOOST_AUTO_TEST_CASE(SubGroups)
{
    using namespace npu;
    PMask mask = Mask("(one)(two)");
    PToken pref = Token();
    PToken suff = Token();

    BOOST_CHECK_NO_THROW(Parse("/pref/onetwo/suff/", pref / mask / suff));
    BOOST_CHECK(pref->GetValue() == "pref");
    BOOST_CHECK(suff->GetValue() != "suff"); // Потому что маска содержит две группы.
}

BOOST_AUTO_TEST_CASE(NotCapture)
{
    using namespace npu;
    PMask mask = Mask("(?:one)(?:two)");
    PToken pref = Token();
    PToken suff = Token();

    BOOST_CHECK_NO_THROW(Parse("/pref/onetwo/suff/", pref / mask / suff));
    BOOST_CHECK(pref->GetValue() == "pref");
    BOOST_CHECK(suff->GetValue() == "suff");
}

BOOST_AUTO_TEST_CASE(WrongEndpoint)
{
    using namespace npu;
    PEndpoint ep = Endpoint();

    BOOST_CHECK_THROW(Parse("", ep), XFailed);
    BOOST_CHECK_THROW(Parse("/host/", ep), XFailed);
    BOOST_CHECK_THROW(Parse("/host/Service/", ep), XFailed);
    BOOST_CHECK_THROW(Parse("/host/Service.0/", ep), XFailed);
}

BOOST_AUTO_TEST_CASE(RightEndpoint)
{
    using namespace npu;
    PEndpoint ep = Endpoint();

    const std::string validEp = "host/Service.0/Endpoint.0";
    BOOST_CHECK_NO_THROW(Parse("/" + validEp + "/", ep));
    BOOST_CHECK(ep->GetValue() == validEp);

    PToken pref = Token();
    PToken suff = Token();

    BOOST_CHECK_NO_THROW(Parse("/pref/" + validEp + "/suff/", pref / ep / suff));
    BOOST_CHECK(ep->GetValue() == validEp);
}

BOOST_AUTO_TEST_CASE(ObjNames)
{
    using namespace npu;
    BOOST_CHECK_THROW(Parse("", ObjName(2)), XNotMatch);
    BOOST_CHECK_THROW(Parse("host", ObjName(3)), XNotMatch);

    PObjName objName = ObjName(3);
    BOOST_CHECK_NO_THROW(Parse("hosts/HostName/Service.0", objName));
    BOOST_CHECK(objName->Get().GetObjectId() == "0");
}

BOOST_AUTO_TEST_CASE(ObjNameWithPreffix)
{
    using namespace npu;
    PObjName objName = ObjName(2, "hosts/");

    BOOST_CHECK_NO_THROW(Parse("HostName/Service.0", objName));
    BOOST_CHECK(objName->Get().GetObjectId() == "0");
}

BOOST_AUTO_TEST_CASE(ObjNameWithRef)
{
    using namespace npu;
    NCorbaHelpers::CObjectName name;

    BOOST_CHECK_NO_THROW(Parse("HostName/Service.0", ObjName(name, 2, "hosts/")));
    BOOST_CHECK(name.GetObjectId() == "0");
}

BOOST_AUTO_TEST_CASE(WrongDate)
{
    using namespace npu;
    PDate date = Date();

    BOOST_CHECK_THROW(Parse("", date), XFailed);
    BOOST_CHECK_THROW(Parse("smt", date), XFailed);
}

BOOST_AUTO_TEST_CASE(RightDate)
{
    using namespace npu;
    PDate date = Date();
    PToken pref = Token();
    PToken suff = Token();

    const std::string valid("20120313T120000.123456");
    bpt::ptime d = bpt::from_iso_string(valid);

    BOOST_CHECK_NO_THROW(Parse("/" + valid + "/", date));
    BOOST_CHECK(date->Get() == d);

    BOOST_CHECK_NO_THROW(Parse("/pref/" + valid + "/suff/", pref / date / suff));
    BOOST_CHECK(date->Get() == d);

    BOOST_CHECK_THROW(Parse("", date), XFailed);
    BOOST_CHECK(date->Get() == NONE);
}

BOOST_AUTO_TEST_CASE(DateConstants)
{
    using namespace npu;
    PDate past = Date();
    PDate future = Date();

    BOOST_CHECK_NO_THROW(Parse("/past/future/", past / future));
    BOOST_CHECK(past->Get() == PAST);
    BOOST_CHECK(future->Get() == FUTURE);
}

BOOST_AUTO_TEST_CASE(OptionalDateDefaults)
{
    using namespace npu;
    PDate begin = Begin();
    PDate end   = End();

    BOOST_CHECK(begin->Get() == NONE);
    BOOST_CHECK(end->Get() == NONE);
}

BOOST_AUTO_TEST_CASE(OptionalBegin)
{
    using namespace npu;
    PDate begin = Begin();
    PDate end   = End();

    BOOST_CHECK_NO_THROW(Parse("/past/", end / begin));
    BOOST_CHECK(end->Get() == PAST);
    BOOST_CHECK(begin->Get() == FUTURE);
}

BOOST_AUTO_TEST_CASE(OptionalBeginEnd)
{
    using namespace npu;
    PToken pref = Token();
    PDate begin = Begin();
    PDate end   = End();

    BOOST_CHECK_NO_THROW(Parse("/pref/", pref / end / begin));
    BOOST_CHECK(pref->GetValue() == "pref");
    BOOST_CHECK(end->Get() == PAST);
    BOOST_CHECK(begin->Get() == FUTURE);
}

BOOST_AUTO_TEST_CASE(MandatoryAfterOptional)
{
    using namespace npu;
    PDate optional   = Begin();
    PToken mandatory = Token();

    BOOST_CHECK_THROW(optional / mandatory, XInvalidToken);
    BOOST_CHECK_NO_THROW(mandatory / optional);
}

BOOST_AUTO_TEST_CASE(StepByStep)
{
    using namespace npu;
    PToken one   = Token();
    PToken two   = Token();
    PToken three = Token();
    PToken four  = Token();
    PToken five  = Token();
    PToken tail  = Tail();

    const std::string orig = "/1/2/3/4/5/";

    BOOST_CHECK_NO_THROW(Parse(orig, one   / tail)); // parse: /1/2/3/4/5/
    BOOST_CHECK_NO_THROW(Parse(tail, two   / tail)); // parse:   /2/3/4/5/
    BOOST_CHECK_NO_THROW(Parse(tail, three / tail)); // parse:     /3/4/5/
    BOOST_CHECK_NO_THROW(Parse(tail, four  / tail)); // parse:       /4/5/
    BOOST_CHECK_NO_THROW(Parse(tail, five));         // parse:         /5/

    BOOST_CHECK(one->GetValue()   == "1");
    BOOST_CHECK(two->GetValue()   == "2");
    BOOST_CHECK(three->GetValue() == "3");
    BOOST_CHECK(four->GetValue()  == "4");
    BOOST_CHECK(five->GetValue()  == "5");
}

BOOST_AUTO_TEST_CASE(OptionalStepByStep)
{
    using namespace npu;
    PToken one  = Token();
    PDate date  = Date();
    PDate begin = Begin();
    PDate end   = End();
    PToken tail = Tail();

    const std::string orig = "/1/past/future/";

    BOOST_CHECK_NO_THROW(Parse(orig, one / tail));
    BOOST_CHECK_NO_THROW(Parse(tail, date / tail));
    BOOST_CHECK_NO_THROW(Parse(tail, end / tail));
    BOOST_CHECK_NO_THROW(Parse(tail, begin)); // Будет присвоено значение по умолчанию.

    BOOST_CHECK(one->GetValue() == "1");
    BOOST_CHECK(date->Get()     == PAST);
    BOOST_CHECK(end->Get()      == FUTURE);
    BOOST_CHECK(begin->Get()    == FUTURE); // Этому значения не хватило, было присвоено значение по умолчанию.
}

BOOST_AUTO_TEST_CASE(RefVersions)
{
    using namespace npu;
    std::string str;
    std::string ep;
    bpt::ptime dt;
    bpt::ptime begin;
    bpt::ptime end;

    const std::string STR = "str";
    const std::string EP = "Host/Service.0/endpoint.0";
    const std::string past = "past";
    const std::string orig = STR + "/" + EP + "/" + past + "/" + past + "/" + past;

    BOOST_CHECK_NO_THROW(Parse(orig, Token(str) / Endpoint(ep) / Date(dt) / Begin(begin) / End(end)));
    BOOST_CHECK(str   == STR);
    BOOST_CHECK(ep    == EP);
    BOOST_CHECK(dt    == PAST);
    BOOST_CHECK(begin == PAST);
    BOOST_CHECK(end   == PAST);
}

BOOST_AUTO_TEST_CASE(FailMatchings)
{
    using namespace npu;
    PMask tail = Tail();

    const std::string path = "/1/2/3";
    BOOST_CHECK(Match(path, tail));
    BOOST_CHECK(!Match(tail, Mask("-1") / tail)); // tail не должен измениться
    BOOST_CHECK( Match(tail, Mask("1")  / tail));
    BOOST_CHECK(!Match(tail, Mask("-2") / tail)); // tail не должен измениться
    BOOST_CHECK( Match(tail, Mask("2")  / tail));
    BOOST_CHECK(!Match(tail, Mask("-3") / tail)); // tail не должен измениться
    BOOST_CHECK( Match(tail, Mask("3")  / tail));
}

BOOST_AUTO_TEST_SUITE_END()
