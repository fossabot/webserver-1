#include <boost/asio.hpp>

#include <boost/variant/get.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

using namespace boost::multi_index;

#include "../PTZCalibration/PTZCalibrationLib.h"

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/RefcountedImpl.h>

#include "Observer.h"
#include "PositionPredictor.h"
#include "../AppDataMMSource.h"
#include "../MMClient/MMClient.h"
#include "../MMTransport/MMTransport.h"

namespace
{
    const int MAXPANVALUE = 40000;
    using namespace IPINT30;

    double GetDoubleFromAny(const NMMSS::TAppDataValue& value)
    {
        double val = 0.0;
        try
        {
            val = boost::get<double>(value);
        }
        catch (const boost::bad_get&)
        {
        }
        return val;
    }

    struct SRectangle
    {
        double left;
        double top;
        double right;
        double bottom;
    };

    struct STrack
    {
        STrack()
            : m_id(INVALID_TRACK)
            , m_trackTime(boost::posix_time::not_a_date_time)
        {
        }

        STrack(uint32_t id, const boost::posix_time::ptime& t)
            : m_id(id)
            , m_trackTime(t)
        {
        }

        bool IsValid()
        {
            return INVALID_TRACK != m_id;
        }

        void Reset()
        {
            m_id = INVALID_TRACK;
            m_trackTime = boost::posix_time::not_a_date_time;
        }

        uint32_t m_id;
        boost::posix_time::ptime m_trackTime;
        SRectangle m_rect;
    };

    struct set_rect_value
    {
        set_rect_value(const std::string& propName, const NMMSS::TAppDataValue& value)
            : m_propName(propName)
            , m_value(value)
        {
        }

        void operator()(STrack& t)
        {
            static const char* const LEFT = "left";
            static const char* const TOP = "top";
            static const char* const RIGHT = "right";
            static const char* const BOTTOM = "bottom";

            if (m_propName == LEFT)
                t.m_rect.left = GetDoubleFromAny(m_value);
            else if (m_propName == TOP)
                t.m_rect.top = GetDoubleFromAny(m_value);
            else if (m_propName == RIGHT)
                t.m_rect.right = GetDoubleFromAny(m_value);
            else if (m_propName == BOTTOM)
                t.m_rect.bottom = GetDoubleFromAny(m_value);
        }

        std::string m_propName;
        NMMSS::TAppDataValue m_value;
    };

    struct set_time
    {
        set_time(const boost::posix_time::ptime& t)
            : m_time(t)
        {
        }

        void operator()(STrack& t)
        {
            t.m_trackTime = m_time;
        }

        boost::posix_time::ptime m_time;
    };

    struct SObjectMember
    {
        SObjectMember(const char* name, NMMSS::EValueType type)
            : m_name(name)
            , m_type(type)
        {
        }

        const std::string m_name;
        NMMSS::EValueType m_type;
    };

    class CObjectTracker : public IPINT30::IObjectTracker
                         , public NCorbaHelpers::CWeakReferableImpl
                         , public virtual NMMSS::IAppDataSchema
                         , public virtual NMMSS::IAppDataSink
                         , public NMMSS::IAppDataNotifier
    {
        DECLARE_LOGGER_HOLDER;
    public:
        CObjectTracker(DECLARE_LOGGER_ARG
            , NCorbaHelpers::WPContainerTrans cont
            , const IPINT30::STracker& params
            , WPObserverImpl notifier)
                : m_container(cont)
                , m_trackerName(params.corbaName)
                , m_notifier(notifier)
                , m_predictor(new CPositionPredictor(GET_LOGGER_PTR, boost::posix_time::millisec(params.predictionTime)))
                , m_objectId(0)
                , m_currentFrameTime(boost::posix_time::not_a_date_time)
                , m_currentTrackId(INVALID_TRACK)
                , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
                , m_timer(m_reactor->GetIO())
                , m_signalNewSequence(false)
                , m_PTZCommandSentingTimeout(params.timeBetweenPTZCommands)
        {
            INIT_LOGGER_HOLDER;

            m_calibrator = CreateCalibrator(params.calibrationInfo);
        }

        void Start()
        {
            NCorbaHelpers::PContainerTrans c(m_container);
            if (c)
            {
                NMMSS::PPullStyleSink sink(NMMSS::CreateAppDataMMPullSink(GET_LOGGER_PTR, this, this, this, boost::posix_time::ptime()));

                m_sinkEndpoint = NMMSS::CreatePullConnectionByNsref(GET_LOGGER_PTR
                    , m_trackerName.c_str()
                    , c->GetRootNC()
                    , sink.Get()
                );
            }
        }

        void Stop()
        {
            if( m_sinkEndpoint )
            {
                m_sinkEndpoint->Destroy();
                m_sinkEndpoint.Reset();
            }

            boost::mutex::scoped_lock lock(m_calibratorMutex);
            if (0 != m_calibrator)
            {
                ptzDestroy(m_calibrator);
                m_calibrator = 0;
            }

            if (0 != m_predictor.get())
                m_predictor.reset();
        }

        virtual STrackInfo GetSuitableTrack()
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            if (m_tracks.empty())
            {
                return invalidTrack;
            }
            else
            {
                TTrackContainer::nth_index<1>::type& time_index = m_tracks.get<1>();
                const STrack& t = *(time_index.begin());
                lock.unlock();

                if (t.m_trackTime.is_pos_infinity())
                    return invalidTrack;

                return STrackInfo(m_trackerName, t.m_id, t.m_trackTime);
            }
        }

