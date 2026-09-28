/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <stdio.h>

static void app_center_event(lv_event_t *event)
{
  lv_obj_t *screen = desk_screen(DESK_BG);

  desk_label(screen, "应用中心", 56, 45, 600, DESK_TEXT);
  desk_label(screen, "应用入口将在这里统一管理", 56, 110, 780, DESK_MUTED);
  desk_button(screen, "应用中心\n快应用入口预留", 56, 190, 300, 165, NULL, NULL);
  desk_label(screen, "当前尚未安装可启动的应用", 56, 390, 780, DESK_MUTED);
  desk_button(screen, "返回桌面", 790, 30, 180, 65, desk_home_event, NULL);
  printf("DESKTOP PAGE app-center\n");
}

void desk_home(void)
{
  lv_obj_t *screen = desk_screen(DESK_BG);
  lv_obj_t *panel;

  desk_label(screen, "openvela", 56, 32, 600, DESK_MUTED);
  desk_label(screen, "你好，欢迎回来", 56, 93, 700, DESK_TEXT);
  desk_label(screen, "你的设备，触手可及", 56, 135, 700, DESK_MUTED);
  panel = lv_obj_create(screen);
  lv_obj_set_pos(panel, 56, 200);
  lv_obj_set_size(panel, 912, 245);
  lv_obj_set_style_radius(panel, 28, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x202f4c), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  desk_button(panel, "设置", 15, 30, 410, 155,
              desk_settings_event, NULL);
  desk_button(panel, "应用中心", 445, 30, 410, 155,
              app_center_event, NULL);
  if (g_desk.settings.mode != DESK_NONE)
    desk_button(screen, "锁定屏幕", 755, 490, 213, 64, desk_lock_event, NULL);
  desk_label(screen, "桌面", 56, 507, 400, DESK_MUTED);
  printf("DESKTOP PAGE home\n");
}
