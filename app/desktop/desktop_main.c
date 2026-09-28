/* SPDX-License-Identifier: Apache-2.0 */
#include "desktop.h"
#include <nuttx/mutex.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct desk_state g_desk;
static mutex_t g_owner = NXMUTEX_INITIALIZER;

int main(int argc, char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result = {0};
  int ret;
  bool first = true;

  if (nxmutex_trylock(&g_owner) < 0) return 1;
  if (lv_is_initialized())
    {
      fprintf(stderr, "DESKTOP another LVGL application is active\n");
      nxmutex_unlock(&g_owner);
      return 1;
    }
  memset(&g_desk, 0, sizeof(g_desk));
  ret = desk_settings_load(&g_desk.settings);
  g_desk.storage_ok = ret == 0;
  printf("DESKTOP settings load=%d mode=%lu\n", ret,
         (unsigned long)g_desk.settings.mode);
  lv_init();
  lv_nuttx_dsc_init(&info);
  lv_nuttx_init(&info, &result);
  if (!result.disp || !result.indev)
    {
      fprintf(stderr, "DESKTOP display/input unavailable\n");
      ret = 1;
      goto cleanup;
    }
  if (!g_desk.storage_ok)
    {
      desk_screen(DESK_BG);
      desk_label(g_desk.screen, "无法读取锁屏设置", 90, 160, 800, DESK_TEXT);
      desk_label(g_desk.screen, "请检查设备存储后重新启动。", 90, 230, 800, DESK_MUTED);
      desk_button(g_desk.screen, "退出", 90, 335, 200, 70, desk_exit_request, NULL);
    }
  else desk_lock();
  while (!g_desk.exit_requested)
    {
      lv_timer_handler();
      if (first)
        {
          printf("DESKTOP READY\n");
          fflush(stdout);
          first = false;
        }
      usleep(10000);
    }
  ret = 0;
cleanup:
  lv_nuttx_deinit(&result);
  lv_deinit();
  desk_zero(&g_desk, sizeof(g_desk));
  nxmutex_unlock(&g_owner);
  printf("DESKTOP EXIT\n");
  return ret;
}
