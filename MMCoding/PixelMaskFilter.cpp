#ifdef _MSC_VER
#pragma warning(disable: 4503)
#endif

#include <cmath>
#include "Transforms.h"
#include "../FilterImpl.h"
#include "../PtimeFromQword.h"
#include "../Distributor.h"
#include "../ConnectionResource.h"


namespace
{
    class CPixelMaskTransformer
    {
        DECLARE_LOGGER_HOLDER;

        NMMSS::PPixelMaskProvider m_provider;
        bool                      m_invert;
        boost::posix_time::ptime  m_timestamp;
        uint32_t                  m_width;
        uint32_t                  m_height;
        uint32_t                  m_blurSize;
        NMMSS::TPixelMask         m_mask;
        NMMSS::PSample            m_maskedSample;
        NMMSS::ETransformResult   m_result;

    public:

        CPixelMaskTransformer(DECLARE_LOGGER_ARG, NMMSS::PPixelMaskProvider provider, bool invert)
            : m_provider(provider)
            , m_invert(invert)
        {
            INIT_LOGGER_HOLDER;
        }

        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            m_timestamp = NMMSS::PtimeFromQword(sample->Header().dtTimeBegin);
            m_mask = m_provider->Advance(m_timestamp);

            if (!std::get<0>(m_mask) || !std::get<1>(m_mask) || !std::get<2>(m_mask))
                return NMMSS::ETHROUGH;

            m_maskedSample = holder.GetAllocator()->Alloc(sample->Header().nBodySize);
            m_maskedSample->Header().nBodySize = sample->Header().nBodySize;

            try
            {
                NMMSS::NMediaType::ApplyMediaTypeVisitor(sample, *this);

                if (NMMSS::ETRANSFORMED == m_result)
                {
                    m_maskedSample->Header().dtTimeBegin = sample->Header().dtTimeBegin;
                    m_maskedSample->Header().dtTimeEnd = sample->Header().dtTimeEnd;

                    holder.AddSample(m_maskedSample);
                }
            }
            catch (std::exception & e)
            {
                _log_ << e.what() << std::endl;
                return NMMSS::EFAILED;
            }
            return m_result;
        }

        template<typename TMediaTypeHeader>
        void operator()(TMediaTypeHeader* header, uint8_t* body)
        {
            m_result = NMMSS::ETHROUGH;
        }

        void operator()(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* header, uint8_t* body)
        {
            uint8_t* maskBody = m_maskedSample->GetBody();
            memcpy(maskBody, body, m_maskedSample->Header().nBodySize);
            NMMSS::NMediaType::Video::fccGREY::SubtypeHeader* subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<NMMSS::NMediaType::Video::fccGREY>(m_maskedSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(NMMSS::NMediaType::Video::fccGREY::SubtypeHeader));

            DoMask(maskBody + subheader->nOffset, 0, 0, subheader->nPitch, 0, 0, 0, 0);

            m_result = NMMSS::ETRANSFORMED;
        }

        void operator()(NMMSS::NMediaType::Video::fccI420::SubtypeHeader* header, uint8_t* body)
        {
            DoConvert<NMMSS::NMediaType::Video::fccI420>(header, body, 2, 2);
        }

        void operator()(NMMSS::NMediaType::Video::fccY42B::SubtypeHeader* header, uint8_t* body)
        {
            DoConvert<NMMSS::NMediaType::Video::fccY42B>(header, body, 2, 1);
        }

    private:

        template <typename THeader>
        void DoConvert(typename THeader::SubtypeHeader* header, uint8_t* body, uint8_t uvXFactor, uint8_t uvYFactor)
        {
            m_width = header->nWidth;
            m_height = header->nHeight;

            static const int MIN_BLUR_SIZE = 16;
            static const int BLUR_FACTOR = 64;
            
            m_blurSize = std::max(MIN_BLUR_SIZE, (int)std::sqrt(std::pow(m_width, 2) + std::pow(m_height, 2)) / BLUR_FACTOR);
            
            uint8_t* maskBody = m_maskedSample->GetBody();
            memcpy(maskBody, body, m_maskedSample->Header().nBodySize);
            typename THeader::SubtypeHeader* subheader = 0;
            NMMSS::NMediaType::MakeMediaTypeStruct<THeader>(m_maskedSample->GetHeader(), &subheader);
            memcpy(subheader, header, sizeof(typename THeader::SubtypeHeader));

            DoMask(maskBody + subheader->nOffset,
                maskBody + subheader->nOffsetU,
                maskBody + subheader->nOffsetV,
                subheader->nPitch,
                subheader->nPitchU,
                subheader->nPitchV,
                uvXFactor,
                uvYFactor
            );

            m_result = NMMSS::ETRANSFORMED;
        }

