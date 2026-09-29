/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"

static void notice_close(lv_event_t *event)
{
  lv_obj_delete(lv_event_get_user_data(event));
}

lv_obj_t *desk_screen(uint32_t color)
{
  lv_obj_t *previous = g_desk.screen;
  lv_obj_t *screen = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(screen, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(screen, &desktop_font, 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(DESK_TEXT), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  g_desk.screen = screen;
  g_desk.content = NULL;
  lv_screen_load(screen);
  if (previous)
    {
      lv_obj_delete_async(previous);
    }

  return screen;
}

lv_obj_t *desk_label(lv_obj_t *parent, const char *text, int x, int y,
                     int width, uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_obj_set_width(label, width);
  lv_obj_set_pos(label, x, y);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
  return label;
}

lv_obj_t *desk_button(lv_obj_t *parent, const char *text, int x, int y,
                      int width, int height, lv_event_cb_t cb, void *data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_t *label;

  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_radius(button, 18, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(DESK_CARD), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lv_color_hex(0x35476a), 0);
  label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  if (cb)
    {
      lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, data);
    }

  return button;
}

void desk_notice(const char *text)
{
  lv_obj_t *overlay = lv_obj_create(g_desk.screen);

  lv_obj_set_size(overlay, 620, 250);
  lv_obj_center(overlay);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(DESK_CARD), 0);
  lv_obj_set_style_radius(overlay, 24, 0);
  desk_label(overlay, text, 24, 24, 550, DESK_TEXT);
  desk_button(overlay, "知道了", 390, 145, 170, 60, notice_close, overlay);
}

void desk_home_event(lv_event_t *event)
{
  desk_home();
}

void desk_settings_event(lv_event_t *event)
{
  desk_settings_page();
}

void desk_lock_event(lv_event_t *event)
{
  desk_lock();
}

void desk_exit_request(lv_event_t *event)
{
  g_desk.exit_requested = true;
}
