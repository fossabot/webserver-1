#pragma once

#include "RegexUtility.h"
#include <axxonsoft/bl/domain/Domain.grpc.pb.h>

namespace NPluginHelpers
{
const char* const SEARCH_FILTER_PARAMETER = "filter";

const char* const SEARCH_QUERY_PARAMETER = "query.query";
const char* const SEARCH_FIELDS_PARAMETER = "query.search_fields";
const char* const SEARCH_TYPE_PARAMETER = "query.search_type";
const char* const SEARCH_DECORATED_NAME_TEMPLATE_PARAMETER = "query.decorated_name_template";

const char* const SEARCH_PAGE_TOKEN_PARAMETER = "next_page";
const int DEFAULT_PAGE_SIZE = 1000;

template<typename T>
using ParseFunction = bool (&)(const std::string& name, T* const value);
template<typename T>
using IsValidFunction = bool (&)(int value);

template<typename T>
bool parseEnumValue(const std::string& input, ParseFunction<T>& parseFunction, IsValidFunction<T>& isValidFunction, T& outputValue)
{
    T value;
    if (parseFunction(input, &value))
    {
        outputValue = value;
        return true;
    }

    std::string digits;
    std::copy_if(input.begin(), input.end(), std::back_inserter(digits), isdigit);
    if (digits.size() > 0)
    {
        const auto& maybeNumber = std::atoi(digits.c_str());
        if (isValidFunction(maybeNumber))
        {
            outputValue = static_cast<T>(maybeNumber);
            return true;
        }
    }

    return false;
}

template<typename RequestType>
bool resolveListQuery(RequestType& request, const NPluginUtility::TParams& queryParameters)
{
    const auto& filterParameter(NPluginUtility::GetParam(queryParameters, SEARCH_FILTER_PARAMETER, std::string()));
    const auto& queryParameter(NPluginUtility::GetParam(queryParameters, SEARCH_QUERY_PARAMETER, std::string()));

    if (!filterParameter.empty())
    {
        auto query = request.mutable_query();
        query->add_search_fields(axxonsoft::bl::domain::SearchQuery::ACCESS_POINT);
        query->set_search_type(axxonsoft::bl::domain::SearchQuery::SUBSTRING);
        query->set_query(filterParameter);
    }
    else if (!queryParameter.empty())
    {
        // set query string
        request.mutable_query()->set_query(queryParameter);

        // set search fields
        const auto& fieldsParameter(NPluginUtility::GetParam(queryParameters, SEARCH_FIELDS_PARAMETER, std::string()));
        if (!fieldsParameter.empty())
        {
            std::vector<std::string> tokens;
            boost::split(tokens, fieldsParameter, boost::is_any_of("|"));
            for (const auto& token : tokens)
            {
                const auto& searchFields = request.mutable_query()->search_fields();
                axxonsoft::bl::domain::SearchQuery::SearchField field;
                if (!parseEnumValue(token,
                                    axxonsoft::bl::domain::SearchQuery::SearchField_Parse,
                                    axxonsoft::bl::domain::SearchQuery::SearchField_IsValid,
                                    field))
                {
                    return false;
                }

                if (std::find(searchFields.begin(), searchFields.end(), field) != searchFields.end())
                {
                    continue;  // duplicate field
                }
                request.mutable_query()->add_search_fields(field);
            }
        }

        // set search type
        const auto& typeParameter(NPluginUtility::GetParam(queryParameters, SEARCH_TYPE_PARAMETER, std::string()));
        axxonsoft::bl::domain::SearchQuery::SearchType type;
        if (!parseEnumValue(typeParameter,
                            axxonsoft::bl::domain::SearchQuery::SearchType_Parse,
                            axxonsoft::bl::domain::SearchQuery::SearchType_IsValid,
                            type))
        {
            return false;
        }

        request.mutable_query()->set_search_type(type);

        // set decorated name template
        const auto& nameTemplateParameter(
            NPluginUtility::GetParam(queryParameters, SEARCH_DECORATED_NAME_TEMPLATE_PARAMETER, std::string()));
        request.mutable_query()->set_decorated_name_template(nameTemplateParameter);
    }

    return true;
}
template<typename RequestType>
void resolvePagination(RequestType& request,
                       const NPluginUtility::TParams& queryParameters,
                       const axxonsoft::bl::domain::EViewMode& viewMode)
{
    // resolve view
    request.set_view(viewMode);

    // resolve page token
    const auto& pageTokenParameter(NPluginUtility::GetParam(queryParameters, SEARCH_PAGE_TOKEN_PARAMETER, std::string()));
    request.set_page_token(pageTokenParameter);

    // resolve page size
    const auto& pageSizeParameter(NPluginUtility::GetParam(queryParameters, LIMIT_MASK, DEFAULT_PAGE_SIZE));
    request.set_page_size(pageSizeParameter);
}
}  // namespace NPluginHelpers
