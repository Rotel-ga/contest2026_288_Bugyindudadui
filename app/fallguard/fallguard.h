/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FALLGUARD_H
#define FALLGUARD_H

#include <lvgl/lvgl.h>

/* Show the native LVGL fall-monitoring page using the desktop's LVGL host. */
void fallguard_show(void);
void fallguard_event(lv_event_t *event);

#endif
