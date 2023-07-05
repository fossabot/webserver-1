#include <ItvSdk/include/IErrorService.h>

#include "LiveContext.h"

CLiveContext::CLiveContext(DECLARE_LOGGER_ARG, IPINT30::IParameterHolder* holder)
    : NStructuredConfig::CLiveServiceContext(GET_LOGGER_PTR), m_holder(holder)
{ }

void CLiveContext::GetInitialState(NStructuredConfig::TCategoryParameters& params) const
{
    m_holder->GetInitialState(params);
}

void CLiveContext::OnChanged(const NStructuredConfig::TCategoryParameters &params, const NStructuredConfig::TCategoryParameters& removed)
{
    m_holder->OnChanged(params, removed);
}

void CLiveContext::OnChanged(const NStructuredConfig::TCategoryParameters& meta)
{
    m_holder->OnChanged(meta);
}

void CLiveContext::ResetToDefaultProperties(std::string& deviceDescription)
{
    m_holder->ResetToDefaultProperties(deviceDescription);
}

namespace IPINT30
{
    NStructuredConfig::ILiveServiceContext* CreateLiveContext(DECLARE_LOGGER_ARG, IPINT30::IParameterHolder* holder)
    {
        return new CLiveContext(GET_LOGGER_PTR, holder);
    }
}
