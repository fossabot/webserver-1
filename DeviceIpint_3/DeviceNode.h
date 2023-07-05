#ifndef DEVICEIPINT3_DEVICE_NODE_H
#define DEVICEIPINT3_DEVICE_NODE_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.

#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <CorbaHelpers/RefcountedImpl.h>
#include <CorbaHelpers/Reactor.h>

#include <ItvDeviceSdk/include/IUnit.h>

#include "../Grabber/SampleTimeCorrector.h"
#include "../ItvSdkUtil/CDetectorEventRaiser.h"

#include "DeviceInformation.h"
#include "DeviceSettings.h"
#include "ParamContext.h"

namespace ITV8
{
static const ITV8::hresult_t EOperationInterrupted = EFirstUserError + 1;
}

namespace IPINT30
{

class IDeviceNodeOperationProcessor
{
public:
    virtual bool StartOperation(const std::string& operation, const boost::uuids::uuid& id) = 0;
};
class OperationEndpoint;

class CDevice;
class DeviceNode : public CAsyncChannelHandlerImpl<ITV8::GDRV::IAsyncDeviceChannelHandler>
            , public ITV8::Analytics::IEventFactory
            , public IObjectParamContext
            , public IDeviceInformationProvider
            , public IDeviceNodeOperationProcessor
{
    typedef CAsyncChannelHandlerImpl<ITV8::GDRV::IAsyncDeviceChannelHandler> base_t;
public:
    DeviceNode(DECLARE_LOGGER_ARG,
            NExecutors::PDynamicThreadPool dynExec,
            boost::shared_ptr<IPINT30::IIpintDevice> parent,
            const SDeviceNodeParam& settings,
            boost::shared_ptr<NMMSS::IGrabberCallback> callback,
            const char* objectContext,
            NCorbaHelpers::IContainerNamed* container,
            const char* eventChannel,
            DeviceNode* ownerDeviceNode);

    inline const std::string& getDeviceNodeId() const { return m_settings.deviceNodeId; }
    inline unsigned int getId() const { return m_settings.id; }
    inline const std::string& getObjectId() const { return m_objectId; }
    const std::string& getOwnerObjectId() const;
    inline static std::string createObjectId(const std::string& deviceNodeId, const unsigned int id){
        return deviceNodeId + "." + std::to_string(id);
    }
    static bool parseObjectId(const std::string& objectId, std::string& deviceNodeId, unsigned int& id);

public: // CAsyncChannelHandlerImpl. 
    std::string ToString() const override;

private: // ITV8::IContract
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::Analytics::IEventFactory)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::IAsyncDeviceChannelHandler)
        ITV8_CONTRACT_ENTRY(ITV8::Analytics::IEventFactory)
        ITV8_CONTRACT_ENTRY(ITV8::IEventHandler)
    ITV8_END_CONTRACT_MAP()

private: // ITV8::Analytics::IEventFactory
    ITV8::Analytics::IDetectorEventRaiser* BeginOccasionalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* name, ITV8::timestamp_t time,
        ITV8::uint32_t phase) override;

    ITV8::Analytics::IDetectorEventRaiser* BeginPeriodicalEventRaising(
        ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time) override;

    void Failed(ITV8::IContract* pSource, ITV8::hresult_t error) override;

private: // CChannel
    void DoStart() override;
    void DoStop() override;
    void OnStarted() override;
    void OnStopped() override;

    void OnFinalized() override;

    void OnEnabled() override;
    void OnDisabled() override;

public: // IObjectParamContext
    void SwitchEnabled() override;
    void ApplyChanges(ITV8::IAsyncActionHandler* handler) override;
    IParamContextIterator* GetParamIterator() override;

private: // IDeviceInformationProvider
    std::string GetDynamicParamsContextName() const override;
    void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler) override;

private:
    ITV8::GDRV::IUnit* getOwnerObject();
    
    /// returns string, used as id parameter for driver object creation. NGP used id, that is 
    /// unique (for device) index of object with specified type. Driver as id parameter expects
    /// string like this "0.1.12", that list all indexes from root unit(device node) object to current unit(device node) 
    /// object, driver used this string for loging. createIdForDriver creates such string.
    std::string createIdForDriver()const;

private:
    SDeviceNodeParam                              m_settings;
    const std::string                             m_objectId;
    DeviceNode*                                   m_ownerDeviceNode;
    boost::shared_ptr<CParamContext>              m_context;

    boost::shared_ptr<NMMSS::IGrabberCallback>    m_callback;
    std::string                                   m_objectContext;
    NCorbaHelpers::WPContainerNamed               m_container;
    std::string                                   m_eventChannel;

    typedef boost::shared_ptr<ITV8::GDRV::IUnit>  IObjectPtr;
    IObjectPtr                                    m_object;

    NCorbaHelpers::PReactor                       m_reactor;
    TimeCorrectorUP                               m_eventTimeCorrector;
    NMMSS::PDetectorEventFactory                  m_factory;
    boost::shared_ptr<IDeviceNodeEventFactory>    m_eventFactory;

    typedef boost::shared_ptr<ITimedEventRaiser> TTimedEventRaiser;

    typedef std::map<std::string, TTimedEventRaiser> TEventRaisers;
    typedef TEventRaisers::iterator TEventRaisersIterator;
    TEventRaisers        m_timedEventRaisers;
    boost::mutex        m_raiserMutex;

    typedef std::map<std::string, boost::posix_time::ptime> TEventTimes;
    typedef TEventTimes::iterator TEventTimesIterator;
    TEventTimes            m_eventStartTimes;
    boost::mutex        m_timeMutex;

private: //operation support
    struct OperationDestroyer
    {
        void operator()(ITV8::GDRV::IUnitOperation* instance) const
        {
            ITV8::IDestroyable* p = ITV8::contract_cast<ITV8::IDestroyable>(instance);
            if (p)
                p->Destroy();
        }
    };
    typedef std::unique_ptr<ITV8::GDRV::IUnitOperation, OperationDestroyer> OperationSP;

    struct operationDef
    {
        OperationSP            object;
        std::string            operation;
        boost::uuids::uuid  id;
    };
    typedef std::map<ITV8::IContract*, operationDef> operations_t;

    NCorbaHelpers::PResource        m_operationServant;
    operations_t                    m_operations;
    boost::recursive_mutex            m_operationMutex;
    boost::condition_variable_any    m_operationCond;

    void initOperationsSupport();
    void releaseOperationsSupport();
    
    //IDeviceNodeOperationProcessor
    bool StartOperation(const std::string& operation, const boost::uuids::uuid& id) override;

    //ITV8::IAsyncActionHandler
    void Finished(IContract* source, ITV8::hresult_t code) override;
};

}

#endif // DEVICEIPINT3_DEVICE_NODE_H