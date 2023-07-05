#include "FrameInfo.h"
#include "GetBits.h"

namespace
{
enum COLOR_SPACE
{
    CS_UNKNOWN = 0, //Unknown (in this case the color space must be
                    // signaled outside the VP9 bitstream).
    CS_BT_601,      // Rec. ITU-R BT.601-7
    CS_BT_709,      // Rec. ITU-R BT.709-6
    CS_SMPTE_170,   // SMPTE-170
    CS_SMPTE_240,   // SMPTE-240
    CS_BT_2020,     // Rec. ITU-R BT.2020-2
    CS_RESERVED,    // Reserved
    CS_RGB          // sRGB (IEC 61966-2-1)
};
}
// Based on https://datatracker.ietf.org/doc/html/rfc6386#section-9
bool NMMSS::FindFrameInfoVP8(CFrameInfo& info, const uint8_t* buffer, int buffer_size)
{
    static const int FRAME_HEADER_SZ = 3;
    if (buffer_size < FRAME_HEADER_SZ)
        return false;

    const uint8_t* p = buffer;
    const uint32_t frameData = p[2] << 16 | p[1] << 8 | p[0];
    p += 3; // skip analyzed bytes

    const bool isKeyFrame = (frameData & 0x1) == 0;
    // Use these variables if necessary
    //const uint32_t versionNumber = (frameData >> 1) & 0x7;
    //const bool showFrame = (frameData >> 4) & 0x1;
    //const uint32_t firstPartSize = (frameData >> 5) & 0x7FFFF;
    info.type = frameData;  // 0 - I(key frame), 1 - P(non key), B type frames not supported by VPx codecs
    if (!isKeyFrame)
        return false;

    static const int KEYFRAME_HEADER_SZ = 7;
    if (buffer_size < KEYFRAME_HEADER_SZ)
        return false;

    // vet via sync code
    const uint8_t startCode[3] = 
    {
        *p++,
        *p++,
        *p++
    };
    if (!(startCode[0] == 0x9D && startCode[1] == 0x01 && startCode[2] == 0x2A))
        return false;

    const uint32_t widthAndHScale = p[1] << 8 | p[0];
    info.width = static_cast<int>(widthAndHScale & 0x3FFF); // ignore horizontal scale value
    p += 2;
    const uint32_t heightAndVScale = p[1] << 8 | p[0];
    info.height = static_cast<int>(heightAndVScale & 0x3FFF); // ignore vertical scale value

    return true;
}

// According to https://storage.googleapis.com/downloads.webmproject.org/docs/vp9/vp9-bitstream-specification-v0.6-20160331-draft.pdf
bool NMMSS::FindFrameInfoVP9(CFrameInfo& info, const uint8_t* buffer, int buffer_size)
{
    static const int NECESSARY_HEADER_SIZE = 12;
    if (buffer_size < NECESSARY_HEADER_SIZE)
        return false;

    const uint8_t* p = buffer;
    int offset = 0;

    const int frameMarker = TGetBits<2>(p, offset);
    if (frameMarker != 0x02) // frame marker should be equal to two
        return false;

    const uint8_t profileLowBit = static_cast<uint8_t>(TGetBits<1>(p, offset));
    const uint8_t profileHighBit = static_cast<uint8_t>(TGetBits<1>(p, offset));
    const int16_t profile = (profileHighBit << 1) + profileLowBit;
    if (profile == 0x03)
    {
        const uint8_t reservedZero = static_cast<uint8_t>(TGetBits<1>(p, offset));
        if (reservedZero != 0) // shall be equal to zero
            return false;
    }
    const bool showExistingFrame = static_cast<bool>(TGetBits<1>(p, offset));
    if (showExistingFrame)
        /*const uint8_t frameToShowMapIdx = */TGetBits<3>(p, offset);

    const uint8_t frameType = static_cast<uint8_t>(TGetBits<1>(p, offset));
    /*const uint8_t showFrame = static_cast<uint8_t>(*/TGetBits<1>(p, offset);
    /*const uint8_t errorResilientMode = static_cast<uint8_t>(*/TGetBits<1>(p, offset);
    info.type = frameType; // 0 - I(key frame), 1 - P(non key), B type frames not supported by VPx codecs
    static const uint8_t VP9_KEY_FRAME = 0;
    if (frameType != VP9_KEY_FRAME)
        return false;
    const uint8_t frameSyncCode[3] = 
    {
        static_cast<uint8_t>(TGetBits<8>(p, offset)),
        static_cast<uint8_t>(TGetBits<8>(p, offset)),
        static_cast<uint8_t>(TGetBits<8>(p, offset))
    };
    if (!(frameSyncCode[0] == 0x49 && frameSyncCode[1] == 0x83 && frameSyncCode[2] == 0x42))
        return false;
    // color config
    // int bitDebth = 0;
    if (profile >= 2)
    {
        /*const bool ten_or_twelve_bit = static_cast<bool>(*/TGetBits<1>(p, offset);
        //bitDebth = ten_or_twelve_bit ? 12 : 10;
    }
    //else
    //{
    //    bitDebth = 8;
    //}
    const int colorSpace = TGetBits<3>(p, offset);
    if (colorSpace != CS_RGB)
    {
        /*const uint8_t colorRange = static_cast<uint8_t>(*/TGetBits<1>(p, offset);
        //uint8_t subsamplingX = 0, subsamplingY = 0;
        if (profile == 1 || profile ==3)
        {
            /*subsamplingX = static_cast<uint8_t>(*/TGetBits<1>(p, offset);
            /*subsamplingY = static_cast<uint8_t>(*/TGetBits<1>(p, offset);
            const uint8_t reservedZero = static_cast<uint8_t>(TGetBits<1>(p, offset));
            if (reservedZero != 0)
                return false;
        }
        //else // unnecessary now 
        //{
        //    subsamplingX = 1; 
        //    subsamplingY = 1;
        //}
    }
    else
    {
        // const uint8_t colorRange = 1;
        // subsamplingX = 0 and subsamplingY = 0;
        if (profile == 1 || profile == 3)
        {
            const uint8_t reservedZero = static_cast<uint8_t>(TGetBits<1>(p, offset));
            if (reservedZero != 0)
                return false;
        }
    }

    //frame size
    const int frameWidthMinus1 = TGetBits<16>(p, offset);
    const int frameHeightMinus1 = TGetBits<16>(p, offset);
    info.width = frameWidthMinus1 + 1; 
    info.height = frameHeightMinus1 + 1;

    return true;
}