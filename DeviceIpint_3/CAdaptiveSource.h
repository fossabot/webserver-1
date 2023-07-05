#ifndef DEVICEIPINT3_CADAPTIVESOURCE_H
#define DEVICEIPINT3_CADAPTIVESOURCE_H

#include "../MMTransport/SourceFactory.h"

#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Container.h>

#include <set>
#include <vector>
#include <string>
#include <mutex>

namespace IPINT30
{
    class CAdaptiveSourceFactory : public NCorbaHelpers::CRefcountedImpl,
        public virtual NMMSS::IQoSAwareSourceFactory,
        public NLogging::WithLogger
    {
    public:
        CAdaptiveSourceFactory(DECLARE_LOGGER_ARG, NCorbaHelpers::IContainerNamed* container, const std::string& accessPoint);

        void Enable(const std::string& accessPoint, bool useForGreenStream);
        void Disable(const std::string& accessPoint);

        NMMSS::IQoSAwareSource* CreateSource(MMSS::QualityOfService const& qos) override;
        NMMSS::SAllocatorRequirements GetFactoryAllocatorRequirements() override;
        const NMMSS::IStatisticsProvider* GetStatisticsProvider() const override;

    private:
        void updateStreamings();

    private:
        NCorbaHelpers::WPContainerNamed m_container;
        std::string m_accessPoint;
        std::mutex m_lock;
        std::set<std::string> m_availableStreamings;
        std::set<std::string> m_greenStreamStreamings;
        std::vector<std::string> m_selectedStreamings;
        NCorbaHelpers::PResource m_servant;
    };
}

#endif // DEVICEIPINT3_CADAPTIVESOURCE_H