#ifndef MMTRANSPORT_H_
#define MMTRANSPORT_H_

#include <ace/OS.h>
#include <boost/function.hpp>
#include <boost/thread/future.hpp>

#include <CorbaHelpers/Container.h>
#include <MMIDL/MMEndpointC.h>
#include <MMIDL/MMStorageC.h>
#include "../MMSS.h"
#include "AbstractRpcClients.h"
#include "Exports.h"
#include "SourceFactory.h"


namespace NMMSS
{

    typedef boost::function1<void, PortableServer::Servant> FSourceEndpointDtor;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /// —оздать endpoint дл€ указанного источника.
    /// @param pContainerDestroy - контейнер, из которого ep будет удален при вызове Destroy(). ћожет быть 0,
    /// в этом случае вызов Destroy() игнорируетс€.
    MMTRANSPORT_DECLSPEC PortableServer::Servant CreatePullSourceEndpoint(DECLARE_LOGGER_ARG,
        NCorbaHelpers::IContainer* container,
        ISourceFactory* pSourceFactory,
        FSourceEndpointDtor onDestroy=0);

    /// —оздать endpoint дл€ указанного источника.
    MMTRANSPORT_DECLSPEC PortableServer::Servant CreateSeekableSourceEndpoint(DECLARE_LOGGER_ARG,
        NCorbaHelpers::IContainer* container,
        ISeekableSource* pSourceImpl,
        FSourceEndpointDtor onDestroy=0);

    /// Create pull source based on data from other sources
    MMTRANSPORT_DECLSPEC NMMSS::ISeekableSource* CreatePlannedSequenceSource(DECLARE_LOGGER_ARG,
        const StorageSourcesList_t& storageSources,
        const std::string& beginTime,
        NMMSS::EEndpointStartPosition position,
        NMMSS::EPlayModeFlags mode);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    struct IEndpointFactory : public virtual NCorbaHelpers::IRefcounted
    {
        virtual MMSS::Endpoint* MakeEndpoint() = 0;
        virtual bool IsRemakePossible() const = 0;
        virtual boost::posix_time::seconds RemakeTimeout() const = 0;
    };
    typedef NCorbaHelpers::CAutoPtr<IEndpointFactory> PEndpointFactory;

    class ISinkEndpoint
        :   public virtual NCorbaHelpers::IWeakReferable
    {
    public:
        virtual void RequestQoS( MMSS::QualityOfService const& qos ) = 0;
        virtual void Destroy() = 0; // инициирует разрыв соединений.
    };
    typedef NCorbaHelpers::CAutoPtr<NMMSS::ISinkEndpoint> PSinkEndpoint;

    MMTRANSPORT_DECLSPEC IPullStyleSource* CreatePullSourceByObjref(DECLARE_LOGGER_ARG,
        MMSS::Endpoint*,
        MMSS::ENetworkTransport transport = MMSS::EAUTO,
        MMSS::QualityOfService const* qos = nullptr,
        EFrameBufferingPolicy bufferingPolicy = EFrameBufferingPolicy::Buffered);

    MMTRANSPORT_DECLSPEC ISinkEndpoint* CreatePullConnectionByObjref(DECLARE_LOGGER_ARG,
        MMSS::Endpoint*, NMMSS::IPullStyleSink*, 
        MMSS::ENetworkTransport transport = MMSS::EAUTO, 
        MMSS::QualityOfService const* qos = nullptr,
        EFrameBufferingPolicy bufferingPolicy = EFrameBufferingPolicy::Buffered);

    MMTRANSPORT_DECLSPEC ISinkEndpoint* CreatePullConnectionByNsref(DECLARE_LOGGER_ARG,
        const char*, CosNaming::NamingContextExt*, NMMSS::IPullStyleSink*,
        MMSS::ENetworkTransport transport = MMSS::EAUTO, MMSS::QualityOfService const* qos = nullptr,
        EFrameBufferingPolicy bufferingPolicy = EFrameBufferingPolicy::Buffered);

    MMTRANSPORT_DECLSPEC ISinkEndpoint* CreatePullConnectionByEpFactory(DECLARE_LOGGER_ARG,
        NMMSS::IEndpointFactory*, NMMSS::IPullStyleSink*, MMSS::ENetworkTransport transport = MMSS::EAUTO, MMSS::QualityOfService const* qos = nullptr,
        EFrameBufferingPolicy bufferingPolicy = EFrameBufferingPolicy::Buffered);


    MMTRANSPORT_DECLSPEC ISinkEndpoint* CreatePullConnectionByEndpointClient(DECLARE_LOGGER_ARG,
        PEndpointClient, NMMSS::IPullStyleSink*,
        MMSS::ENetworkTransport transport = MMSS::EAUTO, MMSS::QualityOfService const* qos = nullptr,
        EFrameBufferingPolicy bufferingPolicy = EFrameBufferingPolicy::Buffered);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class PSinkEndpoinConnectionWrapper
    {
    public:
        PSinkEndpoinConnectionWrapper() = default;
        PSinkEndpoinConnectionWrapper(const PSinkEndpoinConnectionWrapper&) = delete;
        PSinkEndpoinConnectionWrapper& operator = (const PSinkEndpoinConnectionWrapper&) = delete;

        explicit PSinkEndpoinConnectionWrapper(ISinkEndpoint* sinkEndpoint) :
            m_endpoint(sinkEndpoint)
        {
        }

        explicit PSinkEndpoinConnectionWrapper(PSinkEndpoint sinkEndpoint) : 
            m_endpoint(sinkEndpoint)
        {
        }

        ~PSinkEndpoinConnectionWrapper()
        {
            destroy();
        }

        void reset()
        {
            destroy();
        }

        void reset(PSinkEndpoint endpoint)
        {
            destroy();
            m_endpoint = endpoint;
        }

        void reset(ISinkEndpoint* endpoint)
        {
            reset(PSinkEndpoint(endpoint));
        }

        ISinkEndpoint* GetSinkEndpoint()
        {
            return m_endpoint.Get();
        }

        explicit operator bool() const
        {
            return m_endpoint.Get() != nullptr;
        }

    private:
        void destroy()
        {
            if (m_endpoint)
            {
                m_endpoint->Destroy();
                m_endpoint.Reset();
            }
        }

    private:
        PSinkEndpoint m_endpoint;
    };

    enum EServerStatus
    {
        SUCCESS,
        UNKNOWN_ERROR,
        BUSY_TRY_LATER,
        NOT_FOUND
    };

    class XServerError: public std::runtime_error
    {
    public:
        XServerError(EServerStatus status, const std::string& message):
            std::runtime_error(message),
            m_status(status)
        { }

        EServerStatus Status() const { return m_status; }

    private:
        EServerStatus m_status;
    };

}

#endif // MMTRANSPORT_H_