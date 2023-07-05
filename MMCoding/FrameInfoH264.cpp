#include "FrameInfo.h"
#include "GetBits.h"

static const uint8_t* FindStartCodeH264(const uint8_t *p, const uint8_t *end)
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

bool NMMSS::FindFrameInfoH264(NMMSS::CFrameInfo& info, const uint8_t *buffer, int buffer_size)
{
    const uint8_t *p = buffer;
    const uint8_t *end = buffer + buffer_size;

    for (;;)
    {
        p = FindStartCodeH264(p, end);
        if (p >= end) return false;

        uint8_t unit_type = *(p++) & 0x1F;
        if (unit_type == 7)
            break;
    }

    int offset = 0;
    int profile_idc = TGetBits<8>(p, offset);
    offset += 8; //skip constraint_setN_flag, reserved_zero_5bits
    int level_idc = TGetBits<8>(p, offset);
    SkipExpGolomb(p,offset); //skip seq_parameter_set_id
    if (profile_idc == 100 || profile_idc == 110 ||
        profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
        profile_idc ==  83 || profile_idc ==  86 || profile_idc == 118)
    {
        int chroma_format_idc = GetExpGolomb(p,offset);
        if (chroma_format_idc == 3)
            ++offset; //skip separate_colour_plane_flag
        SkipExpGolomb(p, offset); //skip bit_depth_luma_minus8
        SkipExpGolomb(p, offset); //skip bit_depth_chroma_minus8
        ++offset; //skip qpprime_y_zero_transform_bypass_flag
        int seq_scaling_matrix_present_flag = TGetBits<1>(p,offset);
        if (seq_scaling_matrix_present_flag)
        {
            for (int i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); ++i)
            {
                int seq_scaling_list_present_flag_i = TGetBits<1>(p, offset);
                if (seq_scaling_list_present_flag_i)
                {
                    int sizeOfScalingList = (i < 6) ? 16 : 64;
                    int nextScale = 8;
                    for (int j = 0; (j < sizeOfScalingList) && nextScale; ++j)
                    {
                        int delta_scale = GetExpGolombSigned(p,offset);
                        nextScale = (nextScale + delta_scale + 0x100) & 0xFF;
                    }
                }
            }
        }
    }
    SkipExpGolomb(p,offset); //skip log2_max_frame_num_minus4
    int pic_order_cnt_type = GetExpGolomb(p,offset);
    if (pic_order_cnt_type == 0)
    {
        SkipExpGolomb(p,offset); //skip log2_max_pic_order_cnt_lsb_minus4
    }
    else if (pic_order_cnt_type == 1)
    {
        ++offset; //skip delta_pic_order_always_zero_flag
        SkipExpGolomb(p,offset); //skip offset_for_non_ref_pic
        SkipExpGolomb(p,offset); //skip offset_for_top_to_bottom_field
        int num_ref_frames_in_pic_order_cnt_cycle = GetExpGolomb(p,offset);
        for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i)
        {
            SkipExpGolomb(p,offset); //skip offset_for_ref_frame[i]
        }
    }
    else if (pic_order_cnt_type != 2)
    {
        return false; //wrong value for pic_order_cnt_type
    }

    SkipExpGolomb(p, offset); //skip max_num_ref_frames
    ++offset; //skip gaps_in_frame_num_value_allowed_flag

    int pic_width_in_mbs_minus1 = GetExpGolomb(p,offset);
    int pic_height_in_map_units_minus1 = GetExpGolomb(p,offset);

    int frame_mbs_only_flag = TGetBits<1>(p, offset);
    if (!frame_mbs_only_flag)
        ++offset; //skip mb_adaptive_frame_field_flag
    ++offset; //skip  direct_8x8_inference_flag

    int frame_cropping_flag = TGetBits<1>(p, offset);
    int frame_crop_right = 0, frame_crop_bottom = 0;
    if (frame_cropping_flag)
    {
        SkipExpGolomb(p,offset); //skip frame_crop_left_offset
        frame_crop_right = GetExpGolomb(p,offset);
        frame_crop_right = (frame_crop_right < 8) ? frame_crop_right : 7;
        SkipExpGolomb(p,offset); //skip frame_crop_top_offset
        frame_crop_bottom = GetExpGolomb(p,offset);
        frame_crop_bottom = (frame_crop_bottom < 8) ? frame_crop_bottom : 7;
    }

    int mb_width = pic_width_in_mbs_minus1 + 1;
    int mb_height = (pic_height_in_map_units_minus1 + 1) * (2 - frame_mbs_only_flag);

    info.width  = mb_width*16 - 2*frame_crop_right;
    if (frame_mbs_only_flag)
        info.height = mb_height*16 - 2*frame_crop_bottom;
    else
        info.height = mb_height*16 - 4*frame_crop_bottom;

    info.type = 0;
    info.profile = profile_idc;
    info.level = level_idc;

    return true;
}
