/* SPDX-License-Identifier: Apache-2.0 */
#include "fallguard.h"
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

  if (lv_obj_has_state(button, LV_STATE_CHECKED))
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

void fallguard_show(void)
{
  lv_obj_t *screen = desk_screen(DESK_BG);
  lv_obj_t *video;
  lv_obj_t *card;
  lv_obj_t *monitor;

  g_status = FALLGUARD_NORMAL;
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
  desk_label(video, "视频画面预留", 0, 112, 590, DESK_MUTED);
  lv_obj_set_style_text_align(lv_obj_get_child(video, 0), LV_TEXT_ALIGN_CENTER, 0);

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
  desk_label(card, "演示模式", 24, 238, 240, DESK_TEXT);

  monitor = desk_button(screen, "开始监控", 56, 460, 250, 70,
                        monitoring_toggle, NULL);
  lv_obj_add_flag(monitor, LV_OBJ_FLAG_CHECKABLE);
  desk_button(screen, "模拟摔倒", 330, 460, 250, 70,
              simulate_event, NULL);
  status_render();
  printf("DESKTOP PAGE fallguard\n");
}
