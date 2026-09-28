/* SPDX-License-Identifier: Apache-2.0 */
#include "photo_identify.h"
#include "identify_bridge.h"
#include "../desktop/desktop.h"
#include <nuttx/mutex.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <nuttx/config.h>
#include "../fallguard/camera_preview.h"
#include "../fallguard/panel_control.h"
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST
#  include "../p4x_selftest/camera_live.h"
#endif

#define MESSAGE_CAPACITY 1024
#define INITIAL_MESSAGE "等待识别结果。\n\n拍照后，这里将显示电脑端 AI 返回的物品名称和提示信息。"
#define DEMO_MESSAGE "已点击拍照识别。\n\n实时预览已独立运行，拍照识别按钮暂未连接电脑端 AI。\n\n后续识别提示将在此处显示。"

static mutex_t g_message_lock = NXMUTEX_INITIALIZER;
static char g_message[MESSAGE_CAPACITY] = INITIAL_MESSAGE;
static bool g_message_dirty = true;
static lv_obj_t *g_screen;
static lv_obj_t *g_result_label;
static lv_obj_t *g_video;
static lv_obj_t *g_video_hint;
static uint16_t *g_video_pixels;
static uint32_t g_video_sequence;
static bool g_live_owned;
static int g_video_status;
static lv_image_dsc_t g_video_dsc =
{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565,
  .header.w = CAMERA_PREVIEW_W,
  .header.h = CAMERA_PREVIEW_H,
  .header.stride = CAMERA_PREVIEW_W * 2,
  .data_size = CAMERA_PREVIEW_BYTES
};

static void video_stop(void)
{
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST
  if (g_live_owned) camera_live_stop();
#endif
  g_live_owned = false;
}

static void video_poll(void)
{
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST
  if (!g_live_owned || !g_video_pixels || identify_pending()) return;
  uint32_t sequence = camera_live_take(g_video_pixels, g_video_sequence);
  if (sequence != g_video_sequence)
    {
      char text[64];
      g_video_sequence = sequence;
      lv_image_cache_drop(&g_video_dsc);
      lv_image_set_src(g_video, &g_video_dsc);
      lv_obj_remove_flag(g_video, LV_OBJ_FLAG_HIDDEN);
      snprintf(text, sizeof(text), "Live #%lu", (unsigned long)sequence);
      lv_label_set_text(g_video_hint, text);
      lv_obj_align(g_video_hint, LV_ALIGN_TOP_LEFT, 8, 4);
      lv_obj_invalidate(g_video);
    }
  int status = camera_live_status();
  if (status != 1 && g_video_status != status)
    {
      char text[80];
      g_video_status = status;
      snprintf(text, sizeof(text), "Preview stopped (%d)", status);
      lv_label_set_text(g_video_hint, text);
      photo_identify_set_message("预览已停止，请返回后重试。若监控正在运行，请先停止监控。");
    }
#endif
}

int photo_identify_set_message(const char *message)
{
  size_t length;
  if (message == NULL) return -EINVAL;
  length = strnlen(message, MESSAGE_CAPACITY);
  if (length >= MESSAGE_CAPACITY) return -E2BIG;
  nxmutex_lock(&g_message_lock);
  memcpy(g_message, message, length + 1);
  g_message_dirty = true;
  nxmutex_unlock(&g_message_lock);
  return 0;
}

void photo_identify_poll(void)
{
  char message[MESSAGE_CAPACITY];
  bool changed;
  if (g_screen == NULL || g_screen != g_desk.screen)
    {
      video_stop();
      return;
    }
  video_poll();
  if (nxmutex_trylock(&g_message_lock) < 0) return;
  changed = g_message_dirty;
  if (changed)
    {
      memcpy(message, g_message, sizeof(message));
      g_message_dirty = false;
    }
  nxmutex_unlock(&g_message_lock);
  if (changed) lv_label_set_text(g_result_label, message);
}

void photo_identify_reset(void)
{
  video_stop();
  identify_cancel();
  g_screen = NULL;
  g_result_label = NULL;
  free(g_video_pixels);
  g_video_pixels = NULL;
  photo_identify_set_message(INITIAL_MESSAGE);
}

static void screen_deleted(lv_event_t *event)
{
  if (lv_event_get_target(event) == g_screen)
    {
      video_stop();
      identify_cancel();
      lv_image_cache_drop(&g_video_dsc);
      free(g_video_pixels);
      g_video_pixels = NULL;
      g_video_dsc.data = NULL;
      g_screen = NULL;
      g_result_label = NULL;
    }
}

static void return_to_apps(lv_event_t *event)
{
  video_stop();
  desk_app_center(event);
}

