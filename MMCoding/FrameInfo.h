#ifndef FRAME_INFO_HEADER
#define FRAME_INFO_HEADER

#include "../MediaType.h"
#include "MMCodingExports.h"

namespace NMMSS
{
    struct CFrameInfo
    {
        int width{};    // ������ ������ � ��������
        int height{};   // ������ ������ � ��������
        int type{};     // ��� ������: 0 - I-type(��������), 1 - P-type, 2 - B-type.
        int profile{};  // video codec profile
        int level{};    // video codec level
    };


    // ����� ������� �������, ������ ��������� ������ � �������� ������
    // info - ��������� � ���������� �����������
    // buffer, buffer_size - ��������� � ������ �������� ������� � ������������ �������
    // return true - ���� ��������� �������, ����� - false

    MMCODING_DECLSPEC bool FindFrameInfoJPEG(CFrameInfo& info, const uint8_t *buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoMPEG4(CFrameInfo& info, const uint8_t *buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoH264(CFrameInfo& info, const uint8_t *buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoH265(CFrameInfo& info, const uint8_t* buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoVP8(CFrameInfo& info, const uint8_t* buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoVP9(CFrameInfo& info, const uint8_t* buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoMPEG2(CFrameInfo& info, const uint8_t *buffer, int buffer_size);
    MMCODING_DECLSPEC bool FindFrameInfoJPEG2000(CFrameInfo& info, uint8_t& resolution_levels, const uint8_t *buffer, size_t buffer_size);
}

#endif //FRAME_INFO_HEADER

