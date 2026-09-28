/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <stdio.h>
#include <sys/utsname.h>

static void mode_event(lv_event_t *event)
{
  desk_request_mode((enum desk_lock_mode)(uintptr_t)lv_event_get_user_data(event));
}

static void settings_about(lv_event_t *event)
{
  lv_obj_clean(g_desk.content);
  desk_label(g_desk.content, "关于桌面", 24, 24, 540, DESK_TEXT);
  desk_label(g_desk.content, "openvela 桌面 0.1\n\n"
             "支持滑动解锁、PIN 密码与关闭锁屏。\n"
             "触摸与显示均使用系统驱动。", 24, 90, 545, DESK_MUTED);
  desk_button(g_desk.content, "退出桌面", 24, 270, 250, 65,
              desk_exit_request, NULL);
  desk_label(g_desk.content, "退出后可从串口重新启动桌面。", 24, 355, 540,
             DESK_MUTED);
}

static void control_center(lv_event_t *event)
{
  lv_obj_t *obj;

  lv_obj_clean(g_desk.content);
  desk_label(g_desk.content, "控制中心", 24, 24, 540, DESK_TEXT);
  desk_label(g_desk.content, "触摸控件体验", 24, 68, 540, DESK_MUTED);
  obj = lv_switch_create(g_desk.content);
  lv_obj_set_pos(obj, 28, 135);
  lv_obj_set_size(obj, 130, 65);
  desk_label(g_desk.content, "示例开关", 195, 150, 350, DESK_TEXT);
  desk_label(g_desk.content, "示例滑条 · 不改变屏幕亮度", 24, 245, 560,
             DESK_MUTED);
  obj = lv_slider_create(g_desk.content);
  lv_obj_set_pos(obj, 28, 315);
  lv_obj_set_size(obj, 560, 36);
  lv_slider_set_range(obj, 0, 100);
  lv_slider_set_value(obj, 50, LV_ANIM_OFF);
}

static void device_about(lv_event_t *event)
{
  struct utsname sys;
  char text[320];

  uname(&sys);
  snprintf(text, sizeof(text),
           "ESP32-P4X Function EV Board\n\n"
           "系统  %s %s\n架构  %s\n"
           "屏幕  1024 × 600\n触摸  GT911\n"
           "桌面  0.1", sys.sysname, sys.release, sys.machine);
  lv_obj_clean(g_desk.content);
  desk_label(g_desk.content, "关于设备", 24, 24, 540, DESK_TEXT);
  desk_label(g_desk.content, text, 24, 90, 570, DESK_TEXT);
}

static void lock_options(lv_event_t *event)
{
  const char *modes[] = {"无需解锁", "滑动解锁", "PIN 密码"};
  char text[128];
  int i;

  lv_obj_clean(g_desk.content);
  desk_label(g_desk.content, "锁屏与密码", 24, 24, 540, DESK_TEXT);
  desk_label(g_desk.content, "选择打开设备后的解锁方式", 24, 67, 550, DESK_MUTED);
  for (i = 0; i < 3; i++)
    {
      snprintf(text, sizeof(text), "%s%s", modes[i],
               g_desk.settings.mode == i ? "    已选" : "");
      lv_obj_t *button = desk_button(g_desk.content, text, 24, 120 + 87 * i,
                                    560, 70, mode_event, (void *)(uintptr_t)i);
      if (g_desk.settings.mode == i)
        lv_obj_set_style_bg_color(button, lv_color_hex(DESK_BLUE), 0);
    }

  desk_label(g_desk.content,
             g_desk.storage_ok ? "设置自动保存，重新开机后生效。" :
             "存储不可用，暂时不能更改设置。",
             24, 400, 555, DESK_MUTED);
}

void desk_settings_page(void)
{
  lv_obj_t *screen = desk_screen(0x0d1626);

  desk_label(screen, "设置", 35, 40, 500, DESK_TEXT);
  desk_button(screen, "返回桌面", 790, 25, 185, 62, desk_home_event, NULL);
  desk_button(screen, "锁屏与密码", 25, 120, 270, 65, lock_options, NULL);
  desk_button(screen, "控制中心", 25, 200, 270, 65, control_center, NULL);
  desk_button(screen, "关于设备", 25, 280, 270, 65, device_about, NULL);
  desk_button(screen, "关于桌面", 25, 360, 270, 65, settings_about, NULL);
  g_desk.content = lv_obj_create(screen);
  lv_obj_set_pos(g_desk.content, 325, 115);
  lv_obj_set_size(g_desk.content, 660, 465);
  lv_obj_set_style_radius(g_desk.content, 24, 0);
  lv_obj_set_style_bg_color(g_desk.content, lv_color_hex(DESK_CARD), 0);
  lv_obj_set_style_border_width(g_desk.content, 0, 0);
  lv_obj_remove_flag(g_desk.content, LV_OBJ_FLAG_SCROLLABLE);
  lock_options(NULL);
  printf("DESKTOP PAGE settings\n");
}
