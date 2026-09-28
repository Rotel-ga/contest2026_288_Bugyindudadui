/* SPDX-License-Identifier: Apache-2.0 */
#include "identify_bridge.h"
#include "photo_identify.h"
#include "../fallguard/camera_preview.h"
#include <nuttx/config.h>
#include <nuttx/mutex.h>
#include <nuttx/clock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>
#ifdef CONFIG_ESPRESSIF_USBSERIAL
#  include "esp_usbserial.h"
#endif

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static uint16_t *g_snapshot;
static uint32_t g_id;
static bool g_pending;
static atomic_bool g_cancelled;
static clock_t g_started;
static char g_result[1024];
static size_t g_received;
static size_t g_expected;
static uint32_t g_checksum;
static bool g_receiving;

int identify_request(const uint16_t *pixels)
{
  if (!pixels) return -EINVAL;
  if (nxmutex_trylock(&g_lock) < 0) return -EBUSY;
  if (g_pending && !atomic_load(&g_cancelled))
    {
      nxmutex_unlock(&g_lock);
      return -EBUSY;
    }
  if (!g_snapshot) g_snapshot = malloc(CAMERA_PREVIEW_BYTES);
  if (!g_snapshot)
    {
      nxmutex_unlock(&g_lock);
      return -ENOMEM;
    }
  memcpy(g_snapshot, pixels, CAMERA_PREVIEW_BYTES);
  if (++g_id == 0) g_id = 1;
  atomic_store(&g_cancelled, false);
  g_pending = true;
  g_receiving = false;
  g_started = clock_systime_ticks();
  nxmutex_unlock(&g_lock);
  return 0;
}

bool identify_pending(void)
{
  bool pending;
  if (nxmutex_trylock(&g_lock) < 0) return true;
  if (g_pending && clock_systime_ticks() - g_started > MSEC2TICK(180000))
    {
      g_pending = false;
      g_receiving = false;
      photo_identify_set_message("识别超时，请确认电脑识物脚本正在运行，然后重试。");
    }
  pending = g_pending && !atomic_load(&g_cancelled);
  nxmutex_unlock(&g_lock);
  return pending;
}

void identify_cancel(void)
{
  /* UI navigation must not wait behind a USB transfer. Snapshot is reused
   * on the next request; an in-flight transfer observes cancellation. */
  atomic_store(&g_cancelled, true);
  if (nxmutex_trylock(&g_lock) == 0)
    {
      g_pending = false;
      g_receiving = false;
      free(g_snapshot);
      g_snapshot = NULL;
      nxmutex_unlock(&g_lock);
    }
}

static int send_frame(void)
{
#ifdef CONFIG_ESPRESSIF_USBSERIAL
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t *data = (const uint8_t *)g_snapshot;
  char line[128];
  uint32_t sum = 0;
  int ret = esp_usbserial_frame_begin();
  if (ret < 0) return ret;
  for (size_t i = 0; i < CAMERA_PREVIEW_BYTES; i++) sum += data[i];
  int n = snprintf(line, sizeof(line), "\nPICBEGIN id=%lu w=480 h=270 bytes=%u sum=%lu\n",
                   (unsigned long)g_id, CAMERA_PREVIEW_BYTES, (unsigned long)sum);
  ret = esp_usbserial_frame_write(line, n);
  for (size_t i = 0; ret == 0 && i < CAMERA_PREVIEW_BYTES;)
    {
      if (atomic_load(&g_cancelled)) {ret = -ECANCELED; break;}
      int at = 0;
      memcpy(line, "PIC:", 4);
      at = 4;
      for (int k = 0; k < 18 && i < CAMERA_PREVIEW_BYTES; k++, i += 3)
        {
          /* The fixed RGB565 frame size is divisible by three. */
          line[at++] = table[data[i] >> 2];
          line[at++] = table[((data[i] & 3) << 4) | (data[i + 1] >> 4)];
          line[at++] = table[((data[i + 1] & 15) << 2) | (data[i + 2] >> 6)];
          line[at++] = table[data[i + 2] & 63];
        }
      line[at++] = '\n';
      ret = esp_usbserial_frame_write(line, at);
    }
  if (ret == 0)
    {
      n = snprintf(line, sizeof(line), "PICEND id=%lu\n", (unsigned long)g_id);
      ret = esp_usbserial_frame_write(line, n);
    }
  esp_usbserial_frame_end();
  return ret;
#else
  return -ENOSYS;
#endif
}

