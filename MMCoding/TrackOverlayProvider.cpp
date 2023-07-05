#include <list>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/variant/get.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <CorbaHelpers/RefcountedImpl.h>
#include "../AppData.h"
#include "../AppDataMMSource.h"
#include "../MMTransport/MMTransport.h"
#include "../ConnectionResource.h"
#include "Transforms.h"


namespace
{
    double DoubleFromVariant(const NMMSS::TAppDataValue& value)
    {
       double val = 0.0;
       try
       {
           val = boost::get<double>(value);
       }
       catch (const boost::bad_get&) { }
       return val;
    }


    class CTrackStore : public virtual NMMSS::IAppDataSchema
                      , public virtual NMMSS::IAppDataSink
                      , public virtual NMMSS::IAppDataNotifier
                      , public virtual NCorbaHelpers::CWeakReferableImpl
    {
        struct frame_t
        {
            boost::posix_time::ptime timestamp;
            NMMSS::Box rectangle;
        };

        struct track_t
        {
            bool closed = false;
            std::list<frame_t> frames;
        };

        typedef std::map<uint32_t, track_t> track_collection_t;
        typedef std::map<uint32_t, std::string> parameter_map_t;
        typedef std::map<uint32_t, NMMSS::Box> location_map_t;

        DECLARE_LOGGER_HOLDER;

        boost::posix_time::time_duration m_wait;
        boost::condition                 m_waiter;
        parameter_map_t                  m_params;
        location_map_t                   m_locations;
        boost::posix_time::ptime         m_timestamp;
        boost::posix_time::ptime         m_passed;
        track_collection_t               m_tracks;
        bool                             m_full;
        boost::mutex                     m_mutex;

    public:

        CTrackStore(DECLARE_LOGGER_ARG, const boost::posix_time::time_duration& wait)
            : m_wait(wait)
            , m_timestamp(boost::posix_time::not_a_date_time)
            , m_passed(boost::posix_time::min_date_time)
            , m_full(false)
        {
            INIT_LOGGER_HOLDER;

            _log_ << "Track store [" << this << "] is created";
        }

        ~CTrackStore()
        {
            _log_ << "Track store [" << this << "] is removed";
        }

        NMMSS::TBoxList Advance(const boost::posix_time::ptime& time)
        {
            boost::mutex::scoped_lock lock(m_mutex);

            m_waiter.timed_wait(lock, m_wait, [=]() { return m_full || m_passed >= time; });

            NMMSS::TBoxList frame;

            auto iter = m_tracks.begin();
            while (iter != m_tracks.end())
            {
                static const boost::posix_time::seconds THRESHOLD = boost::posix_time::seconds(1);

                auto next = std::find_if(iter->second.frames.begin(), iter->second.frames.end(), [time](const frame_t& item) { return item.timestamp > time; });
                auto prev = std::prev(next);

                if (prev != iter->second.frames.end())
                {
                    if (prev->timestamp + THRESHOLD < time)
                        iter->second.frames.erase(iter->second.frames.begin(), next);
                    else
                        iter->second.frames.erase(iter->second.frames.begin(), prev);
                }

                if (!iter->second.frames.empty())
                {
                    if (time + THRESHOLD >= iter->second.frames.begin()->timestamp)
                        frame.emplace_back(iter->second.frames.begin()->rectangle);
                }

                if (iter->second.closed && iter->second.frames.empty())
                    iter = m_tracks.erase(iter);
                else
                    ++iter;
            }

            return frame;
        }

        /// IAppDataSchema implementation //////////////////////////////////////////

        uint32_t GenerateId() override { return 0; }

        void Reset(const char*) override { }

        void AddClass(const char*, uint32_t) override { }
            
        void AddClassMember(uint32_t classID, const char* memberName, NMMSS::EValueType type, uint32_t memberId) override
        {
            boost::mutex::scoped_lock lock(m_mutex);

            auto it = m_params.find(memberId);
            if (m_params.end() == it)
                m_params.insert(std::make_pair(memberId, memberName));

            _dbg_ << "Track store [" << this << "] add class member: class_id=" << classID << " member_name=" << memberName << " member_id=" << memberId << " value_type=" << type;
        }

        /// IAppDataSink implementation ////////////////////////////////////////////

       void BeginFrame(const boost::posix_time::ptime& timestamp) override
       {
           _trc_ << "Track store [" << this << "] new frame " << timestamp;

           boost::mutex::scoped_lock lock(m_mutex);
           if (!m_timestamp.is_not_a_date_time())
               _err_ << "Mask store [" << this << "] frame intermixing";

           m_timestamp = timestamp;
       }

       uint32_t CreateObject(uint32_t classID, uint32_t objectId) override
       {
           _trc_ << "Track store [" << this << "] create object: object_id=" << objectId << " class_id=" << classID;

           boost::mutex::scoped_lock lock(m_mutex);
           m_tracks.insert(std::make_pair(objectId, track_t()));
           return objectId;
       }

       void PutObjectValue(uint32_t objectID, uint32_t valueID, const NMMSS::TAppDataValue& value) override
       {
           boost::mutex::scoped_lock lock(m_mutex);

           NMMSS::Box &box = m_locations.insert(
                std::make_pair(objectID, NMMSS::Box(NMMSS::Point(0.0, 0.0), NMMSS::Point(0.0, 0.0)))
                ).first->second;

           _trc_ << "Track store [" << this << "] put object value: value_id=" << valueID << " object_id=" << objectID;

           auto iter = m_params.find(valueID);
           if (m_params.end() != iter)
           {
               static const char *const LEFT = "left";
               static const char *const TOP = "top";
               static const char *const RIGHT = "right";
               static const char *const BOTTOM = "bottom";

               if (iter->second == LEFT)
                   box.min_corner().x(DoubleFromVariant(value));
               else if (iter->second == TOP)
                   box.min_corner().y(DoubleFromVariant(value));
               else if (iter->second == RIGHT)
                   box.max_corner().x(DoubleFromVariant(value));
               else if (iter->second == BOTTOM)
                   box.max_corner().y(DoubleFromVariant(value));
           }
       }

       void RemoveObject(uint32_t objectID) override
       {
           boost::mutex::scoped_lock lock(m_mutex);
           
           auto iter = m_tracks.find(objectID);
           if (iter != m_tracks.end())
               iter->second.closed = true;

           _trc_ << "Track store [" << this << "] remove object: object_id=" << objectID;
       }

       void RemoveObjs(const std::vector<uint32_t>& objectIds) override
       {
       }

       void EndFrame() override
       {
           boost::mutex::scoped_lock lock(m_mutex);
           for (auto& loc : m_locations)
           {
               auto iter = m_tracks.find(loc.first);
               if (m_tracks.end() != iter)
                   iter->second.frames.emplace_back(frame_t{m_timestamp, loc.second});
           }

           m_passed = m_timestamp;
           m_timestamp = boost::posix_time::not_a_date_time;
           m_locations.clear();

           m_waiter.notify_all();
       }

       /// IAppDataNotifier implementation ///////////////////////////////////////////

       void OnEOF() override
       {
           boost::mutex::scoped_lock lock(m_mutex);
           m_full = true;

           _dbg_ << "Tracker was exhausted";

           m_waiter.notify_all();
       }

       void OnDisconnected() override
       {
           boost::mutex::scoped_lock lock(m_mutex);
           m_full = true;

           _dbg_ << "Tracker was disconnected";

           m_waiter.notify_all();
       }
    };

