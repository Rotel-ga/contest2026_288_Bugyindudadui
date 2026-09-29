/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <stdio.h>
#include <sys/utsname.h>

static lv_obj_t *g_settings_nav;

static void style_nav_button(lv_obj_t *button)
{
  lv_obj_set_height(button, 72);
  lv_obj_set_style_radius(button, 14, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(DESK_CARD), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(button, lv_color_hex(DESK_TEXT), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x2a3d61), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(button, lv_color_hex(DESK_TEXT), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(button, lv_color_hex(DESK_BLUE), LV_STATE_CHECKED);
  lv_obj_set_style_text_color(button, lv_color_hex(DESK_TEXT), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(button, lv_color_hex(DESK_BLUE),
                            LV_STATE_CHECKED | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(button, lv_color_hex(DESK_TEXT),
                              LV_STATE_CHECKED | LV_STATE_PRESSED);
}

static void select_nav(lv_event_t *event)
{
  uint32_t i;

  if (!event || !g_settings_nav)
    {
      return;
    }

  for (i = 0; i < lv_obj_get_child_count(g_settings_nav); i++)
    {
      lv_obj_remove_state(lv_obj_get_child(g_settings_nav, i),
                          LV_STATE_CHECKED);
    }

  lv_obj_add_state(lv_event_get_target(event), LV_STATE_CHECKED);
}

static void mode_event(lv_event_t *event)
{
  desk_request_mode((enum desk_lock_mode)(uintptr_t)lv_event_get_user_data(event));
}

static void settings_about(lv_event_t *event)
{
  select_nav(event);
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

  select_nav(event);
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
  const char *names[] =
  {
    "设备型号", "操作系统", "处理器架构", "显示屏", "触摸控制器", "桌面版本"
  };
  char system[96];
  const char *values[6];
  lv_obj_t *info;
  lv_obj_t *label;
  int i;
  static int32_t columns[] = {150, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] =
  {
    48, 48, 48, 48, 48, 48, LV_GRID_TEMPLATE_LAST
  };

  uname(&sys);
  snprintf(system, sizeof(system), "%s %s", sys.sysname, sys.release);
  values[0] = "ESP32-P4X Function EV Board";
  values[1] = system;
  values[2] = sys.machine;
  values[3] = "1024 × 600 MIPI DSI";
  values[4] = "GT911";
  values[5] = "0.1";
  select_nav(event);
  lv_obj_clean(g_desk.content);
  desk_label(g_desk.content, "关于设备", 24, 24, 540, DESK_TEXT);
  info = lv_obj_create(g_desk.content);
  lv_obj_set_pos(info, 24, 80);
  lv_obj_set_size(info, 600, 345);
  lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(info, 0, 0);
  lv_obj_set_style_pad_all(info, 0, 0);
  lv_obj_set_style_grid_column_dsc_array(info, columns, 0);
  lv_obj_set_style_grid_row_dsc_array(info, rows, 0);
  lv_obj_set_layout(info, LV_LAYOUT_GRID);
  lv_obj_remove_flag(info, LV_OBJ_FLAG_SCROLLABLE);
  for (i = 0; i < 6; i++)
    {
      label = lv_label_create(info);
      lv_label_set_text(label, names[i]);
      lv_obj_set_style_text_color(label, lv_color_hex(DESK_MUTED), 0);
      lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1,
                           LV_GRID_ALIGN_CENTER, i, 1);
      label = lv_label_create(info);
      lv_label_set_text(label, values[i]);
      lv_obj_set_style_text_color(label, lv_color_hex(DESK_TEXT), 0);
      lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 1, 1,
                           LV_GRID_ALIGN_CENTER, i, 1);
    }
}

static void lock_options(lv_event_t *event)
{
  const char *modes[] = {"无需解锁", "滑动解锁", "PIN 密码"};
  char text[128];
  int i;

  select_nav(event);
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
  lv_obj_t *button;

  desk_label(screen, "设置", 35, 40, 500, DESK_TEXT);
  desk_button(screen, "返回桌面", 790, 25, 185, 62, desk_home_event, NULL);
  g_settings_nav = lv_obj_create(screen);
  lv_obj_set_pos(g_settings_nav, 25, 115);
  lv_obj_set_size(g_settings_nav, 270, 465);
  lv_obj_set_style_bg_color(g_settings_nav, lv_color_hex(DESK_CARD), 0);
  lv_obj_set_style_border_width(g_settings_nav, 0, 0);
  lv_obj_set_style_radius(g_settings_nav, 24, 0);
  lv_obj_set_style_pad_all(g_settings_nav, 10, 0);
  lv_obj_set_style_pad_row(g_settings_nav, 8, 0);
  lv_obj_set_flex_flow(g_settings_nav, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_settings_nav, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(g_settings_nav, LV_OBJ_FLAG_SCROLLABLE);
  button = desk_button(g_settings_nav, "锁屏与密码", 0, 0, 250, 72,
                       lock_options, NULL);
  style_nav_button(button);
  lv_obj_add_state(button, LV_STATE_CHECKED);
  button = desk_button(g_settings_nav, "控制中心", 0, 0, 250, 72,
                       control_center, NULL);
  style_nav_button(button);
  button = desk_button(g_settings_nav, "关于设备", 0, 0, 250, 72,
                       device_about, NULL);
  style_nav_button(button);
  button = desk_button(g_settings_nav, "关于桌面", 0, 0, 250, 72,
                       settings_about, NULL);
  style_nav_button(button);
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
