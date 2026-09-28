/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PHOTO_IDENTIFY_H
#define PHOTO_IDENTIFY_H

#include <lvgl/lvgl.h>

void photo_identify_event(lv_event_t *event);
void photo_identify_poll(void);
void photo_identify_reset(void);

/* Future PC receiver may call this from task context. Copies UTF-8 text,
 * up to 1023 bytes, into a mailbox; only the desktop task touches LVGL.
 * Returns 0, -EINVAL for NULL, or -E2BIG without changing the old message.
 * Transport/PC protocol is intentionally not connected in this UI stage.
 */
int photo_identify_set_message(const char *message);

#endif
