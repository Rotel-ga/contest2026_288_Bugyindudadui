/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <stdio.h>
#include <string.h>

enum pin_stage { PIN_UNLOCK, PIN_AUTHORIZE, PIN_NEW, PIN_CONFIRM };
static enum pin_stage g_stage;
static enum desk_lock_mode g_target;
static char g_digits[DESK_PIN_DIGITS + 1];
static char g_first[DESK_PIN_DIGITS + 1];
static unsigned int g_count;
static lv_obj_t *g_prompt;
static lv_obj_t *g_dots;
static lv_point_t g_press;

static void pin_page(void);

static void pin_reset(void)
{
  desk_zero(g_digits, sizeof(g_digits));
  g_count = 0;
}

static void update_dots(void)
{
  char dots[64] = "";
  unsigned int i;
  for (i = 0; i < DESK_PIN_DIGITS; i++)
    strcat(dots, i < g_count ? "●  " : "○  ");
  lv_label_set_text(g_dots, dots);
}

static void apply_mode(const char *pin)
{
  struct desk_settings next = g_desk.settings;
  int ret;

  next.mode = g_target;
  if (g_target == DESK_PIN)
    ret = desk_pin_set(&next, pin);
  else
    {
      desk_zero(next.salt, sizeof(next.salt));
      desk_zero(next.digest, sizeof(next.digest));
      ret = 0;
    }

  if (ret == 0) ret = desk_settings_save(&next);
  if (ret == 0) g_desk.settings = next;
  desk_zero(&next, sizeof(next));
  desk_zero(g_first, sizeof(g_first));
  pin_reset();
  desk_settings_page();
  desk_notice(ret == 0 ? "解锁方式已保存" : "保存失败，原设置未改变");
  printf("DESKTOP mode save result=%d\n", ret);
}

static void pin_accept(void)
{
  if (g_stage == PIN_UNLOCK || g_stage == PIN_AUTHORIZE)
    {
      if (!desk_pin_verify(g_digits))
        {
          g_desk.failed_pins++;
          if (g_desk.failed_pins >= 5)
            {
              g_desk.retry_after = lv_tick_get() + 30000;
              g_desk.failed_pins = 0;
              lv_label_set_text(g_prompt, "尝试过多，请等待 30 秒后重试");
            }
          else lv_label_set_text(g_prompt, "密码不正确，请重试");
          pin_reset();
          update_dots();
          return;
        }
      g_desk.failed_pins = 0;
      pin_reset();
      if (g_stage == PIN_UNLOCK) desk_home();
      else if (g_target == DESK_PIN)
        {
          g_stage = PIN_NEW;
          pin_page();
        }
      else apply_mode(NULL);
    }
  else if (g_stage == PIN_NEW)
    {
      memcpy(g_first, g_digits, sizeof(g_first));
      g_stage = PIN_CONFIRM;
      pin_reset();
      pin_page();
    }
  else
    {
      if (memcmp(g_first, g_digits, DESK_PIN_DIGITS) != 0)
        {
          desk_zero(g_first, sizeof(g_first));
          pin_reset();
          g_stage = PIN_NEW;
          pin_page();
          lv_label_set_text(g_prompt, "两次输入不同，请重新设置");
          return;
        }
      apply_mode(g_digits);
    }
}

static void key_event(lv_event_t *event)
{
  intptr_t key = (intptr_t)lv_event_get_user_data(event);
  if ((int32_t)(g_desk.retry_after - lv_tick_get()) > 0)
    {
      lv_label_set_text(g_prompt, "请稍后重试");
      return;
    }
  if (key == 10)
    {
      if (g_count) g_digits[--g_count] = '\0';
    }
  else if (key == 11)
    {
      if (g_count == DESK_PIN_DIGITS) pin_accept();
      else lv_label_set_text(g_prompt, "请输入完整的 6 位密码");
      return;
    }
  else if (g_count < DESK_PIN_DIGITS)
    {
      g_digits[g_count++] = '0' + key;
      g_digits[g_count] = '\0';
    }
  update_dots();
}

