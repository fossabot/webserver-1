#include <MMIDL/TelemetryS.h>
#include "Observer.h"

#include <CorbaHelpers/CorbaStl.h>

namespace
{
    using namespace IPINT30;

    class CObserverServant : public POA_Equipment::Observer
                           , public PortableServer::RefCountServantBase
    {
        DECLARE_LOGGER_HOLDER;
        IPINT30::SObserver m_cfg;

    public:
        CObserverServant(DECLARE_LOGGER_ARG
        , NCorbaHelpers::WPContainerTrans c
        , const IPINT30::SObserver& cfg
        , const char* telemetryName
        , NMMSS::ITelemetry* telemetry
        , NCommonNotification::PEventSupplierSink eventSink)
            : m_cfg(cfg)
            , m_observerImpl(CreateObserverImplementation(GET_LOGGER_PTR, c, cfg, telemetryName, telemetry, eventSink))
        {
            INIT_LOGGER_HOLDER;
        }

        ~CObserverServant()
        {
        }

        void SetMode(::Equipment::Observer::EMode mode)
            /*throw (CORBA::SystemException, Equipment::Observer::XUnsupportedMode)*/
        {
            try
            {
                m_observerImpl->SetMode(ConvertMode(mode));
            }
            catch (const std::logic_error&)
            {
                throw Equipment::Observer::XUnsupportedMode();
            }
        }
        ::Equipment::Observer::EMode GetMode()
            /*throw (CORBA::SystemException, Equipment::Observer::XUnsupportedMode)*/
        {
            try
            {
                return ConvertMode(m_observerImpl->GetMode());
            }
            catch (const std::logic_error&)
            {
                throw Equipment::Observer::XUnsupportedMode();
            }
        }

        void SetSwitchingFrequency(CORBA::Long sec)
            /*throw (CORBA::SystemException)*/
        {
            m_observerImpl->SetTimerFrequency(boost::posix_time::seconds(sec));
        }

        void FollowTrack(const char* trackerName, CORBA::Long trackId)
            /*throw (CORBA::SystemException)*/
        {
            if (-1 == trackId)
                m_observerImpl->UnfollowTrack();
            else
                m_observerImpl->FollowTrack(trackerName, trackId);
        }

        void MoveToCoords(const char* trackerName, CORBA::Float x, CORBA::Float y)
        {
            m_observerImpl->MoveToCoords(trackerName, x, y);
        }

        void TakeControl()
        {
            m_observerImpl->TakeControl();
        }

        void ReleaseControl()
        {
            m_observerImpl->ReleaseControl();
        }

        Equipment::Observer::TrackersInfo* GetTrackersInfo() override
        {
            Equipment::Observer::TrackersInfo_var result = new Equipment::Observer::TrackersInfo();
            result->mode = GetMode();
            for (const Equipment::Observer::Tracker& p : NCorbaHelpers::make_range( result->trackers))
            {
                NCorbaHelpers::PushBack(result->trackers, p);
            }
            return result._retn();
        }

    private:
        EObserverMode ConvertMode(::Equipment::Observer::EMode mode)
        {
            switch (mode)
            {
            case ::Equipment::Observer::EOFF:
                return EOFF;
            case ::Equipment::Observer::EMANUAL:
                return EMANUAL;
            case ::Equipment::Observer::EAUTOMATIC:
                return EAUTOMATIC;
            case ::Equipment::Observer::EUSER_PRIORITY:
                return EUSER_PRIORITY;
            case ::Equipment::Observer::EUSER_PRIORITY_MANUAL:
                return EUSER_PRIORITY_MANUAL;
            }
            throw std::logic_error("Unsupported mode");
        }
        ::Equipment::Observer::EMode ConvertMode(EObserverMode mode)
        {
            switch (mode)
            {
            case EOFF:
                return ::Equipment::Observer::EOFF;
            case EMANUAL:
                return ::Equipment::Observer::EMANUAL;
            case EAUTOMATIC:
                return ::Equipment::Observer::EAUTOMATIC;
            case EUSER_PRIORITY:
                return ::Equipment::Observer::EUSER_PRIORITY;
            case EUSER_PRIORITY_MANUAL:
                return ::Equipment::Observer::EUSER_PRIORITY_MANUAL;
            }
            throw std::logic_error("Unsupported mode");
        }

        IPINT30::PObserverImpl m_observerImpl;
    };
}

namespace IPINT30
{
    PortableServer::Servant CreateObserverServant(DECLARE_LOGGER_ARG, NCorbaHelpers::WPContainerTrans c,
        const IPINT30::SObserver& cfg, const char* telemetryName, NMMSS::ITelemetry* telemetry,
        NCommonNotification::PEventSupplierSink eventSink)
    {
        return new CObserverServant(GET_LOGGER_PTR, c, cfg, telemetryName, telemetry, eventSink);
    }
}
