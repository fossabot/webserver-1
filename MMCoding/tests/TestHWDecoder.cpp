#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>

#include "tests/Samples.h"
#include "HWCodecs/HWDevicePool.h"
#include "HWCodecs/IHWDevice.h"
#include "HWCodecs/IHWDecoder.h"
#include "HWCodecs/HWUtils.h"
#include "FrameLagHandler.h"
#include "ConnectionResource.h"

#include <Logging/log2.h>

namespace
{

class MockHWDecoder : public IAsyncHWDecoder
{
public:
    MockHWDecoder(HWDeviceSP device) :
        m_device(device)
    {
        ++m_constructorCount;
    }

    void DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll) override
    {
        HWDecoderUtils::DecodeAndGetResult(*this, data, holder, preroll);
    }

    void Decode(const CompressedData& data, bool preroll) override
    {
        if (data.Ptr)
        {
            m_lagHandler.RegisterInputFrame(data.Timestamp, preroll);
        }
        m_giveResults |= (m_lagHandler.Count() > m_decoderLag || !data.Ptr);
    }

    bool GetDecodedSamples(NMMSS::CDeferredAllocSampleHolder* holder, bool waitForSample) override
    {
        sendSample(holder);
        bool hasDecoderResult = m_giveResults && m_lagHandler.Count();
        if (hasDecoderResult && m_lagHandler.RegisterOutputFrame())
        {
            m_sampleInProcess = NMMSS::PSample(new NMMSS::UnitTesting::Sample(m_lagHandler.LastTimestamp(), false));
        }
        if (waitForSample)
        {
            sendSample(holder);
        }
        return hasDecoderResult;
    }

    static void SetDecoderLag(int decoderLag)
    {
        m_decoderLag = decoderLag;
    }

    static int ConstructorCount()
    {
        return m_constructorCount;
    }

    bool IsValid() const override { return true; }
    void ReleaseSamples() override {}
    HWDeviceSP Device() const override { return m_device; }
    const VideoPerformanceInfo& GetPerformanceInfo(std::chrono::milliseconds recalc_for_period = std::chrono::milliseconds(0)) const override { return m_info; }

private:
    void sendSample(NMMSS::CDeferredAllocSampleHolder* holder)
    {
        if (m_sampleInProcess)
        {
            holder->AddSample(m_sampleInProcess);
            m_sampleInProcess.Reset();
        }
    }

private:
    HWDeviceSP m_device;
    VideoPerformanceInfo m_info;
    NMMSS::FrameLagHandler m_lagHandler;
    bool m_giveResults{};
    NMMSS::PSample m_sampleInProcess;
    static int m_decoderLag, m_constructorCount;
};

int MockHWDecoder::m_decoderLag{};
int MockHWDecoder::m_constructorCount{};

class MockHWDevice : public IHWDevice, public std::enable_shared_from_this<MockHWDevice>
{
public:
    IHWDecoderSP CreateDecoder(NLogging::ILogger* logger, VideoPerformanceInfo info, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) override
    {
        return std::make_shared<MockHWDecoder>(shared_from_this());
    }

    bool IsPrimary() const override { return true; }
    int GetPitch(int width) const override { return 0; }
    NMMSS::EHWDeviceType GetDeviceType() const override { return NMMSS::EHWDeviceType::Any; }
    int GetDeviceIndex() const override { return 0; }
    bool CanProcessOutput() const override { return true; }
};

void InitHWDevicePool()
{
    static bool Initialized{};
    if(!Initialized)
    {
        HWDevicePool::Instance()->SetExternalDevice(std::make_shared<MockHWDevice>());
        Initialized = true;
    }
}


