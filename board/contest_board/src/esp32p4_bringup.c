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

  return ret;
}
