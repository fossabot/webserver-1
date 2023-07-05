#ifndef DEVICEIPINT3_NOTIFY_H
#define DEVICEIPINT3_NOTIFY_H

#include "IIPManager3.h"
#include <boost/optional.hpp>
#include <vector>
#include <memory>

#include <CommonNotificationCpp/StatisticsAggregator.h>

namespace NMMSS
{
    class IGrabberCallback;
}

namespace IPINT30
{
    std::string EIpDeviceStateToString(NMMSS::EIpDeviceState value);

    using PStatPointSafe = std::shared_ptr<NStatisticsAggregator::StatPointSafe>;

    class NotifyStateImpl : public INotifyState
    {
        DECLARE_LOGGER_HOLDER;
    public:
        NotifyStateImpl(DECLARE_LOGGER_ARG, boost::shared_ptr<NMMSS::IGrabberCallback> callback, 
            NStatisticsAggregator::PStatisticsAggregator aggregator,
            const std::string& deviceNodeId, bool skipSignalLost = false);
        NotifyStateImpl(DECLARE_LOGGER_ARG, boost::shared_ptr<NMMSS::IGrabberCallback> callback, 
            NStatisticsAggregator::PStatisticsAggregator aggregator,
            const std::vector<std::string>& deviceNodeIds);
        ~NotifyStateImpl();
        void Notify(NMMSS::EIpDeviceState state, Json::Value&& data = Json::Value()) override;
    private:
        boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;
        const std::vector<std::string> m_deviceNodeIds;
        const std::vector<PStatPointSafe> m_statPoints;
        boost::optional<NMMSS::EIpDeviceState> m_lastReported;
        std::string m_lastReportedTimestamp;
        const bool m_breakUnusedConncetionEnabled;
        NStatisticsAggregator::PStatisticsAggregator m_statAggregator;
    };
}

#endif // DEVICEIPINT3_NOTIFY_H