static void capture_clicked(lv_event_t *event)
{
  (void)event;
  if (!g_video_sequence || !g_video_pixels)
    {
      photo_identify_set_message("画面尚未就绪，请稍后拍照。");
    }
  else if (identify_pending())
    {
      photo_identify_set_message("正在识别当前照片，请等待结果。");
    }
  else
    {
      int ret = identify_request(g_video_pixels);
      photo_identify_set_message(ret == 0 ?
        "已锁定当前画面，等待电脑 AI 识别。请保持识物脚本运行。" :
        "拍照失败，请稍后重试。");
    }
  photo_identify_poll();
}

void photo_identify_event(lv_event_t *event)
{
  lv_obj_t *screen;
  lv_obj_t *image_box;
  lv_obj_t *result_box;
  lv_obj_t *result_scroll;
  lv_obj_t *button;
  (void)event;

  screen = desk_screen(DESK_BG);
  g_screen = screen;
  lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, NULL);
  desk_label(screen, "拍照识物", 56, 35, 600, DESK_TEXT);
  desk_button(screen, "返回应用中心", 765, 25, 210, 62,
              return_to_apps, NULL);

  image_box = lv_obj_create(screen);
  lv_obj_set_pos(image_box, 56, 115);
  lv_obj_set_size(image_box, 590, 300);
  lv_obj_set_style_radius(image_box, 24, 0);
  lv_obj_set_style_bg_color(image_box, lv_color_hex(0x0b1220), 0);
  lv_obj_set_style_border_width(image_box, 1, 0);
  lv_obj_set_style_border_color(image_box, lv_color_hex(0x35476a), 0);
  lv_obj_set_style_pad_all(image_box, 0, 0);
  lv_obj_remove_flag(image_box, LV_OBJ_FLAG_SCROLLABLE);
  g_video = lv_image_create(image_box);
  lv_obj_center(g_video);
  lv_obj_add_flag(g_video, LV_OBJ_FLAG_HIDDEN);
  g_video_hint = desk_label(image_box, "Starting camera...", 30, 112, 530, DESK_MUTED);
  /* Reusing this descriptor is safe only after the prior page is deleted. */
  if (g_video_pixels)
    {
      lv_image_cache_drop(&g_video_dsc);
      free(g_video_pixels);
    }
  g_video_pixels = malloc(CAMERA_PREVIEW_BYTES);
  g_video_dsc.data = (const uint8_t *)g_video_pixels;
  g_video_sequence = 0;
  g_video_status = 1;

  result_box = lv_obj_create(screen);
  lv_obj_set_pos(result_box, 675, 115);
  lv_obj_set_size(result_box, 293, 300);
  lv_obj_set_style_radius(result_box, 24, 0);
  lv_obj_set_style_bg_color(result_box, lv_color_hex(DESK_CARD), 0);
  lv_obj_set_style_border_width(result_box, 0, 0);
  lv_obj_set_style_pad_all(result_box, 0, 0);
  lv_obj_remove_flag(result_box, LV_OBJ_FLAG_SCROLLABLE);
  desk_label(result_box, "识别提示", 20, 16, 253, DESK_TEXT);

  result_scroll = lv_obj_create(result_box);
  lv_obj_set_pos(result_scroll, 16, 64);
  lv_obj_set_size(result_scroll, 261, 218);
  lv_obj_set_style_bg_opa(result_scroll, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(result_scroll, 0, 0);
  lv_obj_set_style_pad_all(result_scroll, 4, 0);
  lv_obj_set_scroll_dir(result_scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(result_scroll, LV_SCROLLBAR_MODE_AUTO);
  g_result_label = desk_label(result_scroll, "", 0, 0, 243, DESK_MUTED);
  lv_label_set_long_mode(g_result_label, LV_LABEL_LONG_WRAP);

  button = desk_button(screen, "拍照识别", 56, 460, 250, 70,
                       capture_clicked, NULL);
  lv_obj_set_style_bg_color(button, lv_color_hex(DESK_BLUE), 0);
  desk_label(screen, "拍照锁定当前画面，等待识别提示", 330, 477, 638, DESK_MUTED);

  nxmutex_lock(&g_message_lock);
  g_message_dirty = true;
  nxmutex_unlock(&g_message_lock);
#ifdef CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST
  struct panel_control_state monitor;
  panel_control_snapshot(&monitor);
  int ret = monitor.requested ? -EBUSY :
            (g_video_pixels ? camera_live_start() : -ENOMEM);
  g_live_owned = ret == 0;
  if (ret < 0)
    {
      lv_label_set_text(g_video_hint, "Camera unavailable");
      photo_identify_set_message("无法启动预览，请先停止跌倒监控，然后返回重试。");
    }
#else
  lv_label_set_text(g_video_hint, "Camera not enabled");
#endif
  photo_identify_poll();
  printf("DESKTOP PAGE photo-identify\n");
}
