#include "SourceFactory.h"
#include "QoSPolicyImpl.h"
#include "../ConnectionResource.h"
#include "../ProxyPinImpl.h"
#include "../MMCoding/AugmentedSourceFactory.h"
#include <CorbaHelpers/RefcountedImpl.h>

namespace {

    class CFakeSourceFactory
        : public NMMSS::IQoSAwareSourceFactory
        , public NCorbaHelpers::CRefcountedImpl
    {
        class CSource
            : public virtual NMMSS::IQoSAwareSource
            , public NMMSS::CProxySourceImpl<NMMSS::IAugmentedSource>
            , public NCorbaHelpers::CRefcountedImpl
        {
        public:
            CSource(
                DECLARE_LOGGER_ARG,
                NMMSS::IPullStyleSource* source,
                NMMSS::IQoSPolicy* policy,
                MMSS::QualityOfService const& qos)
            : NMMSS::CProxySourceImpl<NMMSS::IAugmentedSource>(nullptr)
            , m_policy(policy, NCorbaHelpers::ShareOwnership())
            {
                m_policy->SubscribePolicyChanged(this);
                auto augs = m_policy->PrepareConfiguration(qos).augs;
                setPin(NMMSS::CreateAugmentedSource(GET_LOGGER_PTR, source, augs), NCorbaHelpers::TakeOwnership());
                m_lastUsedQoS = qos;
            }
            ~CSource()
            {
                m_policy->UnsubscribePolicyChanged(this);
            }
            void ModifyQoS( MMSS::QualityOfService const& qos ) override
            {
                auto config = m_policy->PrepareConfiguration(qos);
                getPin()->Modify(config.start, config.augs);
                m_lastUsedQoS = qos;
            }
            void ReprocessQoS() override
            {
                auto config = m_policy->PrepareConfiguration(m_lastUsedQoS);
                getPin()->Modify(config.start, config.augs);
            }
        private:
            NMMSS::PQoSPolicy m_policy;
            MMSS::QualityOfService m_lastUsedQoS;
        };

    public:
        CFakeSourceFactory( DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource * source, NMMSS::IQoSPolicy * policy )
            : m_source( source, NCorbaHelpers::ShareOwnership() )
            , m_policy( policy, NCorbaHelpers::ShareOwnership() )
        {
            INIT_LOGGER_HOLDER;
            _trc_ << "CFakeSourceFactory(), this=0x" << (void*)this;
        }
        ~CFakeSourceFactory()
        {
            _trc_ << "~CFakeSourceFactory(), this=0x" << (void*)this;
        }
        NMMSS::IQoSAwareSource * CreateSource( MMSS::QualityOfService const& qos ) override
        {
            boost::unique_lock<boost::mutex> lock (m_mutex);
            if( !m_source )
                return nullptr;
            auto result = new CSource( GET_LOGGER_PTR, m_source.Get(), m_policy.Get(), qos );
            m_source.Reset();
            m_policy.Reset();
            return result;
        }
        NMMSS::SAllocatorRequirements GetFactoryAllocatorRequirements() override
        {
            boost::unique_lock<boost::mutex> lock (m_mutex);
            if( m_source )
                return m_source->GetAllocatorRequirements();
            else
                return NMMSS::SAllocatorRequirements();
        }
    private:
        virtual const NMMSS::IStatisticsProvider* GetStatisticsProvider() const
        {
            return dynamic_cast<const NMMSS::IStatisticsProvider*>(m_source.Get());
        }

    private:
        DECLARE_LOGGER_HOLDER;
        mutable boost::mutex m_mutex;
        NMMSS::PPullStyleSource m_source;
        NMMSS::PQoSPolicy m_policy;
    };

    ////////////////////////////////////////////////////////////////////////////

