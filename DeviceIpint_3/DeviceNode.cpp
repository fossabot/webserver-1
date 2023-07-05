#include <ItvSdk/include/IErrorService.h>

#include "DeviceNode.h"

#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>

#include "../ItvSdkUtil/ItvSdkUtil.h"

#include <MMIDL/DeviceNodeS.h>

#include <ORM_IDL/ORMC.h>

#include "CIpInt30.h"

#include "Notify.h"
#include "Utility.h"
#include "TimeStampHelpers.h"

namespace IPINT30
{

/////////////////////////////////////////////////////////////////////////////////////
namespace
{

const boost::posix_time::time_duration EVENT_DURATION = boost::posix_time::milliseconds(3000);
const boost::posix_time::time_duration OPERATION_STOP_TIMEOUT(boost::posix_time::seconds(60));

}

/////////////////////////////////////////////////////////////////////////////////////
DeviceNode::DeviceNode(DECLARE_LOGGER_ARG,
            NExecutors::PDynamicThreadPool dynExec, 
            boost::shared_ptr<IPINT30::IIpintDevice> parent,
            const SDeviceNodeParam& settings,
            boost::shared_ptr<NMMSS::IGrabberCallback> callback,
            const char* objectContext,
            NCorbaHelpers::IContainerNamed* container,
            const char* eventChannel,
            DeviceNode* ownerDeviceNode)
    : WithLogger(GET_LOGGER_PTR)
    , base_t(GET_LOGGER_PTR, dynExec, parent, nullptr)
    , m_settings(settings)
    , m_objectId(createObjectId(settings.deviceNodeId, settings.id))
    , m_ownerDeviceNode(ownerDeviceNode)
    , m_context(new CParamContext)
    , m_callback(callback)
    , m_objectContext(objectContext)
    , m_container(container)
    , m_eventChannel(eventChannel)
    , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
    , m_eventTimeCorrector(CreateTimeCorrector(true, PTimeCorrectorSyncPoint()))
{
    assert(m_parent);

    SetFlag(cfSinkConnected, true);

    std::string contextID = DEVICE_NODE_CONTEXT_NAME_PREFIX + getObjectId();
    m_context->AddContext(contextID.c_str(), MakeContext(m_settings));
}

std::string DeviceNode::ToString() const
{
    std::ostringstream str;
    str << m_objectContext 
        << "\\."
        << DEVICE_NODE_CONTEXT_NAME_PREFIX
        << getObjectId();
    return str.str();
}

ITV8::GDRV::IUnit* DeviceNode::getOwnerObject()
{
    if (!m_ownerDeviceNode)
        return nullptr;
    return m_ownerDeviceNode->m_object.get();
}

std::string DeviceNode::createIdForDriver()const
{
    std::string result;
    result = std::to_string(getId());
    const DeviceNode* p = m_ownerDeviceNode;
    while (p != nullptr)
    {
        result = std::to_string(p->getId()) + "." + result;
        p = p->m_ownerDeviceNode;
    }
    return result;
}

namespace
{
static const std::string emptyObjectId;
}
const std::string& DeviceNode::getOwnerObjectId() const
{
    return m_ownerDeviceNode ? m_ownerDeviceNode->m_objectId : emptyObjectId;
}

bool DeviceNode::parseObjectId(const std::string& objectId, std::string& deviceNodeId, unsigned int& id)
{
    auto pos = objectId.find('.');
    if (pos == std::string::npos)
        return false;
    deviceNodeId = objectId.substr(0, pos);
    id = boost::lexical_cast<unsigned int>(objectId.substr(pos + 1));
    return true;
}

ITV8::Analytics::IDetectorEventRaiser* DeviceNode::BeginOccasionalEventRaising(
                                                ITV8::Analytics::IDetector* sender, 
                                                const char* name, 
                                                ITV8::timestamp_t time, 
                                                ITV8::uint32_t phase)
{
    m_eventTimeCorrector->SetTimestamps(ipintTimestampToPtime(time), 
        [&time, this](bpt::ptime time_, bool)
        {
            _dbg_ << "DeviceNode::BeginOccasionalEventRaising, event time: "
                << ipintTimestampToIsoString(time) << " -> " << boost::posix_time::to_iso_string(time_);
            time = toIpintTime(time_);
        });

    if (!m_eventFactory)
    {
        throw std::runtime_error((boost::format("m_eventFactory for %1% was not initialized.")
            % ToString()).str());
    }

    if (ITV8::Analytics::esEnd == phase)
    {
        boost::posix_time::ptime eventStartTime = boost::posix_time::min_date_time;
        {
            boost::mutex::scoped_lock timeLock(m_timeMutex);
            TEventTimesIterator it = m_eventStartTimes.find(name);
            if (m_eventStartTimes.end() != it)
                eventStartTime = it->second;
        }

        boost::posix_time::time_duration td = boost::posix_time::microsec_clock::universal_time() - eventStartTime;
        if (EVENT_DURATION > td)
        {
            boost::mutex::scoped_lock raiserLock(m_raiserMutex);
            TEventRaisersIterator it = m_timedEventRaisers.find(name);
            if (m_timedEventRaisers.end() == it)
            {
                TTimedEventRaiser ter(m_eventFactory->BeginTimedEventRaising(sender, name, time, phase, m_reactor->GetIO()));
                m_timedEventRaisers.insert(std::make_pair(name, ter));
                ter->SetCommitTime(EVENT_DURATION - td);
                return ter.get();
            }
            else if (it->second->DelayCommit(EVENT_DURATION - td))
            {
                return m_eventFactory->BeginNoOpEventRaising(name, time);
            }
            return it->second.get();
        }
        else
        {
            {
                boost::mutex::scoped_lock raiserLock(m_raiserMutex);
                TEventRaisersIterator it = m_timedEventRaisers.find(name);
                if ((m_timedEventRaisers.end() != it) && (nullptr != it->second.get()))
                {
                    it->second->Stop();
                    m_timedEventRaisers.erase(it);
                    return m_eventFactory->BeginNoOpEventRaising(name, time);
                }
            }
            return m_eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
        }
    }
    else if (ITV8::Analytics::esBegin == phase)
    {
        {
            boost::mutex::scoped_lock timeLock(m_timeMutex);
            m_eventStartTimes[name] = boost::posix_time::microsec_clock::universal_time();
        }
        {
            boost::mutex::scoped_lock raiserLock(m_raiserMutex);
            TEventRaisersIterator it = m_timedEventRaisers.find(name);
            if (m_timedEventRaisers.end() != it)
            {
                if ((nullptr != it->second.get()) && it->second->Prolongate())
                {
                    return m_eventFactory->BeginNoOpEventRaising(name, time);
                }
                m_timedEventRaisers.erase(it);
            }
        }
        return m_eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
    }
    // esMomentary?
    return m_eventFactory->BeginOccasionalEventRaising(sender, name, time, phase);
}

ITV8::Analytics::IDetectorEventRaiser* DeviceNode::BeginPeriodicalEventRaising(
    ITV8::Analytics::IDetector* sender, const char* metadataType, ITV8::timestamp_t time)
{
    m_eventTimeCorrector->SetTimestamps(ipintTimestampToPtime(time), [&time, this](bpt::ptime time_, bool)
    {
        _dbg_ << "DeviceNode::BeginPeriodicalEventRaising, event time: "
            << ipintTimestampToIsoString(time) << " -> " << boost::posix_time::to_iso_string(time_);
        time = toIpintTime(time_);
    });
    if (!m_eventFactory)
    {
        throw std::runtime_error((boost::format("m_eventFactory for %1% was not initialized.")
            % ToString()).str());
    }
    return m_eventFactory->BeginPeriodicalEventRaising(sender, metadataType, time);
}

void DeviceNode::Failed(ITV8::IContract* source, ITV8::hresult_t error)
{
    TRACE_BLOCK;
    std::string message = get_last_error_message(source, error);
    _err_ << " Failed. It's unexpected event, err:" << message << std::endl;

    NMMSS::EIpDeviceState st;
    switch (error)
    {
    case ITV8::EAuthorizationFailed:
        st = NMMSS::IPDS_AuthorizationFailed;
        break;
    case ITV8::EDeviceReboot:
        st = NMMSS::IPDS_Rebooted;
        break;
    case ITV8::EHostUnreachable:
    case ITV8::ENetworkDown:
        st = NMMSS::IPDS_NetworkFailure;
        break;
    case ITV8::EGeneralConnectionError:
        st = NMMSS::IPDS_ConnectionError;
        break;
    default:
        st = NMMSS::IPDS_IpintInternalFailure;
        break;
    }

    this->Notify(st);
}

//----------------------------------------------------------------------------------------------------------
void DeviceNode::DoStart()
{
    TRACE_BLOCK;
    _dbg_ << "DeviceNode::DoStart for " << ToString() << std::endl;

    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
        return;

    // Reset previous instance of driver object because it can reference to invalid m_eventFactory.
    m_object.reset();
    ITV8::GDRV::IDevice *device = nullptr;
    try
    {
        device = getDevice();
    }
    catch (const std::runtime_error& e)
    {
        _err_ << " Exception: Couldn't getDevice(). msg=" << e.what() << std::endl;
        return;
    }

    try
    {
        const std::string accessPoint(m_objectContext + "/EventSupplier." + getObjectId());
        const std::string sourceOrigin(m_objectContext + "/" + getObjectId());
        m_factory = NMMSS::CreateDetectorEventFactory(accessPoint.c_str(), sourceOrigin.c_str(),
            cont.Get(), m_eventChannel.c_str(), getObjectId().c_str());
        m_eventFactory =
            ITVSDKUTILES::CreateDeviceNodeEventFactory(GET_THIS_LOGGER_PTR, m_factory, ToString().c_str());

        initOperationsSupport();

        m_object = IObjectPtr(device->CreateUnit(this,
                                                getOwnerObject(), 
                                                getDeviceNodeId().c_str(),
                                                createIdForDriver().c_str()),
                              ipint_destroyer<ITV8::GDRV::IUnit>());

        if (!m_object.get())
        {
            std::string message = get_last_error_message(device);
            _err_ << "Exception: Device Node was not created. msg="
                << message << std::endl;
            return;
        }
    }
    catch (const CORBA::Exception& ex)
    {
        _err_ << "Corba Exception occured on CreateDetectorEventFactory " << ex
            << ". Object:" << ToString();
        return;
    }
    catch (const std::exception& e)
    {
        _err_ << " std::exception: occured on CreateDetectorEventFactory. msg=" << e.what()
            << ". Object:" << ToString();
        return;
    }

    ApplyChanges(nullptr);

    m_object->Start();

    CChannel::DoStart();
}

void DeviceNode::OnStarted()
{
    TRACE_BLOCK;
    _dbg_ << "DeviceNode::OnStarted for " << ToString() << std::endl;
    base_t::OnStarted();
}

void DeviceNode::DoStop()
{
    TRACE_BLOCK;
    _dbg_ << "DeviceNode::DoStop for " << ToString() << std::endl;

    IObjectPtr object(m_object);
    if (nullptr != object.get())
    {
        WaitForApply();
        object->Stop();
    }
    else
    {
        // Just signals that channel stopped.
        CChannel::RaiseChannelStopped();
    }
}

void DeviceNode::OnStopped()
{
    TRACE_BLOCK;
    _dbg_ << "DeviceNode::OnStopped for " << ToString() << std::endl;
    base_t::OnStopped();

    try
    {
        TEventRaisersIterator it1 = m_timedEventRaisers.begin(), it2 = m_timedEventRaisers.end();
        for (; it1 != it2; ++it1)
            it1->second->Stop();
        m_timedEventRaisers.clear();
        m_eventFactory.reset();
        releaseOperationsSupport();
    }
    catch (const CORBA::Exception &e)
    {
        _log_ << "CORBA exception occurred (" << e._name() << ") during m_eventFactory.reset()"
            << std::endl;
    }
    m_object.reset();
}

void DeviceNode::OnFinalized()
{
    TRACE_BLOCK;
    
    SetEnabled(false);

    _dbg_ << "DeviceNode::OnFinalized for " << ToString() << std::endl;
}

void DeviceNode::OnEnabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    this->SetNotifier(new NotifyStateImpl(GET_LOGGER_PTR, m_callback,
        NStatisticsAggregator::GetStatisticsAggregatorImpl(m_container),
        ToString()));
}

