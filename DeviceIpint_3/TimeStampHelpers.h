#ifndef DEVICEIPINT3_TIMESTAMPHELPERS_H
#define DEVICEIPINT3_TIMESTAMPHELPERS_H

#include <PtimeFromQword.h>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace IPINT30
{

inline boost::gregorian::date startOfEpoch()
{
    static const boost::gregorian::date DATE_EPOCH(1970, 1, 1);
    return DATE_EPOCH;
}

// Generate time stamp value for current time
inline ITV8::timestamp_t ipintNow()
{
    boost::posix_time::time_duration diff = 
        boost::posix_time::microsec_clock::universal_time()    - boost::posix_time::ptime(startOfEpoch());
    return diff.total_milliseconds();
}

inline ITV8::timestamp_t toIpintTime(const boost::posix_time::ptime& time)
{
    const boost::posix_time::ptime epoch(startOfEpoch());
    if (time < epoch)
    {
        return 0;
    }
    return (time - epoch).total_milliseconds();
}

inline ITV8::timestamp_t toIpintTime(const std::string& time)
{
    return toIpintTime(boost::posix_time::from_iso_string(time));
}

inline ITV8::timestamp_t toIpintTime(const boost::gregorian::date& date)
{
    return toIpintTime(boost::posix_time::ptime(date));
}

inline boost::posix_time::ptime ipintTimestampToPtime(ITV8::timestamp_t timestamp)
{
    using namespace boost::posix_time;
    return ptime(startOfEpoch(), milliseconds(timestamp));
}

inline ::uint64_t ipintTimeToQword(ITV8::timestamp_t timestamp)
{
    return NMMSS::PtimeToQword(ipintTimestampToPtime(timestamp));
}

inline std::string ipintTimestampToIsoString(ITV8::timestamp_t timestamp)
{
    return boost::posix_time::to_iso_string(ipintTimestampToPtime(timestamp));
}

}

#endif

