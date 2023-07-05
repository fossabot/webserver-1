///
/// All JPEG2000 standard references are from
/// ISO/IEC 15444-1:2000(E) "JPEG 2000 image coding system — Part 1: Core coding system"
///

#include <cstring>

#if defined(_WIN32) // ntohl
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

#include <boost/range/iterator_range_core.hpp>

#include "FrameInfo.h"

namespace
{

typedef uint8_t jpeg2000_magic_t[2];

const jpeg2000_magic_t
    JPEG2000_COD_MAGIC = { 0xff, 0x52 }
;

const uint8_t JPEG2000_SOC_SIZ[] = { 0xff, 0x4f, 0xff, 0x51 };

const uint8_t JP2_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A
};

#pragma pack(push, 1)
struct Jpeg2000Segment
{
    jpeg2000_magic_t MAGIC;
    uint16_t Length;

    uint16_t getLength() const
    {
        return ntohs(Length);
    }

    uint16_t getTotalLength() const
    {
        return getLength() + sizeof(MAGIC);
    }
};

struct Jpeg2000_SIZ: public Jpeg2000Segment
{
    uint16_t Rsiz;

    uint32_t Xsiz;
    uint32_t Ysiz;

    uint32_t XOSiz;
    uint32_t YOSiz;

    // other stuff, variable width

    uint32_t getWidth() const
    {
        return ntohl(Xsiz) - ntohl(XOSiz);
    }

    uint32_t getHeight() const
    {
        return ntohl(Ysiz) - ntohl(YOSiz);
    }
};

struct Jpeg2000_COD: public Jpeg2000Segment
{
    uint8_t Scod;
    uint32_t SGcod;
    // SPcod
    uint8_t DecompositionLevels; // 0-32
};

struct Jpeg2000MainHeader
{
    jpeg2000_magic_t START_OF_CODE;
    Jpeg2000_SIZ SIZ;
};

struct JP2Box
{
    uint32_t length;
    uint8_t type[4];

    uint32_t getLength() const
    {
        return ntohl(length);
    }
};
#pragma pack(pop)

typedef boost::iterator_range<const uint8_t*> bytestream_range_t;

bytestream_range_t& safe_advance(bytestream_range_t& input, size_t n)
{
    input.advance_begin(std::min(n, static_cast<size_t>(input.size())));
    return input;
}

size_t range_size(const bytestream_range_t& range)
{
    return static_cast<size_t>(range.size());
}

/// Iterate over top-level JP2 boxes to find a codestream box 'jp2c'
/// See Annex I: JP2 file format syntax
bytestream_range_t JP2FindCodestream(bytestream_range_t input)
{
    char const * const JP2_CODESTREAM_BOX_TYPE = "jp2c";

    if (range_size(input) <= sizeof(JP2_SIGNATURE) ||
            memcmp(&input.front(), JP2_SIGNATURE, sizeof(JP2_SIGNATURE)) != 0)
    {
        return bytestream_range_t();
    }

    safe_advance(input, sizeof(JP2_SIGNATURE));

    // while (offset + sizeof(JP2Box) + sizeof(Jpeg2000MainHeader) < bufferSize)
    while (range_size(input) > sizeof(JP2Box) + sizeof(Jpeg2000MainHeader))
    {
        JP2Box const * const box =
            reinterpret_cast<JP2Box const * const>(&input.front());

        if (memcmp(box->type, JP2_CODESTREAM_BOX_TYPE, sizeof(box->type)) == 0)
        {
            safe_advance(input, sizeof(JP2Box));
            return input;
        }

        uint32_t boxLength = box->getLength();
        if (boxLength == 0U)
        {
            // last box in container
            break;
        }
        else if (boxLength == 1U)
        {
            // Requires extended length handling, I hope we are not supporting
            // frames larger than 4G just yet.
            break;
        }

        safe_advance(input, boxLength);
    }

    return bytestream_range_t();
}

bool GetResolutionLevels(bytestream_range_t codestream, uint8_t& resolutionLevels)
{
    // Number of resolution levels is encoded into "Number of decomposition
    // levels" field of SPcod subsegment of COD segment.
    //
    // References:
    //  * Figure A-3: Construction of the main header
    //  * A.6.1 Coding style default (COD)

    const Jpeg2000MainHeader* header =
        reinterpret_cast<const Jpeg2000MainHeader*>(&codestream.front());

    // skip SIZ segment
    safe_advance(codestream,
            sizeof(jpeg2000_magic_t) + // SOC
            header->SIZ.getTotalLength()
    );

    // find COD segment
    while (range_size(codestream) > sizeof(Jpeg2000_COD))
    {
        const Jpeg2000_COD* cod =
            reinterpret_cast<const Jpeg2000_COD*>(&codestream.front());

        if (memcmp(cod->MAGIC, JPEG2000_COD_MAGIC, sizeof(JPEG2000_COD_MAGIC)) == 0)
        {
            resolutionLevels = cod->DecompositionLevels;
            return true;
        }

        safe_advance(codestream, cod->getTotalLength());
    }

    return false;
}

} // anonymous namespace

bool NMMSS::FindFrameInfoJPEG2000(NMMSS::CFrameInfo& info, uint8_t& resolutionLevels,
                                    const uint8_t *buffer, size_t bufferSize)
{
    auto input = boost::make_iterator_range(buffer, buffer + bufferSize);

    // JPEG2000 frame is either a JP2 container or bare codestream
    auto codestream = JP2FindCodestream(input);
    if (codestream.empty())
        codestream = input;

    if (range_size(codestream) <= sizeof(Jpeg2000MainHeader))
        return false;

    // JPEG2000 codestream always starts with SOC SIZ magic
    if (memcmp(&codestream.front(), JPEG2000_SOC_SIZ, sizeof(JPEG2000_SOC_SIZ)) != 0)
        return false;

    const Jpeg2000MainHeader* header =
        reinterpret_cast<const Jpeg2000MainHeader*>(&codestream.front());

    info.type = 0; // key frame
    info.width = header->SIZ.getWidth();
    info.height = header->SIZ.getHeight();

    if (!GetResolutionLevels(codestream, resolutionLevels))
        return false;

    return true;
}

