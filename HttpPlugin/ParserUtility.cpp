#include "ParserUtility.h"

#include <boost/regex.hpp>
#include <boost/date_time/posix_time/time_parsers.hpp>

namespace
{
    const char* const ENDPOINT_TEMPLATE = "^/([^/]*/[^/]*/[^/]*)";

    const char* const TIME_TEMPLATE = "^/([0-9]{8,8}T[0-9]{6,6})";
    const char* const TIME_NOW_TEMPLATE = "^/now";
    const char* const TIME_PAST_TEMPLATE = "^/past";
    const char* const TIME_FUTURE_TEMPLATE = "^/future";
}

namespace NPluginUtility
{
    bool GetTimeParameter(const char*& beg, const char*& end, boost::posix_time::ptime& t,
        bool checkCompletion)
    {
        bool res = false;
        boost::regex reTimeDeclaration(TIME_TEMPLATE, boost::regex::perl|boost::regex::icase);
        boost::cmatch searchResult;
        if (boost::regex_search(beg, end, searchResult, reTimeDeclaration))
        {
            std::string tm(searchResult[1]);
            t = boost::posix_time::from_iso_string(tm);
            beg += searchResult.prefix().length();
            beg += searchResult.length();
            res = true;
        }
        if (!res)
        {
            res = true;
            boost::regex reNowDeclaration(TIME_NOW_TEMPLATE, boost::regex::perl|boost::regex::icase);
            if (boost::regex_search(beg, end, searchResult, reNowDeclaration))
            {
                t = boost::posix_time::microsec_clock::universal_time();
                beg += searchResult.prefix().length();
                beg += searchResult.length();
            }
            else
            {
                boost::regex rePastDeclaration(TIME_PAST_TEMPLATE, boost::regex::perl|boost::regex::icase);
                if (boost::regex_search(beg, end, searchResult, rePastDeclaration))
                {
                    t = boost::date_time::min_date_time;
                    beg += searchResult.prefix().length();
                    beg += searchResult.length();
                }
                else
                {
                    boost::regex reFutureDeclaration(TIME_FUTURE_TEMPLATE, boost::regex::perl|boost::regex::icase);
                    if (boost::regex_search(beg, end, searchResult, reFutureDeclaration))
                    {
                        t = boost::date_time::max_date_time;
                        beg += searchResult.prefix().length();
                        beg += searchResult.length();
                    }
                    else
                    {
                        res = false;
                    }
                }
            }
        }

        if (checkCompletion)
            return beg == end;

        return res;
    }

    bool GetEndpoint(const char*& beg, const char*& end, std::string& value)
    {
        bool res = false;
        boost::regex reEndpointDeclaration(ENDPOINT_TEMPLATE, boost::regex::perl|boost::regex::icase);
        boost::cmatch searchResult;
        if (boost::regex_search(beg, end, searchResult, reEndpointDeclaration))
        {
            value.append(searchResult[1]);
            beg += searchResult.prefix().length();
            beg += searchResult.length();
            res = true;
        }
        return res;
    }
}