static void pin_cancel(lv_event_t *event)
{
  pin_reset();
  desk_zero(g_first, sizeof(g_first));
  if (g_stage == PIN_UNLOCK) desk_lock();
  else desk_settings_page();
}

static void pin_page(void)
{
  const char *titles[] = {"输入密码解锁", "验证当前密码", "设置 6 位 PIN", "再次输入 PIN"};
  const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "删除", "0", "确认"};
  const int values[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 11};
  lv_obj_t *screen = desk_screen(DESK_BG);
  int i;

  desk_label(screen, titles[g_stage], 62, 95, 510, DESK_TEXT);
  g_prompt = desk_label(screen, "请输入 6 位数字密码", 62, 150, 485, DESK_MUTED);
  g_dots = desk_label(screen, "", 62, 230, 480, DESK_TEXT);
  update_dots();
  desk_button(screen, "返回", 62, 435, 195, 70, pin_cancel, NULL);
  for (i = 0; i < 12; i++)
    desk_button(screen, keys[i], 600 + (i % 3) * 115, 85 + (i / 3) * 112,
                100, 94, key_event, (void *)(intptr_t)values[i]);
}

void desk_request_mode(enum desk_lock_mode mode)
{
  if (!g_desk.storage_ok)
    {
      desk_notice("存储不可用，无法保存设置");
      return;
    }
  g_target = mode;
  pin_reset();
  desk_zero(g_first, sizeof(g_first));
  if (g_desk.settings.mode == DESK_PIN)
    {
      g_stage = PIN_AUTHORIZE;
      pin_page();
    }
  else if (mode == DESK_PIN)
    {
      g_stage = PIN_NEW;
      pin_page();
    }
  else apply_mode(NULL);
}

void desk_pin_unlock(void)
{
  pin_reset();
  g_stage = PIN_UNLOCK;
  pin_page();
}

static void pin_open(lv_event_t *event)
{
  desk_pin_unlock();
}

static void swipe_event(lv_event_t *event)
{
  lv_indev_t *input = lv_indev_active();
  lv_point_t point;
  if (!input) return;
  lv_indev_get_point(input, &point);
  if (lv_event_get_code(event) == LV_EVENT_PRESSED)
    {
      g_press = point;
      printf("DESKTOP swipe start x=%d y=%d\n", point.x, point.y);
    }
  else if (lv_event_get_code(event) == LV_EVENT_RELEASED &&
           g_press.y - point.y > 90)
    {
      printf("DESKTOP swipe unlock\n");
      desk_home();
    }
}

void desk_lock(void)
{
  lv_obj_t *screen;
  lv_obj_t *label;

  pin_reset();
  desk_zero(g_first, sizeof(g_first));
  if (g_desk.settings.mode == DESK_NONE)
    {
      desk_home();
      return;
    }
  screen = desk_screen(0x182747);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x09101e), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
  desk_label(screen, "openvela", 60, 40, 600, DESK_MUTED);
  desk_label(screen, "欢迎使用", 60, 175, 800, DESK_TEXT);
  desk_label(screen, "属于你的交互空间", 60, 222, 800, DESK_MUTED);
  if (g_desk.settings.mode == DESK_PIN)
    desk_button(screen, "输入密码解锁", 355, 435, 315, 80, pin_open, NULL);
  else
    {
      label = desk_label(screen, "↑ 向上滑动解锁", 315, 452, 500, DESK_TEXT);
      lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
      lv_obj_add_event_cb(screen, swipe_event, LV_EVENT_PRESSED, NULL);
      lv_obj_add_event_cb(screen, swipe_event, LV_EVENT_RELEASED, NULL);
      lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    }
  printf("DESKTOP PAGE lock mode=%lu\n", (unsigned long)g_desk.settings.mode);
}
