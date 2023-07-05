#ifndef DEVICEIPINT3_PARAM_CONTEXT_H
#define DEVICEIPINT3_PARAM_CONTEXT_H

#include <string>
#include <vector>
#include <boost/foreach.hpp>
#include <boost/function.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>

#include "ParamIterator.h"
#include "DeviceSettings.h"

typedef std::vector<std::string> TCategoryPathParts;
typedef std::vector<TCategoryPathParts> TCategoryPath;

typedef boost::shared_ptr<IPINT30::ICustomProperty> ICustomPropertyPtr;
typedef std::vector<ICustomPropertyPtr> TCustomProperties;

inline bool IsDisabled(const NStructuredConfig::TCustomParameters& parameters)
{
    static const NStructuredConfig::SCustomParameter ENABLE_PARAM("enabled", "bool", "0");
    return parameters.size() == 1 && parameters[0].name==ENABLE_PARAM.name && 
        parameters[0].ValueUtf8() == ENABLE_PARAM.ValueUtf8();
}
inline bool IsDeviceNodeOwnerParam(const NStructuredConfig::SCustomParameter& parameter)
{
    static const std::string OWNER_DEVICE_NODE_PARAMETER("ownerDeviceNode");
    return parameter.name == OWNER_DEVICE_NODE_PARAMETER;
}
static const std::string DEVICE_NODE_CONTEXT_NAME_PREFIX("deviceNode:");
static const char DEVICE_NODE_GROUP_DELIMITER(':');
inline bool IsDeviceNodeContext(const std::string& groupName)
{
    return groupName.compare(0, DEVICE_NODE_CONTEXT_NAME_PREFIX.size(), DEVICE_NODE_CONTEXT_NAME_PREFIX) == 0;
}

// "ObjectType1.objectId:ObjectSubId/ObjectType2.objectId:ObjectSubId/ObjectType3.objectId"
inline void ParsePropertyGroupName(const std::string& category, TCategoryPath& path)
{
    std::vector<std::string> directories; 
    boost::split(directories, category, boost::is_any_of("/"), boost::token_compress_on);

    for (const std::string& dir : directories)
    {
        path.push_back(TCategoryPathParts());
        std::vector<std::string>& parts = path.back();
        boost::split(parts, dir, boost::is_any_of(".:"), boost::token_compress_on);
    }
}

namespace
{

    template <typename T, typename TIterator>
    class OperationsStrategy
    {
    public:
        static void GatherParameters(const T& params, const std::string& group,
            NStructuredConfig::TCategoryParameters& categories)
        {
        }

        static bool UpdateParameter(T& params, const std::string& group,
            const NStructuredConfig::SCustomParameter &newParam)
        {
            return false;
        }
    };

    template <typename T>
    class OperationsStrategy<T, Mutable>
    {
    public:
        static void GatherParameters(const T& settings, const std::string& group,
            NStructuredConfig::TCategoryParameters& categories)
        {
            categories[group].push_back(
                NStructuredConfig::SCustomParameter("enabled", "bool", !settings.enabled ? "0" : "1"));
        }

        static bool UpdateParameter(T& settings, const std::string& group,
            const NStructuredConfig::SCustomParameter &newParam)
        {
            static const NStructuredConfig::SCustomParameter ENABLE_PARAM("enabled", "bool", "0");
            if(newParam.name == ENABLE_PARAM.name)
            {
                settings.enabled = newParam.ValueUtf8() != ENABLE_PARAM.ValueUtf8() ? 1 : 0;
                return true;
            }
            return false;
        }
    };

    template <typename T>
    class OperationsStrategy<T, Iterable>
    {
    public:
        static void GatherParameters(const T& settings, const std::string& group,
            NStructuredConfig::TCategoryParameters& categories)
        {
            // добавляем в результирующую структуру пустой вектор параметров под именем контекста
            NStructuredConfig::TCustomParameters& parameters(categories[group]);
            
            std::copy(settings.publicParams.begin(), settings.publicParams.end(),
                std::back_inserter(parameters));
        }

