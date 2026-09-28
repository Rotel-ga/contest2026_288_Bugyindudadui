/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_preview.h"
#include <nuttx/mutex.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static uint16_t *g_pending;
static uint32_t g_sequence;

static uint8_t corrected(unsigned int value, unsigned int max,
                         uint32_t gain)
{
  uint64_t channel = (value * 255 + max / 2) / max;
  channel = (channel * gain + 32768) >> 16;
  return channel > 255 ? 255 : channel;
}

void camera_preview_resize(uint16_t *dst, const uint16_t *src,
                           unsigned int width, unsigned int height,
                           const uint32_t *gains)
{
  uint8_t red[32];
  uint8_t green[64];
  uint8_t blue[32];
  for (unsigned int i = 0; i < 64; i++)
    {
      green[i] = corrected(i, 63, gains ? gains[1] : 65536);
      if (i < 32)
        {
          red[i] = corrected(i, 31, gains ? gains[0] : 65536);
          blue[i] = corrected(i, 31, gains ? gains[2] : 65536);
        }
    }

  /* Box average, retaining the original orientation and full 16:9 frame. */
  for (unsigned int y = 0; y < CAMERA_PREVIEW_H; y++)
    {
      unsigned int y0 = y * height / CAMERA_PREVIEW_H;
      unsigned int y1 = (y + 1) * height / CAMERA_PREVIEW_H;
      if (y1 <= y0) y1 = y0 + 1;
      for (unsigned int x = 0; x < CAMERA_PREVIEW_W; x++)
        {
          unsigned int x0 = x * width / CAMERA_PREVIEW_W;
          unsigned int x1 = (x + 1) * width / CAMERA_PREVIEW_W;
          unsigned int r = 0, g = 0, b = 0, n = 0;
          if (x1 <= x0) x1 = x0 + 1;
          for (unsigned int sy = y0; sy < y1; sy++)
            for (unsigned int sx = x0; sx < x1; sx++)
              {
                uint16_t p = src[(size_t)sy * width + sx];
                r += red[p >> 11];
                g += green[(p >> 5) & 63];
                b += blue[p & 31];
                n++;
              }
          dst[y * CAMERA_PREVIEW_W + x] =
            ((r / n * 31 + 127) / 255 << 11) |
            ((g / n * 63 + 127) / 255 << 5) |
            ((b / n * 31 + 127) / 255);
        }
    }
}

int camera_preview_open(void)
{
  int ret = 0;
  nxmutex_lock(&g_lock);
  if (!g_pending)
    {
      g_pending = malloc(CAMERA_PREVIEW_BYTES);
      if (!g_pending) ret = -ENOMEM;
      g_sequence = 0;
    }
  nxmutex_unlock(&g_lock);
  return ret;
}

void camera_preview_close(void)
{
  nxmutex_lock(&g_lock);
  free(g_pending);
  g_pending = NULL;
  g_sequence = 0;
  nxmutex_unlock(&g_lock);
}

int camera_preview_publish(const uint16_t *src, unsigned int width,
                           unsigned int height, const uint32_t *gains)
{
  if (!src || width != 1280 || height != 720) return -EINVAL;
  nxmutex_lock(&g_lock);
  if (!g_pending)
    {
      nxmutex_unlock(&g_lock);
      return -ENODEV;
    }
  camera_preview_resize(g_pending, src, width, height, gains);
  if (++g_sequence == 0) g_sequence = 1;
  nxmutex_unlock(&g_lock);
  return 0;
}

uint32_t camera_preview_take(uint16_t *dst, uint32_t previous)
{
  uint32_t sequence = previous;
  if (nxmutex_trylock(&g_lock) < 0) return previous;
  if (dst && g_pending && g_sequence && g_sequence != previous)
    {
      memcpy(dst, g_pending, CAMERA_PREVIEW_BYTES);
      sequence = g_sequence;
    }
  nxmutex_unlock(&g_lock);
  return sequence;
}
