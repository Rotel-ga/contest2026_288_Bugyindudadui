/****************************************************************************
 * board/contest_board/src/esp32p4_bringup.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <sys/mount.h>

#include <nuttx/fs/fs.h>

#ifdef CONFIG_VIDEO_FB
#  include <nuttx/video/fb.h>
#endif

#include "esp32p4-function-ev-board.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp_bringup
 *
 * Description:
 *   Perform architecture-specific initialization for the L0 (minimal NSH)
 *   baseline.  Peripheral bring-up (I2C/SPI/LEDC/RMT/...) is intentionally
 *   omitted here and added incrementally once the console is up.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value on failure.
 *
 ****************************************************************************/

int esp_bringup(void)
{
#if defined(CONFIG_I2C_DRIVER) && \
    defined(CONFIG_ESPRESSIF_I2C1_MASTER_MODE)
  int i2c_ret;
#endif
  int ret = OK;

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      _err("Failed to mount procfs at /proc: %d\n", ret);
    }
#endif

#ifdef CONFIG_FS_TMPFS
  /* Mount the tmpfs file system */

  ret = nx_mount(NULL, CONFIG_LIBC_TMPDIR, "tmpfs", 0, NULL);
  if (ret < 0)
    {
      _err("Failed to mount tmpfs at %s: %d\n", CONFIG_LIBC_TMPDIR, ret);
    }
#endif

#ifdef CONFIG_DEV_GPIO
  /* 初始化 GPIO 输出设备，注册 /dev/gpio0
   * 只有 defconfig 里 CONFIG_DEV_GPIO=y 时才会编译这段 */

  ret = esp_gpio_init();
  if (ret < 0)
    {
      _err("Failed to initialize GPIO Driver: %d\n", ret);
    }
#endif

#if defined(CONFIG_I2C_DRIVER) && \
    defined(CONFIG_ESPRESSIF_I2C1_MASTER_MODE)
  i2c_ret = board_i2c_init();
  if (i2c_ret < 0)
    {
      _err("Failed to initialize I2C Driver: %d\n", i2c_ret);
      if (ret >= 0)
        {
          ret = i2c_ret;
        }
    }
#endif

#ifdef CONFIG_ESP32P4_DESKTOP
  int storage_ret = board_desktop_storage_initialize();
  if (storage_ret < 0)
    {
      _err("Desktop storage failed: %d\n", storage_ret);
    }
#endif

#ifdef CONFIG_ESP32P4_BOARD_LCD
  /* Bring up the MIPI-DSI panel and register /dev/fb0.  fb_register()
   * invokes up_fbinitialize() (which runs esp32p4_lcd_initialize()) and
   * then registers the framebuffer character device.
   */

  ret = fb_register(0, 0);
  if (ret < 0)
    {
      _err("Failed to register framebuffer: %d\n", ret);
    }
#endif

#ifdef CONFIG_ESP32P4_BOARD_TOUCH
  int touch_ret = board_touch_initialize();
  if (touch_ret < 0)
    {
      _err("GT911 registration failed: %d\n", touch_ret);
      if (ret >= 0)
        {
          ret = touch_ret;
        }
    }
#endif

  return ret;
}
