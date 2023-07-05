#include "Callback.h"
#include "FrameGeometryAdvisor.h"
#include "HWDevicePool.h"
#include "IHWDecoder.h"
#include "IHWDevice.h"
#include "Points.h"
#include "SessionWatcher.h"
#include "../FilterImpl.h"
#include "../MediaType.h"
#include <CommonNotificationCpp/StatisticsAggregator.h>
#include <CorbaHelpers/RefcountedImpl.h>


namespace
{

const std::chrono::milliseconds STATISTICS_COLLECTION_PERIOD { 30000 };
const int NR_FRAMES_BETWEEN_STATISTICS_COLLECTION_ATTEMPTS = 32;
const int FRAMES_TO_ASSURE_STABLE_DECODING = 10;

uint32_t RoundUpTo2(uint32_t size)
{
    return (size + 1) & ~1;
}

class FrameSizeCorrector
    : public NMMSS::IFrameGeometryAdvisor
    , public NCorbaHelpers::CRefcountedImpl
{
public:
    FrameSizeCorrector(NMMSS::IFrameGeometryAdvisor * advisor) :
        m_advisor(advisor, NCorbaHelpers::ShareOwnership()),
        m_srcWidth(0),
        m_srcHeight(0)
    {}

    NMMSS::IFrameGeometryAdvisor::EAdviceType GetAdvice(uint32_t& resultWidth, uint32_t& resultHeight) const override
    {
        resultWidth = 0;
        resultHeight = 0;
        auto adviceType = m_advisor->GetAdvice(resultWidth, resultHeight);
        if ((resultWidth && resultWidth < m_srcWidth) || (resultHeight && resultHeight < m_srcHeight))
        {
            if (!resultWidth || !resultHeight || (adviceType == NMMSS::IFrameGeometryAdvisor::ATLimitSizeAndKeepAspect))
            {
                double factorX = (double)resultWidth / m_srcWidth;
                double factorY = (double)resultHeight / m_srcHeight;
                double factor = (!factorX || (factorY && factorY < factorX)) ? factorY : factorX;
                resultWidth = (int)(factor * m_srcWidth + 0.5);
                resultHeight = (int)(factor * m_srcHeight + 0.5);
                if (adviceType == NMMSS::IFrameGeometryAdvisor::ATLimitSizeAndKeepAspect)
                {
                    resultWidth = RoundUpTo2(resultWidth);
                    resultHeight = RoundUpTo2(resultHeight);
                }
            }
        }
        else
        {
            resultWidth = m_srcWidth;
            resultHeight = m_srcHeight;
        }
        return adviceType;
    }

    void SetOriginalSize(const Point& size)
    {
        m_srcWidth = size.X;
        m_srcHeight = size.Y;
    }

private:
    NMMSS::PFrameGeometryAdvisor m_advisor;
    uint32_t m_srcWidth;
    uint32_t m_srcHeight;
};

class DecoderSelector : public NLogging::WithLogger
{
public:
    DecoderSelector(DECLARE_LOGGER_ARG, const NMMSS::DecoderAdvisors& advisors, const NMMSS::HWDecoderOptionalSettings& settings) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_geometryAdvisor(advisors.GeometryAdvisor, NCorbaHelpers::ShareOwnership()),
        m_decoderAdvisor(advisors.DecoderAdvisor, NCorbaHelpers::ShareOwnership()),
        m_onCannotFindDecoder(settings.OnCannotFindDecoder, NCorbaHelpers::ShareOwnership())
    {
    }
    ~DecoderSelector()
    {
        resetDecoder();
    }
    bool FindDecoder(uint32_t codecId, const Point& size, bool discontinuity, const NMMSS::HWDecoderRequirements& requirements)
    {
        if (discontinuity || !m_decoderUnavailable)
        {
            m_decoderUnavailable = false;
            HWDeviceSP device, prevDevice;
            if (discontinuity)
            {
                m_info = {};
                m_info.Width = size.X;
                m_info.Height = size.Y;
                m_failedDevices.clear();
            }
            else if (m_Decoder)
            {
                prevDevice = device = m_Decoder->Device();
                m_info = m_Decoder->GetPerformanceInfo();
            }
            m_info.Codec = codecId;
            m_info.Forced = requirements.GpuIdMask != NMMSS::MASK_ANY;
            resetDecoder();

            _log_ << "Attempt to find HW decoder. Width = " << size.X << ", height = " << size.Y << ", codec = " << codecId << " (" << MMSS_PARSEFOURCC(codecId) << ")";

            for (int i = 0; i < (1 + !!prevDevice); ++i)
            {
                do
                {
                    device = HWDevicePool::Instance()->NextDevice(device, requirements);
                    if (device && !isFailed(device) && (m_Decoder = device->CreateDecoder(GET_LOGGER_PTR, m_info, m_geometryAdvisor.Get(), requirements)))
                    {
                        if (m_decoderAdvisor)
                        {
                            m_Decoder->SetAdvisor(m_decoderAdvisor.Get());
                        }
                        return true;
                    }
                } while (device && (device != prevDevice));
            }

            _err_ << "Failed to find HW decoder. Giving up...";

            if (m_onCannotFindDecoder)
            {
                m_onCannotFindDecoder->Call();
            }

            m_decoderUnavailable = true;
        }
        return false;
    }
    IHWDecoderSP Decoder()
    {
        return m_Decoder;
    }
    void DecoderFailed()
    {
        auto device = m_Decoder->Device();
        if (!isFailed(device))
        {
            m_failedDevices.push_back(device);
            _err_ << "Decoder failed!";
        }
    }

private:
    void resetDecoder()
    {
        if (m_Decoder)
        {
            m_Decoder->ReleaseSamples();
            m_Decoder.reset();
        }
    }
    bool isFailed(HWDeviceSP device) const
    {
        return std::find(m_failedDevices.begin(), m_failedDevices.end(), device) != m_failedDevices.end();
    }

private:
    NMMSS::PFrameGeometryAdvisor m_geometryAdvisor;
    NMMSS::PHWDecoderAdvisor m_decoderAdvisor;
    IHWDecoderSP m_Decoder;
    bool m_decoderUnavailable{};
    VideoPerformanceInfo m_info;
    std::vector<HWDeviceSP> m_failedDevices;
    NCorbaHelpers::CAutoPtr<NMMSS::ICallback> m_onCannotFindDecoder;
};

std::string GetDecoderPrefix()
{
    static std::atomic<int> index(0);
    return "HWDecoder " + std::to_string(index++) + "/";
}

std::string GetReceiverPrefix()
{
    static std::atomic<int> index(0);
    return "HWReceiver " + std::to_string(index++) + "/";
}

class BaseHWDecoderTransform : public NLogging::WithLogger
{
public:
    BaseHWDecoderTransform(DECLARE_LOGGER_ARG, const NMMSS::DecoderAdvisors& advisors, const NMMSS::HWDecoderOptionalSettings& settings) :
        NLogging::WithLogger(GET_LOGGER_PTR, GetDecoderPrefix()),
        m_decoderAdvisor(advisors.DecoderAdvisor, NCorbaHelpers::ShareOwnership()),
        m_Result(NMMSS::EFAILED),
        m_DecoderFlushed(true),
        m_statSink(settings.StatSink, NCorbaHelpers::ShareOwnership{}),
        m_statEP(settings.OwnerEndpoint)
    {
        if (advisors.GeometryAdvisor)
        {
            m_geometryAdvisor = NCorbaHelpers::MakeRefcounted<FrameSizeCorrector>(advisors.GeometryAdvisor);
        }
        m_decoderSelector = std::make_unique<DecoderSelector>(GET_THIS_LOGGER_PTR, NMMSS::DecoderAdvisors{ m_geometryAdvisor.Get(), m_decoderAdvisor.Get() }, settings);
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* in, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        m_Holder = &holder;
        m_Sample = in;
        m_SampleHeader = &(in->Header());

        NMMSS::IAllocator* allocator = m_Holder->GetAllocator().Get();
        if (!allocator)
        {
            _err_ << "Video decoder receive NULL allocator" << std::endl;
            return NMMSS::EFAILED;
        }

        bool streamChanged = m_SampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFDiscontinuity || m_Allocator != allocator;
        if (streamChanged || checkRequirementsChanged(streamChanged))
        {
            m_Allocator = allocator;
            m_LastCodecType = 0;
            m_SessionWatcher.RegisterStartSample(in);
        }

        try
        {
            processSample();
            return m_Result;
        }
        catch (std::exception& e)
        {
            _log_ << e.what() << std::endl;
            return NMMSS::EFAILED;
        }
    }

private:
    bool checkRequirementsChanged(bool streamChanged)
    {
        return (streamChanged || m_SampleHeader->IsKeySample()) && m_decoderAdvisor && m_decoderAdvisor->GetRequirementsIfChanged(m_requirements, m_requirementsRevision);
    }