    class CQoSAwareSource
        : public virtual NMMSS::IQoSAwareSource
        , public NMMSS::CProxySourceImpl<NMMSS::IAugmentedSource>
        , public NCorbaHelpers::CRefcountedImpl
        , public NLogging::WithLogger
    {
        using Base = NMMSS::CProxySourceImpl<NMMSS::IAugmentedSource>;
    public:
        CQoSAwareSource(
            DECLARE_LOGGER_ARG,
            NMMSS::IAugmentedSourceFactory* asf,
            NMMSS::IQoSPolicy* policy,
            MMSS::QualityOfService const& qos,
            NMMSS::IConsumerConnectionReactor* notifySource)
        : Base(nullptr)
        , NLogging::WithLogger(GET_LOGGER_PTR)
        , m_policy(policy, NCorbaHelpers::ShareOwnership())
        , m_notifySource(notifySource, NCorbaHelpers::ShareOwnership())
        {
            TRACE_BLOCK;
            m_policy->SubscribePolicyChanged(this);
            auto cfg = m_policy->PrepareConfiguration(qos);
            Base::setPin(asf->CreateSource(cfg.start, cfg.augs), NCorbaHelpers::TakeOwnership());
            m_policy->QoSRequested(this, qos);
            m_lastUsedQoS = qos;
            if (m_notifySource)
                m_notifySource->OnConsumerConnected();
        }
        ~CQoSAwareSource()
        {
            TRACE_BLOCK;
            try
            {
                m_policy->UnsubscribePolicyChanged(this);
                m_policy->QoSRevoked(this);
                if (m_notifySource)
                    m_notifySource->OnConsumerDisconnected();
            }
            catch (CORBA::Exception& ex)
            {
                _err_ << "~CQoSAwareSource exception:" << ex;
            }
        }
        void ModifyQoS(MMSS::QualityOfService const& qos) override
        {
            TRACE_BLOCK;
            auto cfg = m_policy->PrepareConfiguration(qos);
            Base::getPin()->Modify(cfg.start, cfg.augs);
            m_policy->QoSRequested(this, qos);
            m_lastUsedQoS = qos;
        }
        void ReprocessQoS() override
        {
            TRACE_BLOCK;
            auto cfg = m_policy->PrepareConfiguration(m_lastUsedQoS);
            Base::getPin()->Modify(cfg.start, cfg.augs);
            m_policy->QoSRequested(this, m_lastUsedQoS);
        }
    private:
        MMSS::QualityOfService m_lastUsedQoS;
        NMMSS::PQoSPolicy m_policy;
        NCorbaHelpers::CAutoPtr<NMMSS::IConsumerConnectionReactor> m_notifySource;
    };

    class CQoSAwareSourceFactory
        : public virtual NMMSS::IQoSAwareSourceFactory
        , public NCorbaHelpers::CRefcountedImpl
        , public NLogging::WithLogger
    {
    public:
        CQoSAwareSourceFactory(DECLARE_LOGGER_ARG, NMMSS::IPullStyleSource* source, NMMSS::CAugment const& dist, 
                               NMMSS::IQoSPolicy* policy, NMMSS::IConsumerConnectionReactor* notifySource)
            : NLogging::WithLogger(GET_LOGGER_PTR)
            , m_policy(policy, NCorbaHelpers::ShareOwnership())
            , m_source(source, NCorbaHelpers::ShareOwnership())
            , m_notifySource(notifySource, NCorbaHelpers::ShareOwnership())
        {
            TRACE_BLOCK;
            auto augs = NMMSS::CAugments{ dist };
            m_sf = NMMSS::CreateAugmentedSourceFactory(GET_LOGGER_PTR, source, augs);
        }
        ~CQoSAwareSourceFactory()
        {
            TRACE_BLOCK;
        }
        NMMSS::IQoSAwareSource* CreateSource(MMSS::QualityOfService const& qos) override
        {
            return new CQoSAwareSource(GET_LOGGER_PTR, m_sf.Get(), m_policy.Get(), qos, m_notifySource.Get());
        }
        NMMSS::SAllocatorRequirements GetFactoryAllocatorRequirements() override
        {
            return NMMSS::SAllocatorRequirements();
        }
        NMMSS::IStatisticsProvider const* GetStatisticsProvider() const override
        {
            return dynamic_cast<NMMSS::IStatisticsProvider const*>(m_source.Get());
        }
    private:
        NMMSS::PQoSPolicy m_policy;
        NMMSS::PPullStyleSource m_source;
        NMMSS::PAugmentedSourceFactory m_sf;
        NCorbaHelpers::CAutoPtr<NMMSS::IConsumerConnectionReactor> m_notifySource;
    };

} // anonymous namespace

namespace NMMSS {

    IQoSAwareSourceFactory* CreateQoSAwareSourceFactory(
        DECLARE_LOGGER_ARG, IPullStyleSource* source, IQoSPolicy* policy, CAugment const& dist, IConsumerConnectionReactor* notifySource)
    {
        return new CQoSAwareSourceFactory(GET_LOGGER_PTR, source, dist, policy, notifySource);
    }

    IQoSAwareSourceFactory* CreateDefaultSourceFactory(
        DECLARE_LOGGER_ARG, IPullStyleSource* source, CAugment const& dist, IConsumerConnectionReactor* notifySource)
    {
        auto policy = NCorbaHelpers::MakeRefcounted<CDefaultQoSPolicy<>>();
        return CreateQoSAwareSourceFactory(GET_LOGGER_PTR, source, policy.Get(), dist, notifySource);
    }

    IQoSAwareSourceFactory* CreateDisposablePullStyleSourceFactory(DECLARE_LOGGER_ARG, IPullStyleSource* source)
    {
        auto policy = NCorbaHelpers::MakeRefcounted<CDefaultQoSPolicy<>>();
        return new CFakeSourceFactory(GET_LOGGER_PTR, source, policy.Get());
    }

} // namespace NMMSS
