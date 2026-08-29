#ifndef RSS_VUI_H
#define RSS_VUI_H

#include <stdint.h>

/*
 * rss_vui_set_full_range -- declare full-range video in an SPS NAL.
 *
 * The ISP hands the encoder full-range pixels (coded luma spans
 * 0-255), but the SPS VUI the encoder writes declares limited range,
 * so a spec-honoring player rescales 16-235 and clips both ends of
 * the image. The VUI it emits already carries the video_signal_type
 * block, so correcting the declaration is a single-bit edit of
 * video_full_range_flag that leaves the escaped byte length intact --
 * safe to apply in place even when consumers read the same memory.
 *
 * `nal` points at the NAL header (a leading 3- or 4-byte Annex B
 * start code is detected and skipped). `is_h265` selects the SPS
 * layout. The edit is applied in place.
 *
 * Returns 1  bytes modified (flag was 0, now 1)
 *         0  nothing to do (already full range, or the VUI carries no
 *            video_signal_type block, or there is no VUI)
 *        -1  not an SPS, unparseable, or the edit would change the
 *            escaped length (never applied partially)
 */
int rss_vui_set_full_range(uint8_t *nal, uint32_t len, int is_h265);

/*
 * rss_vui_set_matrix -- correct the matrix_coefficients declaration.
 *
 * The ISP converts sensor data to YUV with its BT.601 full-range
 * table (the firmware boot default; nothing in this stack reprograms
 * it) and the OSD compositor measures as BT.601 as well (injected
 * primaries land on the 601 luma predictions within 7 codes, 15-24
 * away from 709), but the SPS VUI the encoder writes declares BT.709,
 * so players reconstruct hues with the wrong matrix.
 * matrix_coefficients is a fixed 8-bit field, so the rewrite is
 * fixed-width; as with the range flag, the edit is committed only
 * when the escaped byte length is unchanged.
 *
 * Returns 1 modified, 0 already declared / no colour description
 * block present, -1 unparseable or the length would change.
 */
int rss_vui_set_matrix(uint8_t *nal, uint32_t len, int is_h265, uint8_t matrix);

#endif
