#ifndef DEVICEIPINT3_IPUSHSTYLESOURCEIMPL_H
#define DEVICEIPINT3_IPUSHSTYLESOURCEIMPL_H

#include <memory>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

// for ITVSDKUTILES::CreateFrameFactory
#include "../ItvSdkUtil/ItvSdkUtil.h"
// for NMMSS::SAllocatorRequirements
#include "../ConnectionBroker.h"
#include "../PullStylePinsBaseImpl.h"
#include "../SampleAdvisor.h"

//declares NMMSS::IPushStyleSource
#include "../MMSS.h"
#include "../MediaType.h"
#include "../FpsCounter.h"
#include "../Grabber/SampleTimeCorrector.h"
#include "../MMTransport/SourceFactory.h"
#include "../MMTransport/StatisticsCollectorImpl.h"

#include <CorbaHelpers/Reactor.h>
#include <CorbaHelpers/Trader.h>
#include <CommonNotificationCpp/StatisticsAggregator.h>

namespace IPINT30
{

class CSourceImpl 
    : public NMMSS::CPullStyleSourceBasePureRefcounted
    , public virtual NLogging::WithLogger
    , public NMMSS::IStatisticsProvider
    , public NMMSS::IConsumerConnectionReactor
{
protected:
    CSourceImpl(DECLARE_LOGGER_ARG,
                const std::string& federalName,
                NCorbaHelpers::IContainerNamed* container,
                bool breakUnusedConnection = false)
        : NLogging::WithLogger(GET_LOGGER_PTR)
        , m_reactOnRegistrations(breakUnusedConnection)
        , m_blockApplyFromStart(false)
        , m_itv8logger(ITVSDKUTILES::CreateLogger(GET_LOGGER_PTR, ""))
        , m_reactor(NCorbaHelpers::GetReactorInstanceShared())
        , m_statisticsCollector(NMMSS::CreateStatisticsCollector(federalName, NStatisticsAggregator::GetStatisticsAggregatorImpl(container->GetParentContainer()), true))
        , m_time2SampleHeaderAdaptor(DEFAULT_SAMPLE_DURATION)
        , m_loggingFrameCounter(0)
        , m_registrationCount(0)
        , m_lastStreamType(0)
    {
    }

    void PushToSink(NMMSS::PSample sample, bool discontinue)
    {
        if (!sample)
        {
            return;
        }

        if (discontinue)
        {
            sample->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
        }

        Push(sample);
    }

    void OnConsumerConnected() override
    {
        auto sendSinkConnected = false;
        {
            boost::mutex::scoped_lock lock(mutex());
            ++m_registrationCount;
            sendSinkConnected = (m_registrationCount == 1 && m_reactOnRegistrations);
        }

        if (sendSinkConnected)
        {
            m_isConsumersDisconnected = false;
            SetSinkConnected(true);
        }   
    }

    void OnConsumerDisconnected() override
    {
        auto sendSinkDisconnected = false;
        {
            boost::mutex::scoped_lock lock(mutex());
            --m_registrationCount;
            sendSinkDisconnected = (m_registrationCount == 0 && m_reactOnRegistrations);
        }

        if (sendSinkDisconnected)
        {
            m_isConsumersDisconnected = true;
            SetSinkConnected(false);
        }            
    }

    void onConnected(boost::mutex::scoped_lock& lock)
    {
        NMMSS::PAllocator pAllocator(getAllocator(lock));
        m_mmFactory = ITVSDKUTILES::CreateFrameFactory(GET_LOGGER_PTR, pAllocator.Get(),
            (ToString() + "FrameFactory").c_str());
        lock.unlock();

        if (!m_reactOnRegistrations)
            SetSinkConnected(true);
    }

    void onDisconnected(boost::mutex::scoped_lock&)
    {
        if (!m_reactOnRegistrations)
            SetSinkConnected(false);
    }

    virtual void SetSinkConnected(bool) = 0;
    ITV8::ILogger* GetLogger() { return m_itv8logger.get(); }
    virtual std::string ToString() const = 0;

    void Push(NMMSS::PSample sample)
    {
        m_statisticsCollector->Update(sample.Get());

        if (!sample)
            return;

        if (m_lastStreamType != sample->Header().nSubtype)
        {
            m_lastStreamType = sample->Header().nSubtype;
            if (m_streamTypeChangedCb)
                m_streamTypeChangedCb();
        }

        if (m_sampleTimeCorrector)
        {
            ++m_loggingFrameCounter;

            uint64_t timeBeforeCorrection = sample->Header().dtTimeBegin;
            m_time2SampleHeaderAdaptor.CorrectSampleTime(sample->Header(), m_sampleTimeCorrector);
            uint64_t timeAfterCorrection = sample->Header().dtTimeBegin;
            
            if (m_loggingFrameCounter % 1000 == 1)
            {
                _log_ << "[" << ToString() << "] TimeCorrector has changed sample time"
                    << " from " << boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(timeBeforeCorrection))
                    << " to " << boost::posix_time::to_iso_string(NMMSS::PtimeFromQword(timeAfterCorrection));
            }
        }

        boost::mutex::scoped_lock lock(mutex());
        sendSample(lock, sample.Get(), true);
    }

    ITV8::MFF::IMultimediaFrameFactory* getMultimediaFrameFactory()
    {
        return m_mmFactory.get();
    }

    void SetTimeCorrector(TimeCorrectorUP&& sampleTimeCorrector)
    {
        m_sampleTimeCorrector = std::move(sampleTimeCorrector);
    }

    void setStreamTypeChangedCallback(std::function<void()> cb)
    {
        m_streamTypeChangedCb = cb;
    }

    const bool m_reactOnRegistrations;
    std::atomic_flag m_applyFromStart;
    std::atomic_bool m_blockApplyFromStart;
private:
    virtual const NMMSS::IStatisticsCollector* GetStatisticsCollector() const
    {
        return m_statisticsCollector.get();
    }

    ITVSDKUTILES::ILoggerPtr m_itv8logger;
    NCorbaHelpers::PReactor m_reactor;
    boost::shared_ptr<ITV8::MFF::IMultimediaFrameFactory> m_mmFactory;
    std::unique_ptr<NMMSS::IStatisticsCollectorImpl> m_statisticsCollector;

    CTime2SampleHeaderAdaptor m_time2SampleHeaderAdaptor;
    TimeCorrectorUP m_sampleTimeCorrector;
    int m_loggingFrameCounter;
    uint64_t m_registrationCount;
    uint32_t m_lastStreamType;
    std::function<void()> m_streamTypeChangedCb;
};

}//namespace IPINT30

#endif //DEVICEIPINT3_IPUSHSTYLESOURCEIMPL_H
