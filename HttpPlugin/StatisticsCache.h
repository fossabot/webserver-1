#ifndef STATISTICS_CACHE_H__
#define STATISTICS_CACHE_H__

#include <memory>
#include <Logging/log2.h>
#include <google/protobuf/repeated_field.h>

namespace NCorbaHelpers
{
class IContainer;
}
namespace axxonsoft
{
namespace bl
{
namespace statistics
{
class StatsResponse;
class StatPoint;
}  // namespace statistics
}  // namespace bl
}  // namespace axxonsoft

namespace NHttp
{
    struct StatisticsData
    {
        double m_fps = 0.0;
        uint64_t m_bitrate = 0;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_mediaType = 0;
        uint32_t m_streamType = 0;

        StatisticsData();
        StatisticsData(const axxonsoft::bl::statistics::StatPoint& sp);
        virtual ~StatisticsData();

        void SetValue(const axxonsoft::bl::statistics::StatPoint& sp);
    };

    struct IStatisticsCache
    {
        virtual ~IStatisticsCache() {}

        virtual void AddOrUpdateStatisticsData(const google::protobuf::RepeatedPtrField<axxonsoft::bl::statistics::StatPoint>& stats) = 0;
        virtual StatisticsData GetData(const std::string& stat_name) = 0;
    };
    using PStatisticsCache = std::shared_ptr<IStatisticsCache>;

    PStatisticsCache CreateStatisticsCache(NCorbaHelpers::IContainer* c);
}

#endif // STATISTICS_CACHE_H__
