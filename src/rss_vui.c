/*
 * rss_vui.c -- correct the coded range and colour matrix declarations
 * in an SPS VUI.
 *
 * The walk implements exactly as much of H.264 7.3.2.1.1 / H.265
 * 7.3.2.2.1 as it takes to reach the video_signal_type block, but
 * every field on that path is consumed per spec (scaling lists,
 * short-term ref pic sets, sub-layer loops), so an encoder that emits
 * any of the optional structures does not derail the offsets. The
 * bitstream is unescaped before parsing and re-escaped after the
 * edit; the edit is only committed when the escaped length is
 * unchanged, which keeps it legal for buffers other readers map.
 */

#include <string.h>

#include "rss_vui.h"

#define VUI_RBSP_MAX 1024

/* Walker results below zero: */
#define VUI_ABSENT -2 /* no VUI or no video_signal_type block */
#define VUI_BAD -1    /* unparseable */

typedef struct {
    const uint8_t *buf;
    uint32_t size;
    uint32_t bit; /* next bit to read, from the buffer start */
    int overflow;
} vui_bits_t;

static uint32_t vb_read(vui_bits_t *b, int n)
{
    uint32_t v = 0;
    while (n-- > 0) {
        if (b->bit >= b->size * 8) {
            b->overflow = 1;
            return 0;
        }
        v = (v << 1) | ((b->buf[b->bit >> 3] >> (7 - (b->bit & 7))) & 1);
        b->bit++;
    }
    return v;
}

static uint32_t vb_ue(vui_bits_t *b)
{
    int zeros = 0;
    while (!b->overflow && vb_read(b, 1) == 0 && zeros < 32)
        zeros++;
    return (1u << zeros) - 1 + vb_read(b, zeros);
}

static int32_t vb_se(vui_bits_t *b)
{
    uint32_t v = vb_ue(b);
    return (v & 1) ? (int32_t)((v + 1) / 2) : -(int32_t)(v / 2);
}

/* RBSP unescape: 00 00 03 xx -> 00 00 xx. Returns output length. */
static uint32_t vui_unescape(const uint8_t *src, uint32_t len, uint8_t *dst, uint32_t cap)
{
    uint32_t out = 0;
    for (uint32_t i = 0; i < len && out < cap; i++) {
        if (i >= 2 && src[i] == 0x03 && src[i - 1] == 0x00 && src[i - 2] == 0x00 && i + 1 < len &&
            src[i + 1] <= 0x03)
            continue;
        dst[out++] = src[i];
    }
    return out;
}

/* RBSP escape: insert 03 after any 00 00 followed by 00..03. */
static uint32_t vui_escape(const uint8_t *src, uint32_t len, uint8_t *dst, uint32_t cap)
{
    uint32_t out = 0;
    int zeros = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (zeros >= 2 && src[i] <= 0x03) {
            if (out >= cap)
                return 0;
            dst[out++] = 0x03;
            zeros = 0;
        }
        if (out >= cap)
            return 0;
        dst[out++] = src[i];
        zeros = (src[i] == 0x00) ? zeros + 1 : 0;
    }
    return out;
}

/* H.264 scaling_list (7.3.2.1.1.1): consume delta_scale entries. */
static void h264_scaling_list(vui_bits_t *b, int size)
{
    int last = 8, next = 8;
    for (int j = 0; j < size; j++) {
        if (next != 0) {
            next = (last + vb_se(b) + 256) % 256;
        }
        last = (next == 0) ? last : next;
    }
}

/* Walk an H.264 SPS RBSP up to video_full_range_flag.
 * Returns the bit offset of the flag, or -1 when absent/unparseable. */
