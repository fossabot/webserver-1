#include "DataBuffer.h"
#include "CommonUtility.h"

#include <mmss/Sample.h>
#include <mmss/MediaType.h>

namespace
{
    class SampleDataBuffer : public NHttp::IDataBuffer
    {
    public:
        SampleDataBuffer(NMMSS::ISample* s, bool urgent)
            : m_sample(s, NCorbaHelpers::ShareOwnership())
            , m_urgent(urgent)
        {
        }

        std::uint8_t* GetData() const override
        {
            return m_sample->GetBody();
        }

        std::uint64_t GetSize() const override
        {
            return m_sample->Header().nBodySize;
        }

        std::uint64_t GetTimestamp() const override
        {
            return m_sample->Header().dtTimeBegin;
        }

        bool IsKeyData() const override
        {
            return NPluginUtility::IsKeyFrame(m_sample.Get());
        }

        bool IsEoS() const override
        {
            return NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Auxiliary::EndOfStream>(&(m_sample->Header()));
        }

        bool IsUrgent() const override
        {
            return m_urgent ? m_urgent : IsEoS();
        }

        bool IsBinary() const override
        {
            NMMSS::SMediaSampleHeader& h = m_sample->Header();
            return !(NMMSS::NMediaType::CheckMediaType<NMMSS::NMediaType::Application::TypedOctetStream>(&h));
        }
    private:
        NMMSS::PSample m_sample;
        bool m_urgent;
    };

    class EoSDataBuffer : public NHttp::IDataBuffer
    {
    public:
        EoSDataBuffer(std::uint64_t ts) 
            : m_timestamp(ts)
        {}

        std::uint8_t* GetData() const override
        {
            return nullptr;
        }

        std::uint64_t GetSize() const override
        {
            return 0;
        }

        std::uint64_t GetTimestamp() const override
        {
            return m_timestamp;
        }

        bool IsKeyData() const override
        {
            return false;
        }

        bool IsEoS() const override
        {
            return true;
        }

        bool IsUrgent() const override
        {
            return true;
        }

        bool IsBinary() const override
        {
            return true;
        }

    private:
        std::uint64_t m_timestamp;
    };
}

namespace NHttp
{
    PDataBuffer CreateDataBufferFromSample(NMMSS::ISample* s, bool urgent)
    {
        return PDataBuffer(new SampleDataBuffer(s, urgent));
    }

    PDataBuffer CreateEoSBuffer(std::uint64_t ts)
    {
        return PDataBuffer(new EoSDataBuffer(ts));
    }
}
