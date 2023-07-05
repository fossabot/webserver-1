#ifndef __FRAME__BUILDER__H__
#define __FRAME__BUILDER__H__

#include "../mIntTypes.h"
#include "MMCodingExports.h"
#include <CorbaHelpers/Refcounted.h>

namespace NMMSS
{

class ISample;
class IAllocator;

class IFrameBuilder : public virtual NCorbaHelpers::IRefcounted
{
public:
    virtual NCorbaHelpers::CAutoPtr<ISample> PreprocessSample(NCorbaHelpers::CAutoPtr<ISample> pSample,
        IAllocator* pAlloc, size_t blockSize) = 0;
    virtual bool InitializeSubheader(ISample* pSample) = 0;
    virtual void Restart() = 0;
};

MMCODING_DECLSPEC IFrameBuilder* CreateMPEG2FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateMPEG4FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateJPEGFrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateJPEG2000FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateMXPEGFrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateH264FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateH265FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateVP8FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateVP9FrameBuilder();
MMCODING_DECLSPEC IFrameBuilder* CreateAACFrameBuilder();

MMCODING_DECLSPEC IFrameBuilder* CreateGreyFrameBuilder(uint32_t width, uint32_t height);
MMCODING_DECLSPEC IFrameBuilder* CreateI420FrameBuilder(uint32_t width, uint32_t height);
MMCODING_DECLSPEC IFrameBuilder* CreateY42BFrameBuilder(uint32_t width, uint32_t height);

MMCODING_DECLSPEC IFrameBuilder* CreateIntellectFrameBuilder(uint32_t width, uint32_t height);
MMCODING_DECLSPEC IFrameBuilder* CreateWavecamFrameBuilder();

MMCODING_DECLSPEC IFrameBuilder* CreateNullFrameBuilder();

}

#endif // __FRAME__BUILDER__H__