static int64_t h264_full_range_bit(vui_bits_t *b)
{
    uint32_t profile_idc = vb_read(b, 8);
    vb_read(b, 8); /* constraint flags + reserved */
    vb_read(b, 8); /* level_idc */
    vb_ue(b);      /* seq_parameter_set_id */

    switch (profile_idc) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135: {
        uint32_t chroma_format_idc = vb_ue(b);
        if (chroma_format_idc == 3)
            vb_read(b, 1);   /* separate_colour_plane_flag */
        vb_ue(b);            /* bit_depth_luma_minus8 */
        vb_ue(b);            /* bit_depth_chroma_minus8 */
        vb_read(b, 1);       /* qpprime_y_zero_transform_bypass */
        if (vb_read(b, 1)) { /* seq_scaling_matrix_present */
            int lists = (chroma_format_idc == 3) ? 12 : 8;
            for (int i = 0; i < lists; i++)
                if (vb_read(b, 1))
                    h264_scaling_list(b, i < 6 ? 16 : 64);
        }
        break;
    }
    default:
        break;
    }

    vb_ue(b); /* log2_max_frame_num_minus4 */
    uint32_t poc_type = vb_ue(b);
    if (poc_type == 0) {
        vb_ue(b); /* log2_max_pic_order_cnt_lsb_minus4 */
    } else if (poc_type == 1) {
        vb_read(b, 1); /* delta_pic_order_always_zero */
        vb_se(b);      /* offset_for_non_ref_pic */
        vb_se(b);      /* offset_for_top_to_bottom_field */
        uint32_t n = vb_ue(b);
        if (n > 255)
            return -1;
        for (uint32_t i = 0; i < n; i++)
            vb_se(b);
    }
    vb_ue(b);            /* max_num_ref_frames */
    vb_read(b, 1);       /* gaps_in_frame_num_value_allowed */
    vb_ue(b);            /* pic_width_in_mbs_minus1 */
    vb_ue(b);            /* pic_height_in_map_units_minus1 */
    if (!vb_read(b, 1))  /* frame_mbs_only_flag */
        vb_read(b, 1);   /* mb_adaptive_frame_field */
    vb_read(b, 1);       /* direct_8x8_inference */
    if (vb_read(b, 1)) { /* frame_cropping */
        vb_ue(b);
        vb_ue(b);
        vb_ue(b);
        vb_ue(b);
    }
    if (!vb_read(b, 1)) /* vui_parameters_present */
        return b->overflow ? VUI_BAD : VUI_ABSENT;

    if (vb_read(b, 1)) { /* aspect_ratio_info_present */
        if (vb_read(b, 8) == 255) {
            vb_read(b, 16);
            vb_read(b, 16);
        }
    }
    if (vb_read(b, 1)) /* overscan_info_present */
        vb_read(b, 1);
    if (!vb_read(b, 1)) /* video_signal_type_present */
        return b->overflow ? VUI_BAD : VUI_ABSENT;
    vb_read(b, 3); /* video_format */
    if (b->overflow)
        return VUI_BAD;
    return (int64_t)b->bit; /* video_full_range_flag */
}

/* H.265 profile_tier_level (7.3.3), general + sub-layers. */
static void h265_profile_tier_level(vui_bits_t *b, uint32_t max_sub_layers_minus1)
{
    vb_read(b, 8);  /* profile_space, tier, profile_idc */
    vb_read(b, 32); /* compatibility flags */
    vb_read(b, 32); /* progressive..reserved (48 bits total) */
    vb_read(b, 16);
    vb_read(b, 8); /* general_level_idc */

    uint32_t profile_present[8] = {0}, level_present[8] = {0};
    for (uint32_t i = 0; i < max_sub_layers_minus1 && i < 8; i++) {
        profile_present[i] = vb_read(b, 1);
        level_present[i] = vb_read(b, 1);
    }
    if (max_sub_layers_minus1 > 0)
        for (uint32_t i = max_sub_layers_minus1; i < 8; i++)
            vb_read(b, 2); /* reserved alignment */
    for (uint32_t i = 0; i < max_sub_layers_minus1 && i < 8; i++) {
        if (profile_present[i]) {
            vb_read(b, 8);
            vb_read(b, 32);
            vb_read(b, 32);
            vb_read(b, 16);
        }
        if (level_present[i])
            vb_read(b, 8);
    }
}

/* H.265 scaling_list_data (7.3.4). */
static void h265_scaling_list_data(vui_bits_t *b)
{
    for (int size_id = 0; size_id < 4; size_id++) {
        for (int matrix_id = 0; matrix_id < (size_id == 3 ? 2 : 6); matrix_id++) {
            if (!vb_read(b, 1)) { /* pred_mode */
                vb_ue(b);         /* pred_matrix_id_delta */
            } else {
                int coefs = 64 < (1 << (4 + (size_id << 1))) ? 64 : (1 << (4 + (size_id << 1)));
                if (size_id > 1)
                    vb_se(b); /* dc_coef */
                for (int i = 0; i < coefs; i++)
                    vb_se(b);
            }
        }
    }
}

