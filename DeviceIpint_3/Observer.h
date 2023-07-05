#ifndef __OBSERVER__H__
#define __OBSERVER__H__

#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <Logging/log2.h>
#include <CorbaHelpers/Container.h>
#include <CommonNotificationCpp/Connector.h>
#include "../AppData.h"
#include "DeviceSettings.h"

namespace NMMSS
{
    class ITelemetry;
}

namespace IPINT30
{
    const uint32_t INVALID_TRACK = 0xFFFFFFFF;

    struct STrackInfo
    {
        STrackInfo(const std::string& name, uint32_t id, const boost::posix_time::ptime& t)
            : m_name(name)
            , m_id(id)
            , m_time(t)
        {
        }

        std::string m_name;
        uint32_t m_id;
        boost::posix_time::ptime m_time;
    };

    inline bool operator <(const STrackInfo& ti1, const STrackInfo& ti2)
    {
        return ti1.m_time < ti2.m_time;
    }

    static const STrackInfo invalidTrack("", INVALID_TRACK, boost::posix_time::not_a_date_time);

    struct ITrackingNotifier
    {
        ~ITrackingNotifier() {}

        virtual void OnPtzMove(double pan, double tilt, double zoom) = 0;
        virtual void OnTrackFinished(STrackInfo) = 0;
        virtual void OnNewSequence() = 0;
    };

    struct IObjectTracker : public virtual NCorbaHelpers::IWeakReferable
    {
        virtual void Start() = 0;
        virtual void Stop() = 0;

        virtual STrackInfo GetSuitableTrack() = 0;
        virtual void StartTracking(uint32_t trackId) = 0;
        virtual void StopTracking() = 0;

        virtual void RequestPtzPoint(uint32_t) = 0;

        virtual void MoveToCoords(float, float) = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IObjectTracker> PObjectTracker;

    enum EObserverMode
    {
        EOFF,
        EMANUAL,
        EAUTOMATIC,
        EUSER_PRIORITY,
        EUSER_PRIORITY_MANUAL
    };

    struct IObserverImpl : public virtual NCorbaHelpers::IWeakReferable
                         , public ITrackingNotifier
    {
        virtual void SetMode(EObserverMode) = 0;
        virtual void SetTimerFrequency(const boost::posix_time::time_duration&) = 0;

        virtual void AddTracker(const STracker&) = 0;
        virtual void RemoveTracker(const char* const) = 0;

        virtual void FollowTrack(const char* const, uint32_t) = 0;
        virtual void UnfollowTrack() = 0;

        virtual void ChangeTrack() = 0;

        virtual void MoveToCoords(const char* const, float, float) = 0;
        virtual EObserverMode GetMode() = 0;

        virtual void TakeControl() = 0;
        virtual void ReleaseControl() = 0;

        virtual long GetMaxPan() const = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IObserverImpl> PObserverImpl;
    typedef NCorbaHelpers::CWeakPtr<IObserverImpl> WPObserverImpl;

    IObjectTracker* CreateObjectTracker(DECLARE_LOGGER_ARG
        , NCorbaHelpers::WPContainerTrans
        , const STracker& params
        , WPObserverImpl notifier);

    PortableServer::Servant CreateObserverServant(DECLARE_LOGGER_ARG
        , NCorbaHelpers::WPContainerTrans
        , const IPINT30::SObserver&
        , const char* telemetryName
        , NMMSS::ITelemetry*
        , NCommonNotification::PEventSupplierSink);

    IObserverImpl* CreateObserverImplementation(DECLARE_LOGGER_ARG
        , NCorbaHelpers::WPContainerTrans
        , const IPINT30::SObserver&
        , const char* telemetryName
        , NMMSS::ITelemetry*
        , NCommonNotification::PEventSupplierSink);
}

#endif // __OBSERVER__H__
