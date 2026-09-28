/****************************************************************************
 * app/p4x_selftest/p4x_camera_capture.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_P4X_SELFTEST_P4X_CAMERA_CAPTURE_H
#define __APPS_P4X_SELFTEST_P4X_CAMERA_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Capture one SC2336 frame through the board's V4L2 camera lower-half.
 *
 * The lower-half is responsible for sensor initialization and CSI/DMA
 * configuration.  This function reports success only after VIDIOC_DQBUF
 * returns a completed buffer; an I2C ACK or chip-ID match is not sufficient.
 */

int p4x_camera_capture_one(const char *device, const char *output,
                           uint16_t width, uint16_t height, uint16_t fps,
                           uint32_t *bytesused);

/* gain: NULL to leave the sensor gain registers at their power-on defaults,
 * or a 3-element array {DIG_FINE, DIG_COARSE, ANG} written to 0x3e07, 0x3e06
 * and 0x3e09 respectively.  The official mode table does not touch these, so
 * without an override the sensor runs at minimum gain (1.0x).
 */

int p4x_camera_capture_csi(const char *output, const int *gain);

/* When non-zero, a successful capture encodes the full-resolution frame to a
 * baseline JPEG on the CPU (software, no DMA2D) and prints it as base64
 * instead of the RGB565 thumbnail.  g_p4x_jpeg_awb applies grey-world gains
 * while encoding.  Set by the --jpeg-capture command.
 */

extern int g_p4x_jpeg_emit;
extern int g_p4x_jpeg_awb;

/* Encode a synthetic gradient to JPEG and emit it (sensor-independent). */

int p4x_jpeg_selftest(void);

#ifdef __cplusplus
}
#endif

#endif