    void processSample()
    {
        m_Result = NMMSS::EIGNORED;
        uint32_t major = m_SampleHeader->nMajor;
        uint32_t minor = m_SampleHeader->nSubtype;
        if (major == NMMSS::NMediaType::Video::ID)
        {
            if (!m_requirements.KeySamplesOnly || m_SampleHeader->IsKeySample())
            {
                decode(minor);
            }
        }
        else if (major == NMMSS::NMediaType::Auxiliary::ID && minor == NMMSS::NMediaType::Auxiliary::EndOfStream::ID)
        {
            processEos();
        }
        else
        {
            m_Result = NMMSS::ETHROUGH;
        }
    }

    void processEos()
    {
        m_Result = NMMSS::ETHROUGH;
        if (flushDecoder() && m_Result == NMMSS::ETRANSFORMED)
        {
            m_Holder->AddSample(NMMSS::PSample(m_Sample, NCorbaHelpers::ShareOwnership()));
        }
    }

    void decode(uint32_t codecId)
    {
        Point size;
        if (const auto* subheader = NMMSS::NMediaType::GetSampleOfSubtype<NMMSS::NMediaType::Video::SCodedHeader>(m_Sample))
        {
            size = { (int)subheader->nCodedWidth, (int)subheader->nCodedHeight };
        }
        if (size.X && size.Y && m_geometryAdvisor)
        {
            static_cast<FrameSizeCorrector&>(*m_geometryAdvisor).SetOriginalSize(size);
        }

        while (checkDecoder(codecId, size))
        {
            m_DecoderFlushed = false;
            decode({ m_Sample->GetBody(), m_SampleHeader->nBodySize, m_SampleHeader->dtTimeBegin }, !!(m_SampleHeader->eFlags & NMMSS::SMediaSampleHeader::EFPreroll));
            if(hasValidDecoder())
            {
                return;
            }
            else if (m_frameCounter < FRAMES_TO_ASSURE_STABLE_DECODING)
            {
                m_decoderSelector->DecoderFailed();
            }
        }
        m_Result = NMMSS::ETHROUGH;
    }