        void StartTracking(uint32_t trackId)
        {
            {
                boost::mutex::scoped_lock lock(m_followMutex);
                _log_ << "Track object " << trackId;
                m_currentTrackId = trackId;
            }
            m_reactor->GetIO().post(boost::bind(&CObjectTracker::Timeout,
                NCorbaHelpers::CWeakPtr<IObjectTracker>(this), trackId,
                boost::system::error_code()));
        }

        void StopTracking()
        {
            InvalidateCurrentTrack();
            m_timer.cancel();
        }

        void RequestPtzPoint(uint32_t id)
        {
            uint32_t objectId = INVALID_TRACK;
            {
                boost::mutex::scoped_lock lock(m_followMutex);
                objectId = m_currentTrackId;
            }

            if (INVALID_TRACK != objectId)
            {
                boost::mutex::scoped_lock lock(m_trackMutex);
                TTrackContainer::iterator it = m_tracks.find(objectId);
                if (m_tracks.end() != it)
                {
                    const SRectangle& r = (*it).m_rect;
                    uint32_t oid = it->m_id;
                    lock.unlock();

                    double x = r.left + (r.right - r.left) / 2;
                    double y = r.top + (r.bottom - r.top);

                    if ((x < 0 || x > 1) || (y < 0 || y > 1))
                    {
                        _err_ << "Invalid tracker data";
                        Schedule(objectId);
                        return;
                    }

                    std::pair<double, double> predicted(x, y);

                    double pan, tilt, zoom = 0;
                    {
                        boost::mutex::scoped_lock lock(m_calibratorMutex);
                        if (CPositionPredictor::eSuccess != m_predictor->DoPrediction(oid, predicted))
                        {
                            lock.unlock();

                            _log_ << "Error in predication " << oid << " object.";

                            InvalidateCurrentTrack();
                            RemoveTrack(objectId);

                            PObserverImpl oimpl(m_notifier);
                            if (oimpl)
                                oimpl->OnTrackFinished(STrackInfo(m_trackerName, objectId, boost::posix_time::not_a_date_time));
                            return;
                        }

                        {
                            // TODO: Проверить валидность получаемых PTZ координат
                            if (0 != m_calibrator)
                                ptzGetValues(m_calibrator, predicted.first, predicted.second, pan, tilt, zoom);
                        }
                    }

                    Schedule(objectId);

                    PObserverImpl oimpl(m_notifier);
                    if (oimpl)
                        oimpl->OnPtzMove(pan, tilt, zoom);
                }
            }
        }

        void MoveToCoords(float x, float y)
        {
            double pan, tilt, zoom;
            boost::mutex::scoped_lock lock(m_calibratorMutex);
            if (0 != m_calibrator)
            {
                ptzGetValues(m_calibrator, x, y, pan, tilt, zoom);
                lock.unlock();

                PObserverImpl oimpl(m_notifier);
                if (oimpl)
                    oimpl->OnPtzMove(pan, tilt, zoom);
            }
        }

        /// IAppDataSchema implementation //////////////////////////////////////////

