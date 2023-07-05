#ifndef ITV_MOTION_WAVELET_CODEC__HEADER__
#define ITV_MOTION_WAVELET_CODEC__HEADER__

#include "../MediaType.h"
#include "../FilterImpl.h"

namespace NMMSS
{

    class CITVDecoder
    {
    public:
        CITVDecoder();

        ~CITVDecoder();
    
        bool Decode(
            NMMSS::CDeferredAllocSampleHolder& holder,
            NMMSS::NMediaType::Video::fccITV::SubtypeHeader* pHeader, 
            uint8_t* pData,
            int scalePowerOfTwo = 0);

    protected:
        bool InitDecoder(int width, int height, int frameFormat);

    private:
        void* mpContext;
        int m_width, m_height;
        int m_levels;
        int m_frameFormat;
    };

}

#endif //ITV_MOTION_WAVELET_CODEC__HEADER__