void DeviceNode::OnDisabled()
{
    boost::mutex::scoped_lock lock(m_switchMutex);
    this->SetNotifier(nullptr);
}

//----------------------------------------------------------------------------------------------------------
void DeviceNode::SwitchEnabled()
{
    SetEnabled(m_settings.enabled);
}

void DeviceNode::ApplyChanges(ITV8::IAsyncActionHandler* handler)
{
    TRACE_BLOCK;
    if ((cfEnvironmentReady == (m_flags & cfEnvironmentReady)) && (nullptr != m_object.get()))
    {
        auto asyncAj = ITV8::contract_cast<ITV8::IAsyncAdjuster>(m_object.get());
        if (asyncAj)
        {
            SetValues(asyncAj, m_settings.publicParams);

            //Applies new settings.
            _dbg_ << "DeviceNode::ApplyChanges for " << ToString()<< ", handler=" << handler << " : " << static_cast<ITV8::IAsyncActionHandler*>(this);
            ApplySettings(asyncAj, WrapAsyncActionHandler(static_cast<ITV8::IAsyncActionHandler*>(this), handler));
        }
        else
        {
            _wrn_ << ToString() << " couldn't cast to ITV8::IAsyncAdjuster. Applying changes skipped." << std::endl;
        }
    }
}