    bool flushDecoder()
    {
        if (!m_DecoderFlushed && hasValidDecoder())
        {
            if (m_LastCodecType || !m_SessionWatcher.SessionChanged())
            {
                while (decode(CompressedData(), false));
            }
            m_DecoderFlushed = true;
            return true;
        }
        return false;
    }

    bool checkDecoder(uint32_t codecId, const Point& size)
    {
        bool discontinuity = ((unsigned)m_LastCodecType != codecId);
        if (discontinuity || !hasValidDecoder())
        {
            flushDecoder();
            m_decoderSelector->FindDecoder(codecId, size, discontinuity, m_requirements);
            m_LastCodecType = codecId;
            m_lastStatTakenAt = std::chrono::steady_clock::now();
            m_framesProcessedSinceLastStat = 0u;
            m_frameCounter = 0;
        }
        return hasValidDecoder();
    }

    void collectDecoderStatistics()
    {
        if (!m_statSink || 0 != ++m_framesProcessedSinceLastStat % NR_FRAMES_BETWEEN_STATISTICS_COLLECTION_ATTEMPTS)
            return;

        auto now = std::chrono::steady_clock::now();
        auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStatTakenAt);
        if (diff_ms < STATISTICS_COLLECTION_PERIOD)
            return;

        auto vdec = m_decoderSelector->Decoder();
        auto ep = m_statEP;
        ep += "/HWCodec";
        ep += std::to_string(static_cast<int>(vdec->Device()->GetDeviceType()));
        auto push = [this, &ep](const char* metric, auto&& value)
        {
            NStatisticsAggregator::StatPoint info(metric, ep, STATISTICS_COLLECTION_PERIOD);
            info.AddValue(value);
            m_statSink->Push( std::move(info) );
        };
        auto info = vdec->GetPerformanceInfo(diff_ms);
        push(NStatisticsAggregator::LiveFPS, info.Fps);
        push(NStatisticsAggregator::LiveWidth, info.Width);
        push(NStatisticsAggregator::LiveHeight, info.Height);
        for (auto const& item : info.Extra)
        {
            push(item.first, item.second);
        }
        m_framesProcessedSinceLastStat = 0;
        m_lastStatTakenAt = now;
    }

    bool decode(const CompressedData& data, bool preroll)
    {
        auto vdec = m_decoderSelector->Decoder();
        const auto sizeBefore = m_Holder->GetSamples().size();
        if (vdec && vdec->IsValid())
        {
            vdec->DecodeBitStream(data, m_Holder, preroll);
            if (m_Holder->GetSamples().size() > sizeBefore)
            {
                collectDecoderStatistics();
                if (!m_frameCounter)
                {
                    m_Holder->GetSamples()[sizeBefore]->Header().eFlags |= NMMSS::SMediaSampleHeader::EFDiscontinuity;
                }
                ++m_frameCounter;
                m_Result = NMMSS::ETRANSFORMED;
                return true;
            }
        }
        return false;
    }

