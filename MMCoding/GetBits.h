#ifndef GET_BITS_HEADER
#define GET_BITS_HEADER

template<unsigned char n>
static inline int TShowBits(const uint8_t* buf, int offset)
{
    return (TShowBits<n-1>(buf,offset) << 1) | TShowBits<1>(buf,offset+n-1);
}

template<>
inline int TShowBits<1>(const uint8_t* buf, int offset)
{
    return (buf[offset>>3] >> (7 - (offset&0x07))) & 1;
}

template<unsigned char n>
static inline int TGetBits(const uint8_t* buf, int& offset)
{
    int res = TShowBits<n>(buf, offset);
    offset += n;
    return res;
}

static inline void SkipExpGolomb(const uint8_t* buf, int& offset)
{
    unsigned char len = 0;
    while(!TShowBits<1>(buf,offset++)) ++len;
    offset += len;
}

static inline int GetExpGolomb(const uint8_t* buf, int& offset)
{
    unsigned char len = 0;
    while(!TShowBits<1>(buf,offset++)) ++len;
    int res = 0;
    for (unsigned char i = 0; i < len; ++i)
    {
        res = (res << 1) | TShowBits<1>(buf,offset++);
    }

    return (1 << len) + res - 1;
}

static inline int GetExpGolombSigned(const uint8_t* buf, int& offset)
{
    int code = GetExpGolomb(buf, offset);
    return (code & 1) ? (code+1)/2 : -code/2;
}

#endif  //GET_BITS_HEADER
