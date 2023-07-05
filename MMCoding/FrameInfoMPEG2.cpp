#include "FrameInfo.h"
#include "GetBits.h"

static const uint8_t* FindStartCodeMPEG2(const uint8_t *p, const uint8_t *end)
{
    p += 3;
    while (p < end)
    {
        if (p[-1] > 1)   p += 3;
        else if (p[-2])  p += 2;
        else if (p[-3] | (p[-1]-1)) ++p;
        else break;
    }

    return (p < end) ? p : end;
}

bool NMMSS::FindFrameInfoMPEG2(NMMSS::CFrameInfo& info, const uint8_t *buffer, int buffer_size)
{
    const uint8_t *p = buffer;
    const uint8_t *end = buffer + buffer_size;

    for (;;)
    {
        p = FindStartCodeMPEG2(p, end);
        if (p >= end) return false;
        int offset = 0;
        if (0xB3 == p[0])
        {
            ++p;
            int horizontal_size_value = TGetBits<12>(p,offset);
            int vertical_size_value = TGetBits<12>(p,offset);
            info.width = horizontal_size_value;
            info.height = vertical_size_value;
            info.type = 0;

            break;
        }
    }

    return true;
}
