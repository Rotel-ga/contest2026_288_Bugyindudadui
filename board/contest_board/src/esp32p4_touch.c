/****************************************************************************
 * board/contest_board/src/esp32p4_touch.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/kthread.h>
#include <nuttx/signal.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include "espressif/esp_i2c.h"

static struct i2c_master_s *g_i2c;
static struct touch_lowerhalf_s g_lower;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int touch_read_reg(uint16_t reg, uint8_t *buf, int len)
{
  uint8_t index[2] =
  {
    reg >> 8, reg & 0xff
  };

  struct i2c_msg_s msgs[2] =
  {
    {
      100000, 0x5d, I2C_M_NOSTOP, index, 2
    },
    {
      100000, 0x5d, I2C_M_READ, buf, len
    }
  };

  return I2C_TRANSFER(g_i2c, msgs, 2);
}

static int touch_ack(void)
{
  uint8_t buf[3] =
  {
    0x81, 0x4e, 0
  };

  struct i2c_msg_s msg =
  {
    100000, 0x5d, 0, buf, 3
  };

  return I2C_TRANSFER(g_i2c, &msg, 1);
}

static void touch_report(uint8_t flags, uint8_t id, int x, int y)
{
  struct touch_sample_s sample;

  memset(&sample, 0, sizeof(sample));
  sample.npoints = 1;
  sample.point[0].id = id;
  sample.point[0].flags = flags | TOUCH_ID_VALID | TOUCH_POS_VALID;
  sample.point[0].x = x;
  sample.point[0].y = y;
  sample.point[0].timestamp = touch_get_time();
  if (flags != TOUCH_MOVE)
    {
      syslog(LOG_INFO, "GT911 event=%u x=%d y=%d\n", flags, x, y);
    }

  touch_event(g_lower.priv, &sample);
}

static int touch_worker(int argc, char *argv[])
{
  uint8_t status;
  uint8_t points[40];
  uint8_t id = 0;
  bool down = false;
  unsigned int errors = 0;
  int x = 0;
  int y = 0;
  int nextx;
  int nexty;
  int count;
  int ret;
  const char *stage = "status";

  for (; ; )
    {
      nxsig_usleep(16000);
      stage = "status";
      ret = touch_read_reg(0x814e, &status, 1);
      if (ret < 0)
        {
          goto error;
        }

      if (!(status & 0x80))
        {
          continue;
        }

      count = status & 0x0f;
      if (count > 5)
        {
          ret = -EPROTO;
          goto error;
        }

      if (count > 0)
        {
          stage = "points";
          ret = touch_read_reg(0x814f, points, count * 8);
          if (ret < 0)
            {
              goto error;
            }
        }

      stage = "ack";
      ret = touch_ack();
      if (ret < 0)
        {
          goto error;
        }

      errors = 0;
      if (down && (count == 0 || points[0] != id))
        {
          touch_report(TOUCH_UP, id, x, y);
          down = false;
        }

      if (count > 0)
        {
          /* One primary contact. Mirror once, here, into pixel space. */

          nextx = 1023 - (points[1] | points[2] << 8);
          nexty = 599 - (points[3] | points[4] << 8);
          nextx = nextx < 0 ? 0 : nextx;
          nexty = nexty < 0 ? 0 : nexty;
          if (!down || nextx != x || nexty != y)
            {
              touch_report(down ? TOUCH_MOVE : TOUCH_DOWN,
                           points[0], nextx, nexty);
            }

          id = points[0];
          x = nextx;
          y = nexty;
          down = true;
        }

      continue;
error:
      if (errors++ % 60 == 0)
        {
          syslog(LOG_ERR, "GT911 %s error=%d; state retained\n",
                 stage, ret);
        }

      nxsig_usleep(100000);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_touch_initialize(void)
{
  uint8_t id[4];
  int ret;

  if (g_i2c != NULL)
    {
      return -EALREADY;
    }

  g_i2c = esp_i2cbus_initialize(ESPRESSIF_I2C1);
  if (g_i2c == NULL)
    {
      return -ENODEV;
    }

  ret = touch_read_reg(0x8140, id, sizeof(id));
  if (ret >= 0 && memcmp(id, "911", 3) != 0)
    {
      ret = -ENODEV;
    }

  if (ret < 0)
    {
      goto fail;
    }

  g_lower.maxpoint = 1;
  ret = touch_register(&g_lower, "/dev/input0", 64);
  if (ret < 0)
    {
      goto fail;
    }

  ret = kthread_create("gt911", 100, 4096, touch_worker, NULL);
  if (ret < 0)
    {
      touch_unregister(&g_lower, "/dev/input0");
      goto fail;
    }

  syslog(LOG_INFO, "GT911 /dev/input0 ready; polling, mirror X/Y\n");
  return 0;
fail:
  esp_i2cbus_uninitialize(g_i2c);
  g_i2c = NULL;
  return ret;
}
