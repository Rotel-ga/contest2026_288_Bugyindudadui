/* SPDX-License-Identifier: Apache-2.0 */
#include "fallguard.h"
#include "panel_control.h"
#include "camera_preview.h"
#include <stdlib.h>
#include "../desktop/desktop.h"
#include <stdio.h>

enum fallguard_status
{
  FALLGUARD_NORMAL,
  FALLGUARD_DETECTING,
  FALLGUARD_SUSPECTED,
  FALLGUARD_CONFIRMED
};

static enum fallguard_status g_status = FALLGUARD_NORMAL;
static lv_obj_t *g_status_label;
static lv_obj_t *g_status_dot;
static lv_obj_t *g_screen;
static lv_obj_t *g_pc_label;
static const char *g_last_pc_text;
static void status_render(void);
static uint16_t *g_preview_pixels;
static uint32_t g_preview_sequence;
static lv_obj_t *g_preview_image;
static lv_obj_t *g_preview_hint;
static lv_image_dsc_t g_preview =
{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565,
  .header.w = CAMERA_PREVIEW_W,
  .header.h = CAMERA_PREVIEW_H,
  .header.stride = CAMERA_PREVIEW_W * 2,
  .data_size = CAMERA_PREVIEW_BYTES
};

void fallguard_preview_init(void)
{
  g_preview_pixels = malloc(CAMERA_PREVIEW_BYTES);
  if (g_preview_pixels && camera_preview_open() < 0)
    {
      free(g_preview_pixels);
      g_preview_pixels = NULL;
    }
  g_preview.data = (const uint8_t *)g_preview_pixels;
  g_preview_sequence = 0;
}

/* Called after LVGL teardown, when no widget/cache references the pixels. */
void fallguard_preview_deinit(void)
{
  camera_preview_close();
  free(g_preview_pixels);
  g_preview_pixels = NULL;
  g_preview.data = NULL;
  g_screen = NULL;
}

static void preview_render(void)
{
  uint32_t sequence;
  if (!g_preview_pixels) return;
  sequence = camera_preview_take(g_preview_pixels, g_preview_sequence);
  if (sequence != g_preview_sequence)
    {
      char text[64];
      g_preview_sequence = sequence;
      lv_image_cache_drop(&g_preview);
      lv_image_set_src(g_preview_image, &g_preview);
      lv_obj_remove_flag(g_preview_image, LV_OBJ_FLAG_HIDDEN);
      lv_obj_invalidate(g_preview_image);
      snprintf(text, sizeof(text), "Photo #%lu", (unsigned long)sequence);
      lv_label_set_text(g_preview_hint, text);
    }
}

void fallguard_poll(void)
{
  struct panel_control_state state;
  const char *text;

  if (g_screen == NULL || g_screen != g_desk.screen)
    {
      return;
    }

  preview_render();
  panel_control_snapshot(&state);
  if (!state.connected)
    {
      text = "PC offline";
    }
  else if (state.revision != state.acknowledged)
    {
      text = state.requested ? "Waiting for PC" : "Stopping after frame";
    }
  else if (!state.requested)
    {
      text = "PC ready / stopped";
    }
  else if (state.pc_state == PANEL_PC_ERROR)
    {
      text = "Monitor error / retry";
    }
  else if (state.pc_state == PANEL_PC_RUNNING)
    {
      text = "Monitoring";
    }
  else if (state.pc_state == PANEL_PC_FALL)
    {
      text = "Fall detected";
    }
  else if (state.pc_state == PANEL_PC_OK)
    {
      text = "No fall detected";
    }
  else
    {
      text = "Waiting for PC";
    }

  if (text != g_last_pc_text)
    {
      g_status = !state.requested ? FALLGUARD_NORMAL :
        (state.revision == state.acknowledged && state.pc_state == PANEL_PC_FALL ?
         FALLGUARD_SUSPECTED :
         (state.revision == state.acknowledged && state.pc_state == PANEL_PC_OK ?
          FALLGUARD_NORMAL : FALLGUARD_DETECTING));
      status_render();
      lv_label_set_text(g_pc_label, text);
      g_last_pc_text = text;
    }
}

static void status_render(void)
{
  const char *text;
  uint32_t color;

  switch (g_status)
    {
      case FALLGUARD_DETECTING:
        text = "检测中";
        color = DESK_BLUE;
        break;
      case FALLGUARD_SUSPECTED:
        text = "疑似跌倒";
        color = 0xe89b4a;
        break;
      case FALLGUARD_CONFIRMED:
        text = "已确认";
        color = 0xe45865;
        break;
      default:
        text = "正常";
        color = 0x49b983;
        break;
    }

  if (g_status_label)
    {
      lv_label_set_text(g_status_label, text);
      lv_obj_set_style_text_color(g_status_label, lv_color_hex(color), 0);
    }
  if (g_status_dot)
    {
      lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(color), 0);
    }
}

