#pragma once

#include "RegexUtility.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast/try_lexical_convert.hpp>

namespace NPluginUtility
{
template<typename T>
using ParseFunction = bool (*)(const std::string& name, T* const value);
template<typename T>
using IsValidFunction = bool (*)(const std::underlying_type_t<T> value);

namespace detail
{
template<typename ParametersType>
boost::optional<std::string> ParseParameterValue(const ParametersType& parameters, const typename ParametersType::key_type& parameter)
{
    return NPluginUtility::GetParamOptional(parameters, parameter);
}

template<typename ParametersType>
boost::optional<std::vector<std::string>> ParseParameterValueRepeated(const ParametersType& parameters,
                                                                      const typename ParametersType::key_type& parameter)
{
    const auto& value = ParseParameterValue(parameters, parameter);
    if (value)
    {
        std::vector<std::string> tokens;
        boost::split(tokens, *value, boost::is_any_of("|"));
        return tokens;
    }

    return boost::none;
}

template<typename ParametersType, typename TargetParameterType>
TargetParameterType ParseTokenAsBasicValue(const typename ParametersType::key_type& parameter, const std::string& token)
{
    TargetParameterType value;
    if (boost::conversion::try_lexical_convert(token, value))
    {
        return value;
    }

    throw std::runtime_error("Can't parse token \"" + token + "\" of parameter \"" + parameter + "\"");
}

template<typename ParametersType, typename TargetParameterType>
TargetParameterType ParseTokenAsEnumValue(const typename ParametersType::key_type& parameter,
                                          const std::string& token,
                                          const ParseFunction<TargetParameterType>& parseFunction,
                                          const IsValidFunction<TargetParameterType>& isValidFunction)
{
    {  // try parsing
        TargetParameterType value;
        if (parseFunction(token, &value))
        {
            return value;
        }
    }
    {  // try plain number
        std::underlying_type_t<TargetParameterType> value;
        if (boost::conversion::try_lexical_convert(token, value))
        {
            if (isValidFunction(value))
            {
                return static_cast<TargetParameterType>(value);
            }
        }
    }

    throw std::runtime_error("Can't parse token \"" + token + "\" of parameter \"" + parameter + "\"");
}
}  // namespace detail

template<typename ParametersType, typename TargetParameterType>
boost::optional<TargetParameterType> ParseAsBasicValue(const ParametersType& parameters, const typename ParametersType::key_type& parameter)
{
    const auto& token = detail::ParseParameterValue(parameters, parameter);
    if (token)
    {
        return detail::ParseTokenAsBasicValue<ParametersType, TargetParameterType>(parameter, *token);
    }

    return boost::none;
}

template<typename ParametersType, typename TargetParameterType>
boost::optional<std::vector<TargetParameterType>> ParseAsBasicValueRepeated(const ParametersType& parameters,
                                                                            const typename ParametersType::key_type& parameter)
{
    const auto& tokens = detail::ParseParameterValueRepeated(parameters, parameter);
    if (tokens)
    {
        std::vector<TargetParameterType> results;
        for (const auto& token : *tokens)
        {
            results.push_back(detail::ParseTokenAsBasicValue<ParametersType, TargetParameterType>(parameter, token));
        }
        return results;
    }

    return boost::none;
}

template<typename ParametersType, typename TargetParameterType>
boost::optional<TargetParameterType> ParseAsEnumValue(const ParametersType& parameters,
                                                      const typename ParametersType::key_type& parameter,
                                                      const ParseFunction<TargetParameterType>& parseFunction,
                                                      const IsValidFunction<TargetParameterType>& isValidFunction)
{
    const auto& token = detail::ParseParameterValue(parameters, parameter);
    if (token)
    {
        return detail::ParseTokenAsEnumValue<ParametersType, TargetParameterType>(parameter, *token, parseFunction, isValidFunction);
    }

    return boost::none;
}

template<typename ParametersType, typename TargetParameterType>
boost::optional<std::vector<TargetParameterType>> ParseAsEnumValueRepeated(const ParametersType& parameters,
                                                                           const typename ParametersType::key_type& parameter,
                                                                           const ParseFunction<TargetParameterType>& parseFunction,
                                                                           const IsValidFunction<TargetParameterType>& isValidFunction)
{
    const auto& tokens = detail::ParseParameterValueRepeated(parameters, parameter);
    if (tokens)
    {
        std::vector<TargetParameterType> results;
        for (const auto& token : *tokens)
        {
            results.push_back(
                detail::ParseTokenAsEnumValue<ParametersType, TargetParameterType>(parameter, token, parseFunction, isValidFunction));
        }
        return results;
    }

    return boost::none;
}
}  // namespace NPluginUtility
