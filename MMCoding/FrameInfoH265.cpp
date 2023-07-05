#include "FrameInfo.h"
#include "GetBits.h"

#include <vector>

static const uint8_t* FindStartCodeH265(const uint8_t *p, const uint8_t *end)
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

bool NMMSS::FindFrameInfoH265(NMMSS::CFrameInfo& info, const uint8_t *buffer, int buffer_size)
{
    const uint8_t *p = buffer;
    const uint8_t *end = buffer + buffer_size;
    
    for (;;)
    {
        p = FindStartCodeH265(p, end);
        if (p >= end) return false;
    
        uint8_t unit_type = (*(p++) & 0x7E) >> 1;
        // Looking for SPS.
        if (unit_type == 33)
            break;
    }
    p++;
    
    // Clear off emulation_prevention_three_bytes. Limit SPS length to 100 bytes for performance.
    size_t rest = std::distance(p, end);
    if (rest == 0)
    {
        return false;
    }
    else
    if (rest > 100)
    {
        rest = 100;
    }
    
    std::vector<uint8_t> sps_bytes(rest);
    size_t new_size = 0;
    for (size_t i = 0; i < rest-2; i++)
    {
        if ((i + 2 < rest) &&
            ((p[i + 0] == 0) && (p[i + 1] == 0) && (p[i + 2] == 3)))
        {
            sps_bytes[new_size++] = p[i];
            sps_bytes[new_size++] = p[i + 1];
            i += 2;
        }
        else
        {
            sps_bytes[new_size++] = p[i];
        }
    }
    // Clearing done, proceed to parsing.
    p = &sps_bytes[0];
    
    int offset = 0; // Bits.
    offset += 4; //uint8_t sps_video_parameter_set_id = TGetBits<4>(p, offset);
    uint8_t sps_max_sub_layers_minus1 = TGetBits<3>(p, offset); // Valid values in [0, 6].
    offset += 1; //uint8_t sps_temporal_id_nesting_flag = TGetBits<1>(p, offset);
    
    // Profiles parsing.
    offset += 3;
    int profile_idc = TGetBits<5>(p, offset);
    offset += 80;
    
    // General level idc.
    int level_idc = TGetBits<8>(p, offset);
    
    std::vector<uint8_t> sub_layer_profile_present_flag(sps_max_sub_layers_minus1 + 1);
    std::vector<uint8_t> sub_layer_level_present_flag(sps_max_sub_layers_minus1 + 1);
    
    for (uint8_t i = 0; i < sps_max_sub_layers_minus1; i++) {
        sub_layer_profile_present_flag[i] = TGetBits<1>(p, offset);
        sub_layer_level_present_flag[i] = TGetBits<1>(p, offset);
    }
    
    if (sps_max_sub_layers_minus1 > 0)
        for (uint8_t i = sps_max_sub_layers_minus1; i < 8; i++)
            offset += 2;
    
    for (uint8_t i = 0; i < sps_max_sub_layers_minus1; i++)
    {
        if (sub_layer_profile_present_flag[i])
        {
            offset += 88;
        }
        if (sub_layer_level_present_flag[i])
            offset += 8;
    }
    // End profiles parsing.
    
    /*uint32_t sps_seq_parameter_set_id = */GetExpGolomb(p, offset); // Valid values are in [0, 15].
    uint32_t chroma_format_idc = GetExpGolomb(p, offset);
    
    // separate_colour_plane_flag = 0;
    if (chroma_format_idc == 3)
    {
        offset += 1; // Can be useful: separate_colour_plane_flag = TGetBits<1>(p, offset).
    }
    
    uint32_t pic_width_in_luma_samples = GetExpGolomb(p, offset);
    uint32_t pic_height_in_luma_samples = GetExpGolomb(p, offset);
    
    uint8_t conformance_window_flag = TGetBits<1>(p, offset);
    if (conformance_window_flag)
    {
        // Need to cut result frame size.
        uint32_t conf_win_left_offset = GetExpGolomb(p, offset);
        uint32_t conf_win_right_offset = GetExpGolomb(p, offset);
        uint32_t conf_win_top_offset = GetExpGolomb(p, offset);
        uint32_t conf_win_bottom_offset = GetExpGolomb(p, offset);
        
        pic_width_in_luma_samples -= conf_win_left_offset + conf_win_right_offset;
        pic_height_in_luma_samples -= conf_win_top_offset + conf_win_bottom_offset;
    }
    
    info.width = pic_width_in_luma_samples;
    info.height = pic_height_in_luma_samples;
    info.type = 0;
    info.profile = profile_idc;
    info.level = level_idc;
    
    return true;
}
