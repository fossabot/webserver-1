#pragma once
#include "../MakeFourCC.h"
#include "Logging/log2.h"

class IAsyncHWDecoder;
class CompressedData;

namespace NMMSS
{
    class CDeferredAllocSampleHolder;
};


constexpr const char* CANNOT_CREATE_DECODER = "Cannot create decoder: ";

template<typename TCodecMap>
void LogUnsupportedCodec(DECLARE_LOGGER_ARG, uint32_t codecId, const TCodecMap& codecMap)
{
    _log_block_(os)
    {
        os << CANNOT_CREATE_DECODER << "Codec for " << MMSS_PARSEFOURCC(codecId) << " is not supported. Supported codecs:";
        for (auto it = codecMap.begin(); it != codecMap.end(); ++it)
        {
            if (it != codecMap.begin())
            {
                os << ",";
            }
            os << " " << MMSS_PARSEFOURCC(it->first);
        }
    }
}

constexpr int RoundUpToPow2(int size, int alignmentPow2)
{
    int remainder = alignmentPow2 - 1;
    return (size + remainder) & ~remainder;
}

constexpr int Aligned16(int size)
{
    return RoundUpToPow2(size, 16);
}

constexpr int Aligned32(int size)
{
    return RoundUpToPow2(size, 32);
}

class ErrorCounter
{
public:
    bool ShowError(bool critical = false);

private:
    int m_counter{};
};

class HWDecoderUtils
{
public:
    static void DecodeAndGetResult(IAsyncHWDecoder& decoder, const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll, bool waitForSamples = false);
};
