#pragma once

#include "../MMSS.h"
#include "Exports.h"
#include <Logging/log2.h>
#include <CommonNotificationCpp/StatisticsAggregator.h>

namespace NMMSS
{
    struct IStatisticsCollectorImpl : public IStatisticsCollector
    {
        virtual void Update(NMMSS::ISample* sample) = 0;
    };

    MMTRANSPORT_EXPORT_DECLSPEC
    IStatisticsCollectorImpl* CreateStreamQualityMeasurer();

    MMTRANSPORT_EXPORT_DECLSPEC
    IStatisticsCollectorImpl* CreateStatisticsCollector(
                                    const std::string& federalName,
                                    NStatisticsAggregator::PStatisticsAggregator aggregator,
                                    bool calcKeyFps = false);

    MMTRANSPORT_EXPORT_DECLSPEC
    NMMSS::IFilter* CreateStatisticsCollectorFilter(DECLARE_LOGGER_ARG,
            const std::string& federalName, NStatisticsAggregator::PStatisticsAggregator aggregator);
}