IParamContextIterator* DeviceNode::GetParamIterator()
{
    return m_context.get();
}

//----------------------------------------------------------------------------------------------------------
std::string DeviceNode::GetDynamicParamsContextName() const
{
    return DEVICE_NODE_CONTEXT_NAME_PREFIX + getObjectId();
}

void DeviceNode::AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler)
{
    if (!handler)
    {
        _log_ << ToString() << " IDynamicParametersHandler doesn't exist";
        return;
    }

    // If source is not active than we should not do any call to driver.
    if (!GetFlag(cfStarted))
    {
        handler->Failed(nullptr, ITV8::EGeneralError);
    }

    if (!m_object)
    {
        _log_ << ToString() << " video analytics doesn't exist";
        handler->Failed(nullptr, ITV8::EGeneralError);
        return;
    }

    auto provider = ITV8::contract_cast<ITV8::IDynamicParametersProvider>(m_object.get());
    if (provider)
    {
        return provider->AcquireDynamicParameters(handler);
    }
    _log_ << ToString() << " IDynamicParametersProvider is not supported";
    handler->Failed(m_object.get(), ITV8::EUnsupportedCommand);
}

bool DeviceNode::StartOperation(const std::string& operation, const boost::uuids::uuid& id)
{
    auto processor = ITV8::contract_cast<ITV8::GDRV::IOperationProcessor>(m_object.get());
    if (!processor)
    {
        _err_ << ToString() << " does not support operations. Operation "
              << operation << " will not be performed." << std::endl;
        return false;
    }

    auto object = processor->CreateOperation(operation.c_str());
    if (!object)
    {
        _err_ << ToString() << " device node couldn't start operation " << operation << std::endl;
        return false;
    }

    boost::recursive_mutex::scoped_lock lock(m_operationMutex);
    auto& it = m_operations[object];
    it.object.reset(object);
    it.id = id;
    it.operation = operation;
    lock.unlock();

    object->Execute(this);
    return true;
}

