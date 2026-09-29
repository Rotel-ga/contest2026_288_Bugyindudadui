/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CAMERA_LIVE_H
#define CAMERA_LIVE_H
#include <stdint.h>
/* Start/stop are nonblocking; take is called only by the LVGL task.
 * status: 1 initializing/running, 0 stopped, negative errno on failure.
 */
int camera_live_start(void);
void camera_live_stop(void);
int camera_live_status(void);
uint32_t camera_live_take(uint16_t *pixels, uint32_t sequence);
#endif
