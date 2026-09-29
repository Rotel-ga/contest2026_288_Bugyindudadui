/****************************************************************************
 * Minimal SC2336 + ESP32-P4 MIPI CSI capture path
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sched.h>
#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/mutex.h>

#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_ldo_regulator.h"
#include "driver/isp.h"
#include "esp_private/esp_cache_private.h"
#include "hal/cam_ctlr_types.h"
#include "freertos/FreeRTOS.h"

#ifdef CONFIG_ESP32P4_USB_CONSOLE_BEST_EFFORT
#  include "esp_usbserial.h"
#endif

#include "jpeg_sw.h"
#ifdef CONFIG_ESP32P4_DESKTOP
#  include "../fallguard/camera_preview.h"
#endif
#include "p4x_camera_capture.h"
#include "camera_live.h"

#define SC2336_ADDR        0x30
#define SC2336_SDA        7
#define SC2336_SCL        8
/* SC2336 has no scaler: the sensor's crop window (0x3200-0x3207) and its
 * declared output size (0x3208-0x320b) must agree.  The mode table below is
 * the official 1280x720 timing (HTS=2240, VTS=1250 => 84 MHz PCLK => 336
 * Mbps/lane for RAW8 over 2 lanes), so keep the output size at 1280x720.
 * Do not shrink only the output-size registers - that desynchronises the
 * table and the sensor stops producing valid frames.
 */

#define SC2336_WIDTH      1280
#define SC2336_HEIGHT     720
/* The sensor feeds RAW8 into the ISP; the ISP hands RGB565 to DMA, so the
 * frame buffer is sized for the ISP output, not the sensor output.
 */

#define SC2336_BYTES_PER_PIXEL 2
#define SC2336_FRAME_SIZE \
  (SC2336_WIDTH * SC2336_HEIGHT * SC2336_BYTES_PER_PIXEL)

/* ISP pixel clock used by Espressif's own ESP32-P4 CSI reference test. */

#define CSI_ISP_CLK_HZ    (80 * 1000 * 1000)
#define SC2336_LANES      2
#define SC2336_BITRATE    336
#define SC2336_I2C        "/dev/i2c1"
#define SC2336_FPS        30

/* Pause between consecutive SCCB transactions.
 *
 * The sensor cannot keep up with back-to-back register writes: pushing the
 * 166-entry mode table out with no gap fails part way through with EIO (the
 * I2C controller reports a NACK), and reads issued immediately after the burst
 * fail the same way while reads issued a few hundred milliseconds later
 * succeed.  This was masked for a long time by CONFIG_I2C_TRACE - its
 * per-transaction logging happened to provide exactly this delay - so the
 * defect only surfaced once the debug options were reverted.
 *
 * The value is empirical.  500 us was not enough - it only moved the failure
 * from one register to another, which is itself evidence that no single
 * register is at fault.  As a scale reference, the trace that used to hide this
 * emitted about four log lines per transaction, i.e. roughly 28 ms at 115200
 * baud, so the working gap is expected to be in the millisecond range.
 * 5 ms costs ~830 ms for the whole 166-entry table, which is acceptable for a
 * one-shot capture.
 */

#define SC2336_SCCB_GAP_US 5000

/* Budget for one frame to complete.  Two frame periods at 30 fps is ~66 ms;
 * keep a wide margin but stay short enough that a failed run still reports
 * its probe counters promptly.
 */

#define SC2336_CAPTURE_TIMEOUT_MS 3000

/* Thumbnail export.  A full 1280x720 RGB565 frame is 1.8 MB, too big for the
 * 115200 console, so send a box-averaged copy: every DIV x DIV source block is
 * averaged into one pixel.  DIV=2 gives 640x360 (460800 bytes, ~53 s of
 * base64); averaging instead of point-sampling removes the decimation aliasing.
 */

/* Default sensor gain, written to {0x3e07, 0x3e06, 0x3e09}.
 *
 * Chosen by sweeping the analog gain and scoring each frame by the number of
 * distinct 16-bit pixel values (a constant fill scores 1-2, a real image
 * scores thousands).  With this scene: ang 0x00 -> 15, 0x08 -> 31,
 * 0x10 -> 1873 (mean 0xb610), 0x18 -> 2150 (mean 0xd448, too bright),
 * 0x1f -> saturated white.  0x10 is the best exposed of the usable steps.
 *
 * NOTE (empirical, not from a datasheet): analog gain values whose low three
 * bits are 0b100 - 0x04, 0x0c, 0x14, 0x1c - make the pipeline emit a constant
 * 0x39e7 fill.  Espressif's own gain map only ever uses 0x00 and 0x1f for this
 * register, so most intermediate values appear to be invalid.  Stick to
 * 0x00/0x08/0x10/0x18/0x1f unless this is re-measured.
 */

#define SC2336_GAIN_DIG_FINE    0x80
#define SC2336_GAIN_DIG_COARSE  0x00
#define SC2336_GAIN_ANG         0x10

/* Static white balance, applied through the ISP's WBG stage.
 *
 * There is no AWB running, and raw Bayer has twice as many green photosites as
 * red or blue, so the unprocessed frame has a strong green cast.  These gains
 * come from a grey-world fit on a real captured frame: measured normalised
 * channel means were R=0.708 G=0.849 B=0.509, and the reciprocals were then
 * scaled so the largest gain is exactly 1.0x.  That equalises the channels
 * without clipping anything (a straight grey-world boost would have clipped
 * 38% of R and 26% of B) and incidentally pulls back the overall brightness.
 *
 * Format: 12-bit, 256 = 1.0x (Q4.8), so values below 256 attenuate.
 * See soc/esp32p4/register/hw_ver3/soc/isp_struct.h:2364.
 */

#define SC2336_WB_GAIN_R  184   /* 0.719x */
#define SC2336_WB_GAIN_G  153   /* 0.598x */
#define SC2336_WB_GAIN_B  256   /* 1.000x */

/* Number of completed frames to discard before sampling.
 *
 * The first frames after stream-on are not trustworthy: the sensor's PLL,
 * exposure and the ISP pipeline all need a few frames to settle, and the very
 * first DMA completion can carry a partial frame.  Discarding a handful costs
 * ~170 ms at 30 fps and makes successive runs comparable.
 */

#define SC2336_SKIP_FRAMES 5

#define SC2336_THUMB_DIV  2
#define SC2336_THUMB_W    (SC2336_WIDTH / SC2336_THUMB_DIV)
#define SC2336_THUMB_H    (SC2336_HEIGHT / SC2336_THUMB_DIV)

/* base64 characters per emitted line (must be a multiple of 4) */

#define SC2336_THUMB_COLS 72

/* ESP32-P4 MIPI CSI/DSI PHY is powered by the chip-internal LDO.  The
 * ESP32-P4X-Function-EV-Board user guide states that LDO_VO3 and LDO_VO4
 * feed on-board VDD domains and that the output voltage and enable state
 * "must be configured in software".  Espressif's own ESP32-P4 MIPI-CSI
 * examples acquire LDO channel 3 at 2500 mV for the MIPI PHY rail, so use
 * the same rail and voltage here.  Without this the CSI controller starts
 * but no frame ever arrives (frame-done callback never fires).
 */

#define CSI_PHY_LDO_CHAN     3
#define CSI_PHY_LDO_MV       2500

struct sc2336_reg_s
{
  uint16_t reg;
  uint8_t value;
};

/* SC2336 mode: MIPI 2-lane, 24 MHz input, RAW8, 1280x720 @30fps,
 * 336 Mbps per lane.
 *
 * Verbatim from Espressif's esp_cam_sensor component (Apache-2.0):
 *   esp-video-components/esp_cam_sensor/sensors/sc2336/private_include/
 *     sc2336_mipi_2lane_24Minput_1280x720_raw8_30fps.h
 * upstream tag: cleaned_0x8e_SC2336_MIPI_24Minput_2lane_336Mbps_8bit_
 *               1280x720_30fps
 *
 * Two deviations from the upstream array, both deliberate:
 *   - SC2336_REG_SLEEP_MODE is spelled 0x0100 here.
 *   - the SC2336_REG_END terminator is dropped; this array is sized by
 *     ARRAY_SIZE instead.
 *
 * Do not hand-trim this table.  An earlier 39-entry subset kept only the
 * window/timing registers and dropped every analog, bias and MIPI setting,
 * which left the sensor unable to produce frames.
 */

