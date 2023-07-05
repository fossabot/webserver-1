#include "Transforms.h"
#include "../FilterImpl.h"

namespace
{
    class CSieveTransformer
    {
        DECLARE_LOGGER_HOLDER;

    public:

        CSieveTransformer(DECLARE_LOGGER_ARG, NMMSS::EPassableSample sieve = NMMSS::EPassableSample::PS_EVERY_SECOND)
            : m_sampleCount(0)
            , m_sieve(sieve)
        {
            INIT_LOGGER_HOLDER;
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            m_sievedSample = holder.GetAllocator()->Alloc(sample->Header().nBodySize);
            m_sievedSample->Header().nBodySize = sample->Header().nBodySize;
            
            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_sievedSample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_sievedSample->Header().dtTimeEnd = sample->Header().dtTimeEnd;
                    
                    holder.AddSample(m_sievedSample);
                }
            }
            catch (std::exception& e)
            {
                _log_ << e.what() << std::endl;
                return NMMSS::EFAILED;
            }
            return m_result;
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* subtypeHeader, uint8_t* dataPtr)
        {
            m_result = NMMSS::ETHROUGH;
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            DoSieve<NMMSS::NMediaType::Video::fccGREY>(header, body);
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            DoSieve<NMMSS::NMediaType::Video::fccI420>(header, body);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            DoSieve<NMMSS::NMediaType::Video::fccY42B>(header, body);
        }

    private:

        template <typename THeader>
        void DoSieve(typename THeader::SubtypeHeader* header, uint8_t* body)
        {
            if ((m_sampleCount++) % m_sieve)
            {
                m_result = NMMSS::EIGNORED;
                return;
            }

            memcpy(m_sievedSample->GetBody(), body, m_sievedSample->Header().nBodySize);
            typename THeader::SubtypeHeader *subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_sievedSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(typename THeader::SubtypeHeader));

            m_result = NMMSS::ETRANSFORMED;
        }

        uint64_t                m_sampleCount;
        NMMSS::EPassableSample  m_sieve;
        NMMSS::PSample          m_sievedSample;
        NMMSS::ETransformResult m_result;
    };
}

namespace NMMSS
{
    IFilter* CreateSieveFilter(DECLARE_LOGGER_ARG, EPassableSample sieve)
    {
        return new CPullFilterImpl<CSieveTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CSieveTransformer(GET_LOGGER_PTR, sieve)
            );
    }
}
