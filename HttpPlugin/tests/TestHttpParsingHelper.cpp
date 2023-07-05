#include "../GrpcIncludes.h"
#include "../HttpParsingHelper.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(TestHttpParsingHelper)

namespace Basic
{
template<typename TargetParameterType>
void test(const std::string& query, const std::string& parameter, const TargetParameterType& value)
{
    NPluginUtility::TParams queryParameters;
    NPluginUtility::ParseParams(query, queryParameters);

    const auto& result = NPluginUtility::ParseAsBasicValue<NPluginUtility::TParams, TargetParameterType>(queryParameters, parameter);
    if (result)
    {
        BOOST_CHECK(*result == value);
        return;
    }

    throw std::runtime_error("");
};

template<typename TargetParameterType>
void testRepeated(const std::string& query, const std::string& parameter, const std::vector<TargetParameterType>& value)
{
    NPluginUtility::TParams queryParameters;
    NPluginUtility::ParseParams(query, queryParameters);

    const auto& result =
        NPluginUtility::ParseAsBasicValueRepeated<NPluginUtility::TParams, TargetParameterType>(queryParameters, parameter);
    if (result)
    {
        for (typename std::vector<TargetParameterType>::size_type i = 0; i < value.size(); i++)
        {
            BOOST_CHECK((*result)[i] == value[i]);
        }

        return;
    }

    throw std::runtime_error("");
};
}  // namespace Basic

