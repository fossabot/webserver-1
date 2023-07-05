#include <cstring>

#include "FrameInfo.h"

static const uint8_t* FindStartCodeJPEG(const uint8_t *p, const uint8_t *end)
{
    for (; p < end - 3; p += 4)
    {
        uint32_t x = *(uint32_t*)p;

        if (x & (~(x+0x01010101)) & 0x80808080)
        {
            if (p[0] == 0xff)
            {
                return p;
            }
            else if (p[1] == 0xff)
            {
                return p+1;
            }
            else if (p[2] == 0xff)
            {
                return p+2;
            }
            else if (p[3] == 0xff)
            {
                return p+3;
            }
        }
    }

    for (; p < end; ++p)
    {
        if (*p == 0xff) return p;
    }

    return end;
}

bool NMMSS::FindFrameInfoJPEG(NMMSS::CFrameInfo& info, const uint8_t *buffer, int buffer_size)
{
    const uint8_t SOI   = 0xD8;
    const uint8_t EOI   = 0xD9;
    const uint8_t RST0  = 0xD0;
    const uint8_t RST7  = 0xD7;
    const uint8_t TEM   = 0x01;
    const uint8_t SOF0  = 0xC0;
    const uint8_t SOF3  = 0xC3;
    const uint8_t SOF48 = 0xF7;
    const uint8_t SOS   = 0xDA;

    const uint8_t *p = buffer;
    const uint8_t *end = buffer + buffer_size;

    uint8_t start_code = 0;

    for (;;)
    {
        p = FindStartCodeJPEG(p, end);
        if (p >= end - 8) break;

        start_code = *(++p);
        ++p;
        if (SOI == start_code || EOI == start_code || TEM == start_code ||
            (RST0 <= start_code && start_code <= RST7))
        {
            continue;
        }
        else if ((SOF0 <= start_code && start_code <= SOF3) || start_code == SOF48)
        {
            int bits = p[2];
            if (bits != 8 && start_code != SOF48)
                return false; //не 8-битный режим поддерживается только в lossless JPEG
            info.height = (p[3] << 8) | p[4];
            info.width  = (p[5] << 8) | p[6];
            info.type   = 0;
            return true;

        }
        else if (SOS == start_code)
        {
            //Если наткнулись на SOS до SOF, то либо JPEG битый, либо это MxPEG.
            //В любом случае данный фрейм информации о своих габаритах не содержит.
            return false;
        }
        else
        {
            int len = (p[0] << 8) | p[1];
            p += len;
        }
    }

    return false;
}

