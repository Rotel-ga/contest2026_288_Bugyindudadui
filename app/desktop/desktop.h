/* SPDX-License-Identifier: Apache-2.0 */
#ifndef DESKTOP_H
#define DESKTOP_H

#include <lvgl/lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define DESK_BG 0x101a2d
#define DESK_CARD 0x1b2941
#define DESK_TEXT 0xf1f5ff
#define DESK_MUTED 0xa6b5d1
#define DESK_BLUE 0x537bff
#define DESK_PIN_DIGITS 6

enum desk_lock_mode
{
  DESK_NONE,
  DESK_SWIPE,
  DESK_PIN
};

struct desk_settings
{
  uint32_t version;
  uint32_t mode;
  uint8_t salt[16];
  uint8_t digest[32];
};

struct desk_state
{
  struct desk_settings settings;
  bool storage_ok;
  bool exit_requested;
  unsigned int failed_pins;
  uint32_t retry_after;
  lv_obj_t *screen;
  lv_obj_t *content;
};

extern struct desk_state g_desk;
extern const lv_font_t desktop_font;

int desk_settings_load(struct desk_settings *settings);
int desk_settings_save(const struct desk_settings *settings);
int desk_pin_set(struct desk_settings *settings, const char *pin);
bool desk_pin_verify(const char *pin);
void desk_zero(void *ptr, size_t length);

lv_obj_t *desk_screen(uint32_t color);
lv_obj_t *desk_label(lv_obj_t *parent, const char *text, int x, int y,
                     int width, uint32_t color);
lv_obj_t *desk_button(lv_obj_t *parent, const char *text, int x, int y,
                      int width, int height, lv_event_cb_t cb, void *data);
void desk_notice(const char *text);
void desk_home(void);
void desk_lock(void);
void desk_settings_page(void);
void desk_request_mode(enum desk_lock_mode mode);
void desk_pin_unlock(void);
void desk_exit_request(lv_event_t *event);
void desk_home_event(lv_event_t *event);
void desk_settings_event(lv_event_t *event);
void desk_lock_event(lv_event_t *event);

#endif