        static bool UpdateParameter(T& settings, const std::string& group,
            const NStructuredConfig::SCustomParameter &newParam)
        {
            // Forget about performance. Keep It Simple, Stupid!
            NStructuredConfig::TCustomParameters::iterator it = 
                std::find_if(settings.publicParams.begin(), settings.publicParams.end(),
                boost::bind(&NStructuredConfig::SCustomParameter::name, _1) == newParam.name);

            if(it != settings.publicParams.end())
            {
                NStructuredConfig::SCustomParameter &param = *it;
                // TODO: If types are not equal, throw exception. Can not be currently used because 
                //       we don't have guarantee that the 'settings' are correct for all streamings.
                param.SetValue(param.Type(), newParam.ValueUtf8());
            }
            else
            {
                settings.publicParams.push_back(newParam);
            }
            return true;
        }
    };

    template <>
    class OperationsStrategy<IPINT30::SAudioDestinationParam, Iterable>
    {
    public:
        static void GatherParameters(const IPINT30::SAudioDestinationParam& settings, const std::string& group,
            NStructuredConfig::TCategoryParameters& categories)
        {
            // добавляем в результирующую структуру пустой вектор параметров под именем контекста
            NStructuredConfig::TCustomParameters& parameters(categories[group]);
            
            std::copy(settings.publicParams.begin(), settings.publicParams.end(),
                std::back_inserter(parameters));

            std::copy(settings.privateParams.begin(), settings.privateParams.end(),
                std::back_inserter(parameters));
        }

        static bool UpdateParameter(IPINT30::SAudioDestinationParam& settings, const std::string& group,
            const NStructuredConfig::SCustomParameter &newParam)
        {
            // Forget about performance. Keep It Simple, Stupid!
            NStructuredConfig::TCustomParameters::iterator it = 
                std::find_if(settings.publicParams.begin(), settings.publicParams.end(),
                boost::bind(&NStructuredConfig::SCustomParameter::name, _1) == newParam.name);

            if(it != settings.publicParams.end())
            {
                NStructuredConfig::SCustomParameter &param = *it;
                UpdateParam(param, newParam);
            }
            else
            {
                it =  std::find_if(settings.privateParams.begin(), settings.privateParams.end(),
                    boost::bind(&NStructuredConfig::SCustomParameter::name, _1) == newParam.name);

                if (it != settings.privateParams.end())
                {
                    NStructuredConfig::SCustomParameter &param = *it;
                    UpdateParam(param, newParam);
                }
                else
                    settings.publicParams.push_back(newParam);
            }
            return true;
        }

    private:
        static void UpdateParam(NStructuredConfig::SCustomParameter &param,
            const NStructuredConfig::SCustomParameter &newParam)
        {
            if (param.ValueUtf8() != newParam.ValueUtf8())
            {
                // Verifies property value type
                if (param.Type() != newParam.Type())
                {
                    throw std::runtime_error(
                        boost::str(boost::format("Unexpected type %1% for property %2%:%3%")
                        % newParam.Type() % param.name%param.Type()));
                }
                param.SetValue(newParam.Type(), newParam.ValueUtf8());
            }
        }
    };

    template<class P>
    class CCustomProperty : public IPINT30::ICustomProperty
    {
    public:
        typedef boost::function<P(void)> TGetter;
        typedef boost::function<void(P)> TSetter;

    public:
        CCustomProperty(const std::string& name, const std::string& type,
            TGetter getter, TSetter setter)
            :m_name(name), m_type(type), m_getter(getter), m_setter(setter)
        {
        }
    // Implements IPINT30::IParamsContext
    public:
        virtual NStructuredConfig::SCustomParameter Get() const
        {
            return NStructuredConfig::SCustomParameter(m_name, m_type, 
                boost::lexical_cast<std::string>(m_getter()));
        }

        virtual void Set(const NStructuredConfig::SCustomParameter &newParam)
        {

            if (newParam.Type() != m_type)
            {
                throw std::runtime_error(
                    boost::str(boost::format("Unexpected type %1% for property %2%:%3%")
                    %newParam.Type()%m_name%m_type));
            }
            m_setter(boost::lexical_cast<P>(newParam.ValueUtf8()));
        }

        virtual const std::string& GetName() const
        {
            return m_name;
        }

    private:
        const std::string m_name;
        const std::string m_type;
        TGetter m_getter;
        TSetter m_setter;
    };

