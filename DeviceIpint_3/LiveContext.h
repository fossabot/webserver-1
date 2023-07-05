#include <InfraServer_IDL/SimpleConfigurableImpl.h>
#include "IIPManager3.h"
#include "DeviceIpint_Exports.h"

namespace
{
    class CLiveContext :
        public virtual NStructuredConfig::CLiveServiceContext
    {
    public:
        CLiveContext(DECLARE_LOGGER_ARG, IPINT30::IParameterHolder* holder);
        void GetInitialState(NStructuredConfig::TCategoryParameters& params) const override;
        void OnChanged(const NStructuredConfig::TCategoryParameters &params, const NStructuredConfig::TCategoryParameters& removed) override;
        void OnChanged(const NStructuredConfig::TCategoryParameters& meta) override;
        void ResetToDefaultProperties(std::string& deviceDescription) override;

    private:
        DECLARE_LOGGER_HOLDER;
        IPINT30::IParameterHolder* m_holder;
    };
}

namespace IPINT30
{
    DEVICEIPINT_DECLSPEC NStructuredConfig::ILiveServiceContext* CreateLiveContext(DECLARE_LOGGER_ARG, IPINT30::IParameterHolder* holder);
}