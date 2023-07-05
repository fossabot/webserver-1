#ifndef WXWL_MOTION_WAVELET_CODEC__HEADER__
#define WXWL_MOTION_WAVELET_CODEC__HEADER__

#include "../FilterImpl.h"

namespace NMMSS
{

    class CWXWLDecoder
    {
    public:
        CWXWLDecoder();

        ~CWXWLDecoder();

        bool Decode(
            NMMSS::CDeferredAllocSampleHolder& holder,
            uint8_t* pData,
            int scalePowerOfTwo = 0);

        static uint16_t GetWidth(uint8_t* pFrameHeader);
        static uint16_t GetHeight(uint8_t* pFrameHeader);

    private:
        void* m_pCodecCtx;
    };

}

#endif // WXWL_MOTION_WAVELET_CODEC__HEADER__
