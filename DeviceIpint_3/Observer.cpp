#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <ItvDeviceSdk/include/ItvDeviceSdk.h>

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/RefcountedServant.h>
#include "Observer.h"
#include "../Telemetry.h"
#include <MMIDL/TelemetryS.h>
#include "../MMIDL/EventTypeTraits.h"

namespace
{
    using namespace IPINT30;

    class CObserver : public virtual IObserverImpl
                    , public NCorbaHelpers::CWeakReferableImpl
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CObserver(DECLARE_LOGGER_ARG, NCorbaHelpers::WPContainerTrans cont
            , const IPINT30::SObserver& cfg
            , const char* telemetryName
            , NMMSS::ITelemetry* telemetry
            , NCommonNotification::PEventSupplierSink eventSink)
            : m_container(cont)
            , m_telemetryName(telemetryName)
            , m_telemetry(telemetry)
            , m_eventSink(eventSink)
            , m_configState((EObserverMode)(cfg.mode))
            , m_frequency(boost::posix_time::seconds(cfg.frequency))
            , m_timer(NCorbaHelpers::GetReactorInstanceShared()->GetIO())
            , m_currentTrack(invalidTrack)
            , m_followTrack(false)
            , m_isControlTaken(false)
        {
            INIT_LOGGER_HOLDER;

            m_panMin = m_panMax = m_tiltMin = m_tiltMax = m_zoomMin = m_zoomMax = 0;

            NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRangeIterator> panIt(m_telemetry->GetPanInfo());
            getRanges(panIt.Get(), m_panMin, m_panMax);

            NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRangeIterator> tiltIt(m_telemetry->GetTiltInfo());
            getRanges(tiltIt.Get(), m_tiltMin, m_tiltMax);

            NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRangeIterator> zoomIt(m_telemetry->GetZoomInfo());
            getRanges(zoomIt.Get(), m_zoomMin, m_zoomMax);

            IPINT30::TTrackers::const_iterator it1 = cfg.trackers.begin(),
                it2 = cfg.trackers.end();
            for (; it1 != it2; ++it1)
            {
                CreateNewTracker(*it1);
            }

            if ((EAUTOMATIC == m_configState) || (EUSER_PRIORITY == m_configState))
                Schedule();
        }

        ~CObserver()
        {
            m_timer.cancel();

            boost::mutex::scoped_lock lock(m_trackMutex);
            m_telemetry = 0;
            StopAllTracking(true);
            m_trackers.clear();
        }

        void SetMode(EObserverMode mode)
        {
            SwitchMode(mode);
        }
        EObserverMode GetMode()
        {
            boost::mutex::scoped_lock lock(m_modeMutex);
            return m_configState;
        }

        void SetTimerFrequency(const boost::posix_time::time_duration& duration)
        {
            m_frequency = duration;
        }

        void AddTracker(const STracker& tracker)
        {
            CreateNewTracker(tracker);
        }

        void RemoveTracker(const char* const trackerName)
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            bool stopTracking = false;
            if (m_currentTrack.m_name == trackerName)
                stopTracking = true;

            TTrackerMap::iterator it = m_trackers.find(trackerName);
            if (m_trackers.end() != it)
            {
                if (stopTracking)
                {
                    it->second->StopTracking();
                    m_currentTrack = invalidTrack;
                }
                it->second->Stop();
                m_trackers.erase(it);
                
            }

