#ifndef MMTRANSPORT_SOURCEFACTORY_H_
#define MMTRANSPORT_SOURCEFACTORY_H_

#include "../Augments.h"
#include "../MMSS.h"
#include "../Distributor.h"
#include "Exports.h"

namespace MMSS { class QualityOfService; }

namespace NMMSS {

    class IQoSAwareSource : public virtual IPullStyleSource
    {
    public:
        virtual void ModifyQoS(MMSS::QualityOfService const&) = 0;
        virtual void ReprocessQoS() = 0;
    };
    using PQoSAwareSource = NCorbaHelpers::CAutoPtr<IQoSAwareSource>;

    class IQoSAwareSourceFactory : public virtual NCorbaHelpers::IRefcounted
    {
    public:
        virtual IQoSAwareSource* CreateSource(MMSS::QualityOfService const&) = 0;
        virtual SAllocatorRequirements GetFactoryAllocatorRequirements() = 0;
        virtual const IStatisticsProvider* GetStatisticsProvider() const = 0;
    };
    using PQoSAwareSourceFactory = NCorbaHelpers::CAutoPtr<IQoSAwareSourceFactory>;

    class IQoSPolicy : public virtual NCorbaHelpers::IRefcounted
    {
    public:
        struct AugmentedSourceConfiguration
        {
            EStartFrom start;
            CAugments augs;
        };
        virtual AugmentedSourceConfiguration PrepareConfiguration(MMSS::QualityOfService const& qos) = 0;

        using uid_type = void const*;
        virtual void QoSRequested(uid_type uid, MMSS::QualityOfService const& qos) = 0;
        virtual void QoSRevoked(uid_type uid) = 0;
        virtual void SubscribePolicyChanged(IQoSAwareSource*) = 0;
        virtual void UnsubscribePolicyChanged(IQoSAwareSource*) = 0;
    };

    class IConsumerConnectionReactor : public virtual NCorbaHelpers::IRefcounted
    {
    protected:
        bool m_isConsumersDisconnected;

    public:
        virtual void OnConsumerConnected() { }
        virtual void OnConsumerDisconnected() { }

        bool isConsumersDisconected() { return m_isConsumersDisconnected; }
    };

    using PQoSPolicy = NCorbaHelpers::CAutoPtr<IQoSPolicy>;
    using ISourceFactory = IQoSAwareSourceFactory;
    using PSourceFactory = PQoSAwareSourceFactory;

    MMTRANSPORT_DECLSPEC IQoSAwareSourceFactory* CreateQoSAwareSourceFactory(
        DECLARE_LOGGER_ARG, IPullStyleSource* source, IQoSPolicy* policy, 
        CAugment const& dist = NAugment::UnbufferedDistributor(), IConsumerConnectionReactor* notifySource = nullptr);

    MMTRANSPORT_DECLSPEC IQoSAwareSourceFactory* CreateDefaultSourceFactory(
        DECLARE_LOGGER_ARG, IPullStyleSource* source, CAugment const& dist = NAugment::UnbufferedDistributor(),
        IConsumerConnectionReactor* notifySource = nullptr);

    MMTRANSPORT_DECLSPEC IQoSAwareSourceFactory* CreateDisposablePullStyleSourceFactory(DECLARE_LOGGER_ARG, IPullStyleSource* source);

} // namespace NMMSS

#endif // MMTRANSPORT_SOURCEFACTORY_H_
