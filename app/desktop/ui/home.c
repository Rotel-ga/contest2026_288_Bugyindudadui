/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include "../fallguard/fallguard.h"
#include "../photo_identify/photo_identify.h"
#include <stdio.h>

void desk_app_center(lv_event_t *event)
{
  lv_obj_t *screen = desk_screen(DESK_BG);
  lv_obj_t *card;

  desk_label(screen, "应用中心", 56, 45, 600, DESK_TEXT);
  desk_button(screen, "返回桌面", 790, 30, 180, 65, desk_home_event, NULL);
  card = desk_button(screen, "跌倒监护", 56, 145, 280, 170,
                     fallguard_event, NULL);
  card = desk_button(screen, "拍照识物", 366, 145, 280, 170,
                     photo_identify_event, NULL);
  (void)card;
  desk_label(screen, "原生应用", 56, 345, 500, DESK_MUTED);
  printf("DESKTOP PAGE app-center\n");
}

void desk_home(void)
{
  lv_obj_t *screen = desk_screen(DESK_BG);
  lv_obj_t *panel;
  lv_obj_t *settings;
  lv_obj_t *apps;
  static int32_t columns[] =
  {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST
  };
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  desk_label(screen, "openvela", 56, 32, 600, DESK_MUTED);
  desk_label(screen, "你好，欢迎回来", 56, 93, 700, DESK_TEXT);
  desk_label(screen, "你的设备，触手可及", 56, 135, 700, DESK_MUTED);
  panel = lv_obj_create(screen);
  lv_obj_set_pos(panel, 56, 200);
  lv_obj_set_size(panel, 912, 245);
  lv_obj_set_style_radius(panel, 28, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x202f4c), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 30, 0);
  lv_obj_set_style_pad_column(panel, 20, 0);
  lv_obj_set_style_grid_column_dsc_array(panel, columns, 0);
  lv_obj_set_style_grid_row_dsc_array(panel, rows, 0);
  lv_obj_set_layout(panel, LV_LAYOUT_GRID);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  settings = desk_button(panel, "设置", 0, 0, 1, 1,
                         desk_settings_event, NULL);
  lv_obj_set_grid_cell(settings, LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  apps = desk_button(panel, "应用中心", 0, 0, 1, 1,
                     desk_app_center, NULL);
  lv_obj_set_grid_cell(apps, LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  if (g_desk.settings.mode != DESK_NONE)
    desk_button(screen, "锁定屏幕", 755, 490, 213, 64, desk_lock_event, NULL);
  desk_label(screen, "桌面", 56, 507, 400, DESK_MUTED);
  printf("DESKTOP PAGE home\n");
}