static const struct sc2336_reg_s g_sc2336_mode[] =
{
  {0x0103, 0x01}, {0x0100, 0x00}, {0x36e9, 0x80}, {0x37f9, 0x80},
  {0x301f, 0x8e}, {0x3031, 0x08}, {0x3037, 0x00}, {0x3106, 0x05},
  {0x3200, 0x01}, {0x3201, 0x34}, {0x3202, 0x00}, {0x3203, 0xb4},
  {0x3204, 0x06}, {0x3205, 0x53}, {0x3206, 0x03}, {0x3207, 0x8b},
  {0x3208, 0x05}, {0x3209, 0x00}, {0x320a, 0x02}, {0x320b, 0xd0},
  {0x320c, 0x08}, {0x320d, 0xc0}, {0x320e, 0x04}, {0x320f, 0xe2},
  {0x3210, 0x00}, {0x3211, 0x10}, {0x3212, 0x00}, {0x3213, 0x04},
  {0x3248, 0x04}, {0x3249, 0x0b}, {0x3253, 0x08}, {0x3301, 0x09},
  {0x3302, 0xff}, {0x3303, 0x10}, {0x3306, 0x68}, {0x3307, 0x02},
  {0x330a, 0x01}, {0x330b, 0x18}, {0x330c, 0x16}, {0x330d, 0xd3},
  {0x3318, 0x02}, {0x3321, 0x0a}, {0x3327, 0x0e}, {0x332b, 0x12},
  {0x3333, 0x10}, {0x3334, 0x40}, {0x335e, 0x06}, {0x335f, 0x0a},
  {0x3364, 0x1f}, {0x337c, 0x02}, {0x337d, 0x0e}, {0x3390, 0x09},
  {0x3391, 0x0f}, {0x3392, 0x1f}, {0x3393, 0x20}, {0x3394, 0x20},
  {0x3395, 0xff}, {0x33a2, 0x04}, {0x33b1, 0x80}, {0x33b2, 0x68},
  {0x33b3, 0x42}, {0x33f9, 0x78}, {0x33fb, 0xe0}, {0x33fc, 0x0f},
  {0x33fd, 0x1f}, {0x349f, 0x03}, {0x34a6, 0x0f}, {0x34a7, 0x1f},
  {0x34a8, 0x42}, {0x34a9, 0x06}, {0x34aa, 0x01}, {0x34ab, 0x28},
  {0x34ac, 0x01}, {0x34ad, 0x90}, {0x3630, 0xf4}, {0x3633, 0x22},
  {0x3639, 0xf4}, {0x363c, 0x47}, {0x3670, 0x09}, {0x3674, 0xf4},
  {0x3675, 0xfb}, {0x3676, 0xed}, {0x367c, 0x09}, {0x367d, 0x0f},
  {0x3690, 0x22}, {0x3691, 0x22}, {0x3692, 0x22}, {0x3698, 0x89},
  {0x3699, 0x96}, {0x369a, 0xd0}, {0x369b, 0xd0}, {0x369c, 0x09},
  {0x369d, 0x0f}, {0x36a2, 0x09}, {0x36a3, 0x0f}, {0x36a4, 0x1f},
  {0x36d0, 0x01}, {0x36ea, 0x0e}, {0x36eb, 0x0a}, {0x36ec, 0x1a},
  {0x36ed, 0x18}, {0x3722, 0xe1}, {0x3724, 0x41}, {0x3725, 0xc1},
  {0x3728, 0x20}, {0x37fa, 0x15}, {0x37fb, 0x32}, {0x37fc, 0x11},
  {0x37fd, 0x17}, {0x3900, 0x0d}, {0x3905, 0x98}, {0x391b, 0x81},
  {0x391c, 0x10}, {0x3933, 0x81}, {0x3934, 0xc5}, {0x3940, 0x68},
  {0x3941, 0x00}, {0x3942, 0x01}, {0x3943, 0xc6}, {0x3952, 0x02},
  {0x3953, 0x0f}, {0x3e01, 0x37}, {0x3e02, 0xe0}, {0x3e08, 0x1f},
  {0x3e1b, 0x14}, {0x4509, 0x38}, {0x4819, 0x06}, {0x481b, 0x04},
  {0x481d, 0x0c}, {0x481f, 0x03}, {0x4821, 0x0a}, {0x4823, 0x03},
  {0x4825, 0x03}, {0x4827, 0x03}, {0x4829, 0x05}, {0x5799, 0x06},
  {0x5ae0, 0xfe}, {0x5ae1, 0x40}, {0x5ae2, 0x30}, {0x5ae3, 0x28},
  {0x5ae4, 0x20}, {0x5ae5, 0x30}, {0x5ae6, 0x28}, {0x5ae7, 0x20},
  {0x5ae8, 0x3c}, {0x5ae9, 0x30}, {0x5aea, 0x28}, {0x5aeb, 0x3c},
  {0x5aec, 0x30}, {0x5aed, 0x28}, {0x5aee, 0xfe}, {0x5aef, 0x40},
  {0x5af4, 0x30}, {0x5af5, 0x28}, {0x5af6, 0x20}, {0x5af7, 0x30},
  {0x5af8, 0x28}, {0x5af9, 0x20}, {0x5afa, 0x3c}, {0x5afb, 0x30},
  {0x5afc, 0x28}, {0x5afd, 0x3c}, {0x5afe, 0x30}, {0x5aff, 0x28},
  {0x36e9, 0x54}, {0x37f9, 0x54},
};