static void simulate_event(lv_event_t *event)
{
  (void)event;
  if (g_status == FALLGUARD_NORMAL)
    {
      g_status = FALLGUARD_DETECTING;
    }
  else if (g_status == FALLGUARD_DETECTING)
    {
      g_status = FALLGUARD_SUSPECTED;
    }
  else
    {
      g_status = FALLGUARD_NORMAL;
    }
  status_render();
}

static void monitoring_toggle(lv_event_t *event)
{
  lv_obj_t *button = lv_event_get_target(event);
  lv_obj_t *label = lv_obj_get_child(button, 0);

  bool enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
  panel_control_request(enabled);
  if (enabled)
    {
      lv_label_set_text(label, "停止监控");
      g_status = FALLGUARD_DETECTING;
    }
  else
    {
      lv_label_set_text(label, "开始监控");
      g_status = FALLGUARD_NORMAL;
    }
  status_render();
}

void fallguard_event(lv_event_t *event)
{
  (void)event;
  fallguard_show();
}

static void preview_screen_deleted(lv_event_t *event)
{
  if (lv_event_get_target(event) == g_screen)
    {
      g_screen = NULL;
      g_preview_image = NULL;
      g_preview_hint = NULL;
    }
}

void fallguard_show(void)
{
  lv_obj_t *screen = desk_screen(DESK_BG);
  lv_obj_t *video;
  lv_obj_t *card;
  lv_obj_t *monitor;

  struct panel_control_state state;
  panel_control_snapshot(&state);
  g_status = state.requested ? FALLGUARD_DETECTING : FALLGUARD_NORMAL;
  g_screen = screen;
  lv_obj_add_event_cb(screen, preview_screen_deleted, LV_EVENT_DELETE, NULL);
  g_last_pc_text = NULL;
  g_status_label = NULL;
  g_status_dot = NULL;
  desk_label(screen, "跌倒监护", 56, 35, 600, DESK_TEXT);
  desk_button(screen, "返回应用中心", 765, 25, 210, 62,
              desk_app_center, NULL);

  video = lv_obj_create(screen);
  lv_obj_set_pos(video, 56, 115);
  lv_obj_set_size(video, 590, 300);
  lv_obj_set_style_radius(video, 24, 0);
  lv_obj_set_style_bg_color(video, lv_color_hex(0x0b1220), 0);
  lv_obj_set_style_border_width(video, 1, 0);
  lv_obj_set_style_border_color(video, lv_color_hex(0x35476a), 0);
  lv_obj_remove_flag(video, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(video, 0, 0);
  g_preview_image = lv_image_create(video);
  lv_obj_center(g_preview_image);
  if (g_preview_sequence)
    {
      lv_image_set_src(g_preview_image, &g_preview);
    }
  else
    {
      lv_obj_add_flag(g_preview_image, LV_OBJ_FLAG_HIDDEN);
    }
  g_preview_hint = desk_label(video,
    g_preview_pixels ? "Waiting for photo" : "Preview unavailable",
    8, 4, 280, DESK_MUTED);
  if (g_preview_sequence)
    {
      char text[64];
      snprintf(text, sizeof(text), "Photo #%lu", (unsigned long)g_preview_sequence);
      lv_label_set_text(g_preview_hint, text);
    }

  card = lv_obj_create(screen);
  lv_obj_set_pos(card, 675, 115);
  lv_obj_set_size(card, 293, 300);
  lv_obj_set_style_radius(card, 24, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(DESK_CARD), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  desk_label(card, "当前状态", 24, 22, 220, DESK_MUTED);
  g_status_dot = lv_obj_create(card);
  lv_obj_set_pos(g_status_dot, 24, 67);
  lv_obj_set_size(g_status_dot, 22, 22);
  lv_obj_set_style_radius(g_status_dot, LV_RADIUS_CIRCLE, 0);
  g_status_label = desk_label(card, "正常", 62, 61, 190, DESK_TEXT);
  desk_label(card, "最近事件", 24, 125, 220, DESK_MUTED);
  desk_label(card, "暂无异常", 24, 158, 240, DESK_TEXT);
  desk_label(card, "监护模式", 24, 205, 220, DESK_MUTED);
  g_pc_label = desk_label(card, "PC offline", 24, 238, 240, DESK_TEXT);

  monitor = desk_button(screen, "开始监控", 56, 460, 250, 70,
                        monitoring_toggle, NULL);
  lv_obj_add_flag(monitor, LV_OBJ_FLAG_CHECKABLE);
  if (state.requested)
    {
      lv_obj_add_state(monitor, LV_STATE_CHECKED);
      lv_label_set_text(lv_obj_get_child(monitor, 0), "停止监控");
    }
  desk_button(screen, "模拟摔倒", 330, 460, 250, 70,
              simulate_event, NULL);
  status_render();
  fallguard_poll();
  printf("DESKTOP PAGE fallguard\n");
}