/* H.265 st_ref_pic_set (7.3.7); num_delta_pocs tracks NumDeltaPocs. */
static int h265_st_ref_pic_set(vui_bits_t *b, uint32_t idx, uint32_t num_sets,
                               uint32_t *num_delta_pocs)
{
    (void)num_sets;
    uint32_t inter_pred = (idx != 0) ? vb_read(b, 1) : 0;
    if (inter_pred) {
        vb_read(b, 1); /* delta_rps_sign */
        vb_ue(b);      /* abs_delta_rps_minus1 */
        uint32_t prev = num_delta_pocs[idx - 1];
        uint32_t kept = 0;
        for (uint32_t j = 0; j <= prev; j++) {
            uint32_t used = vb_read(b, 1);
            uint32_t use_delta = 1;
            if (!used)
                use_delta = vb_read(b, 1);
            if (used || use_delta)
                kept++;
        }
        num_delta_pocs[idx] = kept;
    } else {
        uint32_t neg = vb_ue(b);
        uint32_t pos = vb_ue(b);
        if (neg > 64 || pos > 64)
            return -1;
        for (uint32_t j = 0; j < neg; j++) {
            vb_ue(b);
            vb_read(b, 1);
        }
        for (uint32_t j = 0; j < pos; j++) {
            vb_ue(b);
            vb_read(b, 1);
        }
        num_delta_pocs[idx] = neg + pos;
    }
    return b->overflow ? -1 : 0;
}

static int64_t h265_full_range_bit(vui_bits_t *b)
{
    vb_read(b, 4); /* sps_video_parameter_set_id */
    uint32_t max_sub_layers_minus1 = vb_read(b, 3);
    vb_read(b, 1); /* sps_temporal_id_nesting */
    h265_profile_tier_level(b, max_sub_layers_minus1);
    vb_ue(b); /* sps_seq_parameter_set_id */
    uint32_t chroma_format_idc = vb_ue(b);
    if (chroma_format_idc == 3)
        vb_read(b, 1);   /* separate_colour_plane */
    vb_ue(b);            /* pic_width_in_luma_samples */
    vb_ue(b);            /* pic_height_in_luma_samples */
    if (vb_read(b, 1)) { /* conformance_window */
        vb_ue(b);
        vb_ue(b);
        vb_ue(b);
        vb_ue(b);
    }
    vb_ue(b); /* bit_depth_luma_minus8 */
    vb_ue(b); /* bit_depth_chroma_minus8 */
    uint32_t log2_max_poc_lsb_minus4 = vb_ue(b);
    uint32_t ordering_present = vb_read(b, 1);
    for (uint32_t i = ordering_present ? 0 : max_sub_layers_minus1; i <= max_sub_layers_minus1;
         i++) {
        vb_ue(b);
        vb_ue(b);
        vb_ue(b);
    }
    vb_ue(b);              /* log2_min_luma_coding_block_size_minus3 */
    vb_ue(b);              /* log2_diff_max_min_luma_coding_block_size */
    vb_ue(b);              /* log2_min_luma_transform_block_size_minus2 */
    vb_ue(b);              /* log2_diff_max_min_luma_transform_block_size */
    vb_ue(b);              /* max_transform_hierarchy_depth_inter */
    vb_ue(b);              /* max_transform_hierarchy_depth_intra */
    if (vb_read(b, 1))     /* scaling_list_enabled */
        if (vb_read(b, 1)) /* sps_scaling_list_data_present */
            h265_scaling_list_data(b);
    vb_read(b, 1);       /* amp_enabled */
    vb_read(b, 1);       /* sample_adaptive_offset_enabled */
    if (vb_read(b, 1)) { /* pcm_enabled */
        vb_read(b, 4);
        vb_read(b, 4);
        vb_ue(b);
        vb_ue(b);
        vb_read(b, 1);
    }
    uint32_t num_sets = vb_ue(b);
    if (num_sets > 64)
        return -1;
    uint32_t num_delta_pocs[64] = {0};
    for (uint32_t i = 0; i < num_sets; i++)
        if (h265_st_ref_pic_set(b, i, num_sets, num_delta_pocs) != 0)
            return -1;
    if (vb_read(b, 1)) { /* long_term_ref_pics_present */
        uint32_t n = vb_ue(b);
        if (n > 32)
            return -1;
        for (uint32_t i = 0; i < n; i++) {
            vb_read(b, (int)(log2_max_poc_lsb_minus4 + 4));
            vb_read(b, 1);
        }
    }
    vb_read(b, 1);      /* sps_temporal_mvp_enabled */
    vb_read(b, 1);      /* strong_intra_smoothing_enabled */
    if (!vb_read(b, 1)) /* vui_parameters_present */
        return b->overflow ? VUI_BAD : VUI_ABSENT;

    if (vb_read(b, 1)) { /* aspect_ratio_info_present */
        if (vb_read(b, 8) == 255) {
            vb_read(b, 16);
            vb_read(b, 16);
        }
    }
    if (vb_read(b, 1)) /* overscan_info_present */
        vb_read(b, 1);
    if (!vb_read(b, 1)) /* video_signal_type_present */
        return b->overflow ? VUI_BAD : VUI_ABSENT;
    vb_read(b, 3); /* video_format */
    if (b->overflow)
        return VUI_BAD;
    return (int64_t)b->bit; /* video_full_range_flag */
}

