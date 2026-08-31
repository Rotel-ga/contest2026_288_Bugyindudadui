/****************************************************************************
 * board/contest_board/src/esp32p4_board_i2c.c
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

#if defined(CONFIG_I2C_DRIVER) && \
    defined(CONFIG_ESPRESSIF_I2C1_MASTER_MODE)

#include <debug.h>
#include <errno.h>

#include <nuttx/i2c/i2c_master.h>

#include "espressif/esp_i2c.h"
#include "esp32p4-function-ev-board.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_i2c_init
 *
 * Description:
 *   Initialize I2C1 and register it as /dev/i2c1.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value on failure.
 *
 ****************************************************************************/

int board_i2c_init(void)
{
  struct i2c_master_s *i2c;
  int ret;

  i2c = esp_i2cbus_initialize(ESPRESSIF_I2C1);
  if (i2c == NULL)
    {
      i2cerr("Failed to initialize I2C1\n");
      return -ENODEV;
    }

  ret = i2c_register(i2c, ESPRESSIF_I2C1);
  if (ret < 0)
    {
      i2cerr("Failed to register I2C1 driver: %d\n", ret);
      esp_i2cbus_uninitialize(i2c);
    }

  return ret;
}

#endif /* CONFIG_I2C_DRIVER && CONFIG_ESPRESSIF_I2C1_MASTER_MODE */
