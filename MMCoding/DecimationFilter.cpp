#include "TweakableFilterImpl.h"
#include "../PtimeFromQword.h"
#include "../SampleAdvisor.h"
#include <mmss/MediaType.h>
#include <boost/type_erasure/any_cast.hpp>

namespace {

    class CDecimationTransform : public NLogging::WithLogger
    {
        const boost::posix_time::millisec ZEROms = boost::posix_time::millisec(0);
    public:
        CDecimationTransform(DECLARE_LOGGER_ARG, NMMSS::NAugment::Decimation const& aug)
            : WithLogger(GET_LOGGER_PTR)
            , m_period(boost::posix_time::millisec(aug.period.count()))
            , m_expected(boost::posix_time::not_a_date_time)
            , m_onlyKeyFrames(aug.onlyKeyFrames)
            , m_markPreroll(aug.markPreroll)
        {
            _dbg_ << "Creating decimation transform. this=" << this
                << " period=" << m_period
                << " onlyKeyFrames=" << m_onlyKeyFrames
                << " markPreroll=" << m_markPreroll;
        }
        ~CDecimationTransform()
        {
            _dbg_ << "Destroying decimation transform. this=" << this;
        }
        CDecimationTransform(CDecimationTransform && other)
            : WithLogger(std::move(other))
            , m_state(other.m_state)
            , m_period(other.m_period)
            , m_expected(other.m_expected)
            , m_onlyKeyFrames(other.m_onlyKeyFrames)
            , m_markPreroll(other.m_markPreroll)
            , m_decodedStream(other.m_decodedStream)
        {}
        void Tweak(NMMSS::CAugment const& aug)
        {
            boost::unique_lock<boost::mutex> lock(m_mutex);
            auto a = boost::type_erasure::any_cast<NMMSS::NAugment::Decimation>(aug);
            m_period = boost::posix_time::millisec(a.period.count());
            m_onlyKeyFrames = a.onlyKeyFrames;
            m_markPreroll = a.markPreroll;
            _dbg_ << "Tweakaed decimation transform. this=" << this
                << " period=" << m_period
                << " onlyKeyFrames=" << m_onlyKeyFrames
                << " markPreroll=" << m_markPreroll;
        }
        NMMSS::CAugment GetTweak() const
        {
            return NMMSS::NAugment::Decimation{
                std::chrono::milliseconds(m_period.total_milliseconds()),
                m_onlyKeyFrames,
                m_markPreroll
            };
        }
        NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
        {
            NMMSS::ETransformResult result = NMMSS::EIGNORED;

            boost::unique_lock<boost::mutex> lock(m_mutex);
            if (m_state.CanDecode(sample))
            {
                NMMSS::SMediaSampleHeader const& header = sample->Header();
                if (IsSpecialFrame(header))
                {
                    result = NMMSS::ETHROUGH;
                }
                else if ((CheckOnlyKeyFrames(header) && CheckPeriod(header)))
                {
                    updateExpectedTime(header);
                    result = NMMSS::ETHROUGH;
                }
                else if (m_markPreroll && !m_onlyKeyFrames)
                {
                    result = NMMSS::ETHROUGH;

                    if (!sample->Header().HasFlag(NMMSS::SMediaSampleHeader::EFPreroll))
                    {
                        auto clone = NMMSS::PSample(NMMSS::EclipseSampleHeader(sample, holder.GetAllocator().Get()));
                        clone->Header().eFlags |= NMMSS::SMediaSampleHeader::EFPreroll;
                        holder.AddSample(clone);
                        result = NMMSS::ETRANSFORMED;
                    }
                }
            }

            m_state.TakeIntoAccount(sample, result == NMMSS::ETHROUGH || result == NMMSS::ETRANSFORMED);
            return result;
        }
    private:
        void updateExpectedTime(NMMSS::SMediaSampleHeader const& header)
        {
            auto const time = NMMSS::PtimeFromQword(header.dtTimeBegin);
            if (m_expected.is_not_a_date_time())
                m_expected = time;
            m_expected += m_period;
            if (m_expected <= time)
                m_expected = time + m_period;
        }
        bool IsSpecialFrame(NMMSS::SMediaSampleHeader const& header) const
        {
            return header.eFlags & NMMSS::SMediaSampleHeader::EFDiscontinuity
                || header.eFlags & NMMSS::SMediaSampleHeader::EFInitData
                || (header.nMajor == NMMSS::NMediaType::Auxiliary::ID &&
                    header.nSubtype == NMMSS::NMediaType::Auxiliary::EndOfStream::ID);
        }
        bool CheckOnlyKeyFrames(NMMSS::SMediaSampleHeader const& header)
        {
            return !m_onlyKeyFrames || checkKeySample(header);
        }
        bool CheckPeriod(NMMSS::SMediaSampleHeader const& header) const
        {
            return m_period == ZEROms
                || m_expected.is_not_a_date_time()
                || m_expected <= NMMSS::PtimeFromQword(header.dtTimeBegin);
        }
        bool checkKeySample(const NMMSS::SMediaSampleHeader& header)
        {
            bool decodedFromKeySample = !!(header.eFlags & NMMSS::SMediaSampleHeader::EFTransformedFromKeySample);
            m_decodedStream |= decodedFromKeySample;
            return m_decodedStream ? decodedFromKeySample : header.IsKeySample();
        }
    private:
        boost::mutex mutable m_mutex;
        NMMSS::CSampleStreamState m_state;
        boost::posix_time::time_duration m_period;
        boost::posix_time::ptime m_expected;
        bool m_onlyKeyFrames;
        bool m_markPreroll;
        bool m_decodedStream{};
    };

} // anonymous namespace

namespace NMMSS {

    ITweakableFilter* CreateDecimationFilter(DECLARE_LOGGER_ARG, NAugment::Decimation const& aug)
    {
        return new CTweakableFilterImpl<CDecimationTransform>(GET_LOGGER_PTR, new CDecimationTransform(GET_LOGGER_PTR, aug));
    }

} // namespace NMMSS
