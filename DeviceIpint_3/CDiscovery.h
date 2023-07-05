#ifndef DEVICEIPINT3_CDISCOVERY_H
#define DEVICEIPINT3_CDISCOVERY_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <Discovery_IDL/DiscoveryS.h>
#include <Discovery_IDL/DiscoveryBase.h>

#include <DeviceManager/IDeviceManager.h>
#include <CorbaHelpers/Reactor.h>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/asio/deadline_timer.hpp>

namespace ITV8
{
struct IEventHandler;
}

namespace IPINT30
{
extern const char PROP_DRIVER[];
extern const char PROP_DRIVER_VERSION[];
extern const char PROP_MAC_ADDRESS[];
extern const char PROP_IP_ADDRESS[];
extern const char PROP_IP_PORT[];
extern const char PROP_VENDOR[];
extern const char PROP_MODEL[];
extern const char PROP_FIRMWARE_VERSION[];
extern const char PROP_WAN_ADDRESS[];
extern const char PROP_DEVICE_DESCRIPTION[];

typedef boost::shared_ptr<ITV8::IEventHandler> EventHandlerSP;
typedef std::list<EventHandlerSP> eventHandlerList_t;

class CIPINT30DiscoveryModule : public NDiscovery::IDiscoveryModule
{
public:
    CIPINT30DiscoveryModule(const char *globalObjectName, 
        NCorbaHelpers::IContainerNamed* container, std::wistream& isConfig);
    ~CIPINT30DiscoveryModule();

    virtual void StartDiscover(NDiscovery::IDiscoverHandler* handler);
    virtual void StartProbe(const char* criteria, NDiscovery::IProbeHandler* handler);
    virtual int AdviseTimeoutForNextDiscover();

private:
    DECLARE_LOGGER_HOLDER;
    // The number of seconds for search or autodetect operation.
    int m_timeout;
    // The number of seconds between search attempts.
    int m_interval;

    NCorbaHelpers::PReactor     m_reactor;
    boost::asio::deadline_timer m_deviceSearchTimer;
    boost::mutex                m_handlersGuard;
    eventHandlerList_t          m_eventHandlers;
};

}
#endif //DEVICEIPINT3_CDISCOVERY_H