        void DoMask(uint8_t* pY, uint8_t* pU, uint8_t* pV, uint32_t yPitch, uint32_t uPitch, uint32_t vPitch, uint8_t xUVFactor, uint8_t yUVFactor)
        {
            uint32_t maskW = std::get<1>(m_mask);
            uint32_t maskH = std::get<2>(m_mask);

            unsigned char* mask = std::get<0>(m_mask).get();

            if (maskW < m_width)
            {
                float scaleW = float(m_width) / maskW;
                float scaleH = float(m_height) / maskH;

                for (uint32_t my = 0; my < maskH; ++my)
                {
                    for (uint32_t mx = 0; mx < maskW; ++mx)
                    {
                        if ((!m_invert && mask[my * maskW + mx]) || (m_invert && !mask[my * maskW + mx]))
                        {
                            for (uint32_t y = uint32_t(scaleH * my); y < uint32_t(scaleH * (my + 1)); ++y)
                            {
                                for (uint32_t x = uint32_t(scaleW * mx); x < uint32_t(scaleW * (mx + 1)); ++x)
                                {
                                    int32_t xx = x - x % m_blurSize;
                                    int32_t yy = y - y % m_blurSize;

                                    pY[yPitch * y + x] = pY[yPitch * yy + xx];

                                    if (pU)
                                        pU[uPitch * (y / yUVFactor) + x / xUVFactor] = pU[uPitch * (yy / yUVFactor) + xx / xUVFactor];
                                    if (pV)
                                        pV[vPitch * (y / yUVFactor) + x / xUVFactor] = pV[vPitch * (yy / yUVFactor) + xx / xUVFactor];
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                float scaleW = float(maskW) / m_width;
                float scaleH = float(maskH) / m_height;

                for (uint32_t y = 0; y < m_height; ++y)
                {
                    for (uint32_t x = 0; x < m_width; ++x)
                    {
                        uint32_t index = uint32_t(scaleH * y) * maskW + uint32_t(scaleW * x);
                        if ((!m_invert && mask[index]) || (m_invert && !mask[index]))
                        {
                            int32_t xx = x - x % m_blurSize;
                            int32_t yy = y - y % m_blurSize;

                            pY[yPitch * y + x] = pY[yPitch * yy + xx];

                            if (pU)
                                pU[uPitch * (y / yUVFactor) + x / xUVFactor] = pU[uPitch * (yy / yUVFactor) + xx / xUVFactor];
                            if (pV)
                                pV[vPitch * (y / yUVFactor) + x / xUVFactor] = pV[vPitch * (yy / yUVFactor) + xx / xUVFactor];
                        }
                    }
                }
            }
        }
    };

    class CRuntimePixelMaskFilter : public virtual NMMSS::IFilter, public virtual NCorbaHelpers::CRefcountedImpl
    {
        DECLARE_LOGGER_HOLDER;

        NMMSS::PDistributor        m_distributor;
        NMMSS::PPullFilter         m_maskProducer;
        NMMSS::PPullFilter         m_videoFilter;
        NMMSS::CConnectionResource m_maskConnection;
        NMMSS::CConnectionResource m_videoConnection;

    public:

        CRuntimePixelMaskFilter(DECLARE_LOGGER_ARG, NMMSS::PDistributor distributor, NMMSS::PPullFilter maskProducer, NMMSS::PPullFilter videoFilter)
            : m_distributor(distributor)
            , m_maskProducer(maskProducer)
            , m_videoFilter(videoFilter)
            , m_maskConnection(m_distributor->CreateSource(), m_maskProducer->GetSink(), GET_LOGGER_PTR)
            , m_videoConnection(m_distributor->CreateSource(), m_videoFilter->GetSink(), GET_LOGGER_PTR)
        {
            INIT_LOGGER_HOLDER;

            _log_ << "Created runtime pixel mask filter: " << this;
        }

        ~CRuntimePixelMaskFilter()
        {
            _log_ << "Destroy runtime pixel mask filter: " << this;
        }

        NMMSS::IPullStyleSink* GetSink() override
        {
            return m_distributor->GetSink();
        }

        NMMSS::IPullStyleSource* GetSource() override
        {
            return m_videoFilter->GetSource();
        }

        void AddUnfilteredFrameType(uint32_t major, uint32_t minor) override {}
        void RemoveUnfilteredFrameType(uint32_t major, uint32_t minor) override {}
    };
}

namespace NMMSS
{
    IFilter* CreatePixelMaskFilter(DECLARE_LOGGER_ARG, PPixelMaskProvider provider, bool invert)
    {
        return new CPullFilterImpl<CPixelMaskTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CPixelMaskTransformer(GET_LOGGER_PTR, provider, invert)
            );
    }

    IFilter* CreateRuntimePixelMaskFilter(DECLARE_LOGGER_ARG, NMMSS::IFilter* producer, bool invert)
    {
        NMMSS::PDistributor distributor(NMMSS::CreateDistributor(GET_LOGGER_PTR, NMMSS::NAugment::BarrierDistributor{}));
        NMMSS::PPullFilter maskProducer(producer, NCorbaHelpers::ShareOwnership());
        NMMSS::PPixelMaskProvider maskProvider(NMMSS::CreatePixelMaskProvider(GET_LOGGER_PTR, maskProducer->GetSource()));
        NMMSS::PPullFilter videoFilter(new CPullFilterImpl<CPixelMaskTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CPixelMaskTransformer(GET_LOGGER_PTR, maskProvider, invert)
            ));

        return new CRuntimePixelMaskFilter(GET_LOGGER_PTR, distributor, maskProducer, videoFilter);
    }

    IFilter* CreateRuntimeTrackMaskFilter(DECLARE_LOGGER_ARG, NMMSS::IFilter* producer, bool invert)
    {
        NMMSS::PDistributor distributor(NMMSS::CreateDistributor(GET_LOGGER_PTR, NMMSS::NAugment::BarrierDistributor{}));
        NMMSS::PPullFilter maskProducer(producer, NCorbaHelpers::ShareOwnership());
        NMMSS::PPixelMaskProvider maskProvider(NMMSS::CreateTrackMaskProvider(GET_LOGGER_PTR, maskProducer->GetSource()));
        NMMSS::PPullFilter videoFilter(new CPullFilterImpl<CPixelMaskTransformer, true>(
            GET_LOGGER_PTR,
            SAllocatorRequirements(0),
            SAllocatorRequirements(0),
            new CPixelMaskTransformer(GET_LOGGER_PTR, maskProvider, invert)
            ));

        return new CRuntimePixelMaskFilter(GET_LOGGER_PTR, distributor, maskProducer, videoFilter);
    }
}