/* Continue a walk from the full-range flag to matrix_coefficients.
 * Returns its bit offset, VUI_ABSENT when the stream carries no
 * colour_description block, VUI_BAD on overflow. */
static int64_t vui_matrix_bit(vui_bits_t *b, int64_t full_range_bit)
{
    b->bit = (uint32_t)full_range_bit;
    b->overflow = 0;
    vb_read(b, 1);      /* video_full_range_flag */
    if (!vb_read(b, 1)) /* colour_description_present */
        return b->overflow ? VUI_BAD : VUI_ABSENT;
    vb_read(b, 8); /* colour_primaries */
    vb_read(b, 8); /* transfer_characteristics */
    if (b->overflow)
        return VUI_BAD;
    return (int64_t)b->bit; /* matrix_coefficients */
}

/* Shared edit core. `matrix` < 0 sets video_full_range_flag; 0..255
 * rewrites matrix_coefficients to that value. Both edits re-escape and
 * commit only when the escaped byte length is unchanged. */
static int vui_edit(uint8_t *nal, uint32_t len, int is_h265, int matrix)
{
    if (!nal || len < 4)
        return -1;

    /* Skip an Annex B start code when the caller left it on. */
    if (len > 4 && nal[0] == 0 && nal[1] == 0) {
        if (nal[2] == 1) {
            nal += 3;
            len -= 3;
        } else if (nal[2] == 0 && nal[3] == 1) {
            nal += 4;
            len -= 4;
        }
    }

    uint32_t hdr = is_h265 ? 2 : 1;
    if (len <= hdr || len - hdr > VUI_RBSP_MAX)
        return -1;
    if (is_h265) {
        if (((nal[0] >> 1) & 0x3f) != 33)
            return -1;
    } else {
        if ((nal[0] & 0x1f) != 7)
            return -1;
    }

    uint8_t rbsp[VUI_RBSP_MAX];
    uint32_t rbsp_len = vui_unescape(nal + hdr, len - hdr, rbsp, sizeof(rbsp));

    vui_bits_t b = {.buf = rbsp, .size = rbsp_len, .bit = 0, .overflow = 0};
    int64_t bit = is_h265 ? h265_full_range_bit(&b) : h264_full_range_bit(&b);
    if (bit == VUI_ABSENT)
        return 0;
    if (bit < 0 || (uint32_t)bit >= rbsp_len * 8)
        return -1;

    if (matrix < 0) {
        uint8_t *byte = &rbsp[bit >> 3];
        uint8_t mask = (uint8_t)(1u << (7 - (bit & 7)));
        if (*byte & mask)
            return 0; /* already full range */
        *byte |= mask;
    } else {
        int64_t mbit = vui_matrix_bit(&b, bit);
        if (mbit == VUI_ABSENT)
            return 0;
        if (mbit < 0 || (uint32_t)mbit + 8 > rbsp_len * 8)
            return -1;
        vui_bits_t r = b;
        r.bit = (uint32_t)mbit;
        r.overflow = 0;
        if (vb_read(&r, 8) == (uint32_t)matrix)
            return 0; /* already declared */
        for (int i = 0; i < 8; i++) {
            uint32_t pos = (uint32_t)mbit + (uint32_t)i;
            uint8_t mask = (uint8_t)(1u << (7 - (pos & 7)));
            if ((matrix >> (7 - i)) & 1)
                rbsp[pos >> 3] |= mask;
            else
                rbsp[pos >> 3] &= (uint8_t)~mask;
        }
    }

    uint8_t esc[VUI_RBSP_MAX + 64];
    uint32_t esc_len = vui_escape(rbsp, rbsp_len, esc, sizeof(esc));
    if (esc_len != len - hdr) {
        return -1; /* the edit would change the byte length */
    }
    memcpy(nal + hdr, esc, esc_len);
    return 1;
}

int rss_vui_set_full_range(uint8_t *nal, uint32_t len, int is_h265)
{
    return vui_edit(nal, len, is_h265, -1);
}

int rss_vui_set_matrix(uint8_t *nal, uint32_t len, int is_h265, uint8_t matrix)
{
    return vui_edit(nal, len, is_h265, matrix);
}