    typedef NCorbaHelpers::CAutoPtr<CTrackStore> PTrackStore;
    typedef NCorbaHelpers::CWeakPtr<CTrackStore> PWeakTrackStore;
    typedef NCorbaHelpers::CAutoPtr<NMMSS::IAppDataMMPullSink> PAppDataMMPullSink;


    class CTrackOverlayProvider : public virtual NMMSS::IOverlayProvider, public virtual NCorbaHelpers::CRefcountedImpl
    {
        PWeakTrackStore            m_store;
        NMMSS::CConnectionResource m_connection;

    public:

        CTrackOverlayProvider(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* vmda, const boost::posix_time::ptime& end, const boost::posix_time::time_duration& wait)
        {
            PTrackStore store(new CTrackStore(GET_LOGGER_PTR, wait));
            NCorbaHelpers::CAutoPtr<NMMSS::IAppDataMMPullSink> sink(NMMSS::CreateAppDataMMPullSink(GET_LOGGER_PTR, store.Get(), store.Get(), store.Get(), end));

            m_store = PWeakTrackStore(store);
            m_connection = NMMSS::CConnectionResource(vmda, sink.Get(), GET_LOGGER_PTR);
        }

        NMMSS::TBoxList Advance(const boost::posix_time::ptime& time) override
        {
            PTrackStore store(m_store);
            return store ? store->Advance(time) : NMMSS::TBoxList();
        }
    };