static int sc2336_write_reg(int fd, uint16_t reg, uint8_t value)
{
  struct i2c_transfer_s transfer;
  struct i2c_msg_s message;
  uint8_t data[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
  int ret;

  message.frequency = I2C_SPEED_STANDARD;
  message.addr = SC2336_ADDR;
  message.flags = 0;
  message.buffer = data;
  message.length = sizeof(data);
  transfer.msgv = &message;
  transfer.msgc = 1;
  ret = ioctl(fd, I2CIOC_TRANSFER,
               (unsigned long)((uintptr_t)&transfer));
  return ret < 0 ? -errno : ret;
}

static int sc2336_read_reg(int fd, uint16_t reg, uint8_t *value)
{
  struct i2c_transfer_s transfer;
  struct i2c_msg_s messages[2];
  uint8_t address[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
  int ret;

  messages[0].frequency = I2C_SPEED_STANDARD;
  messages[0].addr = SC2336_ADDR;
  messages[0].flags = I2C_M_NOSTOP;
  messages[0].buffer = address;
  messages[0].length = sizeof(address);
  messages[1].frequency = I2C_SPEED_STANDARD;
  messages[1].addr = SC2336_ADDR;
  messages[1].flags = I2C_M_READ;
  messages[1].buffer = value;
  messages[1].length = 1;
  transfer.msgv = messages;
  transfer.msgc = 2;

  ret = ioctl(fd, I2CIOC_TRANSFER,
              (unsigned long)((uintptr_t)&transfer));
  return ret < 0 ? -errno : ret;
}

static int sc2336_read_id(int fd, uint8_t *id_high, uint8_t *id_low,
                          uint16_t *failed_reg)
{
  int ret;

  *failed_reg = 0x3107;
  ret = sc2336_read_reg(fd, 0x3107, id_high);
  if (ret == 0)
    {
      *failed_reg = 0x3108;
      ret = sc2336_read_reg(fd, 0x3108, id_low);
    }

  return ret;
}

static int sc2336_init(const int *gain)
{
  int fd;
  size_t i;
  int ret = 0;

  fd = open(SC2336_I2C, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  /* Force the sensor out of streaming before anything else.
   *
   * The camera module has its own supply and is not reset when the P4 is, so a
   * previous run leaves 0x0100=0x01 latched.  While streaming the sensor stops
   * acknowledging SCCB reads, which makes every readback in this function fail
   * with EIO even though the writes land.  Errors here are expected on a cold
   * boot (nothing to stop) and on a streaming sensor (the write itself may be
   * NACKed), so they are deliberately ignored - this is a best-effort reset to
   * a known state.
   */

  sc2336_write_reg(fd, 0x0100, 0x00);
  usleep(50000);

  {
    uint8_t id_high;
    uint8_t id_low;
    uint8_t stream_state;

    uint16_t failed_reg;

    ret = sc2336_read_id(fd, &id_high, &id_low, &failed_reg);
    if (ret < 0)
      {
        printf("camera_capture: SC2336 ID read reg=0x%04x failed errno=%d; retrying\n",
               failed_reg, -ret);
        usleep(5000);
        ret = sc2336_read_id(fd, &id_high, &id_low, &failed_reg);
      }

    if (ret < 0)
      {
        printf("camera_capture: SC2336 ID retry reg=0x%04x failed errno=%d\n",
               failed_reg, -ret);
        close(fd);
        return ret;
      }

    printf("camera_capture: SC2336 ID before mode init=0x%02x%02x\n",
           id_high, id_low);

    ret = sc2336_read_reg(fd, 0x0100, &stream_state);
    if (ret < 0)
      {
        printf("camera_capture: SC2336 0x0100 pre-read failed errno=%d\n",
               -ret);
        close(fd);
        return ret;
      }

    printf("camera_capture: SC2336 0x0100 before write=0x%02x\n",
           stream_state);
  }

  /* Apply everything except the final stream-enable.  The SC2336 on this
   * board stops acknowledging SCCB once it is streaming (observed as I2C
   * NACK, i.e. esp_i2c "Transfer error 1024"), so the configuration has to be
   * read back while the sensor is still idle.  Streaming is turned on after
   * verification, at the end of this function.
   */

  for (i = 0; i < sizeof(g_sc2336_mode) / sizeof(g_sc2336_mode[0]); i++)
    {
      if (g_sc2336_mode[i].reg == 0x0100 && g_sc2336_mode[i].value != 0x00)
        {
          continue;
        }

      /* Throttle: see SC2336_SCCB_GAP_US. */

      usleep(SC2336_SCCB_GAP_US);

      ret = sc2336_write_reg(fd, g_sc2336_mode[i].reg,
                             g_sc2336_mode[i].value);
      if (ret < 0)
        {
          /* Retry once.  Reopen the character device first: the ESP32-P4 I2C
           * controller can report an error on the final SCCB byte and leave
           * its state machine non-idle, in which case every later transaction
           * on the same handle fails too.  The previous code only slept before
           * retrying, which is why a failure at one register aborted the whole
           * table.
           */

          close(fd);
          usleep(5000);
          fd = open(SC2336_I2C, O_RDWR);
          if (fd < 0)
            {
              return -errno;
            }

          usleep(SC2336_SCCB_GAP_US);
          ret = sc2336_write_reg(fd, g_sc2336_mode[i].reg,
                                 g_sc2336_mode[i].value);
        }

      if (ret < 0)
        {
          if (g_sc2336_mode[i].reg == 0x36e9 ||
              g_sc2336_mode[i].reg == 0x37f9)
            {
              printf("camera_capture: warning SC2336 optional register 0x%04x unavailable errno=%d\n",
                     g_sc2336_mode[i].reg, -ret);
              continue;
            }

          if (g_sc2336_mode[i].reg == 0x0100)
            {
              uint8_t stream_state;
              int read_ret;

              /* The ESP32-P4 I2C controller can report an error on the
               * final SCCB byte and leave its state machine non-idle.  Reopen
               * the character device before deciding that the sensor write
               * was lost. */
              close(fd);
              usleep(1000);
              fd = open(SC2336_I2C, O_RDWR);
              if (fd >= 0)
                {
                  read_ret = sc2336_read_reg(fd, 0x0100, &stream_state);
                  if (read_ret == 0)
                    {
                      printf("camera_capture: SC2336 0x0100 recovered readback=0x%02x\n",
                             stream_state);
                      if (stream_state == g_sc2336_mode[i].value)
                        {
                          printf("camera_capture: SC2336 0x0100 write accepted despite I2C error\n");
                          ret = 0;
                        }
                    }
                  else
                    {
                      printf("camera_capture: SC2336 0x0100 recovered readback failed errno=%d\n",
                             -read_ret);
                    }
                }

              if (ret == 0)
                {
                  usleep(1000);
                  continue;
                }
            }

          printf("camera_capture: SC2336 register 0x%04x write failed errno=%d\n",
                 g_sc2336_mode[i].reg, -ret);
          break;
        }

      /* 0x0103 is the sensor software reset.  Give the sensor time to come
       * back before pushing the rest of the table at it.
       */

      if (g_sc2336_mode[i].reg == 0x0103)
        {
          usleep(20000);
        }
      else
        {
          usleep(1000);
        }
    }

  /* Write-then-readback verification.  "ioctl returned 0" does not prove the
   * sensor latched the value - verify the registers that decide whether any
   * MIPI data is produced at all.
   */

  if (ret == 0)
    {
      static const uint16_t verify[] =
        {
          0x36e9, 0x37f9,   /* PLL control        */
          0x3208, 0x3209,   /* output width       */
          0x320a, 0x320b,   /* output height      */
          0x320c, 0x320d,   /* HTS                */
          0x320e, 0x320f,   /* VTS                */
        };
      size_t k;
      int bad = 0;

      for (k = 0; k < sizeof(verify) / sizeof(verify[0]); k++)
        {
          uint8_t got;
          uint8_t want = 0;
          bool have_want = false;
          size_t j;
          int rr;

          /* Find the last value the mode table wrote to this register. */

          for (j = 0; j < sizeof(g_sc2336_mode) / sizeof(g_sc2336_mode[0]);
               j++)
            {
              if (g_sc2336_mode[j].reg == verify[k])
                {
                  want = g_sc2336_mode[j].value;
                  have_want = true;
                }
            }

          usleep(SC2336_SCCB_GAP_US);
          rr = sc2336_read_reg(fd, verify[k], &got);
          if (rr < 0)
            {
              printf("camera_capture: verify 0x%04x readback failed errno=%d\n",
                     verify[k], -rr);
              bad++;
              continue;
            }

          if (have_want && got != want)
            {
              printf("camera_capture: verify 0x%04x MISMATCH want=0x%02x "
                     "got=0x%02x\n", verify[k], want, got);
              bad++;
            }
          else
            {
              printf("camera_capture: verify 0x%04x = 0x%02x %s\n",
                     verify[k], got, have_want ? "ok" : "(not in table)");
            }
        }

      printf("camera_capture: verify summary mismatches=%d\n", bad);
    }

  /* Optional gain override.
   *
   * The official mode table deliberately leaves the gain registers alone:
   * Espressif's own SC2336 driver notes that {0x3e06, 0x3e07, 0x3e09} "are
   * not set in format reg_list, the default values are used here", and index
   * 0 of its gain map is {DIG_FINE, DIG_COARSE, ANG} = {0x80, 0x00, 0x00},
   * i.e. 1.0x.  Those registers are meant to be driven at runtime by an
   * auto-exposure loop; we do not create the ISP AE controller, so without an
   * override the sensor stays at minimum gain and the frame comes out using
   * only a couple of quantisation levels.  Register addresses taken from
   * esp-video-components/esp_cam_sensor/sensors/sc2336 (Apache-2.0).
   *
   * Must happen before stream-on: the sensor stops acknowledging SCCB once it
   * is streaming.
   */

  if (ret == 0)
    {
      static const uint16_t gregs[3] =
      {
        0x3e07, 0x3e06, 0x3e09
      };

      static const int gdefault[3] =
      {
        SC2336_GAIN_DIG_FINE, SC2336_GAIN_DIG_COARSE, SC2336_GAIN_ANG
      };

      if (gain == NULL)
        {
          gain = gdefault;
        }

      uint8_t got;
      int rr;
      int g;

      for (g = 0; g < 3 && ret == 0; g++)
        {
          usleep(SC2336_SCCB_GAP_US);
          ret = sc2336_write_reg(fd, gregs[g], (uint8_t)gain[g]);
          if (ret < 0)
            {
              printf("camera_capture: gain 0x%04x write failed errno=%d\n",
                     gregs[g], -ret);
              break;
            }

          usleep(SC2336_SCCB_GAP_US);
          rr = sc2336_read_reg(fd, gregs[g], &got);
          printf("camera_capture: gain 0x%04x = 0x%02x %s\n", gregs[g],
                 rr < 0 ? 0 : got,
                 rr < 0 ? "(readback failed)" :
                 got == (uint8_t)gain[g] ? "ok" : "MISMATCH");
        }
    }

  /* Now start streaming.  Anything that needs SCCB must happen before this
   * point (see the comment above the mode-table loop).
   *
   * The sensor is deliberately left streaming only until the caller is done;
   * sc2336_stream_off() below restores a known state so that the next run does
   * not inherit this one's state (the module keeps its own supply and is not
   * reset together with the P4).
   */

  if (ret == 0)
    {
      ret = sc2336_write_reg(fd, 0x0100, 0x01);
      printf("camera_capture: stream-on 0x0100=0x01 ret=%d\n", ret);
      usleep(30000);
    }

  close(fd);
  return ret;
}

/****************************************************************************
 * Name: sc2336_stream_off
 *
 * Description:
 *   Put the sensor back into standby at the end of a capture.
 *
 *   The camera module has its own supply and is not reset when the P4 is, so
 *   whatever state a run leaves behind becomes the next run's starting state.
 *   Leaving it streaming made successive runs alternate between a good frame
 *   and a constant 0x39e7 fill, and made every SCCB readback in sc2336_init()
 *   fail with EIO (a streaming SC2336 does not acknowledge reads).  Restoring
 *   standby here makes runs independent and repeatable.
 *
 *   Errors are ignored on purpose: this is a best-effort cleanup and there is
 *   nothing useful to do if it fails.
 *
 ****************************************************************************/

static void sc2336_stream_off(void)
{
  int fd;

  fd = open(SC2336_I2C, O_RDWR);
  if (fd < 0)
    {
      return;
    }

  sc2336_write_reg(fd, 0x0100, 0x00);
  close(fd);
}

struct csi_capture_s
{
  /* The DMA callbacks now run through riscv_doirq in NuttX interrupt
   * context.  Keep the existing bounded task-side polling protocol.
   */

  volatile bool finished;

  /* Frame buffer handed to the driver from on_get_new_trans().  The CSI
   * driver picks the DMA destination in an if/else-if chain: as soon as an
   * on_get_new_trans callback is registered it *never* reads the transaction
   * queue filled by esp_cam_ctlr_receive() (esp_cam_ctlr_csi.c:365-377).  So
   * the callback is the only way our buffer can reach the DMA - if it leaves
   * trans->buffer untouched the driver silently falls back to its internal
   * backup buffer and our frame stays all zeroes.
   */

  void *buffer;
  size_t buflen;

  /* Probe counters.  Both callbacks run in ISR context (the CSI driver
   * invokes them from the DW-GDMA transfer-done interrupt), so never print
   * from inside them - only bump a counter and report from task context.
   */

  volatile uint32_t get_calls;
  volatile uint32_t done_calls;
};

#ifdef CONFIG_ESP32P4_DESKTOP
/* get-buffer runs before the previous transfer's done callback. Three slots
 * leave a free destination even while one completed frame is being scaled.
 */
enum live_slot_state { LIVE_FREE, LIVE_DMA, LIVE_READY, LIVE_READING };
static void *g_live_frames[3];
static enum live_slot_state g_live_slots[3];
static atomic_bool g_live_stop;
static atomic_bool g_camera_fault;
static mutex_t g_live_lock = NXMUTEX_INITIALIZER;
static mutex_t g_live_preview_lock = NXMUTEX_INITIALIZER;
static bool g_live_running;
static int g_live_result;
static uint16_t *g_live_preview;
static uint32_t g_live_sequence;
static unsigned int g_live_completed;

static bool live_get_buffer(esp_cam_ctlr_handle_t handle,
                            esp_cam_ctlr_trans_t *trans, void *arg)
{
  irqstate_t flags = enter_critical_section();
  (void)handle;
  (void)arg;
  for (int i = 0; i < 3; i++)
    {
      if (g_live_slots[i] == LIVE_FREE || g_live_slots[i] == LIVE_READY)
        {
          g_live_slots[i] = LIVE_DMA;
          trans->buffer = g_live_frames[i];
          trans->buflen = SC2336_FRAME_SIZE;
          break;
        }
    }
  leave_critical_section(flags);
  return false;
}

static bool live_frame_done(esp_cam_ctlr_handle_t handle,
                            esp_cam_ctlr_trans_t *trans, void *arg)
{
  irqstate_t flags = enter_critical_section();
  (void)handle;
  (void)arg;
  g_live_completed++;
  for (int i = 0; i < 3; i++)
    {
      if (g_live_slots[i] == LIVE_READY) g_live_slots[i] = LIVE_FREE;
    }
  for (int i = 0; i < 3; i++)
    {
      if (trans->buffer == g_live_frames[i])
        g_live_slots[i] = g_live_completed >= SC2336_SKIP_FRAMES ?
                          LIVE_READY : LIVE_FREE;
    }
  leave_critical_section(flags);
  return false;
}

static int live_stream_loop(void)
{
  clock_t last_frame = clock_systime_ticks();
  clock_t next_preview = last_frame;
  unsigned int seen = 0;
  while (!atomic_load(&g_live_stop))
    {
      int slot = -1;
      clock_t now = clock_systime_ticks();
      irqstate_t flags = enter_critical_section();
      if (seen != g_live_completed)
        {
          seen = g_live_completed;
          last_frame = now;
        }
      if ((int32_t)(now - next_preview) >= 0)
        for (int i = 0; i < 3; i++)
          if (g_live_slots[i] == LIVE_READY)
            {
              slot = i;
              g_live_slots[i] = LIVE_READING;
              break;
            }
      leave_critical_section(flags);
      if (slot >= 0)
        {
          int ret = esp_cache_msync(g_live_frames[slot], SC2336_FRAME_SIZE,
                                     ESP_CACHE_MSYNC_FLAG_DIR_M2C);
          if (ret != ESP_OK) return -EIO;
          nxmutex_lock(&g_live_preview_lock);
          camera_preview_resize(g_live_preview, g_live_frames[slot],
                                SC2336_WIDTH, SC2336_HEIGHT, NULL);
          if (++g_live_sequence == 0) g_live_sequence = 1;
          nxmutex_unlock(&g_live_preview_lock);
          flags = enter_critical_section();
          g_live_slots[slot] = LIVE_FREE;
          leave_critical_section(flags);
          next_preview = clock_systime_ticks() + MSEC2TICK(200);
        }
      if (now - last_frame > MSEC2TICK(3000)) return -ETIMEDOUT;
      usleep(10000);
    }
  return 0;
}
#endif

static bool csi_get_buffer(esp_cam_ctlr_handle_t handle,
                           esp_cam_ctlr_trans_t *trans, void *arg)
{
  struct csi_capture_s *capture = arg;

  (void)handle;

  if (capture == NULL || trans == NULL)
    {
      return false;
    }

  /* Publish the frame buffer to the driver.  Mirrors Espressif's own CSI
   * reference test (upper_hal_cam/test_apps/csi/main/test_csi_ov5647.c:37).
   * The driver only accepts it when buflen >= its framebuffer size.
   */

  trans->buffer = capture->buffer;
  trans->buflen = capture->buflen;

  capture->get_calls++;

  return false;
}

static bool csi_frame_done(esp_cam_ctlr_handle_t handle,
                           esp_cam_ctlr_trans_t *trans, void *arg)
{
  struct csi_capture_s *capture = arg;

  (void)handle;
  (void)trans;

  if (capture == NULL)
    {
      return false;
    }

  capture->done_calls++;

  /* Discard the first frames, then publish.  Set the flag *after* the counter
   * so that a poller which observes finished == true is guaranteed to see a
   * consistent count.  No OS primitive here on purpose - see
   * struct csi_capture_s.
   */

  if (capture->done_calls >= SC2336_SKIP_FRAMES)
    {
      capture->finished = true;
    }

  return false;
}

/****************************************************************************
 * Name: csi_report_stats
 *
 * Description:
 *   Summarise the captured frame.  This is the evidence that separates "the
 *   DMA reported a completed transfer" from "we actually captured an image":
 *   the CSI driver falls back to its own internal backup buffer whenever the
 *   on_get_new_trans callback does not supply one, in which case our buffer
 *   would still read back as all zeroes.
 *
 ****************************************************************************/

static void csi_report_stats(const uint8_t *frame)
{
  const size_t npx = SC2336_FRAME_SIZE / SC2336_BYTES_PER_PIXEL;
  uint8_t *seen;
  uint64_t sum = 0;
  size_t distinct = 0;
  size_t ndiff = 0;
  size_t i;
  unsigned int v;
  unsigned int first;
  unsigned int lo = 0xffff;
  unsigned int hi = 0;

  /* Count *distinct 16-bit pixel values*, not byte min/max.
   *
   * Byte min/max cannot tell a uniform frame from a real image: a frame that
   * is entirely 0x5acb reports min=0x5a max=0xcb, which looks like a healthy
   * range but is a single colour.  A real 1280x720 scene has thousands of
   * distinct RGB565 values, so the distinct count is the criterion that
   * actually separates "an image" from "a constant fill".
   *
   * 65536 bits = 8 KiB of presence bitmap, taken from the heap because it is
   * far too large for the stack.
   */

  seen = calloc(65536 / 8, 1);
  first = frame[0] | (frame[1] << 8);

  for (i = 0; i < npx; i++)
    {
      v = frame[i * 2] | (frame[i * 2 + 1] << 8);
      sum += v;

      if (v != first)
        {
          ndiff++;
        }

      if (v < lo)
        {
          lo = v;
        }

      if (v > hi)
        {
          hi = v;
        }

      if (seen != NULL && (seen[v >> 3] & (1 << (v & 7))) == 0)
        {
          seen[v >> 3] |= 1 << (v & 7);
          distinct++;
        }
    }

  printf("camera_capture: frame px=%lu distinct=%lu diff_from_first=%lu "
         "min=0x%04x max=0x%04x mean=0x%04x\n",
         (unsigned long)npx, (unsigned long)distinct, (unsigned long)ndiff,
         lo, hi, (unsigned int)(sum / npx));

  printf("camera_capture: first px %04x %04x %04x %04x\n", first,
         (unsigned int)(frame[2] | (frame[3] << 8)),
         (unsigned int)(frame[4] | (frame[5] << 8)),
         (unsigned int)(frame[6] | (frame[7] << 8)));

  if (seen == NULL)
    {
      printf("camera_capture: WARNING distinct count unavailable "
             "(bitmap alloc failed)\n");
    }
  else if (distinct < 64)
    {
      printf("camera_capture: WARNING only %lu distinct values - this is a "
             "near-constant frame, not a real image\n",
             (unsigned long)distinct);
    }

  free(seen);
}

/****************************************************************************
 * Name: csi_emit_thumbnail
 *
 * Description:
 *   Print a decimated copy of the frame as base64 so that the host can
 *   rebuild a viewable image over the serial console alone.  Every line is
 *   tagged so the decoder can pick the payload out of interleaved console
 *   output (the I2C trace is noisy when debug is enabled).
 *
 ****************************************************************************/

static int g_frame_error;
static mutex_t g_capture_lock = NXMUTEX_INITIALIZER;

static int camera_frame_begin(void)
{
  g_frame_error = 0;
  fflush(stdout);
#ifdef CONFIG_ESP32P4_USB_CONSOLE_BEST_EFFORT
  g_frame_error = esp_usbserial_frame_begin();
  if (g_frame_error == 0)
    {
      g_frame_error = esp_usbserial_frame_write("\n", 1);
      if (g_frame_error < 0)
        {
          esp_usbserial_frame_end();
        }
    }
#endif
  return g_frame_error;
}

static void camera_frame_printf(const char *fmt, ...)
{
  char line[256];
  va_list ap;
  int len;

  if (g_frame_error < 0)
    {
      return;
    }

  va_start(ap, fmt);
  len = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (len < 0 || len >= (int)sizeof(line))
    {
      g_frame_error = -EOVERFLOW;
      return;
    }

#ifdef CONFIG_ESP32P4_USB_CONSOLE_BEST_EFFORT
  g_frame_error = esp_usbserial_frame_write(line, len);
#else
  if (fwrite(line, 1, len, stdout) != (size_t)len)
    {
      g_frame_error = -EIO;
    }
#endif
}

static void camera_frame_end(void)
{
#ifdef CONFIG_ESP32P4_USB_CONSOLE_BEST_EFFORT
  esp_usbserial_frame_end();
#else
  fflush(stdout);
#endif
  if (g_frame_error < 0)
    {
      printf("camera_capture: transport failed ret=%d\n", g_frame_error);
    }
}

static void csi_emit_thumbnail(const uint8_t *frame)
{
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789+/";
  char line[SC2336_THUMB_COLS + 1];
  uint8_t trio[3];
  uint32_t sum = 0;
  size_t off;
  int ntrio = 0;
  int nline = 0;
  int row;
  int col;
  int i;
  int sr;
  int sc;
  int n;
  uint32_t racc;
  uint32_t gacc;
  uint32_t bacc;
  uint16_t px;
  uint16_t avg;
  uint8_t pxb[SC2336_BYTES_PER_PIXEL];

  if (camera_frame_begin() < 0)
    {
      return;
    }

  camera_frame_printf("camera_capture: thumb begin w=%d h=%d fmt=rgb565le bytes=%d\n",
         SC2336_THUMB_W, SC2336_THUMB_H,
         SC2336_THUMB_W * SC2336_THUMB_H * SC2336_BYTES_PER_PIXEL);

  for (row = 0; row < SC2336_THUMB_H; row++)
    {
      for (col = 0; col < SC2336_THUMB_W; col++)
        {
          racc = 0;
          gacc = 0;
          bacc = 0;

          /* Box-average the DIV x DIV source block into one RGB565 pixel. */

          for (sr = 0; sr < SC2336_THUMB_DIV; sr++)
            {
              for (sc = 0; sc < SC2336_THUMB_DIV; sc++)
                {
                  off = ((size_t)(row * SC2336_THUMB_DIV + sr) * SC2336_WIDTH +
                         (size_t)(col * SC2336_THUMB_DIV + sc)) *
                        SC2336_BYTES_PER_PIXEL;
                  px = (uint16_t)(frame[off] | (frame[off + 1] << 8));
                  racc += (px >> 11) & 0x1f;
                  gacc += (px >> 5) & 0x3f;
                  bacc += px & 0x1f;
                }
            }

          n = SC2336_THUMB_DIV * SC2336_THUMB_DIV;
          avg = (uint16_t)(((racc / n) << 11) |
                           ((gacc / n) << 5) |
                           (bacc / n));
          pxb[0] = (uint8_t)(avg & 0xff);
          pxb[1] = (uint8_t)(avg >> 8);

          for (i = 0; i < SC2336_BYTES_PER_PIXEL; i++)
            {
              sum += pxb[i];
              trio[ntrio++] = pxb[i];

              if (ntrio < 3)
                {
                  continue;
                }

              line[nline++] = b64[trio[0] >> 2];
              line[nline++] = b64[((trio[0] & 0x03) << 4) | (trio[1] >> 4)];
              line[nline++] = b64[((trio[1] & 0x0f) << 2) | (trio[2] >> 6)];
              line[nline++] = b64[trio[2] & 0x3f];
              ntrio = 0;

              if (nline >= SC2336_THUMB_COLS)
                {
                  line[nline] = '\0';
                  camera_frame_printf("THUMB:%s\n", line);
                  nline = 0;
                }
            }
        }
    }

  /* Flush a partial group, then a partial line.  640x360x2 is a multiple of
   * three so the padding branch is not normally taken, but keep it correct.
   */

  if (ntrio > 0)
    {
      line[nline++] = b64[trio[0] >> 2];

      if (ntrio == 1)
        {
          line[nline++] = b64[(trio[0] & 0x03) << 4];
          line[nline++] = '=';
        }
      else
        {
          line[nline++] = b64[((trio[0] & 0x03) << 4) | (trio[1] >> 4)];
          line[nline++] = b64[(trio[1] & 0x0f) << 2];
        }

      line[nline++] = '=';
    }

  if (nline > 0)
    {
      line[nline] = '\0';
      camera_frame_printf("THUMB:%s\n", line);
    }

  camera_frame_printf("camera_capture: thumb end sum32=0x%08lx\n", (unsigned long)sum);
  camera_frame_end();
}

/* Software-JPEG switches, set by the --jpeg-capture command. */

int g_p4x_jpeg_emit;
int g_p4x_jpeg_awb;

/* Grey-world target mean, permille; same as thumb_image._grey_world. */

#define P4X_JPEG_AWB_TARGET 580

/****************************************************************************
 * Name: p4x_jpeg_emit_frame
 *
 * Description:
 *   Encode the RGB565 frame to a baseline JPEG on the CPU and print it as
 *   "jpg:" base64 lines.  With awb set, grey-world gains are applied during
 *   encoding, since the stdlib-only host cannot touch JPEG pixels.
 *
 ****************************************************************************/

static void p4x_jpeg_emit_frame(const uint8_t *frame, int width, int height,
                                int awb)
{
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789+/";
  const int cap = 512 * 1024;
  uint32_t gains[3];
  struct timespec t0;
  struct timespec t1;
  unsigned long enc_ms;
  uint8_t *jpg;
  uint32_t sum = 0;
  char line[80];
  uint8_t trio[3];
  int nl = 0;
  int nt = 0;
  int n;
  int k;

  jpg = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (jpg == NULL)
    {
      jpg = malloc(cap);
    }

  if (jpg == NULL)
    {
      printf("jpeg_sw: output alloc failed (%d bytes)\n", cap);
      return;
    }

  clock_gettime(CLOCK_MONOTONIC, &t0);
  if (awb)
    {
      jpeg_sw_grey_world((const uint16_t *)frame, width * height,
                         P4X_JPEG_AWB_TARGET, gains);
    }

#ifdef CONFIG_ESP32P4_DESKTOP
  int preview_ret = camera_preview_publish((const uint16_t *)frame,
                                          width, height, awb ? gains : NULL);
  if (preview_ret < 0)
    {
      printf("camera_capture: preview unavailable ret=%d\n", preview_ret);
    }
#endif

  n = jpeg_sw_encode_rgb565((const uint16_t *)frame, width, height, 80,
                            awb ? gains : NULL, jpg, cap);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  enc_ms = (unsigned long)((t1.tv_sec - t0.tv_sec) * 1000 +
                           (t1.tv_nsec - t0.tv_nsec) / 1000000);
  if (n < 0)
    {
      printf("jpeg_sw: encode failed (overflow, cap=%d)\n", cap);
      free(jpg);
      return;
    }

  for (k = 0; k < n; k++)
    {
      sum += jpg[k];
    }

  if (camera_frame_begin() < 0)
    {
      free(jpg);
      return;
    }

  if (awb)
    {
      camera_frame_printf("jpeg_sw: awb gain_q16 r=%lu g=%lu b=%lu\n",
             (unsigned long)gains[0], (unsigned long)gains[1],
             (unsigned long)gains[2]);
    }

  camera_frame_printf("jpeg_sw: begin bytes=%d w=%d h=%d q=80 sum32=0x%08lx awb=%s "
         "enc_ms=%lu\n", n, width, height, (unsigned long)sum,
         awb ? "on" : "off", enc_ms);

  for (k = 0; k < n; k++)
    {
      trio[nt++] = jpg[k];
      if (nt == 3)
        {
          line[nl++] = b64[trio[0] >> 2];
          line[nl++] = b64[((trio[0] & 0x3) << 4) | (trio[1] >> 4)];
          line[nl++] = b64[((trio[1] & 0xf) << 2) | (trio[2] >> 6)];
          line[nl++] = b64[trio[2] & 0x3f];
          nt = 0;
          if (nl >= 72)
            {
              line[nl] = '\0';
              camera_frame_printf("jpg:%s\n", line);
              nl = 0;
            }
        }
    }

  if (nt > 0)
    {
      uint8_t a = trio[0];
      uint8_t b = (nt > 1) ? trio[1] : 0;
      line[nl++] = b64[a >> 2];
      line[nl++] = b64[((a & 0x3) << 4) | (b >> 4)];
      line[nl++] = (nt > 1) ? b64[(b & 0xf) << 2] : '=';
      line[nl++] = '=';
    }

  if (nl > 0)
    {
      line[nl] = '\0';
      camera_frame_printf("jpg:%s\n", line);
    }

  camera_frame_printf("jpeg_sw: end\n");
  camera_frame_end();
  free(jpg);
}

/****************************************************************************
 * Name: p4x_jpeg_selftest
 *
 * Description:
 *   Encode a synthetic RGB565 gradient to JPEG and emit it, decoupled from
 *   the camera sensor.  Verifies the on-device software encoder end-to-end
 *   even when the flaky SC2336 I2C init is failing.
 *
 ****************************************************************************/

static int jpeg_selftest_once(void)
{
  const int W = SC2336_WIDTH;
  const int H = SC2336_HEIGHT;
  uint16_t *img;
  int x;
  int y;

  img = heap_caps_malloc((size_t)W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (img == NULL)
    {
      img = malloc((size_t)W * H * 2);
    }
  if (img == NULL)
    {
      printf("jpeg_sw: selftest alloc failed\n");
      return -1;
    }

  for (y = 0; y < H; y++)
    {
      for (x = 0; x < W; x++)
        {
          int r5 = (x * 31) / (W - 1);
          int g6 = (y * 63) / (H - 1);
          int b5 = ((x / 16 + y / 16) & 1) ? 31 : 4;
          img[y * W + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }

  /* AWB off: keep the synthetic output byte-reproducible on the host. */

  p4x_jpeg_emit_frame((const uint8_t *)img, W, H, 0);
  free(img);
  return g_frame_error;
}

static int camera_capture_once(const char *output, const int *gain, bool live)
{
  esp_cam_ctlr_handle_t camera = NULL;
  static struct csi_capture_s capture;
  esp_cam_ctlr_trans_t transaction;
  esp_cam_ctlr_evt_cbs_t callbacks;
  esp_cam_ctlr_csi_config_t config;
  esp_ldo_channel_handle_t phy_ldo = NULL;
  esp_ldo_channel_config_t ldo_config;
  isp_proc_handle_t isp_proc = NULL;
  esp_isp_processor_cfg_t isp_config;
  size_t frame_align;
  void *frame;
  bool camera_stopped = false;
  bool camera_enabled = false;
  bool camera_started = false;
  bool isp_enabled = false;
  int waited_ms;
  int fd;
  int ret;

  /* Power the MIPI CSI PHY rail before touching the CSI controller.  See the
   * CSI_PHY_LDO_* comment above for why this rail and voltage are used.
   */

  ldo_config = (esp_ldo_channel_config_t){
    .chan_id = CSI_PHY_LDO_CHAN,
    .voltage_mv = CSI_PHY_LDO_MV,
  };

  if (esp_ldo_acquire_channel(&ldo_config, &phy_ldo) != ESP_OK)
    {
      printf("camera_capture: MIPI PHY LDO chan%d @%dmV acquire failed\n",
             CSI_PHY_LDO_CHAN, CSI_PHY_LDO_MV);
      return -EIO;
    }

  printf("camera_capture: MIPI PHY LDO chan%d @%dmV enabled\n",
         CSI_PHY_LDO_CHAN, CSI_PHY_LDO_MV);

  ret = sc2336_init(gain);
  if (ret < 0)
    {
      sc2336_stream_off();
      esp_ldo_release_channel(phy_ldo);
      return ret;
    }
#ifdef CONFIG_ESP32P4_DESKTOP
  if (live && atomic_load(&g_live_stop))
    {
      sc2336_stream_off();
      esp_ldo_release_channel(phy_ldo);
      return 0;
    }
#endif

  /* Ask the cache layer for the required DMA alignment instead of assuming
   * 64 bytes.  The frame buffer lives in PSRAM and the ISP writes into it via
   * DMA, so a wrong alignment corrupts memory rather than just degrading
   * performance.  Same call as Espressif's CSI reference test.
   */

  frame_align = 64;
  if (esp_cache_get_alignment(0, &frame_align) != ESP_OK || frame_align == 0)
    {
      frame_align = 64;
    }

  printf("camera_capture: frame align=%u size=%u\n",
         (unsigned int)frame_align, (unsigned int)SC2336_FRAME_SIZE);

  frame = heap_caps_aligned_calloc(frame_align, 1, SC2336_FRAME_SIZE,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (frame == NULL)
    {
      /* Some demo configurations do not enable external PSRAM.  A single
       * RAW8 frame still fits in the available internal heap on this board;
       * fall back to DMA-capable 8-bit memory instead of reporting a generic
       * CSI failure. */
      frame = heap_caps_aligned_calloc(frame_align, 1, SC2336_FRAME_SIZE,
                                       MALLOC_CAP_8BIT);
    }

  if (frame == NULL)
    {
      printf("camera_capture: frame allocation failed: %u bytes\n",
             (unsigned int)SC2336_FRAME_SIZE);
      sc2336_stream_off();
      esp_ldo_release_channel(phy_ldo);
      return -ENOMEM;
    }

#ifdef CONFIG_ESP32P4_DESKTOP
  if (live)
    {
      g_live_frames[0] = frame;
      g_live_completed = 0;
      for (int i = 0; i < 3; i++) g_live_slots[i] = LIVE_FREE;
      for (int i = 1; i < 3; i++)
        {
          g_live_frames[i] = heap_caps_aligned_calloc(frame_align, 1,
            SC2336_FRAME_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
          if (!g_live_frames[i])
            {
              for (int j = 0; j < i; j++) free(g_live_frames[j]);
              memset(g_live_frames, 0, sizeof(g_live_frames));
              sc2336_stream_off();
              esp_ldo_release_channel(phy_ldo);
              return -ENOMEM;
            }
        }
    }
#endif

  config = (esp_cam_ctlr_csi_config_t){
    .ctlr_id = 0,
    .h_res = SC2336_WIDTH,
    .v_res = SC2336_HEIGHT,
    .data_lane_num = SC2336_LANES,
    .lane_bit_rate_mbps = SC2336_BITRATE,
    .input_data_color_type = CAM_CTLR_COLOR_RAW8,
    .output_data_color_type = CAM_CTLR_COLOR_RAW8,
    .queue_items = 1,
    .bk_buffer_dis = live,
  };

  if (esp_cam_new_csi_ctlr(&config, &camera) != ESP_OK)
    {
      sc2336_stream_off();
#ifdef CONFIG_ESP32P4_DESKTOP
      if (live) for (int i = 1; i < 3; i++) free(g_live_frames[i]);
#endif
      free(frame);
      esp_ldo_release_channel(phy_ldo);
      return -ENODEV;
    }

  capture.finished = false;
  capture.buffer = frame;
  capture.buflen = SC2336_FRAME_SIZE;
  capture.get_calls = 0;
  capture.done_calls = 0;
  callbacks = (esp_cam_ctlr_evt_cbs_t){
    .on_get_new_trans = csi_get_buffer,
    .on_trans_finished = csi_frame_done,
  };
  transaction = (esp_cam_ctlr_trans_t){
    .buffer = frame,
    .buflen = SC2336_FRAME_SIZE,
  };

#ifdef CONFIG_ESP32P4_DESKTOP
  if (live)
    {
      callbacks.on_get_new_trans = live_get_buffer;
      callbacks.on_trans_finished = live_frame_done;
    }
#endif

  /* Report每 stage 的返回值，否则链式 if 会把失败点藏起来。 */

  ret = esp_cam_ctlr_register_event_callbacks(camera, &callbacks, &capture);
  printf("camera_capture: stage register_cbs ret=%d\n", ret);

  if (ret == ESP_OK)
    {
      ret = esp_cam_ctlr_enable(camera);
      camera_enabled = ret == ESP_OK;
      printf("camera_capture: stage enable       ret=%d\n", ret);
    }

  /* On ESP32-P4 the CSI bridge feeds the ISP, so an ISP processor must exist
   * and be enabled before the camera controller is started - otherwise the
   * DMA primes once and no frame ever completes.  Ordering and clock taken
   * from Espressif's own CSI reference test
   * (upper_hal_cam/test_apps/csi/main/test_csi_ov5647.c).
   */

  if (ret == ESP_OK)
    {
      isp_config = (esp_isp_processor_cfg_t){
        .clk_hz                 = CSI_ISP_CLK_HZ,
        .input_data_source      = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type  = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,

        /* Espressif's SC2336 driver declares this sensor as BGGR, which is
         * also COLOR_RAW_ELEMENT_ORDER_BGGR == 0, i.e. what an unset field
         * would already give.  Spelled out so it is documented rather than
         * accidental.  Note the remaining green cast is *not* a Bayer-order
         * problem: it comes from having no AWB/CCM stage - raw Bayer has twice
         * as many green photosites as red or blue.
         */

        .bayer_order            = COLOR_RAW_ELEMENT_ORDER_BGGR,
        .has_line_start_packet  = false,
        .has_line_end_packet    = false,
        .h_res                  = SC2336_WIDTH,
        .v_res                  = SC2336_HEIGHT,
      };

      ret = esp_isp_new_processor(&isp_config, &isp_proc);
      printf("camera_capture: stage isp_new      ret=%d\n", ret);
    }

  if (ret == ESP_OK)
    {
      ret = esp_isp_enable(isp_proc);
      isp_enabled = ret == ESP_OK;
      printf("camera_capture: stage isp_enable   ret=%d\n", ret);
    }

  /* Static white balance.  See the SC2336_WB_GAIN_* comment for where these
   * numbers come from.  This is a fixed correction, not AWB: it cancels the
   * green cast of this sensor under this lighting, and will be wrong if the
   * illuminant changes substantially.
   */

  if (ret == ESP_OK)
    {
      /* update_once_configured must be set: without it the driver takes the
       * "defer to the next VSYNC" path, which fails outright when the shadow
       * register's pending bit is still set - and that bit only self-clears on
       * a VSYNC, which cannot happen because streaming has not started yet.
       * Forcing the update is the correct mode for configuring once up front.
       */

      esp_isp_wbg_config_t wbg_config =
      {
        .flags =
        {
          .update_once_configured = 1
        }
      };

      isp_wbg_gain_t wb_gain;

      wb_gain.gain_r = SC2336_WB_GAIN_R;
      wb_gain.gain_g = SC2336_WB_GAIN_G;
      wb_gain.gain_b = SC2336_WB_GAIN_B;

      ret = esp_isp_wbg_configure(isp_proc, &wbg_config);
      if (ret == ESP_OK)
        {
          ret = esp_isp_wbg_enable(isp_proc);
        }

      if (ret == ESP_OK)
        {
          ret = esp_isp_wbg_set_wb_gain(isp_proc, wb_gain);
        }

      printf("camera_capture: stage wbg r=%d g=%d b=%d ret=%d\n",
             SC2336_WB_GAIN_R, SC2336_WB_GAIN_G, SC2336_WB_GAIN_B, ret);

      /* A missing WBG stage must not fail the capture - the frame is still
       * valid, just green.  Report and carry on.
       */

      if (ret != ESP_OK)
        {
          printf("camera_capture: WBG unavailable, frame will keep its "
                 "green cast\n");
          ret = ESP_OK;
        }
    }

  if (ret == ESP_OK)
    {
      ret = esp_cam_ctlr_start(camera);
      camera_started = ret == ESP_OK;
      printf("camera_capture: stage start        ret=%d\n", ret);
    }

  if (ret == ESP_OK && !live)
    {
      ret = esp_cam_ctlr_receive(camera, &transaction, 5000);
      printf("camera_capture: stage receive      ret=%d\n", ret);
    }

#ifdef CONFIG_ESP32P4_DESKTOP
  if (ret == ESP_OK && live) ret = live_stream_loop();
#endif

  if (ret == ESP_OK && !live)
    {
      /* Wait for the requested warm-up frames while the desktop keeps
       * running.  The ISR only publishes counters and the completion flag.
       */

      for (waited_ms = 0; !capture.finished &&
           waited_ms < SC2336_CAPTURE_TIMEOUT_MS; waited_ms += 10)
        {
          usleep(10000);
        }

      ret = capture.finished ? 0 : -ETIMEDOUT;
      printf("camera_capture: stage wait         ret=%d after %dms\n",
             ret, waited_ms);
    }

  /* 回调探针：两者都为 0 => 控制器没开始取数（CSI/bridge 配置问题）；
   * get>0 而 done==0 => DMA 起来了但完成中断没到（DW-GDMA 中断挂载问题）。
   */

  printf("camera_capture: probe on_get_new_trans=%lu on_trans_finished=%lu "
         "finished=%d\n",
         (unsigned long)capture.get_calls,
         (unsigned long)capture.done_calls,
         (int)capture.finished);

  /* Stop the controller *before* reading the buffer.
   *
   * The on_get_new_trans callback hands the driver the same buffer every
   * frame, so the DMA keeps overwriting it at 30 fps.  Reading 1.8 MB while
   * that is happening yields a torn mix of several frames, which is almost
   * certainly why successive runs disagreed with each other.  Stop first, then
   * sample.
   */

  if (camera_started)
    {
      int stop_ret = esp_cam_ctlr_stop(camera);
      camera_stopped = stop_ret == ESP_OK;
      if (!camera_stopped)
        {
          printf("camera_capture: stop failed ret=%d\n", stop_ret);
          ret = -EIO;
        }

      usleep(20000);
    }

  if (!live && ret == 0 && capture.finished)
    {
      csi_report_stats(frame);

      /* The JPEG replaces the thumbnail: sending both would cost the
       * ~7 s of THUMB traffic that JPEG is meant to remove.
       */

      if (g_p4x_jpeg_emit)
        {
          p4x_jpeg_emit_frame(frame, SC2336_WIDTH, SC2336_HEIGHT,
                              g_p4x_jpeg_awb);
        }
      else
        {
#ifdef CONFIG_ESP32P4_DESKTOP
          int preview_ret = camera_preview_publish((const uint16_t *)frame,
                                      SC2336_WIDTH, SC2336_HEIGHT, NULL);
          if (preview_ret < 0)
            {
              printf("camera_capture: preview unavailable ret=%d\n", preview_ret);
            }
#endif
          csi_emit_thumbnail(frame);
        }

      if (g_frame_error < 0)
        {
          ret = g_frame_error;
        }
    }

  if (!live && ret == 0 && capture.finished && output != NULL)
    {
      fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
      if (fd < 0)
        {
          /* This configuration mounts no writable filesystem (FS_TMPFS,
           * FS_FAT and FS_ROMFS are all disabled), so there is nowhere to
           * store the raw frame.  The capture itself succeeded and the
           * thumbnail above carries the evidence, so this is reported but
           * does not fail the test.
           */

          printf("camera_capture: frame not saved to %s (errno=%d); "
                 "use the %s lines above instead\n", output, errno,
                 g_p4x_jpeg_emit ? "jpg" : "THUMB");
        }
      else
        {
          if (write(fd, frame, SC2336_FRAME_SIZE) != SC2336_FRAME_SIZE)
            {
              ret = -EIO;
            }

          close(fd);
        }
    }

  if (camera != NULL)
    {
      if (camera_started && !camera_stopped)
        {
          /* Keep DMA-owned memory alive if the controller cannot stop. */

          if (esp_cam_ctlr_stop(camera) != ESP_OK)
            {
              sc2336_stream_off();
#ifdef CONFIG_ESP32P4_DESKTOP
              atomic_store(&g_camera_fault, true);
#endif
              printf("camera_capture: stop failed; reset required\n");
              return -EIO;
            }
        }

      if (camera_enabled)
        {
          esp_cam_ctlr_disable(camera);
        }

      int del_ret = esp_cam_ctlr_del(camera);
      if (del_ret != ESP_OK)
        {
          sc2336_stream_off();
#ifdef CONFIG_ESP32P4_DESKTOP
          atomic_store(&g_camera_fault, true);
#endif
          printf("camera_capture: delete failed ret=%d; reset required\n", del_ret);
          return -EIO;
        }
    }

  sc2336_stream_off();

  if (isp_proc != NULL)
    {
      if (isp_enabled)
        {
          esp_isp_disable(isp_proc);
        }
      esp_isp_del_processor(isp_proc);
    }
#ifdef CONFIG_ESP32P4_DESKTOP
  if (live)
    {
      for (int i = 1; i < 3; i++) free(g_live_frames[i]);
      memset(g_live_frames, 0, sizeof(g_live_frames));
    }
#endif
  free(frame);
  esp_ldo_release_channel(phy_ldo);
  return ret;
}

int p4x_camera_capture_csi(const char *output, const int *gain)
{
  int ret = nxmutex_trylock(&g_capture_lock);

  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_ESP32P4_DESKTOP
  if (atomic_load(&g_camera_fault))
    {
      nxmutex_unlock(&g_capture_lock);
      return -EIO;
    }
#endif
  ret = camera_capture_once(output, gain, false);
  nxmutex_unlock(&g_capture_lock);
  return ret;
}

int p4x_jpeg_selftest(void)
{
  int ret = nxmutex_trylock(&g_capture_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = jpeg_selftest_once();
  nxmutex_unlock(&g_capture_lock);
  return ret;
}

#ifdef CONFIG_ESP32P4_DESKTOP
static int live_worker(int argc, char **argv)
{
  int ret = nxmutex_trylock(&g_capture_lock);
  (void)argc;
  (void)argv;
  if (ret >= 0)
    {
      if (!atomic_load(&g_live_stop)) ret = camera_capture_once(NULL, NULL, true);
      nxmutex_unlock(&g_capture_lock);
    }
  nxmutex_lock(&g_live_preview_lock);
  free(g_live_preview);
  g_live_preview = NULL;
  nxmutex_unlock(&g_live_preview_lock);
  nxmutex_lock(&g_live_lock);
  g_live_result = ret > 0 ? -EIO : ret;
  g_live_running = false;
  nxmutex_unlock(&g_live_lock);
  printf("CAMERA LIVE stopped ret=%d\n", ret);
  return 0;
}

int camera_live_start(void)
{
  int pid;
  nxmutex_lock(&g_live_lock);
  if (g_live_running || atomic_load(&g_camera_fault))
    {
      nxmutex_unlock(&g_live_lock);
      return -EBUSY;
    }
  g_live_preview = malloc(CAMERA_PREVIEW_BYTES);
  if (!g_live_preview)
    {
      nxmutex_unlock(&g_live_lock);
      return -ENOMEM;
    }
  g_live_sequence = 0;
  g_live_result = 0;
  atomic_store(&g_live_stop, false);
  g_live_running = true;
  pid = task_create("camera_live", 95, 12288, live_worker, NULL);
  if (pid < 0)
    {
      g_live_running = false;
      free(g_live_preview);
      g_live_preview = NULL;
    }
  nxmutex_unlock(&g_live_lock);
  return pid < 0 ? -errno : 0;
}

void camera_live_stop(void)
{
  atomic_store(&g_live_stop, true);
}

int camera_live_status(void)
{
  int ret;
  nxmutex_lock(&g_live_lock);
  ret = g_live_running ? 1 : g_live_result;
  nxmutex_unlock(&g_live_lock);
  return ret;
}

uint32_t camera_live_take(uint16_t *pixels, uint32_t sequence)
{
  if (nxmutex_trylock(&g_live_preview_lock) < 0) return sequence;
  if (g_live_preview && g_live_sequence && g_live_sequence != sequence)
    {
      memcpy(pixels, g_live_preview, CAMERA_PREVIEW_BYTES);
      sequence = g_live_sequence;
    }
  nxmutex_unlock(&g_live_preview_lock);
  return sequence;
}
#endif
