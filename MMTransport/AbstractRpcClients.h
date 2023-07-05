#pragma once

#include <string>
#include <functional>
#include <CorbaHelpers/Container.h>
#include <MMIDL/MMEndpointC.h>
#include <MMIDL/MMStorageC.h>
#include <MMIDL/SinkEndpointC.h>
#include "../MMSS.h"
#include "Exports.h"

namespace NMMSS
{
    // TODO: remove CORBA types
    struct IEndpointClient
    {
        virtual ~IEndpointClient() {}

        virtual bool IsReconnectPossible() const = 0;

        virtual void RequestConnection(std::uint32_t pid, const std::string& hostId,
            const ::MMSS::NetworkTransportSeq& sinkPrefs,
            const ::MMSS::QualityOfService& qos,
            ::MMSS::ConnectionInfo_var& connInfo,
            std::string& cookie) = 0;


        using OnNetworkDisconnectHandler_t = std::function<void()>;
        // Returns empty source on errors.
        using OnInputSinkCreatedHandler_t = std::function<void(PPullStyleSource source)>;

        virtual void AsyncCreateMMTunnel(
            DECLARE_LOGGER_ARG,
            const ::MMSS::QualityOfService& qos,
            OnNetworkDisconnectHandler_t onDisconnect,
            OnInputSinkCreatedHandler_t onCreated) = 0;

        // throws, blocking call
        virtual void RequestQoS(const std::string& cookie, const ::MMSS::QualityOfService& qos) = 0;
    };

    using PEndpointClient = std::shared_ptr<IEndpointClient>;

    // Please keep in sync with  CORBA (MMSS::EEndpointReaderPriority) and grpc enum
    struct IStorageEndpointClient : public virtual IEndpointClient
    {
        virtual void Seek(const std::string& beginTime, EEndpointStartPosition startPos,
            std::int32_t mode, std::uint32_t sessionId) = 0;
    };

    using PStorageEndpointClient = std::shared_ptr<IStorageEndpointClient>;

    struct IStorageSourceClient
    {
        virtual ~IStorageSourceClient() {}

        struct Interval
        {
            std::uint64_t beginTime;
            std::uint64_t endTime;
        };
        using IntervalList_t = std::vector<Interval>;

        virtual void GetHistory(const std::string& beginTime, const std::string& endTime,
            std::uint32_t maxCount, std::uint32_t minGap, IntervalList_t& intervals) = 0;

        

        // Please keep in sync with  CORBA and grpc enum
        enum EEndpointReaderPriority
        {
            ERP_Low = 0,
            ERP_Mid,
            ERP_High
        };

        virtual PStorageEndpointClient GetSourceReaderEndpoint(
            const std::string& ptBeginTime,
            EEndpointStartPosition startPos,
            bool isRealtime,
            std::int32_t mode,
            EEndpointReaderPriority priority) = 0;
    };

    using PStorageSourceClient = std::shared_ptr<IStorageSourceClient>;
    using StorageSourcesList_t = std::vector<PStorageSourceClient>;

    MMTRANSPORT_CLASS_DECLSPEC
    PStorageSourceClient CreateCorbaStorageSourceClient(MMSS::StorageSource_var source);

    MMTRANSPORT_CLASS_DECLSPEC
    PStorageEndpointClient CreateCorbaStorageEndpointClient(MMSS::StorageEndpoint_var source);

    // Abstracts Corba interface MMSS::SinkEndpoint
    struct ISinkEndpointClient
    {
        virtual ~ISinkEndpointClient(){};

        virtual int ConnectByObjectRef(MMSS::Endpoint_var objectRef, int priority) = 0;
        virtual void KeepAlive(int connectionHandle) = 0;
        virtual void Disconnect(int connectionHandle) = 0;
        virtual int KeepAliveMilliseconds() = 0;
    };
    using PSinkEndpointClient = std::shared_ptr<ISinkEndpointClient>;
    
    MMTRANSPORT_CLASS_DECLSPEC
    PSinkEndpointClient CreateCorbaSinkEndpointClient(MMSS::SinkEndpoint_var sink);
}

