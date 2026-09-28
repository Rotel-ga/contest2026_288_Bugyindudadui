/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FALLGUARD_CAMERA_PREVIEW_H
#define FALLGUARD_CAMERA_PREVIEW_H
#include <stddef.h>
#include <stdint.h>
#define CAMERA_PREVIEW_W 480
#define CAMERA_PREVIEW_H 270
#define CAMERA_PREVIEW_BYTES (CAMERA_PREVIEW_W * CAMERA_PREVIEW_H * 2)

/* Producer uses only the mailbox, never LVGL. The UI owns dst. */
int camera_preview_open(void);
void camera_preview_close(void);
int camera_preview_publish(const uint16_t *src, unsigned int width,
                           unsigned int height, const uint32_t *gains);
uint32_t camera_preview_take(uint16_t *dst, uint32_t previous);
void camera_preview_resize(uint16_t *dst, const uint16_t *src,
                           unsigned int width, unsigned int height,
                           const uint32_t *gains);
#endif