    bool hasValidDecoder() const
    {
        return m_decoderSelector->Decoder() && m_decoderSelector->Decoder()->IsValid();
    }

private:
    NMMSS::PFrameGeometryAdvisor m_geometryAdvisor;
    NMMSS::PHWDecoderAdvisor m_decoderAdvisor;
    NMMSS::ETransformResult m_Result;
    NMMSS::ISample* m_Sample;
    const NMMSS::SMediaSampleHeader* m_SampleHeader{};
    NMMSS::CDeferredAllocSampleHolder* m_Holder{};
    int m_LastCodecType{};
    NMMSS::IAllocator* m_Allocator;
    NMMSS::SessionWatcher m_SessionWatcher;
    bool m_DecoderFlushed;
    std::unique_ptr<DecoderSelector> m_decoderSelector;
    NCorbaHelpers::CAutoPtr<NStatisticsAggregator::IStatisticsAggregatorImpl> m_statSink;
    std::string m_statEP;
    std::chrono::steady_clock::time_point m_lastStatTakenAt;
    unsigned m_framesProcessedSinceLastStat{};
    int64_t m_frameCounter{};
    NMMSS::HWDecoderRequirements m_requirements;
    int64_t m_requirementsRevision{};
};

NMMSS::EHWDeviceType GetDeviceType(NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType memoryType)
{
    switch (memoryType)
    {
    case NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::CUDA:
        return NMMSS::EHWDeviceType::NvidiaCUDA;
    case NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::DX11:
    case NMMSS::NMediaType::Video::VideoMemory::EVideoMemoryType::VA:
        return NMMSS::EHWDeviceType::IntelQuickSync;
    default:
        return NMMSS::EHWDeviceType::NoDevice;
    }
}

class HWDecoderSharedReceiver : public NLogging::WithLogger
{
public:
    HWDecoderSharedReceiver(DECLARE_LOGGER_ARG, NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements) :
        NLogging::WithLogger(GET_LOGGER_PTR, GetReceiverPrefix()),
        m_advisor(advisor, NCorbaHelpers::ShareOwnership()),
        m_requirements(requirements)
    {
    }

    NMMSS::ETransformResult operator()(NMMSS::ISample* sample, NMMSS::CDeferredAllocSampleHolder& holder)
    {
        if (sample->Header().nMajor == NMMSS::NMediaType::Video::ID && sample->Header().nSubtype == NMMSS::NMediaType::Video::VideoMemory::ID)
        {
            const auto& header = sample->SubHeader<NMMSS::NMediaType::Video::VideoMemory>();
            HWDeviceId id = { GetDeviceType(header.Type), header.DeviceIndex };
            if (id != m_deviceId)
            {
                m_receiver.reset();
                m_deviceId = id;
                _log_ << "Attempt to find HW receiver. Type = " << id.Type << ", index = " << id.Index;
                if (auto device = HWDevicePool::Instance()->GetDevice(m_deviceId))
                {
                    m_receiver = device->CreateReceiver(GET_LOGGER_PTR, m_advisor.Get(), m_requirements);
                }
                if(!m_receiver)
                {
                    _err_ << "Failed to create HW receiver";
                }
            }
            if (m_receiver)
            {
                m_receiver->ProcessSample(*sample, holder);
            }
            return (holder.GetSamples().size() > 0) ? NMMSS::ETRANSFORMED : NMMSS::EIGNORED;
        }
        return NMMSS::ETHROUGH;
    }

private:
    IHWReceiverSP m_receiver;
    HWDeviceId m_deviceId;
    NMMSS::PFrameGeometryAdvisor m_advisor;
    NMMSS::HWDecoderRequirements m_requirements;
};

} // anonymous namespace



namespace NMMSS
{
    IFilter* CreateHWVideoDecoderPullFilter(DECLARE_LOGGER_ARG, const DecoderAdvisors& advisors, const HWDecoderOptionalSettings& settings)
    {
        return new CPullFilterImpl<BaseHWDecoderTransform, true, false>(GET_LOGGER_PTR, SAllocatorRequirements(), SAllocatorRequirements(),
            new BaseHWDecoderTransform(GET_LOGGER_PTR, advisors, settings));
    }

    IFilter* CreateHWDecoderSharedReceiver(DECLARE_LOGGER_ARG, IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
    {
        return new CPullFilterImpl<HWDecoderSharedReceiver, true, false>(GET_LOGGER_PTR, SAllocatorRequirements(), SAllocatorRequirements(),
            new HWDecoderSharedReceiver(GET_LOGGER_PTR, advisor, requirements));
    }
};
