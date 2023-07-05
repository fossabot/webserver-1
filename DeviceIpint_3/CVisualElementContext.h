#ifndef DEVICEIPINT3_CVISUALELEMENTSCONTEXT_H
#define DEVICEIPINT3_CVISUALELEMENTSCONTEXT_H

#include "ParamIterator.h"
#include "DeviceSettings.h"

namespace IPINT30
{
class CDetectorContext : public IPINT30::IParamsContext
{
public:
    CDetectorContext(SEmbeddedDetectorSettings& settings);

// IPINT30::IParamsContext implementation
public:
    virtual void GatherParameters(const std::string& group, 
        NStructuredConfig::TCategoryParameters& categories);

    virtual void UpdateParameters(const std::string& group,
        const NStructuredConfig::TCustomParameters& parameters);

protected:
    SEmbeddedDetectorSettings& m_settings;
};
class CVisualElementContext : public IPINT30::IParamsContext
{
public:
    CVisualElementContext(SEmbeddedDetectorSettings& settings);

// IPINT30::IParamsContext implementation
public:
    virtual void GatherParameters(const std::string& group, 
        NStructuredConfig::TCategoryParameters& categories);

    virtual void UpdateParameters(const std::string& group,
        const NStructuredConfig::TCustomParameters& parameters);

    virtual void CleanOldParameters(const std::string& uid,
        const NStructuredConfig::TCustomParameters& parameters);

protected:
    SEmbeddedDetectorSettings& m_settings;
};
}//namespace IPINT30

#endif //DEVICEIPINT3_CVISUALELEMENTSCONTEXT_H