            if (stopTracking)
                Reschedule(lock);
        }

        void FollowTrack(const char* const trackerName, uint32_t trackId)
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            TTrackerMap::iterator it = m_trackers.find(trackerName);
            if (m_trackers.end() != it)
            {
                CancelTrack(lock);

                _log_ << "Follow track " << trackId;
                m_currentTrack.m_name = trackerName;
                m_currentTrack.m_id = trackId;
                m_followTrack = true;

                it->second->StartTracking(trackId);

                SendTrackChangedEvent();
            }
        }

        void MoveToCoords(const char* const trackerName, float x, float y)
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            TTrackerMap::iterator it = m_trackers.find(trackerName);
            if (m_trackers.end() != it)
            {
                PObjectTracker ot = it->second;
                lock.unlock();

                ot->MoveToCoords(x, y);
            }
            else
                _wrn_ << "MoveToCoords: tracker " << trackerName << " not found";
        }

        void UnfollowTrack()
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            m_followTrack = false;
            CancelTrack(lock);

            Reschedule(lock);
        }

        void OnPtzMove(double pan, double tilt, double zoom)
        {
            pan = std::min<double>(m_panMax, std::max<double>(m_panMin, pan));
            tilt = std::min<double>(m_tiltMax, std::max<double>(m_tiltMin, tilt));
            zoom = std::min<double>(m_zoomMax, std::max<double>(m_zoomMin, zoom));

            NMMSS::AbsolutePositionInformation pos;
            pos.pan = static_cast<long>(pan);
            pos.tilt = static_cast<long>(tilt);
            pos.zoom = static_cast<long>(zoom);
            pos.mask = ITV8::GDRV::TPan | ITV8::GDRV::TTilt | ITV8::GDRV::TZoom;

            boost::mutex::scoped_lock lock(m_trackMutex);
            m_telemetry->AbsoluteMove(Equipment::Telemetry::SERVER_SESSION_ID, pos);
        }

        void OnTrackFinished(STrackInfo track)
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            _log_ << "Track " << track.m_id << " for " << track.m_name << " finished";
            if (m_currentTrack.m_id == track.m_id)
            {
                _log_ << "Leave track " << m_currentTrack.m_id;
                m_currentTrack = invalidTrack;
                m_followTrack = false;

                Reschedule(lock);
            }
        }

        void OnNewSequence()
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            if (INVALID_TRACK == m_currentTrack.m_id)
            {
                Reschedule(lock);
            }
        }

        void TakeControl()
        {
            {
                boost::mutex::scoped_lock lock(m_modeMutex);
                if (m_configState != EUSER_PRIORITY_MANUAL)
                    return;
                m_isControlTaken = true;
            }

            m_timer.cancel();
            boost::mutex::scoped_lock lock(m_trackMutex);
            m_followTrack = false;
            StopAllTracking(false);
        }

        void ReleaseControl()
        {
            {
                boost::mutex::scoped_lock lock(m_modeMutex);
                if (!m_isControlTaken)
                    return;
                m_isControlTaken = false;
            }

            Reschedule();
        }

        long GetMaxPan() const override
        {
            return m_panMax;
        }

    private:
        void SwitchMode(EObserverMode mode)
        {
            boost::mutex::scoped_lock lock(m_modeMutex);
            if (mode == m_configState)
                return;

            EObserverMode oldConfigState = m_configState;
            m_configState = mode;
            lock.unlock();

            bool oldManualMode = (EOFF == oldConfigState) || (EMANUAL == oldConfigState);
            bool newManualMode = (EOFF == mode) || (EMANUAL == mode);

            if (oldManualMode && newManualMode)
                return;

            if (oldManualMode && !newManualMode)
                Reschedule();
            else if (!oldManualMode && newManualMode)
            {
                m_timer.cancel();

                boost::mutex::scoped_lock lock(m_trackMutex);
                StopAllTracking(false);
                m_isControlTaken = false;
            }
            else if (!newManualMode && !m_followTrack && !m_isControlTaken)
                Reschedule();
        }

        void Reschedule()
        {
            {
                boost::mutex::scoped_lock lock(m_trackMutex);
                SelectTrack(lock);
            }
            Schedule();
        }

        void Reschedule(boost::mutex::scoped_lock& l)
        {
            if (!IsManualMode())
            {
                m_timer.cancel();
                SelectTrack(l);
                l.unlock();

                Schedule();
            }
        }

        void Schedule()
        {
            try
            {
                m_timer.expires_from_now(m_frequency);
                m_timer.async_wait(boost::bind(&CObserver::SwitchToNewTrack,
                    WPObserverImpl(this), _1));
            }
            catch(const boost::system::system_error &) {}
        }

        void CancelTrack(boost::mutex::scoped_lock& lock)
        {
            if (INVALID_TRACK != m_currentTrack.m_id)
            {
                TTrackerMap::iterator it = m_trackers.find(m_currentTrack.m_name);
                if (m_trackers.end() != it)
                {
                    it->second->StopTracking();
                    _log_ << "Cancel track with id=" << m_currentTrack.m_id << " for " << m_currentTrack.m_name;
                    m_currentTrack = invalidTrack;
                }
            }
        }

        void SelectTrack(boost::mutex::scoped_lock& lock)
        {
            typedef std::map<STrackInfo, PObjectTracker> TTimeOrderedTracks;
            TTimeOrderedTracks samples;

            TTrackerMap::iterator it1 = m_trackers.begin(),
                it2 = m_trackers.end();
            for (; it1 != it2; ++it1)
            {
                STrackInfo ti = it1->second->GetSuitableTrack();
                if (INVALID_TRACK != ti.m_id)
                {
                    samples.insert(std::make_pair(ti, it1->second));
                }
            }

            if (!samples.empty())
            {
                TTimeOrderedTracks::iterator it = samples.begin();
                m_currentTrack = it->first;
                _log_ << "Select track with id=" << m_currentTrack.m_id << " for " << m_currentTrack.m_name;
                SendTrackChangedEvent();
                it->second->StartTracking(m_currentTrack.m_id);
            }
        }

        void ChangeTrack()
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            if (!m_followTrack)
            {
                CancelTrack(lock);
                SelectTrack(lock);
                lock.unlock();

                Schedule();
            }
        }

        static void SwitchToNewTrack(WPObserverImpl weak, const boost::system::error_code& error)
        {
            if (!error)
            {
                PObserverImpl observer(weak);
                if (observer)
                    observer->ChangeTrack();
            }
        }

        void CreateNewTracker(const IPINT30::STracker& params)
        {
            IPINT30::PObjectTracker pt(CreateObjectTracker(GET_LOGGER_PTR, m_container, params, WPObserverImpl(this)));
            {
                boost::mutex::scoped_lock lock(m_trackMutex);
                m_trackers.insert(std::make_pair(params.corbaName, pt));
            }
            pt->Start();
        }

        void SendTrackChangedEvent()
        {
            if (m_eventSink)
            {
                Equipment::TrackChangedEvent tcEvent;
                tcEvent.telemetryName = CORBA::string_dup(m_telemetryName.c_str());
                tcEvent.trackerName = CORBA::string_dup(m_currentTrack.m_name.c_str());
                tcEvent.trackId = m_currentTrack.m_id;
                _log_ << "Send event for track " << m_currentTrack.m_id;
                m_eventSink->SendEvents(NCommonNotification::WrapSingleEvent(tcEvent));
            }
        }

        void StopAllTracking(bool finish)
        {
            TTrackerMap::iterator it1 = m_trackers.begin(),
                it2 = m_trackers.end();
            for (; it1 != it2; ++it1)
            {
                it1->second->StopTracking();
                if (finish)
                    it1->second->Stop();
            }
        }

        bool IsManualMode()
        {
            boost::mutex::scoped_lock lock(m_modeMutex);
            return (EOFF == m_configState) ||
                (EMANUAL == m_configState) || 
                (EUSER_PRIORITY == m_configState && m_followTrack) ||
                (EUSER_PRIORITY_MANUAL == m_configState && m_isControlTaken);
        }

        bool requiredRangeInitialization() const
        {
            return (0 == m_panMin && 0 == m_panMax &&
                0 == m_tiltMin && 0 == m_tiltMax &&
                0 == m_zoomMin && 0 == m_zoomMax);
        }

        void getRanges(NMMSS::IFlaggedRangeIterator* it, long& minv, long& maxv)
        {
            if (0 != it)
            {
                size_t count = it->Count();
                for (size_t i = 0; i < count; ++i)
                {
                    NCorbaHelpers::CAutoPtr<NMMSS::IFlaggedRange> range(it->Next());
                    if (NMMSS::EABSOLUTE == range->GetRangeFlag())
                    {
                        minv = range->GetMinValue();
                        maxv = range->GetMaxValue();
                        break;
                    }
                }
            }
        }

        NCorbaHelpers::WPContainerTrans m_container;
        std::string m_telemetryName;
        NMMSS::ITelemetry* m_telemetry;
        NCommonNotification::PEventSupplierSink m_eventSink;

        boost::mutex m_modeMutex;
        EObserverMode m_configState;
        boost::posix_time::time_duration m_frequency;

        typedef std::map<std::string, PObjectTracker> TTrackerMap;
        TTrackerMap m_trackers;

        boost::asio::deadline_timer m_timer;

        boost::mutex m_trackMutex;
        IPINT30::STrackInfo m_currentTrack;
        bool m_followTrack;
        bool m_isControlTaken;

        boost::mutex m_rangeLock;
        long m_panMin, m_panMax;
        long m_tiltMin, m_tiltMax;
        long m_zoomMin, m_zoomMax;
    };
}

namespace IPINT30
{
    IObserverImpl* CreateObserverImplementation(DECLARE_LOGGER_ARG, NCorbaHelpers::WPContainerTrans c,
        const IPINT30::SObserver& cfg, const char* telemetryName, NMMSS::ITelemetry* telemetry, 
        NCommonNotification::PEventSupplierSink eventSink)
    {
        return new CObserver(GET_LOGGER_PTR, c, cfg, telemetryName, telemetry, eventSink);
    }
}
