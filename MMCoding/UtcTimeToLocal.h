#ifndef UTCTIME_TO_LOCAL_H_
#define UTCTIME_TO_LOCAL_H_

#include <boost/date_time/local_time/local_time.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/local_time/posix_time_zone.hpp>
#include <boost/regex.hpp>
#include <Logging/log2.h>
#include <CorbaHelpers/Unicode.h>
#include "MMCodingExports.h"

namespace NMMSS
{
    // converts UTC time to local time and returns time string formatted using the current locale
    MMCODING_CLASS_DECLSPEC std::wstring UtcTimeToLocalAsString(const boost::posix_time::ptime& time, const std::wstring &format);

    // returns UTF8-a time string formatted using the current locale
    MMCODING_CLASS_DECLSPEC std::wstring UtcTimeAsString(const boost::posix_time::ptime &time, const std::wstring &format);

    // converts UTC time to specified local time and returns time string formatted using the current locale
    MMCODING_CLASS_DECLSPEC std::wstring UtcTimeToSpecifiedLocalAsString(const boost::posix_time::ptime &time, 
        const boost::posix_time::minutes &timezone, const std::wstring &format, const std::string& preferredLocale = std::string());
}

#endif
