#include "CudaSampleHolder.h"
#include "CudaDevice.h"
#include "HWAccelerated.h"

namespace
{
    const int MAX_SAMPLE_COUNT = 10;
}

CudaSampleHolder::CudaSampleHolder(CudaDeviceSP device, bool inHostMemory):
    m_stream(device->CreateStream()),
    m_inHostMemory(inHostMemory)
{
    SetMaxSampleCount(MAX_SAMPLE_COUNT);
}

VideoMemorySampleSP CudaSampleHolder::CreateSample()
{
    return std::make_shared<CudaSample>(m_stream, m_inHostMemory, m_sharedMemory);
}

CudaStreamSP CudaSampleHolder::Stream() const
{
    return m_stream;
}

VideoMemorySampleSP CudaSampleHolder::GetFreeSampleBase()
{
    if (m_advisor && m_advisor->GetTargetProcessIdsIfChanged(m_targetProcessIds, m_targetIdsRevision) && !m_targetProcessIds.empty())
    {
        if (!m_sharedMemory)
        {
            m_sharedMemory = m_stream->Device()->CreateSharedMemory(MAX_SAMPLE_COUNT);
            ClearAllSamples();
        }
        if (m_sharedMemory)
        {
            m_sharedMemory->UpdateTargetProcessIds(m_targetProcessIds);
        }
    }
    return VideoMemorySampleHolder::GetFreeSampleBase();
}

void CudaSampleHolder::SetDecoderAdvisor(NMMSS::IHWDecoderAdvisor* advisor)
{
    m_advisor = NCorbaHelpers::ShareRefcounted(advisor);
}

void CudaSampleHolder::ReleaseSharedSamples()
{
    for (const auto& sample : m_samples)
    {
        if (sample->IsReady())
        {
            std::static_pointer_cast<CudaSample>(sample)->ReleaseSharedSample();
        }
    }
}
