#include "BaseSampleTransformer.h"


BaseSampleTransformer::BaseSampleTransformer(DECLARE_LOGGER_ARG, QSDeviceSP device, NMMSS::IFrameGeometryAdvisor* advisor):
    NLogging::WithLogger(GET_LOGGER_PTR),
    m_QSDevice(device),
    m_advisor(advisor, NCorbaHelpers::ShareOwnership())
{
}


Point BaseSampleTransformer::GetResultSize(const Point& srcSize, bool* mipmaps)
{
    int resultWidth = 0, resultHeight = 0;
    auto adviceType = m_advisor ? m_advisor->GetAdvice((uint32_t&)resultWidth, (uint32_t&)resultHeight)
                                : NMMSS::IFrameGeometryAdvisor::ATLargest;
    bool scaling = resultWidth && resultWidth < srcSize.X;
    if (mipmaps)
    {
        *mipmaps = scaling && (adviceType == NMMSS::IFrameGeometryAdvisor::ATLargest);
    }
    if (scaling)
    {
        return {resultWidth, resultHeight};
    }
    return srcSize;
}



BaseQSAllocator::BaseQSAllocator()
{
    pthis = this;
    Alloc = alloc;
    Lock = lock;
    Free = free;
    Unlock = unlock;
    GetHDL = gethdl;
}


mfxStatus BaseQSAllocator::GetFrameHDL(mfxMemId mid, mfxHDL* handle)
{
    return MFX_ERR_UNSUPPORTED;
}

mfxStatus BaseQSAllocator::AllocFrames(mfxFrameAllocRequest* request, mfxFrameAllocResponse* response)
{
    mfxStatus sts = MFX_ERR_NONE;

    // if (request->Type & MFX_MEMTYPE_SYSTEM_MEMORY)
    //     return MFX_ERR_UNSUPPORTED;

    if (m_savedAllocResponse.NumFrameActual && 
        MFX_MEMTYPE_EXTERNAL_FRAME & request->Type &&
        MFX_MEMTYPE_FROM_DECODE & request->Type)
    {
        // Memory for this request was already allocated during manual allocation stage. Return saved response
        //   When decode acceleration device (DXVA) is created it requires a list of D3D surfaces to be passed.
        //   Therefore Media SDK will ask for the surface info/mids again at Init() stage, thus requiring us to return the saved response
        //   (No such restriction applies to Encode or VPP)

        *response = m_savedAllocResponse;
    }
    else
    {
        sts = AllocFramesInternal(request, response);
        if (MFX_MEMTYPE_EXTERNAL_FRAME & request->Type && MFX_MEMTYPE_FROM_DECODE & request->Type)
        {
            m_savedAllocResponse = *response;
        }
    }
    return sts;
}

mfxStatus BaseQSAllocator::lock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

mfxStatus BaseQSAllocator::unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

mfxStatus BaseQSAllocator::gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL* handle)
{
    return ((BaseQSAllocator*)pthis)->GetFrameHDL(mid, handle);
}

mfxStatus BaseQSAllocator::alloc(mfxHDL pthis, mfxFrameAllocRequest* request, mfxFrameAllocResponse* response)
{
    return ((BaseQSAllocator*)pthis)->AllocFrames(request, response);
}

mfxStatus BaseQSAllocator::free(mfxHDL pthis, mfxFrameAllocResponse* response)
{
    return response ? MFX_ERR_NONE : MFX_ERR_NULL_PTR;
    //return ((BaseQSAllocator*)pthis)->FreeFrames(response);
}
