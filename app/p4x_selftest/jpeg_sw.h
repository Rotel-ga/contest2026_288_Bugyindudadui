/****************************************************************************
 * app/p4x_selftest/jpeg_sw.h
 *
 * Self-contained baseline JPEG encoder (RGB565 -> JPEG, 4:4:4).
 * Pure C, no peripheral/DMA dependency: runs entirely on the CPU, so it
 * sidesteps the hardware JPEG (DMA2D) bring-up and its sensor-I2C conflict.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_P4X_SELFTEST_JPEG_SW_H
#define __APPS_P4X_SELFTEST_JPEG_SW_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Encode a little-endian RGB565 image to a baseline JPEG (4:4:4) written
 * into `out` (capacity `out_cap` bytes).
 *
 * quality: 1..100 (higher = better/larger).
 * gain_q16: per-channel {R, G, B} gain, 65536 = 1.0; NULL for none.
 * Returns the JPEG length in bytes, or -1 on bad args / output overflow.
 */

int jpeg_sw_encode_rgb565(const uint16_t *rgb565, int width, int height,
                          int quality, const uint32_t *gain_q16,
                          uint8_t *out, int out_cap);

/* Grey-world gains (same math as tools/monitor/thumb_image._grey_world):
 * scale each channel mean to target_permille / 1000.
 */

void jpeg_sw_grey_world(const uint16_t *rgb565, int npx, int target_permille,
                        uint32_t gain_q16[3]);

#ifdef __cplusplus
}
#endif

#endif /* __APPS_P4X_SELFTEST_JPEG_SW_H */
