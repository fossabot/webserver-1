#ifndef DEVICEIPINT3_IIPMANAGER3_H
#define DEVICEIPINT3_IIPMANAGER3_H

#include <map>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/archive/xml_wiarchive.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/serialization/serialization.hpp>

#include <ItvDeviceSdk/include/ItvDeviceSdk.h>

#include "../MMSS.h"
#include "../Telemetry.h"
#include "../Grabber/Grabber.h"

namespace NCommonNotification
{
    class CEventSupplier;
}

namespace NStructuredConfig
{
    class ILiveServiceContext;
    struct SCustomParameter;
    typedef std::vector<SCustomParameter> TCustomParameters;
    typedef std::map<std::string, TCustomParameters> TCategoryParameters;
}

namespace NCorbaHelpers
{
    class IContainerNamed;
}

namespace IPINT30
{
    //Predefines the configuration of VideoChannel
    struct SVideoChannelParam;

    //Predefines the configuration of VideoStreaming
    struct SVideoStreamingParam;

        struct SMicrophoneParam;

        struct STelemetryParam;

        struct SIoDeviceParam;

    struct SAudioDestinationParam;

    enum EIOState
    {
        EIO_Open,
        EIO_Closed,
        EIO_ShortCircuit
    };

    struct CContactState
    {
        bool m_enabled;
        IPINT30::EIOState m_state;
    };

    struct CContactSettings
    {
        friend class boost::serialization::access;

        template<class Archive>
        void serialize(Archive & ar, const unsigned int)
        {
            bool state = false;
            ar & boost::serialization::make_nvp("contact", m_contact)
                & boost::serialization::make_nvp("enabled", m_state.m_enabled)
                & boost::serialization::make_nvp("defaultState", state);

            m_state.m_state = state ? IPINT30::EIO_Closed : IPINT30::EIO_Open;
        }

        unsigned short m_contact;
        CContactState m_state;
    };

    class IIOPanel : public virtual NCorbaHelpers::IRefcounted
    {
    public:
        virtual void SetRelayState(unsigned short contact, EIOState state) = 0;
    };

    class INotifyState
    {
    public:
        virtual ~INotifyState() {}
        virtual void Notify(NMMSS::EIpDeviceState state, Json::Value&& data = Json::Value()) = 0;
    };

    class IParamContextIterator;

    class IObjectParamContext
    {
    public:
        virtual void SwitchEnabled() = 0;
        virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler) = 0;
        virtual IParamContextIterator* GetParamIterator() = 0;
        virtual void ApplyMetaChanges(const NStructuredConfig::TCustomParameters&) {};
    protected:
        virtual ~IObjectParamContext() {};
    };

    class IParameterHolder
    {
    public:
        virtual void GetInitialState(NStructuredConfig::TCategoryParameters& params) const = 0;
        virtual void OnChanged(const NStructuredConfig::TCategoryParameters& params, const NStructuredConfig::TCategoryParameters& removed) = 0;
        virtual void OnChanged(const NStructuredConfig::TCategoryParameters& meta) = 0;
        virtual void ResetToDefaultProperties(std::string& deviceDescription) { throw std::runtime_error("Implement this method before use"); }
    };

    class IIPManager : public IParameterHolder
    {
    public:
        virtual void Connect() = 0;
    };

    class IIpintDevice
        : public IIPManager
        , public ITV8::GDRV::IDeviceHandler
        , public IObjectParamContext
        , public boost::enable_shared_from_this<IIpintDevice>
    {
    public:
        virtual void Open() = 0;
        virtual void Close() = 0;
        virtual ITV8::GDRV::IDevice* getDevice() const = 0;
        virtual std::string ToString() const = 0;
        virtual bool IsGeneric() const = 0;
        virtual void onChannelSignalRestored() = 0;
        virtual bool BlockConfiguration() const = 0;
    };
}

BOOST_CLASS_IMPLEMENTATION(IPINT30::CContactSettings, boost::serialization::object_serializable)

#endif // DEVICEIPINT3_
