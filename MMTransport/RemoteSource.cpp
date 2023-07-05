#include <ace/OS.h>
#include <string>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/reverse_lock.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <CorbaHelpers/Container.h>
#include <CorbaHelpers/RefcountedImpl.h>
#include "../PtimeFromQword.h"
#include "../MMSS.h"
#include "../MediaType.h"
#include "../ConnectionBroker.h"
#include "MMTransport.h"


namespace
{
    class CRemoteSource
        : public virtual NMMSS::IPullStyleSource
        , public virtual NCorbaHelpers::CWeakReferableImpl
    {
        class CSinkProxy : public NMMSS::IPullStyleSink, public virtual NCorbaHelpers::COwnerRefcountedImpl
        {
            CRemoteSource*          m_owner;
            unsigned int            m_debt;
            NMMSS::PPullStyleSource m_source;
            boost::mutex            m_mutex;

        public:

            CSinkProxy(CRemoteSource* owner)
                : NCorbaHelpers::COwnerRefcountedImpl(owner)
                , m_owner(owner)
                , m_debt(0)
            {
            }

            NMMSS::SAllocatorRequirements GetAllocatorRequirements() override
            {
                return NMMSS::SAllocatorRequirements(0);
            }

            void OnConnected(TConnection* connection) override
            {
                if (connection)
                {
                    boost::mutex::scoped_lock lock(m_mutex);

                    m_source = NMMSS::PPullStyleSource(connection->GetOtherSide(), NCorbaHelpers::ShareOwnership());
                    NMMSS::PPullStyleSource source = m_source;

                    if (source && m_debt)
                    {
                        unsigned int count = m_debt;
                        lock.unlock();

                        source->Request(count);
                    }
                }
            }

            void OnDisconnected(TConnection*) override
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_source.Reset();
            }

            void Receive(NMMSS::ISample* sample) override
            {
                boost::mutex::scoped_lock lock(m_mutex);
                if (m_debt)
                {
                    --m_debt;
                    lock.unlock();

                    m_owner->OnReceive(sample);
                } 
            }

            void DoRequest(unsigned int count)
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_debt += count;
                NMMSS::PPullStyleSource source = m_source;
                if (source)
                {
                    lock.unlock();
                    source->Request(count);
                }
            }

            void DropDebt()
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_debt = 0;
            }
        };

    protected:

        void OnReceive(NMMSS::ISample* sample)
        {
            boost::mutex::scoped_lock lock(m_mutex);
            NMMSS::PPullStyleSink sink = m_sink;
            if (sink)
            {
                lock.unlock();
                sink->Receive(sample);
            }
        }

    public:

        CRemoteSource(DECLARE_LOGGER_ARG, MMSS::Endpoint* endpoint, MMSS::ENetworkTransport transport, MMSS::QualityOfService const* qos,  NMMSS::EFrameBufferingPolicy bufferingPolicy)
            : m_proxy(this)
            , m_endpoint(NMMSS::CreatePullConnectionByObjref(GET_LOGGER_PTR, endpoint, &m_proxy, transport, qos, bufferingPolicy))
        {
            INIT_LOGGER_HOLDER;
        }

        NMMSS::SAllocatorRequirements GetAllocatorRequirements() override
        {
            return NMMSS::SAllocatorRequirements(0);
        }

        void OnConnected(TConnection* connection) override
        {
            if (connection)
            {
                boost::mutex::scoped_lock lock(m_mutex);
                m_sink = NMMSS::PPullStyleSink(connection->GetOtherSide(), NCorbaHelpers::ShareOwnership());
            }
        }

        void OnDisconnected(TConnection*) override 
        {
            m_proxy.DropDebt();

            boost::mutex::scoped_lock lock(m_mutex);
            m_sink.Reset();
        }

        void Request(unsigned int count) override
        {
            m_proxy.DoRequest(count);
        }

    private:

        DECLARE_LOGGER_HOLDER;

        CSinkProxy            m_proxy;
        NMMSS::PSinkEndpoint  m_endpoint;
        NMMSS::PPullStyleSink m_sink;
        boost::mutex          m_mutex;
    };
}

namespace NMMSS
{
    IPullStyleSource* CreatePullSourceByObjref(DECLARE_LOGGER_ARG, MMSS::Endpoint* endpoint, MMSS::ENetworkTransport transport, MMSS::QualityOfService const* qos, EFrameBufferingPolicy bufferingPolicy)
    {
        return new CRemoteSource(GET_LOGGER_PTR, endpoint, transport, qos, bufferingPolicy);
    }
}