    template<class P>
    void RegisterProperty(TCustomProperties& properties, const std::string& name, 
        const std::string& type, boost::function<P(void)> getter, boost::function<void(P)> setter)
    {

        boost::shared_ptr<IPINT30::ICustomProperty> property(
            new CCustomProperty<P>(name, type, getter, setter) );
        properties.push_back(property);
    }

    template <typename T, typename TIteratorStrategy, typename TEnabledStrategy>
    class CParamContextImpl : public IPINT30::IParamsContext
    {
    public:
        CParamContextImpl(T& settings)
            : m_settings(settings)
        {
        }

        CParamContextImpl(T& settings, const TCustomProperties& properties)
            : m_settings(settings), m_properties(properties)
        {
        }

        virtual void GatherParameters(const std::string& group, 
            NStructuredConfig::TCategoryParameters& categories)
        {
            TIteratorStrategy::GatherParameters(m_settings, group, categories);
            TEnabledStrategy::GatherParameters(m_settings, group, categories);
            for (const ICustomPropertyPtr property : m_properties)
            {
                categories[group].push_back(property->Get());
            }
        }

        virtual void UpdateParameters(const std::string& group, 
            const NStructuredConfig::TCustomParameters& parameters)
        {
            for (const NStructuredConfig::SCustomParameter& newParam : parameters)
            {
                if(TEnabledStrategy::UpdateParameter(m_settings, group, newParam))
                {
                    continue;
                }
                if (IsDeviceNodeContext(group) && IsDeviceNodeOwnerParam(newParam))
                {// skip "service" parameter
                    continue;
                }
                // Forget about performance. Keep It Simple, Stupid!
                TCustomProperties::iterator it = 
                    std::find_if(m_properties.begin(), m_properties.end(),
                    boost::bind(&IPINT30::ICustomProperty::GetName, _1) == newParam.name);

                if(it != m_properties.end())
                {
                    (*it)->Set(newParam);
                    continue;
                }
                TIteratorStrategy::UpdateParameter(m_settings, group, newParam);
            }
        }
    protected:
        T& m_settings;
        TCustomProperties m_properties;
    };

}

namespace IPINT30
{
    class CParamContext : public IParamContextIterator
    {
    public:
        CParamContext()
            :m_doReset(false)
        {
        }

        virtual void Reset()
        {
            m_doReset = true;
        }

        virtual bool MoveNext()
        {
            if(m_doReset)
            {
                m_current = m_iters.begin();
                m_doReset = false;
            }
            else if(m_current != m_iters.end())
            {
                m_current++;
            }
            return m_current !=  m_iters.end();
        }

        virtual IParamsContext* Current() const
        {
            return m_current->second.get();
        }

        void AddContext(const char* contextName, boost::shared_ptr<IPINT30::IParamsContext> context)
        {
            m_iters.insert(std::make_pair(contextName, context));
            m_doReset = true;
        }

        virtual const char* GetCurrentContextName()
        {
            return m_current->first.c_str();
        }
        virtual IParamsContext* GetContextByName(const std::string& contextName)
        {
            TContextIterator it = m_iters.find(contextName);
            if (m_iters.end() == it)
                return 0;
            return it->second.get();
        }

    private:
        std::map<std::string, boost::shared_ptr<IPINT30::IParamsContext> > m_iters;

        typedef std::map<std::string, boost::shared_ptr<IPINT30::IParamsContext> >::iterator TContextIterator;
        TContextIterator m_current;
        bool m_doReset;
    };

    template <typename T>
    boost::shared_ptr<IPINT30::IParamsContext> MakeContext(T& params)
    {
        TCustomProperties properties;
        return MakeContext<T>(params, properties);
    }

    template <typename T>
    boost::shared_ptr<IPINT30::IParamsContext> MakeContext(T& params, const TCustomProperties& properties)
    {
        typedef OperationsStrategy<T, typename T::EnableType> EnableStrategy;
        typedef OperationsStrategy<T, typename T::IterableType> IterableStrategy;

        boost::shared_ptr<IPINT30::IParamsContext> context(
            new CParamContextImpl<T, IterableStrategy, EnableStrategy>(params, properties));
        return context;
    }
}

#endif // DEVICEIPINT3_PARAM_CONTEXT_H
