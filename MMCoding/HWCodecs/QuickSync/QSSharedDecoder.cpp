#include "QSSharedDecoder.h"
#include "QSDevice.h"
#include "QSDecoderD3D.h"
#include "BaseSampleTransformer.h"
#include "HWCodecs/IHWDecoder.h"
#include "../FilterImpl.h"

ExternalSurfacesSP CreateQSExternalSurfaces();

namespace
{

class DecoderSet
{
public:
    DecoderSet(int id, int width, int height) :
        m_id(id),
        m_width(width),
        m_height(height)
    {
    }

    bool AddDecoder(DECLARE_LOGGER_ARG, const VideoPerformanceInfo& info)
    {
        if (m_width == info.Width && m_height == info.Height)
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (m_decoderCounter < DECODER_SHARING_FACTOR)
            {
                if (!m_surfaces)
                {
                    m_surfaces = CreateQSExternalSurfaces();
                }
                ++m_decoderCounter;
                _log_ << "DecoderSet::AddDecoder id = " << m_id << ", count = " << m_decoderCounter << ", w = " << m_width << ", h = " << m_height << ", surfaces = " << m_surfaces->Count();
                return true;
            }
        }
        return false;
    }

    void RemoveDecoder(DECLARE_LOGGER_ARG)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        --m_decoderCounter;
        _log_ << "DecoderSet::RemoveDecoder id = " << m_id << ", count = " << m_decoderCounter << ", w = " << m_width << ", h = " << m_height << ", surfaces = " << m_surfaces->Count();
        if (!m_decoderCounter)
        {
            m_surfaces.reset();
        }
    }

    template<typename TFunctor>
    void Exec(const TFunctor& f)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        f();
    }

    ExternalSurfacesSP Surfaces()
    {
        return m_surfaces;
    }

private:
    std::mutex m_lock;
    int m_id;
    int m_decoderCounter{};
    int m_width{};
    int m_height{};
    ExternalSurfacesSP m_surfaces;
};

using DecoderSetSP = std::shared_ptr<DecoderSet>;

class SharedDecoder : public NLogging::WithLogger, public IHWDecoder
{
public:
    SharedDecoder(DECLARE_LOGGER_ARG, IHWDecoderSP decoder, DecoderSet& decoderSet) :
        NLogging::WithLogger(GET_LOGGER_PTR),
        m_decoder(decoder),
        m_decoderSet(decoderSet)
    {
        static_cast<QSDecoderD3D&>(*decoder).SetExternalSurfaces(decoderSet.Surfaces());
    }

    ~SharedDecoder()
    {
        m_decoderSet.RemoveDecoder(GET_LOGGER_PTR);
    }

    void DecodeBitStream(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll) override
    {
        m_decoderSet.Exec([&] 
        {
            decode(data, holder);
            while (decode(CompressedData(), holder));
            //ToDo: Reset decoder?
        });
    }

    bool IsValid() const override
    {
        return m_decoder->IsValid();
    }

    void ReleaseSamples() override
    {
        return m_decoder->ReleaseSamples();
    }

    const VideoPerformanceInfo& GetPerformanceInfo(std::chrono::milliseconds recalc_for_period) const override
    {
        return m_decoder->GetPerformanceInfo(recalc_for_period);
    }

    HWDeviceSP Device() const override
    {
        return m_decoder->Device();
    }

private:
    bool decode(const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder)
    {
        const auto sizeBefore = holder->GetSamples().size();
        if (m_decoder->IsValid())
        {
            m_decoder->DecodeBitStream(data, holder, false);
            return holder->GetSamples().size() > sizeBefore;
        }
        return false;
    }

private:
    IHWDecoderSP m_decoder;
    DecoderSet& m_decoderSet;
};

class SharedDecoderHolder : public IHWDecoderFactory
{
public:
    SharedDecoderHolder(QSDevice& device) :
        m_device(device)
    {
    }

    IHWDecoderSP CreateDecoder(DECLARE_LOGGER_ARG, VideoPerformanceInfo info,
        NMMSS::IFrameGeometryAdvisor* advisor, const NMMSS::HWDecoderRequirements& requirements)
    {
        if (auto decoder = m_device.CreateDecoderInternal(GET_LOGGER_PTR, info, advisor, requirements))
        {
            std::lock_guard<std::mutex> lock(m_lock);
            return std::make_shared<SharedDecoder>(GET_LOGGER_PTR, decoder, findDecoderSet(GET_LOGGER_PTR, info));
        }
        return nullptr;
    }

private:
    DecoderSet& findDecoderSet(DECLARE_LOGGER_ARG, const VideoPerformanceInfo& info)
    {
        for (auto& set : m_decoderSets)
        {
            if (set->AddDecoder(GET_LOGGER_PTR, info))
            {
                return *set;
            }
        }
        auto newSet = std::make_shared<DecoderSet>(m_nextSetId++, info.Width, info.Height);
        newSet->AddDecoder(GET_LOGGER_PTR, info);
        m_decoderSets.push_back(newSet);
        return *newSet;
    }

private:
    QSDevice& m_device;
    std::mutex m_lock;
    std::vector<DecoderSetSP> m_decoderSets;
    int m_nextSetId{};
};

}

HWDecoderFactorySP CreateQSSharedDecoderHolder(QSDevice& device)
{
    return std::make_shared<SharedDecoderHolder>(device);
}
