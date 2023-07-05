#include <cstdarg>

#include <boost/format.hpp>

#include "Constants.h"
#include "RegexUtility.h"

namespace
{
    // !!! DEPRECATED !!!
    const char* const QUERY_NUMBER_PARAMETER_TEMPLATE = "%s=([-]?[0-9]*)";
    const char* const QUERY_STRING_PARAMETER_TEMPLATE = "%s=([^&]*)";

    // !!! DEPRECATED !!!
    bool GetStringValue(const char*& beg, const char*& end, const char* regexTemplate, std::string& value)
    {
        bool res = false;
        boost::regex reTemplate(regexTemplate, boost::regex::perl|boost::regex::icase);
        boost::cmatch searchResult;
        if (boost::regex_search(beg, end, searchResult, reTemplate))
        {
            value.append(searchResult[1]);
            beg += searchResult.prefix().length();
            beg += searchResult.length();
            res = true;
        }
        return res;
    }
}

namespace NPluginUtility
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // !!! DEPRECATED !!!
    bool GetQueryStringParameter(const char*& beg, const char*& end, 
        const char* paramName, std::string& value)
    {
        std::string searchQuery((boost::format(QUERY_STRING_PARAMETER_TEMPLATE) %paramName).str());
        return GetStringValue(beg, end, searchQuery.c_str(), value);
    }

    // !!! DEPRECATED !!!
    bool GetQueryParameters(const char*& beg, const char*& end, int paramCount, ...)
    {
        bool res = true;

        va_list vl;
        va_start(vl, paramCount);
        for (int i = 0; i < paramCount; ++i)
        {
            SQueryParameter* qp = va_arg(vl, SQueryParameter*);

            std::string searchQuery((boost::format(QUERY_NUMBER_PARAMETER_TEMPLATE) % qp->name).str());
            boost::regex reNumberDeclaration(searchQuery.c_str(), boost::regex::perl|boost::regex::icase);
            boost::cmatch searchResult;
            if (boost::regex_search(beg, end, searchResult, reNumberDeclaration))
            {
                qp->value = atoi(searchResult[1].str().c_str());
            }
            else
                res = false;
        }
        va_end(vl);
        return res;
    }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
