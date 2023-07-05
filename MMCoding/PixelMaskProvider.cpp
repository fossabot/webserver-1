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
    class CPixelMaskStore : public virtual NMMSS::IAppDataSchema
                          , public virtual NMMSS::IAppDataSink
                          , public virtual NMMSS::IAppDataNotifier
                          , public virtual NCorbaHelpers::CWeakReferableImpl
    {
        typedef std::pair<boost::posix_time::ptime, NMMSS::TPixelMask> mask_frame_t;
        typedef std::list<mask_frame_t> mask_track_t;
        typedef std::map<uint32_t, std::string> parameter_map_t;
        typedef std::map<uint32_t, NMMSS::TPixelMask> location_map_t;


        DECLARE_LOGGER_HOLDER;

        boost::posix_time::time_duration m_wait;
        boost::condition                 m_waiter;
        parameter_map_t                  m_params;
        boost::posix_time::ptime         m_timestamp;
        boost::posix_time::ptime         m_passed;
        int32_t                          m_width;
        int32_t                          m_height;
        std::vector<unsigned char>       m_buffer;
        mask_track_t                     m_track;
        bool                             m_full;
        boost::mutex                     m_mutex;

    public:

        CPixelMaskStore(DECLARE_LOGGER_ARG, const boost::posix_time::time_duration& wait)
            : m_wait(wait)
            , m_timestamp(boost::posix_time::not_a_date_time)
            , m_passed(boost::posix_time::min_date_time)
            , m_width(0)
            , m_height(0)
            , m_full(false)
        {
            INIT_LOGGER_HOLDER;

            _log_ << "Mask store [" << this << "] is created";
        }

        ~CPixelMaskStore()
        {
            _log_ << "Mask store [" << this << "] is removed";
        }

        NMMSS::TPixelMask Advance(const boost::posix_time::ptime& time)
        {
            static const boost::posix_time::seconds THRESHOLD = boost::posix_time::seconds(3);

            boost::mutex::scoped_lock lock(m_mutex);

            m_waiter.timed_wait(lock, m_wait, [=]() { return m_full || m_passed >= time; });

            auto next = std::find_if(m_track.begin(), m_track.end(), [time](const mask_frame_t& item) { return item.first > time; });
            auto prev = std::prev(next);

            if (prev != m_track.end())
            {
                if (prev->first + THRESHOLD < time)
                    m_track.erase(m_track.begin(), next);
                else
                    m_track.erase(m_track.begin(), prev);
            }

            if (!m_track.empty())
            {
                if (time + THRESHOLD >= m_track.begin()->first)
                    return m_track.begin()->second;
            }

            return std::make_tuple(boost::shared_array<unsigned char>(), 0u, 0u);
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

            _log_ << "Mask store [" << this << "] add class member: class_id=" << classID << " member_name=" << memberName << " member_id=" << memberId << " value_type=" << type;
        }

        /// IAppDataSink implementation ////////////////////////////////////////////

        void BeginFrame(const boost::posix_time::ptime& timestamp) override
        {
            _trc_ << "Mask store [" << this << "] new frame " << timestamp;

            boost::mutex::scoped_lock lock(m_mutex);

            if (!m_timestamp.is_not_a_date_time())
                _err_ << "Mask store [" << this << "] frame intermixing";

            m_timestamp = timestamp;
        }

        uint32_t CreateObject(uint32_t classID, uint32_t objectId) override
        {
            _trc_ << "Mask store [" << this << "] create object: object_id=" << objectId << " class_id=" << classID;
            return objectId;
        }

        void PutObjectValue(uint32_t objectID, uint32_t valueID, const NMMSS::TAppDataValue& value) override
        {
            boost::mutex::scoped_lock lock(m_mutex);

            _trc_ << "Mask store [" << this << "] put object value: value_id=" << valueID << " object_id=" << objectID;

            auto iter = m_params.find(valueID);
            if (m_params.end() != iter)
            {
                const char* const WIDTH = "width";
                const char* const HEIGHT = "height";
                const char* const BUFFER = "buf";
                const char* const BUFSIZE = "buf_size";

                try
                {
                    if (iter->second == WIDTH)
                    {
                        m_width = boost::get<int32_t>(value);
                        _trc_ << "Mask store [" << this << "] put object value: width=" << m_width;
                    }
                    else if (iter->second == HEIGHT)
                    {
                        m_height = boost::get<int32_t>(value);
                        _trc_ << "Mask store [" << this << "] put object value: height=" << m_height;
                    }
                    else if (iter->second == BUFFER)
                    {
                        m_buffer = boost::get<std::vector<unsigned char>>(value);
                        _trc_ << "Mask store [" << this << "] put object value: buf=" << m_buffer.size();
                    }
                    else if (iter->second == BUFSIZE)
                    {
                        _trc_ << "Mask store [" << this << "] put object value: buf_size=" << boost::get<int32_t>(value);
                    }
                }
                catch (const boost::bad_get& ex)
                {
                    _err_ << "Mask store [" << this << "] error: " << ex.what();
                }
            }
        }

        void RemoveObject(uint32_t objectID) override
        {
            _trc_ << "Mask store [" << this << "] remove object: object_id=" << objectID;
        }

        void RemoveObjs(const std::vector<uint32_t>& objectIds) override
        {
        }

        void EndFrame() override
        {
            boost::mutex::scoped_lock lock(m_mutex);

            size_t len = m_buffer.size();
            if (len < size_t(m_width * m_height))
            {
                _err_ << "Mask store [" << this << "] wrong buffer: size=" << len << " width=" << m_width << " height=" << m_height;
            }
            else
            {
                boost::shared_array<unsigned char> buf(new unsigned char[len]);
                std::memcpy(buf.get(), m_buffer.data(), len);

                m_track.push_back(std::make_pair(m_timestamp, std::make_tuple(buf, m_width, m_height)));
            }

            m_passed = m_timestamp;
            m_timestamp = boost::posix_time::not_a_date_time;
            m_buffer.clear();

            m_waiter.notify_all();
        }

        /// IAppDataNotifier implementation ///////////////////////////////////////////

        void OnEOF() override
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_full = true;

            _trc_ << "Mask store [" << this << "] was exhausted";

            m_waiter.notify_all();
        }

        void OnDisconnected() override
        {
            boost::mutex::scoped_lock lock(m_mutex);
            m_full = true;

            _dbg_ << "Mask store [" << this << "] was disconnected";

            m_waiter.notify_all();
        }
    };

    typedef NCorbaHelpers::CAutoPtr<CPixelMaskStore> PPixelMaskStore;
    typedef NCorbaHelpers::CWeakPtr<CPixelMaskStore> PWeakPixelMaskStore;
    typedef NCorbaHelpers::CAutoPtr<NMMSS::IAppDataMMPullSink> PAppDataMMPullSink;


    class CPixelMaskProvider : public virtual NMMSS::IPixelMaskProvider, public virtual NCorbaHelpers::CRefcountedImpl
    {
        PWeakPixelMaskStore        m_store;
        NMMSS::CConnectionResource m_connection;

    public:

        CPixelMaskProvider(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* vmda, const boost::posix_time::ptime& end, const boost::posix_time::time_duration& wait)
        {
            PPixelMaskStore store(new CPixelMaskStore(GET_LOGGER_PTR, wait));
            NCorbaHelpers::CAutoPtr<NMMSS::IAppDataMMPullSink> sink(NMMSS::CreateAppDataMMPullSink(GET_LOGGER_PTR, store.Get(), store.Get(), store.Get(), end));

            m_store = PWeakPixelMaskStore(store);
            m_connection = NMMSS::CConnectionResource(vmda, sink.Get(), GET_LOGGER_PTR);
        }

        NMMSS::TPixelMask Advance(const boost::posix_time::ptime& time) override
        {
            PPixelMaskStore store(m_store);
            return store ? store->Advance(time) : std::make_tuple(boost::shared_array<unsigned char>(), 0u, 0u);
        }
    };
}

namespace NMMSS
{
    IPixelMaskProvider* CreatePixelMaskProvider(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* vmda, const boost::posix_time::ptime& end, const boost::posix_time::time_duration& wait)
    {
        return new CPixelMaskProvider(GET_LOGGER_PTR, vmda, end, wait);
    }
}
