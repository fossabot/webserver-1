#include "AbstractRpcClients.h"

#include <CorbaHelpers/CorbaStl.h>

namespace
{
    struct CorbaEndpointClient : public virtual NMMSS::IEndpointClient
    {
        explicit CorbaEndpointClient(MMSS::Endpoint_var endpont) :
            m_endpont(endpont)
        {
            if (!endpont)
                throw std::runtime_error("Empty corba endpoint passed!");
        }

        virtual bool IsReconnectPossible() const override
        {
            return !m_connectedOnce;
        }

        virtual void RequestConnection(std::uint32_t pid, const std::string& hostId,
            const ::MMSS::NetworkTransportSeq& sinkPrefs,
            const ::MMSS::QualityOfService& qos,
            ::MMSS::ConnectionInfo_var& connInfo,
            std::string& cookie) override
        {
            m_connectedOnce = true;
            CORBA::String_var cc;
            m_endpont->RequestConnection(pid, hostId.c_str(), sinkPrefs, false, qos, connInfo.out(), cc.out());
            cookie = cc.in();
        }

        // throws, blocking call
        virtual void RequestQoS(const std::string& cookie, const ::MMSS::QualityOfService& qos) override
        {
            m_endpont->RequestQoS(cookie.c_str(), qos);
        }

        virtual void AsyncCreateMMTunnel(
            DECLARE_LOGGER_ARG,
            const ::MMSS::QualityOfService& qos,
            OnNetworkDisconnectHandler_t onDisconnect,
            OnInputSinkCreatedHandler_t onCreated) override
        {
            // Unsupported for corba.
            onCreated(NMMSS::PPullStyleSource());
        }

    private:
        MMSS::Endpoint_var m_endpont;
        mutable bool m_connectedOnce = false;
    };


    struct CorbaStorageEndpointClient : public NMMSS::IStorageEndpointClient, public CorbaEndpointClient
    { 
        explicit CorbaStorageEndpointClient(MMSS::StorageEndpoint_var endpoint)
            : CorbaEndpointClient(MMSS::Endpoint::_duplicate(endpoint.in()))
            , m_storageEndpoint(endpoint)
        {
            if (!endpoint)
                throw std::runtime_error("Empty corba storage endpoint passed!");
        }

        void Seek(const std::string& beginTime, NMMSS::EEndpointStartPosition startPos,
            std::int32_t mode, std::uint32_t sessionId)
        {
            static_assert(static_cast<int>(MMSS::EStartPosition::spAtKeyFrame) == static_cast<int>(NMMSS::EEndpointStartPosition::espAtKeyFrame), "Wrong enum mapping");
            static_assert(static_cast<int>(MMSS::EStartPosition::spAtKeyFrameOrAtEos) == static_cast<int>(NMMSS::EEndpointStartPosition::espAtKeyFrameOrAtEos), "Wrong enum mapping");
            m_storageEndpoint->Seek(beginTime.c_str(), static_cast<MMSS::EStartPosition>(startPos), mode, sessionId);
        }

    private:
        MMSS::StorageEndpoint_var m_storageEndpoint;
    };

    struct CorbaStorageSourceClient : public NMMSS::IStorageSourceClient
    {
        CorbaStorageSourceClient(MMSS::StorageSource_var source):
            m_source(source)
        {

        }

        virtual void GetHistory(const std::string& beginTime, const std::string& endTime,
            std::uint32_t maxCount, std::uint32_t minGap, IntervalList_t& intervals)
        {
            MMSS::StorageSource::IntervalSeq_var corbaIntervals;
            m_source->GetHistory(beginTime.c_str(), endTime.c_str(), maxCount, minGap, corbaIntervals.out());
            for (const auto& i : NCorbaHelpers::make_range(corbaIntervals.in()))
            {
                intervals.push_back({
                    static_cast<std::uint64_t>(i.beginTime),
                    static_cast<std::uint64_t>(i.endTime)
                });
            }
        }


        virtual NMMSS::PStorageEndpointClient GetSourceReaderEndpoint(
            const std::string& ptBeginTime,
            NMMSS::EEndpointStartPosition startPos,
            bool isRealtime,
            std::int32_t mode,
            EEndpointReaderPriority priority)
        {
            static_assert(static_cast<int>(MMSS::EEndpointReaderPriority::ERP_Low) == static_cast<int>(EEndpointReaderPriority::ERP_Low), "Wrong enum mapping");
            static_assert(static_cast<int>(MMSS::EEndpointReaderPriority::ERP_Mid) == static_cast<int>(EEndpointReaderPriority::ERP_Mid), "Wrong enum mapping");
            static_assert(static_cast<int>(MMSS::EEndpointReaderPriority::ERP_High) == static_cast<int>(EEndpointReaderPriority::ERP_High), "Wrong enum mapping");
            MMSS::StorageEndpoint_var endpoint = m_source->GetSourceReaderEndpoint(ptBeginTime.c_str(),
                static_cast<MMSS::EStartPosition>(startPos),
                isRealtime,
                mode,
                static_cast<MMSS::EEndpointReaderPriority>(priority));
            if (!endpoint)
                throw std::runtime_error("Source returned empty CORBA endpoint on GetSourceReaderEndpoint call");
            return std::make_shared<CorbaStorageEndpointClient>(endpoint);
        }

    private:
        MMSS::StorageSource_var m_source;
    };


    struct CorbaSinkEndpointClient : public NMMSS::ISinkEndpointClient
    {
        CorbaSinkEndpointClient(MMSS::SinkEndpoint_var sink) :
            m_sink(sink)
        {
            if (!sink)
                throw std::runtime_error("Empty corba endpoint passed!");
        }

        virtual int ConnectByObjectRef(MMSS::Endpoint_var objectRef, int priority) override
        {
            return m_sink->ConnectByObjectRef(objectRef, priority);
        }

        virtual void KeepAlive(int connectionHandle) override
        {
            m_sink->KeepAlive(connectionHandle);
        }

        virtual void Disconnect(int connectionHandle) override
        {
            m_sink->Disconnect(connectionHandle);
        }

        virtual int KeepAliveMilliseconds() override
        {
            return m_sink->KeepAliveMilliseconds();
        }

    private:
        MMSS::SinkEndpoint_var m_sink;
    };

}

namespace NMMSS
{
    PStorageSourceClient CreateCorbaStorageSourceClient(MMSS::StorageSource_var source)
    {
        return std::make_shared<CorbaStorageSourceClient>(source);
    }

    PStorageEndpointClient CreateCorbaStorageEndpointClient(MMSS::StorageEndpoint_var endpoint)
    {
        return std::make_shared<CorbaStorageEndpointClient>(endpoint);
    }

    NMMSS::PSinkEndpointClient CreateCorbaSinkEndpointClient(MMSS::SinkEndpoint_var sink)
    {
        return std::make_shared<CorbaSinkEndpointClient>(sink);
    }

}

