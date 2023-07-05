#include "boost/date_time/c_local_time_adjustor.hpp"
#include "UtcTimeToLocal.h"

namespace NMMSS
{
    std::wstring UtcTimeToSpecifiedLocalAsString(const boost::posix_time::ptime& time,
        const boost::posix_time::minutes& timezone, const std::wstring& format, const std::string& preferredLocale)
    {
        std::wstring result;

        std::stringstream ss;
        boost::local_time::local_time_facet* facet = new boost::local_time::local_time_facet();
        ss.imbue(std::locale(std::locale(preferredLocale), facet));

        facet->format("%H:%M:%S");
        ss.str("");
        ss << timezone;

        boost::local_time::local_date_time loc_time(
            time,
            boost::local_time::time_zone_ptr(new boost::local_time::posix_time_zone(ss.str().c_str()))
            );

        std::wstringstream wss;
        boost::local_time::wlocal_time_facet* output_facet = new boost::local_time::wlocal_time_facet();
        wss.imbue(std::locale(std::locale(preferredLocale), output_facet));

        if (!format.empty())
        {
            boost::wregex pattern(L"%(?:[^%]+|%[^%]+)");
            boost::wsmatch match_result;

            std::wstring::const_iterator start = format.begin();
            std::wstring::const_iterator end = format.end();

            while (boost::regex_search(start, end, match_result, pattern, boost::match_default | boost::match_partial))
            {
                wss.str(L"");

                std::wstring part_format = match_result[0].str().c_str();
                std::wstring part_result;

                if (part_format[1] == L'k')
                {
                    part_format[1] = L'H';
                }
                else if (part_format[1] == L'l')
                {
                    part_format[1] = L'I';
                }

                output_facet->format(part_format.c_str());
                wss << loc_time;
                part_result = wss.str();

                if ((part_format[1] == L'F' && format.find(L"%Y%m%dT%H%M%S%F") != std::string::npos) || (part_format[1] == L'p' && (part_result.empty() || part_result == L" ")))
                {
                    wss.str(L"");
#ifdef WIN32
                    wss.imbue(std::locale(std::locale("en-US"), output_facet));
#else
                    wss.imbue(std::locale(std::locale("en_US.UTF-8"), output_facet));
#endif
                    output_facet->format(part_format.c_str());
                    wss << loc_time;
                    part_result = wss.str();

                    wss.imbue(std::locale(std::locale(""), output_facet));
                }

                result += part_result;
                start = match_result[0].second;
            }
        }
        else
        {
            output_facet->format(L"%Y-%m-%d %H:%M:%S");
            wss.str(L"");
            wss << loc_time;
            result = wss.str();
        }

        return result;
    }

    std::wstring UtcTimeToLocalAsString(const boost::posix_time::ptime& time, const std::wstring& format)
    {
        boost::posix_time::ptime local = boost::date_time::c_local_adjustor<boost::posix_time::ptime>::utc_to_local(time);
        boost::posix_time::time_duration shift = local - time;

        return UtcTimeToSpecifiedLocalAsString(time, boost::posix_time::minutes(shift.total_seconds() / 60), format);
    }

    std::wstring UtcTimeAsString(const boost::posix_time::ptime& time, const std::wstring& format)
    {
        return UtcTimeToSpecifiedLocalAsString(time, boost::posix_time::minutes(0), format);
    }
}