BOOST_AUTO_TEST_CASE(TestParsingBasic)
{
    BOOST_CHECK_NO_THROW(Basic::test<int>("asd.asd=1", "asd.asd", 1));
    BOOST_CHECK_NO_THROW(Basic::test<float>("asd.asd=3.14", "asd.asd", 3.14f));
    BOOST_CHECK_NO_THROW(Basic::test<std::string>("asd.asd=a", "asd.asd", "a"));

    BOOST_CHECK_THROW(Basic::test<int>("asd.asd=1|", "asd.asd", 0), std::runtime_error);
    BOOST_CHECK_THROW(Basic::test<int>("asd.asd=a", "asd.asd", 0), std::runtime_error);
    BOOST_CHECK_THROW(Basic::test<float>("asd.asd=1.2|", "asd.asd", 1.0f), std::runtime_error);
    BOOST_CHECK_THROW(Basic::test<float>("asd.asd=a", "asd.asd", 0.0f), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestParsingBasicRepeated)
{
    BOOST_CHECK_NO_THROW(Basic::testRepeated<int>("asd.asd=1", "asd.asd", {1}));
    BOOST_CHECK_NO_THROW(Basic::testRepeated<float>("asd.asd=3.14|6.66", "asd.asd", {3.14f, 6.66f}));
    BOOST_CHECK_NO_THROW(Basic::testRepeated<std::string>("asd.asd=a|b", "asd.asd", {"a", "b"}));

    BOOST_CHECK_THROW(Basic::testRepeated<int>("asd.asd=1|", "asd.asd", {}), std::runtime_error);
    BOOST_CHECK_THROW(Basic::testRepeated<int>("asd.asd=", "asd.asd", {}), std::runtime_error);
    BOOST_CHECK_THROW(Basic::testRepeated<int>("asd.asd=asd|asd", "asd.asd", {}), std::runtime_error);
    BOOST_CHECK_THROW(Basic::testRepeated<float>("asd.asd=3.14|", "asd.asd", {}), std::runtime_error);
    BOOST_CHECK_THROW(Basic::testRepeated<float>("asd.asd=", "asd.asd", {}), std::runtime_error);
    BOOST_CHECK_THROW(Basic::testRepeated<float>("asd.asd=asd|asd", "asd.asd", {}), std::runtime_error);
}

namespace Enum
{
template<typename TargetParameterType>
void test(const std::string& query,
          const std::string& parameter,
          const NPluginUtility::ParseFunction<TargetParameterType>& parseFunction,
          const NPluginUtility::IsValidFunction<TargetParameterType>& isValidFunction,
          const TargetParameterType& value)
{
    NPluginUtility::TParams queryParameters;
    NPluginUtility::ParseParams(query, queryParameters);

    const auto& result = NPluginUtility::ParseAsEnumValue<NPluginUtility::TParams, TargetParameterType>(queryParameters,
                                                                                                        parameter,
                                                                                                        parseFunction,
                                                                                                        isValidFunction);
    if (result)
    {
        BOOST_CHECK(*result == value);
        return;
    }

    throw std::runtime_error("");
};

template<typename TargetParameterType>
void testRepeated(const std::string& query,
                  const std::string& parameter,
                  const NPluginUtility::ParseFunction<TargetParameterType>& parseFunction,
                  const NPluginUtility::IsValidFunction<TargetParameterType>& isValidFunction,
                  const std::vector<TargetParameterType>& value)
{
    NPluginUtility::TParams queryParameters;
    NPluginUtility::ParseParams(query, queryParameters);

    const auto& result = NPluginUtility::ParseAsEnumValueRepeated<NPluginUtility::TParams, TargetParameterType>(queryParameters,
                                                                                                                parameter,
                                                                                                                parseFunction,
                                                                                                                isValidFunction);
    if (result)
    {
        for (typename std::vector<TargetParameterType>::size_type i = 0; i < value.size(); i++)
        {
            BOOST_CHECK((*result)[i] == value[i]);
        }

        return;
    }

    throw std::runtime_error("");
};
}  // namespace Enum

BOOST_AUTO_TEST_CASE(TestParsingEnum)
{
    BOOST_CHECK_NO_THROW(
        Enum::test<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=1",
                                                                    "asd.asd",
                                                                    &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                    &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                    axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME));
    BOOST_CHECK_NO_THROW(
        Enum::test<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=DISPLAY_NAME",
                                                                    "asd.asd",
                                                                    &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                    &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                    axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME));

    BOOST_CHECK_THROW(Enum::test<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=1|",
                                                                                  "asd.asd",
                                                                                  &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                                  &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                                  axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME),
                      std::runtime_error);
    BOOST_CHECK_THROW(Enum::test<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=a",
                                                                                  "asd.asd",
                                                                                  &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                                  &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                                  axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME),
                      std::runtime_error);
    BOOST_CHECK_THROW(Enum::test<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=display_name",
                                                                                  "asd.asd",
                                                                                  &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                                  &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                                  axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME),
                      std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestParsingEnumRepeated)
{
    BOOST_CHECK_NO_THROW(Enum::testRepeated<axxonsoft::bl::domain::SearchQuery::SearchField>(
        "asd.asd=0|1",
        "asd.asd",
        &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
        &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
        {axxonsoft::bl::domain::SearchQuery::DECORATED_NAME, axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME}));
    BOOST_CHECK_NO_THROW(Enum::testRepeated<axxonsoft::bl::domain::SearchQuery::SearchField>(
        "asd.asd=DECORATED_NAME|DISPLAY_NAME",
        "asd.asd",
        &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
        &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
        {axxonsoft::bl::domain::SearchQuery::DECORATED_NAME, axxonsoft::bl::domain::SearchQuery::DISPLAY_NAME}));

    BOOST_CHECK_THROW(
        Enum::testRepeated<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=1|",
                                                                            "asd.asd",
                                                                            &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                            &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                            {}),
        std::runtime_error);
    BOOST_CHECK_THROW(
        Enum::testRepeated<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=",
                                                                            "asd.asd",
                                                                            &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                            &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                            {}),
        std::runtime_error);
    BOOST_CHECK_THROW(
        Enum::testRepeated<axxonsoft::bl::domain::SearchQuery::SearchField>("asd.asd=decorated_name|display_name",
                                                                            "asd.asd",
                                                                            &axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                                                            &axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                                                            {}),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
