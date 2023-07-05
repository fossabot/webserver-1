#pragma once

#include <ItvSdk/include/Statistics.h>
#include <CommonNotificationCpp/StatisticsAggregator.h>
#include <string>

#include <boost/scope_exit.hpp>

class StatisticsMetricImpl : public ITV8::Statistics::IMetric
{
    NStatisticsAggregator::StatPoint m_stat;

public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY(ITV8::Statistics::IMetric)
    ITV8_END_CONTRACT_MAP()

public:
    template <typename ...Args>
    StatisticsMetricImpl(Args&&... args)
        : m_stat(std::forward<Args>(args)...)
    {}

    void SetValue(double_t value) final override
    {
        m_stat.AddValue(value);
    }

    void SetValue(int32_t value) final override
    {
        m_stat.AddValue(value);
    }

    void SetValue(uint32_t value) final override
    {
        m_stat.AddValue(value);
    }

    void SetValue(int64_t value) final override
    {
        m_stat.AddValue(value);
    }

    void SetValue(uint64_t value) final override
    {
        m_stat.AddValue(value);
    }

    void SetHint(const char* hint) final override
    {
        m_stat.AddHint(hint);
    }

    void SetLabel(const char* name, const char* value) final override
    {
        m_stat.AddLabel(name, value);
    }

    NStatisticsAggregator::StatPoint&& TakeData() &&
    {
        return std::move(m_stat);
    }

    NStatisticsAggregator::StatPoint CopyData() const
    {
        return m_stat.Clone();
    }

    void Destroy() final override
    {
        delete this;
    }
};

template <typename CoInterfaceImpl>
class StatisticsSinkImpl
    : public CoInterfaceImpl
    , public ITV8::Statistics::ISink
{
public:
    ITV8_BEGIN_CONTRACT_MAP_BASE(CoInterfaceImpl)
        ITV8_CONTRACT_ENTRY(ITV8::Statistics::ISink)
    ITV8_END_CONTRACT_MAP()

public:
    template <typename ...Args>
    StatisticsSinkImpl(DECLARE_LOGGER_ARG, const char* endpointName, NStatisticsAggregator::IStatisticsAggregatorImpl* impl, Args&&... args)
        : CoInterfaceImpl(GET_LOGGER_PTR, std::forward<Args>(args)..., endpointName)
        , m_aggregator(impl, NCorbaHelpers::ShareOwnership())
        , m_epName(endpointName)
    {}

    StatisticsMetricImpl* CreateGaugeMetric(const char* key, ITV8::uint32_t ttl_ms)
    {
        return new StatisticsMetricImpl(key, m_epName, std::chrono::milliseconds(ttl_ms));
    }

    void Push(ITV8::Statistics::IMetric* metric, MetricLifecyclePolicy policy)
    {
        if (SingleShot_DestroyedByPush == policy)
        {
            BOOST_SCOPE_EXIT_TPL(metric)
            {
                metric->Destroy();
            }
            BOOST_SCOPE_EXIT_END
            m_aggregator->Push(std::move(static_cast<StatisticsMetricImpl&>(*metric)).TakeData());
        }
        else
        {
            m_aggregator->Push(static_cast<const StatisticsMetricImpl&>(*metric).CopyData());
        }
    }

private:
    NStatisticsAggregator::PStatisticsAggregator m_aggregator;
    std::string m_epName;
};
