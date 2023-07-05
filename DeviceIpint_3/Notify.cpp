#include "Notify.h"
#include "../Grabber/Grabber.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>

namespace IPINT30
{
    inline std::string EIpDeviceStateToString(NMMSS::EIpDeviceState value)
    {
        switch (value)
        {
        case NMMSS::IPDS_Connected: return std::string("Connected");
        case NMMSS::IPDS_IpintInternalFailure: return std::string("IpintInternalFailure");
        case NMMSS::IPDS_Disconnected: return std::string("Disconnected");
        case NMMSS::IPDS_SignalRestored: return std::string("SignalRestored");
        case NMMSS::IPDS_SignalLost: return std::string("SignalLost");
        case NMMSS::IPDS_AuthorizationFailed: return std::string("AuthorizationFailed");
        case NMMSS::IPDS_AcceptSettingsFailure: return std::string("AcceptSettingsFailure");
        case NMMSS::IPDS_Rebooted: return std::string("Rebooted");
        case NMMSS::IPDS_NetworkFailure: return std::string("NetworkFailure");
        case NMMSS::IPDS_ConnectionError: return std::string("ConnectionError");
        case NMMSS::IPDS_SignalLostOnStop: return std::string("SignalLostOnStop");
        case NMMSS::IPDS_SerialNumberValidationFailed: return std::string("SerialNumberValidationFailed");
        case NMMSS::IPDS_DeviceConfigurationChanged: return std::string("DeviceConfigurationChanged");
        case NMMSS::IPDS_SignalRestoredOnStart: return std::string("SignalRestoredOnStart");
        case NMMSS::IPDS_AllConsumersDisconnected: return std::string("AllConsumersDisconnected");
        case NMMSS::IPDS_NotEnoughBandwidth: return std::string("NotEnoughBandwidth");
        default:
            return boost::lexical_cast<std::string>(value);
        }
    }

    std::vector<PStatPointSafe> makeStatPoint(const std::vector<std::string>& ids,
            NStatisticsAggregator::PStatisticsAggregator aggregator)
    {
        std::vector<PStatPointSafe> result;
        auto timeout = aggregator->ScrapeTimeout() * 2;
        std::transform(ids.begin(), ids.end(), std::back_inserter(result), [&](const std::string& id)
        {
            auto point = std::make_shared<NStatisticsAggregator::StatPointSafe>(NStatisticsAggregator::IsActivated, id, timeout);
            point->AddValue(0);
            return point;
        });
        for (auto r : result)
        {
            aggregator->Push(r);
        }
        return result;
    }

    NotifyStateImpl::NotifyStateImpl(DECLARE_LOGGER_ARG, 
            boost::shared_ptr<NMMSS::IGrabberCallback> callback, 
            NStatisticsAggregator::PStatisticsAggregator aggregator,
            const std::string& deviceNodeId, bool breakUnusedConnection):
        m_callback(callback),
        m_deviceNodeIds{ deviceNodeId },
        m_statPoints(makeStatPoint(m_deviceNodeIds, aggregator)),
        m_breakUnusedConncetionEnabled(breakUnusedConnection),
        m_statAggregator(aggregator)
    {
        INIT_LOGGER_HOLDER;
    }

    NotifyStateImpl::NotifyStateImpl(DECLARE_LOGGER_ARG, 
            boost::shared_ptr<NMMSS::IGrabberCallback> callback, 
            NStatisticsAggregator::PStatisticsAggregator aggregator,
            const std::vector<std::string>& deviceNodeIds):
        m_callback(callback),
        m_deviceNodeIds{ deviceNodeIds },
        m_statPoints(makeStatPoint(m_deviceNodeIds, aggregator)),
        m_breakUnusedConncetionEnabled(false),
        m_statAggregator(aggregator)
    {
        INIT_LOGGER_HOLDER;
    }

    NotifyStateImpl::~NotifyStateImpl()
    {
        if (m_breakUnusedConncetionEnabled)
        {
            std::string now = boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time());
            for (const auto& id : m_deviceNodeIds)
            {
                try
                {
                    m_callback->OnNotify(id.c_str(), NMMSS::IPDS_SignalLostOnStop, now.c_str(), m_lastReportedTimestamp.c_str());
                    _log_ << "~NotifyStateImpl: OnNotify(" << id << ", "
                          << EIpDeviceStateToString(NMMSS::IPDS_SignalLostOnStop) << ", " << now << ")";
                }
                catch (const std::logic_error& e)
                {
                    _err_ << "~NotifyStateImpl: OnNotify(" << id << ", "
                          << EIpDeviceStateToString(NMMSS::IPDS_SignalLostOnStop) << ", " << now << ") - failed. Error=" << e.what();
                }
            }
        }
    }

    void NotifyStateImpl::Notify(NMMSS::EIpDeviceState inState, Json::Value&& data)
    {
        if (!m_callback.get())
            return;

        NMMSS::EIpDeviceState state = inState;
        if (state == NMMSS::IPDS_SignalLostOnStop)
            state = NMMSS::IPDS_SignalLost;

        // If state was already reported, just ignore it to prevent identical notifications.
        if (m_lastReported == state && state != NMMSS::IPDS_DeviceConfigurationChanged)
            return;
        
        if (NMMSS::IsPersistent(state))
        {
            bool activated = (state == NMMSS::EIpDeviceState::IPDS_Connected || state == NMMSS::EIpDeviceState::IPDS_SignalRestored);
            for (auto sp : m_statPoints)
            {
                sp->AddValue(int(activated));
            }
        }

        m_lastReported = state;
        std::string now = boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time());
        bool skipNotify = m_breakUnusedConncetionEnabled && (inState == NMMSS::IPDS_SignalRestoredOnStart || inState == NMMSS::IPDS_SignalLostOnStop);
        for (const auto& id: m_deviceNodeIds)
        {
            try
            {
                if (skipNotify)
                {
                    _wrn_ << "OnNotify skipped event cause BUC enabled(" << id << ", " << EIpDeviceStateToString(inState) << ", " << now << ")";
                    continue;
                }

                m_callback->OnNotify(id.c_str(), inState, now.c_str(), m_lastReportedTimestamp.c_str(), std::move(data));
                _log_ << "OnNotify(" << id << ", " << EIpDeviceStateToString(inState) << ", " << now << ")";
            }
            catch (const std::logic_error& e)
            {
                _err_ << "OnNotify(" << id << ", " << EIpDeviceStateToString(inState) << ", " << now << ") - failed. Error=" << e.what();
            }
        }

        if (!skipNotify)
            m_lastReportedTimestamp = now;
    }
}