        virtual uint32_t GenerateId()
        {
            return 0;
        }

        virtual void Reset(const char*)
        {
        }
    
        virtual void AddClass(const char*, uint32_t)
        {
        }
            
        virtual void AddClassMember(uint32_t classID, const char* memberName, 
            NMMSS::EValueType type, uint32_t memberId)
        {
            TClassMembers::iterator it = m_classMembers.find(memberId);
            if (m_classMembers.end() == it)
                m_classMembers.insert(std::make_pair(memberId, SObjectMember(memberName, type)));
        }

        /// IAppDataSink implementation ////////////////////////////////////////////

        void BeginFrame(const boost::posix_time::ptime& timestamp) override
        {
            if (!m_currentFrameTime.is_not_a_date_time())
                _err_ << "Frame intermixing" << std::endl;

            m_currentFrameTime = timestamp;
        }

        void EndFrame() override
        {      
            TTrackContainer tracks;
            TTrackContainer::iterator it1, it2;
            {
                boost::mutex::scoped_lock lock(m_trackMutex);
                tracks = m_tracks;

                it1 = m_tracks.begin(), it2 = m_tracks.end();
                for (; it1 != it2; ++it1)
                {
                    boost::mutex::scoped_lock lock(m_followMutex);
                    if (((*it1).m_id == m_currentTrackId) || ((*it1).m_trackTime.is_pos_infinity()))
                        m_tracks.modify(it1, set_time(m_currentFrameTime));
                }
            }

            it1 = tracks.begin(), it2 = tracks.end();
            for (; it1 != it2; ++it1)
            {
                const STrack& track = (*it1);

                const SRectangle& r = track.m_rect;
                double x = r.left + (r.right - r.left) / 2;
                double y = r.top + (r.bottom - r.top);

                boost::mutex::scoped_lock lock(m_calibratorMutex);
                if (0 != m_predictor.get())
                    m_predictor->AddObjectPosition(track.m_id, std::make_pair(x, y), m_currentFrameTime);
            }

            if (m_signalNewSequence)
            {
                PObserverImpl oimpl(m_notifier);
                if (oimpl)
                    oimpl->OnNewSequence();
            }

            m_signalNewSequence = false;
            m_currentFrameTime = boost::posix_time::not_a_date_time;
        }

        uint32_t CreateObject(uint32_t, uint32_t trackId) override
        {
            uint32_t objectId = trackId;
            _log_ << "Create object " << objectId << " for tracker " << m_trackerName;
            {
                boost::mutex::scoped_lock lock(m_trackMutex);
                if (m_tracks.empty())
                    m_signalNewSequence = true;
                m_tracks.insert(STrack(objectId, boost::posix_time::pos_infin));
            }
            return objectId;
        }

        void RemoveObject(uint32_t objectID) override
        {
            _log_ << "Remove object with id " << objectID;
            {
                boost::mutex::scoped_lock lock(m_calibratorMutex);
                if (0 != m_predictor.get())
                    m_predictor->EraseObject(objectID);
            }
            RemoveTrack(objectID);

            boost::mutex::scoped_lock lock(m_followMutex);
            if (objectID == m_currentTrackId)
            {
                m_currentTrackId = INVALID_TRACK;
                lock.unlock();

                m_timer.cancel();

                PObserverImpl oimpl(m_notifier);
                if (oimpl)
                    oimpl->OnTrackFinished(STrackInfo(m_trackerName, objectID, m_currentFrameTime));
            }
        }

        void RemoveObjs(const std::vector<uint32_t>& objectIds) override
        {
        }

        void PutObjectValue(uint32_t objectID, uint32_t valueID, const NMMSS::TAppDataValue& v) override
        {
            TClassMembers::iterator cit = m_classMembers.find(valueID);
            if (m_classMembers.end() != cit)
            {
                boost::mutex::scoped_lock lock(m_trackMutex);
                TTrackContainer::iterator it = m_tracks.find(objectID);
                if (m_tracks.end() != it)
                {
                    SObjectMember om = cit->second;
                    m_tracks.modify(it, set_rect_value(om.m_name, v));
                }
            }
        }

        virtual void OnEOF()
        {}

        virtual void OnDisconnected()
        {}

