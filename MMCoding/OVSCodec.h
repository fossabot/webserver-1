#ifndef OVSOFT_MOTION_WAVELET_CODEC__HEADER__
#define OVSOFT_MOTION_WAVELET_CODEC__HEADER__

#include "MMCodingExports.h"
#include "../MediaType.h"
#include "../ConnectionBroker.h"
#include "../FilterImpl.h"
#include "FrameGeometryAdvisor.h"
#include "../../Codecs/include/DecoderAPI.h"
#include "../../Codecs/include/EncoderAPI.h"

namespace NMMSS
{

class COVSDecoder
{
public:
    COVSDecoder()
        :   m_pDecoder(NOVSCodec::CreateOVSDecoder())
        ,   m_pContextY(m_pDecoder->CreateDecoderContext())
        ,   m_pContextUV(m_pDecoder->CreateDecoderContext())
    {
    }


    bool Decode(
        NMMSS::NMediaType::Video::fccOVS2::SubtypeHeader* pHeader,
        uint8_t* pData,
        uint32_t nDataSize,
        NMMSS::CDeferredAllocSampleHolder& holder,
        NMMSS::IFrameGeometryAdvisor* pAdvisor);

private:
    std::unique_ptr<NOVSCodec::IDecoder> m_pDecoder;
    std::unique_ptr<NOVSCodec::IDecoderContext> m_pContextY;
    std::unique_ptr<NOVSCodec::IDecoderContext> m_pContextUV;
    bool m_bHaveSeenKeyFrame;
};

}

#endif //OVSOFT_MOTION_WAVELET_CODEC__HEADER__
