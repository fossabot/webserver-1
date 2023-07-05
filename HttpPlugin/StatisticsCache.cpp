#include <mutex>
#include <atomic>

#include <boost/asio.hpp>
#include <CorbaHelpers/Refcounted.h>
#include <CorbaHelpers/Container.h>
#include "StatisticsCache.h"

#include <axxonsoft/bl/statistics/Statistics.grpc.pb.h>

namespace bl = axxonsoft::bl;

namespace NHttp
{
    StatisticsData::StatisticsData()
    {}

    StatisticsData::StatisticsData(const bl::statistics::StatPoint& sp)
    {
        SetValue(sp);
    }

    StatisticsData::~StatisticsData()
    {
    }

    void StatisticsData::SetValue(const bl::statistics::StatPoint& sp)
    {
        switch (sp.key().type())
        {
            case bl::statistics::SPT_LiveFPS:
                m_fps = sp.value_double();
                break;
            case bl::statistics::SPT_LiveBitrate:
                m_bitrate = sp.value_uint64();
                break;
            case bl::statistics::SPT_LiveWidth:
                m_width = sp.value_uint32();
                break;
            case bl::statistics::SPT_LiveHeight:
                m_height = sp.value_uint32();
                break;
            case bl::statistics::SPT_LiveMediaType:
                m_mediaType = sp.value_uint32();
                break;
            case bl::statistics::SPT_LiveStreamType:
                m_streamType = sp.value_uint32();
                break;
            default:
                break;
        }
    }


    class CStatisticsCache : public NHttp::IStatisticsCache
    {
        DECLARE_LOGGER_HOLDER;

    public:
        CStatisticsCache(NCorbaHelpers::IContainer* c)
            : m_container(c, NCorbaHelpers::ShareOwnership())
        {
            INIT_LOGGER_HOLDER_FROM_CONTAINER(c);
        }

        void AddOrUpdateStatisticsData(const google::protobuf::RepeatedPtrField<bl::statistics::StatPoint>& stats) override
        {
            AddOrUpdate(stats);
        };

        NHttp::StatisticsData GetData(const std::string& stat_name) override
        {
            return GetCachedData(stat_name);
        }

    private:
        NCorbaHelpers::PContainer m_container;
        std::mutex m_mutex;
        std::unordered_map<std::string, StatisticsData> m_values;

    private:
        void AddOrUpdate(const google::protobuf::RepeatedPtrField<bl::statistics::StatPoint>& stats)
        {
            for (auto const& stat: stats)
            {
                auto const stat_name = stat.key().name();

                std::lock_guard<std::mutex> lock(m_mutex);
                std::unordered_map<std::string, StatisticsData>::iterator it = m_values.find(stat_name);
                if (it == m_values.end())
                {
                    m_values.insert({stat_name, StatisticsData(stat)});
                }
                else
                {
                    it->second.SetValue(stat);
                }
            }
        }

        NHttp::StatisticsData GetCachedData(const std::string& stat_name)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_values.find(stat_name);
            if (it == m_values.end())
                return StatisticsData();
            else
                return it->second;
        }
    };
}

namespace NHttp
{
    PStatisticsCache CreateStatisticsCache(NCorbaHelpers::IContainer* c)
    {
        return std::make_shared<CStatisticsCache>(c);
    }
}  // namespace NHttp