#ifndef DEVICEIPINT3_CTEXTEVENTSOURCE_H
#define DEVICEIPINT3_CTEXTEVENTSOURCE_H

#include <ace/OS.h>//To resolve conflicts with ETIME and other windows error defines.
#include <boost/shared_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <json/json.h>

#include <CorbaHelpers/RefcountedImpl.h>
#include <ItvDeviceSdk/include/ITextEventSource.h>

#include "../Distributor.h"
#include "../ConnectionResource.h"
#include "../Grabber/Grabber.h"
#include "../mmss/MediaType.h"

#include "PullStyleSourceImpl.h"
#include "AsyncActionHandler.h"
#include "IIPManager3.h"
#include "ParamContext.h"
#include "DeviceInformation.h"
#include "DeviceIpint_Exports.h"

namespace NCorbaHelpers
{
class CResourceSet;
}

namespace ORM
{
    struct JsonEvent;
}

namespace IPINT30
{

class CDevice;

class CTextEventSource :
    public CAsyncChannelHandlerImpl<ITV8::GDRV::ITextEventSourceHandler>,
    public CSourceImpl,
    public IObjectParamContext,
    public IDeviceInformationProvider
{
public:
    CTextEventSource(DECLARE_LOGGER_ARG, NExecutors::PDynamicThreadPool dynExec, boost::shared_ptr<IPINT30::IIpintDevice> parent,
                     const STextEventSourceParam& settings, boost::shared_ptr<NMMSS::IGrabberCallback> callback,
                     const char* objectContext, const char* eventChannel, NCorbaHelpers::IContainerNamed* container);

public:
    ITV8_BEGIN_CONTRACT_MAP()
        ITV8_CONTRACT_ENTRY2(ITV8::IContract, ITV8::GDRV::ITextEventSourceHandler)
        ITV8_CONTRACT_ENTRY(ITV8::GDRV::ITextEventSourceHandler)
    ITV8_END_CONTRACT_MAP()

    // Overrides public virtual methods of ITV8::GDRV::ITextEventSourceHandler class
public:
    virtual void Failed(ITV8::IContract* source, ITV8::hresult_t error);
    virtual void EventGroupBegan();
    virtual void EventData(ITV8::timestamp_t timestamp, const char* data);
    virtual void EventGroupEnded();

public:
    virtual void SwitchEnabled();
    virtual void ApplyChanges(ITV8::IAsyncActionHandler* handler);
    virtual IParamContextIterator* GetParamIterator();

    // Overrides public virtual methods of CChannel class
public:
    std::string ToString() const;

    // Overrides virtual methods of CChannel class
protected:
    virtual void DoStart();
    virtual void DoStop();

    virtual void OnStopped();
    virtual void OnFinalized();

    virtual void OnEnabled();
    virtual void OnDisabled();

    virtual void SetSinkConnected(bool conn)
    {
        CChannel::SetSinkConnected(conn);
    }
private:
    void Prepare();
    void setNotifier();
    void checkCounter();
    void resetCounter();
    void eventGroupEnded();
    void eventGroupBegan();
    void pushSamples(const std::vector<NMMSS::PSample>& samples);
    void pushSampleDelayed(NMMSS::PSample sample);

    void emitTextSample( const char* data, size_t dataSize
        , ITV8::timestamp_t timestampInt, std::string const& timestampStr
        , ITV8::timestamp_t timestampLast );
    bool emitTextEvent(ORM::JsonEvent& je, const char* payload, size_t size, Json::Value event = Json::Value{});

private:
    virtual std::string GetDynamicParamsContextName() const;
    virtual void AcquireDynamicParameters(ITV8::IDynamicParametersHandler* handler);

private:
    NCorbaHelpers::CAutoPtr<NCommonNotification::CEventSupplier> m_connector;

    STextEventSourceParam m_settings;
    typedef boost::shared_ptr<ITV8::GDRV::ITextEventSource> ITextEventSourcePtr;
    ITextEventSourcePtr m_eventSource;

    boost::shared_ptr<NMMSS::IGrabberCallback> m_callback;

    std::string m_objectContext;
    NCorbaHelpers::WPContainerNamed m_container;
    typedef boost::shared_ptr<NCorbaHelpers::IResource> PServant;
    typedef std::set<PServant> TServants;
    TServants m_servants;

    std::string m_channel, m_accessPoint;

    boost::shared_ptr<CParamContext> m_context;

    std::string m_sampleData;
    unsigned short m_subtitlesCounter = 0;

    boost::uuids::uuid m_groupUuid;
    Json::Value m_groupTuples;
    boost::posix_time::ptime m_groupBegins;
    boost::mutex m_mutex;

    NMMSS::PDistributor m_distributor;
    NMMSS::CConnectionResource m_distributorConnection;
    typedef std::set<NMMSS::PPullFilter> TFilters;
    TFilters m_filters;
    typedef std::set<NMMSS::IConnectionBase*> TConnections;
    TConnections m_connections;
    bool m_billsOnlyMode;
    bool m_billStarted;

    struct STimestampCounter
    {
        ITV8::timestamp_t Timestamp(const ITV8::timestamp_t& timestamp)
        {
            boost::mutex::scoped_lock lock(m_locker);
            bool timestampAlreadyUsed = false;
            short delta = 0;
            for (short i = 0; i <= m_repeatCounter; ++i)
            {
                if ((m_timestamp + i) == timestamp)
                {
                    timestampAlreadyUsed = true;
                    delta = i;
                    break;
                }
            }

            if (timestampAlreadyUsed)
                ++m_repeatCounter;
            else
            {
                m_timestamp = timestamp;
                m_repeatCounter = 0;
            }

            return timestamp + m_repeatCounter - delta;
        }

        ITV8::timestamp_t TimestampLast()
        {
            return m_timestamp;
        }

        STimestampCounter()
            : m_timestamp(0), m_repeatCounter(0)
        {}

    private:
        ITV8::timestamp_t m_timestamp;
        short m_repeatCounter;
        boost::mutex m_locker;
    };
    STimestampCounter m_timestampCounter;

    std::mutex m_sampleQueueGuard;
    std::vector<NMMSS::PSample> m_sampleQueue;
    std::shared_ptr<boost::asio::io_service> m_pushSampleService;
    std::unique_ptr<NExecutors::IReactor> m_pushSampleExecutor;
    boost::asio::deadline_timer m_pushSampleTimer;
};

DEVICEIPINT_DECLSPEC NMMSS::IFilter* CreateSubtitleFormatter(DECLARE_LOGGER_ARG, const STextFormat& format);

}//namespace IPINT30

#endif // DEVICEIPINT3_CTRANSACTIONSOURCE_H