class TestFixture : public NLogging::WithLogger
{
    using PSource = NCorbaHelpers::CAutoPtr<NMMSS::UnitTesting::Source>;
    using PSink = NCorbaHelpers::CAutoPtr<NMMSS::UnitTesting::Sink>;

public:
    TestFixture() :
        WithLogger(NLogging::PLogger(NLogging::CreateConsoleLogger(NLogging::LEVEL_DEBUG)).Get()),
        Result(GET_LOGGER_PTR)
    {
        InitHWDevicePool();

        NMMSS::PPullFilter decoder(NMMSS::CreateHardwareAcceleratedVideoDecoderPullFilter(GET_LOGGER_PTR, {}, {}));
        Source = new NMMSS::UnitTesting::Source();
        PSink sink(new NMMSS::UnitTesting::Sink(GET_LOGGER_PTR, Result));
      
        m_connections.emplace_back(NMMSS::CConnectionResource(Source.Get(), decoder->GetSink(), GET_LOGGER_PTR));
        m_connections.emplace_back(NMMSS::CConnectionResource(decoder->GetSource(), sink.Get(), GET_LOGGER_PTR));
    }

private:
    std::vector<NMMSS::CConnectionResource> m_connections;

protected:
    PSource Source;
    NMMSS::UnitTesting::SamplesAccumulator Result;
};

}


BOOST_FIXTURE_TEST_SUITE(HWDecoder, TestFixture)

const auto DECODER_LAG_DATA_SET = boost::unit_test::data::make({ 0, 1, 3, 7 });

const auto PREROLL_DATA_SET = boost::unit_test::data::make({ 0, 3, 9 });

BOOST_DATA_TEST_CASE(TestSimpleSequence, DECODER_LAG_DATA_SET, decoderLag)
{
    MockHWDecoder::SetDecoderLag(decoderLag);
    Source->AddSamples({ 100, 200, 300, 400 });
    auto incomingCount = Source->Count();
    BOOST_REQUIRE_MESSAGE(Source->SendAll(), "Couldn't send all samples");
    BOOST_REQUIRE_MESSAGE(Result.Count() == incomingCount, "Unexpected result samples count");
}

BOOST_DATA_TEST_CASE(TestPreroll, DECODER_LAG_DATA_SET * PREROLL_DATA_SET, decoderLag, prerollCount)
{
    MockHWDecoder::SetDecoderLag(decoderLag);
    Source->AddSequence(100, 20, prerollCount);
    auto incomingCount = Source->Count();
    BOOST_REQUIRE_MESSAGE(Source->SendAll(), "Couldn't send all samples");
    BOOST_REQUIRE_MESSAGE(Result.Count() == incomingCount - prerollCount, "Unexpected result samples count");
}

BOOST_DATA_TEST_CASE(TestSeveralSequences, DECODER_LAG_DATA_SET, decoderLag)
{
    MockHWDecoder::SetDecoderLag(decoderLag);
    const int PREROLL_COUNT = 5;
    const int TS_DELTA = 100;
    Source->AddSequence(TS_DELTA, 3, PREROLL_COUNT);
    Source->AddSequence(TS_DELTA, 5, PREROLL_COUNT);
    Source->AddSequence(TS_DELTA, 8, PREROLL_COUNT);
    Source->AddSequence(TS_DELTA, 20, PREROLL_COUNT);

    std::vector<NMMSS::PSample> nonPreroll;
    Source->GetSamples(nonPreroll, [](NMMSS::PSample& sample) { return !(sample->Header().eFlags & NMMSS::SMediaSampleHeader::EFPreroll); });

    int oldDecoderConstructorCount = MockHWDecoder::ConstructorCount();

    BOOST_REQUIRE_MESSAGE(Source->SendAll(), "Couldn't send all samples");
    BOOST_REQUIRE_MESSAGE(Result.CheckSequence(nonPreroll), "Unexpected output sequence");

    int newDecoderConstructorCount = MockHWDecoder::ConstructorCount();
    BOOST_REQUIRE_MESSAGE(newDecoderConstructorCount - oldDecoderConstructorCount == 4, "Unexpected decoder construction count");
}

BOOST_AUTO_TEST_SUITE_END()