#ifndef MMTRANSPORT_QOSPOLICY_H_
#define MMTRANSPORT_QOSPOLICY_H_

#include "SourceFactory.h"
#include "QualityOfService.h"
#include "../PtimeFromQword.h"
#include "../Distributor.h"
#include "../MMCoding/TweakableFilter.h"
#include <CorbaHelpers/RefcountedImpl.h>
#include <boost/type_erasure/any_cast.hpp>

namespace NMMSS {

    class CDefaultAugmentationPolicy
    {
    public:
        using result_type = void;
        CDefaultAugmentationPolicy(NMMSS::EStartFrom& start, NMMSS::CAugments& augs)
            : m_start(start)
            , m_augments(augs)
        {}
        void operator()(MMSS::QoSRequest::StartFrom const& r)
        {
            switch (r.from)
            {
            case MMSS::QoSRequest::StartFrom::NextKeyFrame:
                m_start = NMMSS::EStartFrom::NextKeyFrame;
                break;
            case MMSS::QoSRequest::StartFrom::PrevKeyFrame:
                m_start = NMMSS::EStartFrom::PrevKeyFrame;
                break;
            case MMSS::QoSRequest::StartFrom::Preroll:
                m_start = NMMSS::EStartFrom::Preroll;
                break;
            default:
                throw std::logic_error("CDefaultAugmentationPolicy: Unknown start policy");
            }
        }
        void operator()(MMSS::QoSRequest::OnlyKeyFrames const& r)
        {
            for (auto& a : m_augments)
            {
                if (auto decim = boost::type_erasure::any_cast<NMMSS::NAugment::Decimation*>(&a))
                {
                    decim->onlyKeyFrames = r.enabled;
                    return;
                }
            }
            m_augments.push_back(NMMSS::NAugment::Decimation{ std::chrono::milliseconds::zero(), r.enabled, false });
        }
        void operator()(MMSS::QoSRequest::FrameRate const& r)
        {
            auto const period = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds{ 1 } / r.fps);
            for (auto& a : m_augments)
            {
                if (auto decim = boost::type_erasure::any_cast<NMMSS::NAugment::Decimation*>(&a))
                {
                    decim->period = period;
                    decim->markPreroll = r.preroll;
                    return;
                }
            }
            m_augments.push_back(NMMSS::NAugment::Decimation{ period, false, r.preroll });
        }
        void operator()(MMSS::QoSRequest::Buffer const& r)
        {
            auto duration = std::chrono::milliseconds(r.duration);
            auto startStr = std::string(r.start);
            auto start = startStr.empty()
                ? boost::date_time::not_a_date_time
                : boost::posix_time::from_iso_string(startStr);
            m_augments.push_back(NMMSS::NAugment::Buffer {duration, start, r.discontinuty});
        }
        template<class TQoSRequest>
        void operator()(TQoSRequest const&)
        {}
    protected:
        NMMSS::EStartFrom& m_start;
        NMMSS::CAugments& m_augments;
    };

    template<class TAugmentationPolicy = CDefaultAugmentationPolicy>
    class CDefaultQoSPolicy
        : public virtual IQoSPolicy
        , public NCorbaHelpers::CRefcountedImpl
    {
    public:
        void QoSRequested(uid_type uid, MMSS::QualityOfService const& qos) override
        {}
        void QoSRevoked(uid_type uid) override
        {}
        AugmentedSourceConfiguration PrepareConfiguration(MMSS::QualityOfService const& qos) override
        {
            AugmentedSourceConfiguration result = {};
            TAugmentationPolicy visitor(result.start, result.augs);
            ApplyVisitor(qos, visitor);
            return result;
        }
        virtual void SubscribePolicyChanged(IQoSAwareSource*) override
        {}
        virtual void UnsubscribePolicyChanged(IQoSAwareSource*) override
        {}
    };

} // namespace NMMSS

#endif // MMTRANSPORT_QOSPOLICY_H_
