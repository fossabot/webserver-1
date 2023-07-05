#include "HWUtils.h"
#include "IHWDecoder.h"

bool ErrorCounter::ShowError(bool critical)
{
    const int MAX_ERROR_COUNT = 100;

    if ((m_counter < MAX_ERROR_COUNT) || (critical && m_counter < MAX_ERROR_COUNT * 2))
    {
        ++m_counter;
        return true;
    }
    return false;
}


void HWDecoderUtils::DecodeAndGetResult(IAsyncHWDecoder& decoder, const CompressedData& data, NMMSS::CDeferredAllocSampleHolder* holder, bool preroll, bool waitForSamples)
{
    waitForSamples |= !data.Ptr;
    decoder.GetDecodedSamples(holder, waitForSamples);

    do
    {
        decoder.Decode(data, preroll);
    } while(waitForSamples && decoder.IsValid() && decoder.GetDecodedSamples(holder, true));
}
