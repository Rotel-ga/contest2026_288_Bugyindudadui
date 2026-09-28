/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <stdio.h>
#include <sys/utsname.h>

static void about_event(lv_event_t *event)
{
  desk_about();
}

static void controls_event(lv_event_t *event)
{
  lv_obj_t *screen = desk_screen(DESK_BG);
  lv_obj_t *obj;

  desk_label(screen, "控制中心", 56, 45, 600, DESK_TEXT);
  desk_label(screen, "触摸体验", 56, 140, 600, DESK_MUTED);
  obj = lv_switch_create(screen);
  lv_obj_set_pos(obj, 60, 200);
  lv_obj_set_size(obj, 130, 65);
  desk_label(screen, "示例开关", 230, 215, 500, DESK_TEXT);
  obj = lv_slider_create(screen);
  lv_obj_set_pos(obj, 75, 365);
  lv_obj_set_size(obj, 840, 36);
  lv_slider_set_range(obj, 0, 100);
  lv_slider_set_value(obj, 50, LV_ANIM_OFF);
  desk_label(screen, "示例滑条 · 不改变屏幕亮度", 56, 300, 800, DESK_MUTED);
  desk_button(screen, "返回桌面", 790, 30, 180, 65, desk_home_event, NULL);
  printf("DESKTOP PAGE controls\n");
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
  desk_button(panel, "设置\n锁屏与偏好", 15, 30, 265, 155,
              desk_settings_event, NULL);
  desk_button(panel, "控制中心\n触摸体验", 300, 30, 265, 155,
              controls_event, NULL);
  desk_button(panel, "关于设备\n系统信息", 585, 30, 265, 155,
              about_event, NULL);
  if (g_desk.settings.mode != DESK_NONE)
    desk_button(screen, "锁定屏幕", 755, 490, 213, 64, desk_lock_event, NULL);
  desk_label(screen, "桌面", 56, 507, 400, DESK_MUTED);
  printf("DESKTOP PAGE home\n");
}

void desk_about(void)
{
  struct utsname sys;
  char text[320];
  lv_obj_t *screen = desk_screen(DESK_BG);

  uname(&sys);
  snprintf(text, sizeof(text),
           "ESP32-P4X Function EV Board\n\n"
           "系统  %s %s\n架构  %s\n"
           "屏幕  1024 × 600\n触摸  GT911\n"
           "桌面  0.1", sys.sysname, sys.release, sys.machine);
  desk_label(screen, "关于设备", 56, 45, 600, DESK_TEXT);
  desk_label(screen, text, 70, 155, 850, DESK_TEXT);
  desk_button(screen, "返回桌面", 790, 30, 180, 65, desk_home_event, NULL);
  printf("DESKTOP PAGE about\n");
}