static int hex_value(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool utf8_valid(const unsigned char *p, size_t n)
{
  size_t i = 0;
  while (i < n)
    {
      uint32_t cp = p[i++];
      int count;
      uint32_t minimum;
      if (cp < 128)
        {
          if (cp < 32 && cp != '\n' && cp != '\t') return false;
          continue;
        }
      if (cp >= 0xc2 && cp <= 0xdf) {count = 1; cp &= 31; minimum = 128;}
      else if (cp >= 0xe0 && cp <= 0xef) {count = 2; cp &= 15; minimum = 2048;}
      else if (cp >= 0xf0 && cp <= 0xf4) {count = 3; cp &= 7; minimum = 65536;}
      else return false;
      if (i + count > n) return false;
      while (count--)
        {
          if ((p[i] & 0xc0) != 0x80) return false;
          cp = (cp << 6) | (p[i++] & 63);
        }
      if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
  return true;
}

static bool number(const char *s, unsigned long *n)
{
  char *end;
  if (*s < '0' || *s > '9') return false;
  errno = 0;
  *n = strtoul(s, &end, 10);
  return errno == 0 && *end == 0 && *n <= UINT32_MAX;
}

int identify_command(int argc, char **argv)
{
  unsigned long id = 0, offset = 0, sum = 0;
  int ret = -EINVAL;
  if (argc == 2 && strcmp(argv[1], "q") == 0)
    {
      nxmutex_lock(&g_lock);
      printf("\nPISTATE id=%lu pending=%d\nPIEND\n", (unsigned long)g_id, g_pending && !atomic_load(&g_cancelled));
      nxmutex_unlock(&g_lock);
      fflush(stdout);
      return 0;
    }
  if (argc < 3 || !number(argv[2], &id)) goto reply;
  nxmutex_lock(&g_lock);
  if (!g_pending || atomic_load(&g_cancelled) || id != g_id) {ret = -ESTALE; goto unlock;}
  if (argc == 3 && strcmp(argv[1], "f") == 0)
    {
      ret = send_frame();
    }
  else if (argc == 5 && strcmp(argv[1], "b") == 0 &&
           number(argv[3], &offset) && number(argv[4], &sum))
    {
      if (offset == 0 || offset >= sizeof(g_result)) goto unlock;
      g_expected = offset;
      g_checksum = sum;
      g_received = 0;
      g_receiving = true;
      ret = 0;
    }
  else if (argc == 5 && strcmp(argv[1], "c") == 0 &&
           g_receiving && number(argv[3], &offset))
    {
      size_t n = strlen(argv[4]);
      if (n == 0 || n % 2 || n > 32 || offset != g_received ||
          g_received + n / 2 > g_expected) goto unlock;
      for (size_t i = 0; i < n; i++) if (hex_value(argv[4][i]) < 0) goto unlock;
      for (size_t i = 0; i < n; i += 2)
        g_result[g_received++] = (hex_value(argv[4][i]) << 4) | hex_value(argv[4][i + 1]);
      ret = 0;
    }
  else if (argc == 3 && strcmp(argv[1], "e") == 0 && g_receiving)
    {
      uint32_t actual = 0;
      for (size_t i = 0; i < g_received; i++) actual += (uint8_t)g_result[i];
      if (g_received != g_expected || actual != g_checksum ||
          !utf8_valid((unsigned char *)g_result, g_received)) goto unlock;
      g_result[g_received] = 0;
      ret = photo_identify_set_message(g_result);
      if (ret == 0) {g_pending = false; g_receiving = false;}
    }
unlock:
  if (atomic_load(&g_cancelled))
    {
      g_pending = false;
      g_receiving = false;
      free(g_snapshot);
      g_snapshot = NULL;
    }
  nxmutex_unlock(&g_lock);
reply:
  printf("\nPIACK id=%lu ret=%d\nPIEND\n", id, ret);
  fflush(stdout);
  return ret == 0 ? 0 : 1;
}
