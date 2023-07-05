#include "TweakableFilter.h"
#include "../PullStylePinsBaseImpl.h"
#include "../LimitedSampleQueue.h"
#include "../PtimeFromQword.h"
#include <CorbaHelpers/Reactor.h>
#include <boost/bind.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/type_erasure/any_cast.hpp>

namespace {

    class CBufferAugmentation
        : public NMMSS::ITweakableFilter
        , public NMMSS::CPullStyleSourceBase
        , public NLogging::WithLogger
    {
        using Lock = boost::unique_lock<boost::mutex>;

        class CSink : public NMMSS::CPullStyleSinkBase
        {
        public:
            CSink(CBufferAugmentation* listener)
                : m_listener(listener)
            {}
        private:
            void Receive(NMMSS::ISample* sample) override
            {
                RequestNextSamples(1);
                if (m_listener)
                    m_listener->onReceive(sample);
            }
            void onConnected(Lock& lock) override
            {
                requestNextSamples(lock, 8, false);
            }
        private:
            CBufferAugmentation* const m_listener;
        };

    public:
        CBufferAugmentation(DECLARE_LOGGER_ARG, NMMSS::NAugment::Buffer const& r)
            : WithLogger(GET_LOGGER_PTR)
            , m_sink(new CSink(this))
            , m_buffer(MakeBufferLimits(r))
            , m_start(r.start)
        {
            _dbg_ << "Creating buffer augmentation. this=" << this
                << " length=" << r.length.count() << "ms"
                << " start=" << r.start
                << " discontinuty=" << r.markDiscontinuty;
        }
        ~CBufferAugmentation()
        {
            _dbg_ << "Destroying buffer augmentation. this=" << this;
        }
        void Tweak(NMMSS::CAugment const& aug) override
        {
            auto const& a = boost::type_erasure::any_cast<NMMSS::NAugment::Buffer>(aug);
            auto limits = MakeBufferLimits(a);
            Lock lock(mutex());
            m_buffer.SetLimits(limits);
            m_start = a.start;
            _dbg_ << "Tweaked buffer augmentation. this=" << this
                << " length=" << a.length.count() << "ms"
                << " start=" << a.start
                << " discontinuty=" << a.markDiscontinuty;
        }
        NMMSS::CAugment GetTweak() const override
        {
            return NMMSS::NAugment::Buffer{
                m_buffer.GetLimits().minDuration,
                boost::posix_time::not_a_date_time,
                m_buffer.GetLimits().markDiscontinuity
            };
        }
        NMMSS::IPullStyleSink* GetSink() override
        {
            return m_sink.Get();
        }
        NMMSS::IPullStyleSource* GetSource() override
        {
            return this;
        }
        void AddUnfilteredFrameType(uint32_t major, uint32_t minor) override
        {
        }
        void RemoveUnfilteredFrameType(uint32_t major, uint32_t minor) override
        {
        }
    private:
        void onRequested(Lock& lock, unsigned int) override
        {
            initSending(lock);
        }
        void onReceive(NMMSS::ISample* sample)
        {
            Lock lock(mutex());
            m_buffer.Push(sample);
            initSending(lock);
        }
    private:
        void disconnect(Lock const&)
        {
            if (m_connection)
            {
                NMMSS::GetConnectionBroker()->DestroyConnection(m_connection);
                m_connection = nullptr;
            }
        }
        void initSending(Lock& lock)
        {
            if (!m_sending)
            {
                postSend();
                m_sending = true;
            }
        }
        void postSend()
        {
            m_reactor->GetIO().post(boost::bind(
                &CBufferAugmentation::doSend,
                NCorbaHelpers::ShareRefcounted(this)));
        }
        void doSend()
        {
            Lock lock(mutex());
            if (!isConnected(lock) || requested(lock) <= 0 || m_buffer.Empty())
            {
                m_buffer.EnforceLimits();
                m_sending = false;
                return;
            }
            while (auto sample = m_buffer.Peek(getAllocator(lock).Get()))
            {
                auto const sampleTime = NMMSS::PtimeFromQword(sample->Header().dtTimeBegin);
                if (!m_start.is_not_a_date_time() && sampleTime < m_start )
                {
                    m_buffer.Pop(false);
                    continue;
                }
                if (sendSample(lock, sample.Get(), true))
                {
                    m_buffer.Pop(true);
                    break;
                }
            }
            m_buffer.EnforceLimits();
            postSend();
        }
        static NMMSS::SBufferLimits MakeBufferLimits(NMMSS::NAugment::Buffer const& aug)
        {
            // limits are set according to ACR-47055
            return NMMSS::SBufferLimits(
                std::numeric_limits<size_t>::max(), // unlimited number of frames
                std::numeric_limits<size_t>::max(), // unlimited memory usage
                aug.length,
                aug.length + std::chrono::seconds(1200),  // 20 minutes window for keyframe
                aug.markDiscontinuty);
        }
    private:
        NCorbaHelpers::CAutoPtr<CSink> m_sink;
        NMMSS::IConnectionBase* m_connection = nullptr;
        NMMSS::CLimitedSampleQueue m_buffer;
        boost::posix_time::ptime m_start;
        bool m_sending = false;
        NCorbaHelpers::PReactor const m_reactor{ NCorbaHelpers::GetReactorInstanceShared() };
    };

} // anonymous namespace

namespace NMMSS {

    ITweakableFilter* CreateBufferFilter(DECLARE_LOGGER_ARG, NAugment::Buffer const& aug)
    {
        return new CBufferAugmentation(GET_LOGGER_PTR, aug);
    }

} // namespace NMMSS
