#include "CVisualElementContext.h"
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include "ParamContext.h"

namespace IPINT30
{
CDetectorContext::CDetectorContext(SEmbeddedDetectorSettings& settings)
    :m_settings(settings)
{
}

void CDetectorContext::GatherParameters(const std::string& group, NStructuredConfig::TCategoryParameters& categories)
{
    for (const NStructuredConfig::SVisualElementSettings& element : m_settings.visual_elements)
    {
        std::string subGroup = group + "/VisualElement." + element.value.name + ':' + element.uid;
        categories[subGroup].push_back(element.value);
        OperationsStrategy<NStructuredConfig::SVisualElementSettings, Iterable>::GatherParameters(element, subGroup, categories);
    }
}

void CDetectorContext::UpdateParameters(const std::string& group,
                                             const NStructuredConfig::TCustomParameters& parameters)
{
    BOOST_FOREACH(const NStructuredConfig::SCustomParameter& newParam, parameters)
    {
        if(newParam.name == "enabled")
        {
            OperationsStrategy<SEmbeddedDetectorSettings, Mutable>::UpdateParameter(m_settings, group, newParam);
        }
        else
        {
            OperationsStrategy<SEmbeddedDetectorSettings, Iterable>::UpdateParameter(m_settings, group, newParam);
        }
    }

}

CVisualElementContext::CVisualElementContext(SEmbeddedDetectorSettings& settings)
    :m_settings(settings)
{
}

void CVisualElementContext::GatherParameters(const std::string& group, 
                                             NStructuredConfig::TCategoryParameters& categories)
{
    // The GatherParameters is empty because we can not guarantee that m_settings will contain data 
    // at the moment of call. Filling of m_settings occurs in time of detector creating from the configuration 
    // data that were downloaded at the start of the service. Further the categories data is used in 
    // CLiveServiceContext::Apply as m_params for determining the changed settings. In the case 
    // of visual element is not necessary since Initial configuration can not contain detector's data.
}

void CVisualElementContext::CleanOldParameters(const std::string& uid, const NStructuredConfig::TCustomParameters& parameters)
{
    auto& v = m_settings.visual_elements;
    v.erase(std::remove_if(v.begin(), v.end(), 
        [&uid](const NStructuredConfig::SVisualElementSettings& element)
    {        
        if (element.uid == uid)
            return true;
        return false;
    }), v.end());
}

void CVisualElementContext::UpdateParameters(const std::string& group,
                                             const NStructuredConfig::TCustomParameters& parameters)
{
    TCategoryPath path;
    // Parse the grope like:
    //  "VideoChannel.0/VideoAnalytics.sony-detector-3g:0/VisualElement.window:64de17f5-bbf7-444d-b6dc-94e02cbd5333"
    ParsePropertyGroupName(group, path);

    // Expects: "VideoChannel.{VideoChannelId}/VideoAnalytics.{DetectorType}/VisualElement.{name}:{guid}"
    if(path.size() != 3 || path[0].size() != 2 || path[1].size() != 3 || path[2].size() != 3 ||
        path[2][0] != "VisualElement" )
    {
        throw std::runtime_error("Unexpected format of group name: "+group);
    }
    const std::string& name = path[2][1];
    const std::string& uid = path[2][2];

    // Forget about performance. Keep It Simple, Stupid!
    NStructuredConfig::TVisualElementSettings::iterator it =
        std::find_if(m_settings.visual_elements.begin(), m_settings.visual_elements.end(),
        boost::bind(&NStructuredConfig::SVisualElementSettings::uid, _1) == uid);

    if(it == m_settings.visual_elements.end())
    {
        it = m_settings.visual_elements.insert(m_settings.visual_elements.end(), NStructuredConfig::SVisualElementSettings());
        it->uid = uid;
    }
    else if ( IsDisabled(parameters) )
    {
        m_settings.visual_elements.erase(it);
        return;
    }

    BOOST_FOREACH(const NStructuredConfig::SCustomParameter& newParam, parameters)
    {
        if(newParam.name == name)
        {
            it->value = newParam;
        }
        else
        {
            OperationsStrategy<NStructuredConfig::SVisualElementSettings, Iterable>::UpdateParameter(*it, group, newParam);
        }
    }

}

}//namespace IPINT30



