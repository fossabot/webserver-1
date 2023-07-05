#ifndef DEVICEIPINT3_PARAM_ITERATOR_H
#define DEVICEIPINT3_PARAM_ITERATOR_H

#include <string>
#include <vector>
#include <map>

namespace NStructuredConfig
{
    struct SCustomParameter;
    typedef std::vector<SCustomParameter> TCustomParameters;
    typedef std::map<std::string, TCustomParameters> TCategoryParameters;
    typedef std::map<std::string, std::vector<std::string>> TCategory2UIDs;
}

namespace IPINT30
{
    class IParamsContext
    {
    public:
        virtual ~IParamsContext() {}

        virtual void GatherParameters(const std::string& group, 
            NStructuredConfig::TCategoryParameters& categories) = 0;

        virtual void UpdateParameters(const std::string& group,
            const NStructuredConfig::TCustomParameters& parameters) = 0;

        virtual void CleanOldParameters(const std::string& uid,
            const NStructuredConfig::TCustomParameters& parameters)
        {
            // Do nothing.
        }
    };

    class IParamContextIterator
    {
    public:
        virtual ~IParamContextIterator() {}

        virtual void Reset()= 0;
        virtual bool MoveNext() = 0;
        virtual IParamsContext* Current() const = 0;
        virtual const char* GetCurrentContextName() = 0;
        virtual IParamsContext* GetContextByName(const std::string& contextName) = 0;
    };

    class ICustomProperty
    {
    public:
        virtual ~ICustomProperty() {}

        virtual NStructuredConfig::SCustomParameter Get() const = 0;

        virtual void Set(const NStructuredConfig::SCustomParameter &newParam) = 0;

        virtual const std::string& GetName() const = 0;
    };


}

#endif // DEVICEIPINT3_PARAM_ITERATOR_H