    private:
        void* CreateCalibrator(const TCalibrationParams& params)
        {
            PObserverImpl oimpl(m_notifier);
            if (!oimpl)
            {
                _err_ << "Telemetry information is not accessible";
                throw std::logic_error("Telemetry information is not accessible");
            }

            std::vector<double> decartPoints;
            std::vector<double> panPoints;
            std::vector<double> tiltPoints;
            std::vector<double> zoomPoints;
            TCalibrationParams::const_iterator it1 = params.begin(),
                it2 = params.end();
            for (; it1 != it2; ++it1)
            {
                decartPoints.push_back(it1->dp.x);
                decartPoints.push_back(it1->dp.y);
                panPoints.push_back((double)it1->pp.pan);
                tiltPoints.push_back((double)it1->pp.tilt);
                zoomPoints.push_back((double)it1->pp.zoom);
            }
            ptzError err;
            int  maxPan = oimpl->GetMaxPan();
            // MAXPANVALUE выбрано для обхода случая, когда камера не имеет максимальных абсолютных значений pan
            // и GetMaxPan() вернет 0
            if (0 == maxPan)
                maxPan = MAXPANVALUE;

            void* handle = ptzCalibratorCreate(&decartPoints[0], &panPoints[0],
                &tiltPoints[0], &zoomPoints[0], params.size(), maxPan, &err);
            if (EPTZNoError != err)
            {
                _err_ << "PTZ calibration object error : " << err;
                throw std::logic_error("Bad calibration info");
            }
            return handle;
        }

        void RemoveTrack(uint32_t objectId)
        {
            boost::mutex::scoped_lock lock(m_trackMutex);
            TTrackContainer::iterator it = m_tracks.find(objectId);
            if (m_tracks.end() != it)
                m_tracks.erase(it);
        }

        void Schedule(uint32_t id)
        {
            try
            {
                m_timer.expires_from_now(boost::posix_time::millisec(m_PTZCommandSentingTimeout));
                m_timer.async_wait(boost::bind(&CObjectTracker::Timeout,
                    NCorbaHelpers::CWeakPtr<IObjectTracker>(this), id, _1));
            }
            catch(const boost::system::system_error &) {}
        }

        static void Timeout(NCorbaHelpers::CWeakPtr<IObjectTracker> weak, uint32_t id,
            const boost::system::error_code& error)
        {
            if (!error)
            {
                NCorbaHelpers::CAutoPtr<IObjectTracker> tracker(weak);
                if (tracker)
                    tracker->RequestPtzPoint(id);
            }
        }

        void InvalidateCurrentTrack()
        {
            boost::mutex::scoped_lock lock(m_followMutex);
            m_currentTrackId = INVALID_TRACK;
        }

        NCorbaHelpers::WPContainerTrans m_container;
        std::string m_trackerName;
        WPObserverImpl m_notifier;

        typedef boost::shared_ptr<CPositionPredictor> TPredictor;
        TPredictor m_predictor;

        boost::mutex m_calibratorMutex;
        void* m_calibrator;
        NCorbaHelpers::CAutoPtr<NMMSS::ISinkEndpoint> m_sinkEndpoint;

        uint32_t m_objectId;
        boost::posix_time::ptime m_currentFrameTime;

        typedef std::map<uint32_t, SObjectMember> TClassMembers;
        TClassMembers m_classMembers;

        typedef multi_index_container<
            STrack,
            indexed_by<
                ordered_unique<member<STrack, uint32_t, &STrack::m_id> >,
                ordered_non_unique<member<STrack, boost::posix_time::ptime, &STrack::m_trackTime> >
            >
        > TTrackContainer;
        TTrackContainer m_tracks;
        boost::mutex m_trackMutex;

        boost::mutex m_followMutex;
        uint32_t m_currentTrackId;

        NCorbaHelpers::PReactor m_reactor;
        boost::asio::deadline_timer m_timer;

        bool m_signalNewSequence;
        const unsigned int m_PTZCommandSentingTimeout;
    };
}

namespace IPINT30
{
    IObjectTracker* CreateObjectTracker(DECLARE_LOGGER_ARG, NCorbaHelpers::WPContainerTrans c, 
        const STracker& params, WPObserverImpl notifier)
    {
        return new CObjectTracker(GET_LOGGER_PTR, c, params, notifier);
    }
}