void DeviceNode::Finished(IContract* source, ITV8::hresult_t code)
{
    if (source == m_object.get())
    {
        // Handling ApplySettings result
        return base_t::Finished(source, code);
    }

    // Handling operation finished
    boost::recursive_mutex::scoped_lock lock(m_operationMutex);
    auto it = m_operations.find(source);
    if (it == m_operations.end())
    {
        _err_ << ToString() << " driver has called Finished for missed (may be already deleted operation) object" << std::endl;
        return;
    }

    boost::uuids::uuid id = it->second.id;
    std::string operation = it->second.operation;
    m_operations.erase(it);
    m_operationCond.notify_all();
    lock.unlock();

    m_eventFactory->NotifyDeviceNodeOperation(operation.c_str(), NCorbaHelpers::StringifyUUID(id).c_str(), static_cast<int32_t>(code));
}

class OperationEndpoint : public POA_Equipment::DeviceNodeOperation
                        , public PortableServer::RefCountServantBase
{
    IDeviceNodeOperationProcessor& m_deviceNode;
public:
    OperationEndpoint(IDeviceNodeOperationProcessor& deviceNode)
        : m_deviceNode(deviceNode)
    {
    }

    Notification::Guid StartOperation(const char* operationId) override
    {
        boost::uuids::uuid id = NCorbaHelpers::GenerateUUID();

        Notification::Guid guid = { 0 };
        if (!m_deviceNode.StartOperation(operationId, id))
            return guid;

        std::copy(id.data, id.data + sizeof(guid.value), guid.value);
        return guid;
    }
};

void DeviceNode::initOperationsSupport()
{
    NCorbaHelpers::PContainerNamed cont = m_container;
    if (!cont)
        return;

    const std::string accessPoint("operations");
    const std::string nsName(m_objectId + "/" + accessPoint);
    PortableServer::Servant operationServant(new OperationEndpoint(*this));
    m_operationServant.reset(NCorbaHelpers::ActivateServant(cont.Get(), operationServant, nsName.c_str()));
}

void DeviceNode::releaseOperationsSupport()
{
    {
        boost::recursive_mutex::scoped_lock lock(m_operationMutex);
        if (!m_operations.empty())
        {
            if (!m_operationCond.timed_wait(lock, OPERATION_STOP_TIMEOUT, [this]() { return m_operations.empty(); }))
            {
                _err_ << " At least " << m_operations.begin()->second.operation
                    << " operation is not finished yet. Crash is possible" << std::endl;
                m_operations.clear();
            }
        }
    }
    m_operationServant.reset();
}

}
