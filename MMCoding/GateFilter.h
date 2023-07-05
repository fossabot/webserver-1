#ifndef _MMSS_GATE_FILTER_IMPL_H_
#define _MMSS_GATE_FILTER_IMPL_H_

#include "../ConnectionResource.h"
#include "../FilterImpl.h"
#include "../EndOfStreamSample.h"

namespace NMMSS
{
    /*
    Класс CGateFilter необходимо использовать в случаях
    необходимости точно управлять прерыванием/возобновлением
    трафика мультимединого потока
    */
    class CGateFilter
        : public IFilter
        , public CPullStyleSourceBase
    {
        using OwnerRefcountedImpl = NCorbaHelpers::TOwnerRefcountedImpl<CGateFilter>;

        class Sink
            : public OwnerRefcountedImpl
            , public CPullStyleSinkBasePureRefcounted
        {
        public:
            Sink(CGateFilter* filter)
                : OwnerRefcountedImpl(filter)
            {
            }
            void Request(unsigned int count)
            {
                RequestNextSamples(count);
            }
        protected:
            void Receive(ISample* sample) override
            {
                GetOwner()->OnReceive(sample);
            }
            void onConnected(TLock& lock) override
            {
                auto const r = GetOwner()->GetRequestCount();
                if (r > 0)
                    requestNextSamples(lock, r, false);
            }
        public:
        };

    public:

        CGateFilter(DECLARE_LOGGER_ARG)
            : m_sink(this)
        {
            INIT_LOGGER_HOLDER;
        }
        void Lock(std::uint32_t sessionId)
        {
            auto lock = TLock(mutex());
            m_sessionId = sessionId;
            m_locked = true;

            m_requested = 0;
        }
        void Unlock(std::uint32_t sessionId)
        {
            auto lock = TLock(mutex());
            m_locked = false;
            m_sessionId = sessionId;
        }

        IPullStyleSink* GetSink() override
        {
            return &m_sink;
        }
        IPullStyleSource* GetSource() override
        {
            return this;
        }
        void AddUnfilteredFrameType(uint32_t major, uint32_t minor) override
        {
            m_unfiltered.AddUnfilteredFrameType(major, minor);
        }
        void RemoveUnfilteredFrameType(uint32_t major, uint32_t minor) override
        {
            m_unfiltered.RemoveUnfilteredFrameType(major, minor);
        }

    protected:

        void OnReceive(ISample* sample)
        {
            auto lock = TLock(mutex());
            std::uint32_t sessionId = -1;
            const bool filteredOut = !NMMSS::GetSampleSessionId(sample, sessionId) || sessionId != m_sessionId;

            if (filteredOut)
            {
                // Keeps request count in sync
                if (m_requested > 0)
                {
                    m_sink.Request(1);
                }
                return;
            }
            m_requested = std::max(m_requested - 1, 0);
            const bool sent = sendSample(lock, sample, false);
            if (!sent)
            {
                _wrn_ << "[CGateFilter] Cant sent frame";
            }
        }
        unsigned int GetRequestCount() const
        {
            auto lock = TLock(mutex());
            return (!m_locked ? m_requested : 0);
        }
        void onRequested(TLock& lock, unsigned int count) override
        {
            if (!m_locked)
            {
                m_requested += count;
                lock.unlock();
                m_sink.Request(count);
            }
        }
        void onDisconnected(TLock& lock) override
        {
        }

    private:
        DECLARE_LOGGER_HOLDER;
        Sink m_sink;
        bool m_locked = false;
        NMMSS::NDetails::CUnfilteredFrameTypesList m_unfiltered;
        std::uint32_t m_sessionId = 0;
        int m_requested = 0;
    };

    typedef NCorbaHelpers::CAutoPtr<CGateFilter> PGateFilter;
}

#endif