    class CTrackMaskProvider : public virtual NMMSS::IPixelMaskProvider, public virtual NCorbaHelpers::CRefcountedImpl
    {
        PWeakTrackStore            m_store;
        NMMSS::CConnectionResource m_connection;

        DECLARE_LOGGER_HOLDER;

    public:

        CTrackMaskProvider(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* vmda, const boost::posix_time::ptime& end, const boost::posix_time::time_duration& wait)
        {
            INIT_LOGGER_HOLDER;

            PTrackStore store(new CTrackStore(GET_LOGGER_PTR, wait));
            NCorbaHelpers::CAutoPtr<NMMSS::IAppDataMMPullSink> sink(NMMSS::CreateAppDataMMPullSink(GET_LOGGER_PTR, store.Get(), store.Get(), store.Get(), end));

            m_store = PWeakTrackStore(store);
            m_connection = NMMSS::CConnectionResource(vmda, sink.Get(), GET_LOGGER_PTR);
        }

        NMMSS::TPixelMask Advance(const boost::posix_time::ptime& time) override
        {
            static const uint32_t MASK_WIDTH = 240;
            static const uint32_t MASK_HEIGHT = 180;

            NMMSS::TPixelMask mask = std::make_tuple(boost::shared_array<unsigned char>(new unsigned char[MASK_WIDTH * MASK_HEIGHT]), MASK_WIDTH, MASK_HEIGHT);
            std::memset(std::get<0>(mask).get(), 0, MASK_WIDTH * MASK_HEIGHT);

            PTrackStore store(m_store);
            NMMSS::TBoxList boxes = store ? store->Advance(time) : NMMSS::TBoxList();

            for (auto& box : boxes)
            {
                if (box.min_corner().x() < box.max_corner().x() && box.min_corner().y() < box.max_corner().y() && box.max_corner().x() > 0 && box.max_corner().y() > 0)
                {
                    uint32_t xmin = std::max(std::min(uint32_t(box.min_corner().x() * MASK_WIDTH), MASK_WIDTH), 0u);
                    uint32_t width = std::max(std::min(uint32_t(box.max_corner().x() * MASK_WIDTH), MASK_WIDTH) - xmin, 1u);

                    uint32_t ymin = std::max(std::min(uint32_t(box.min_corner().y() * MASK_HEIGHT), MASK_HEIGHT), 0u);
                    uint32_t height = std::max(std::min(uint32_t(box.max_corner().y() * MASK_HEIGHT), MASK_HEIGHT) - ymin, 1u);

                    for (uint32_t y = ymin; y < ymin + height; ++y)
                        std::memset(std::get<0>(mask).get() + y * MASK_WIDTH + xmin, 1, width);
                }
            }

            return mask;
        }
    };
}

namespace NMMSS
{
    IOverlayProvider* CreateTrackOverlayProvider(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* vmda, const boost::posix_time::ptime& end, const boost::posix_time::time_duration& wait)
    {
        return new CTrackOverlayProvider(GET_LOGGER_PTR, vmda, end, wait);
    }

    IPixelMaskProvider* CreateTrackMaskProvider(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* vmda, const boost::posix_time::ptime& end, const boost::posix_time::time_duration& wait)
    {
        return new CTrackMaskProvider(GET_LOGGER_PTR, vmda, end, wait);
    }
}
