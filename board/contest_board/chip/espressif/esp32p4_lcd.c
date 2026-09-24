/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 * ESP32-P4X framebuffer adapter for the official Espressif DSI/EK79007 driver.
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/video/fb.h>
#include <nuttx/mutex.h>
#include <errno.h>
#include <stdint.h>
#include <syslog.h>
#include "esp_gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"

#define LCD_WIDTH  1024
#define LCD_HEIGHT 600
#define LCD_STRIDE (LCD_WIDTH * 2)
#define LCD_SIZE   (LCD_STRIDE * LCD_HEIGHT)
#define LCD_BL_GPIO 26

static mutex_t g_lock = NXMUTEX_INITIALIZER;
static esp_lcd_dsi_bus_handle_t g_bus;
static esp_lcd_panel_io_handle_t g_io;
static esp_lcd_panel_handle_t g_panel;
static esp_ldo_channel_handle_t g_ldo;
static bool g_ready;
static struct fb_planeinfo_s g_plane =
{
  .fblen = LCD_SIZE,
  .stride = LCD_STRIDE,
  .bpp = 16,
  .xres_virtual = LCD_WIDTH,
  .yres_virtual = LCD_HEIGHT,
};

static int lcd_errno(esp_err_t err)
{
  switch (err)
    {
      case ESP_OK: return OK;
      case ESP_ERR_NO_MEM: return -ENOMEM;
      case ESP_ERR_INVALID_ARG: return -EINVAL;
      case ESP_ERR_NOT_SUPPORTED: return -ENOTSUP;
      case ESP_ERR_TIMEOUT: return -ETIMEDOUT;
      default: return -EIO;
    }
}

static int lcd_getvideo(struct fb_vtable_s *vtable,
                        struct fb_videoinfo_s *info)
{
  *info = (struct fb_videoinfo_s)
    {
      .fmt = FB_FMT_RGB16_565,
      .xres = LCD_WIDTH,
      .yres = LCD_HEIGHT,
      .nplanes = 1,
    };
  return OK;
}

static int lcd_getplane(struct fb_vtable_s *vtable, int plane,
                        struct fb_planeinfo_s *info)
{
  if (plane != 0 || !g_ready)
    {
      return -EINVAL;
    }

  *info = g_plane;
  return OK;
}

static int lcd_update(struct fb_vtable_s *vtable,
                      const struct fb_area_s *area)
{
  int ret = nxmutex_lock(&g_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!g_ready)
    {
      ret = -ENODEV;
    }
  else if (area->x >= LCD_WIDTH || area->y >= LCD_HEIGHT ||
           area->w > LCD_WIDTH - area->x ||
           area->h > LCD_HEIGHT - area->y)
    {
      ret = -EINVAL;
    }
  else if (area->w == 0 || area->h == 0)
    {
      ret = OK;
    }
  else
    {
      /* A pointer inside the official framebuffer selects its no-copy path:
       * write back updated scanlines before DW-GDMA reads them.
       */

      ret = lcd_errno(esp_lcd_panel_draw_bitmap(g_panel, area->x, area->y,
                         area->x + area->w, area->y + area->h, g_plane.fbmem));
    }

  nxmutex_unlock(&g_lock);
  return ret;
}

static struct fb_vtable_s g_vtable =
{
  .getvideoinfo = lcd_getvideo,
  .getplaneinfo = lcd_getplane,
  .updatearea = lcd_update,
};

/* Caller holds g_lock. Official panel deletion stops DMA before releasing
 * its framebuffers. The board owns the IO, bus and PHY power supply.
 */

static int lcd_release(void)
{
  g_ready = false;
  esp_gpiowrite(LCD_BL_GPIO, false);
  if (g_panel)
    {
      esp_err_t err = esp_lcd_panel_del(g_panel);
      if (err != ESP_OK)
        {
          return lcd_errno(err);
        }

      g_panel = NULL;
    }

  g_plane.fbmem = NULL;
  if (g_io)
    {
      esp_lcd_panel_io_del(g_io);
      g_io = NULL;
    }

  if (g_bus)
    {
      esp_lcd_del_dsi_bus(g_bus);
      g_bus = NULL;
    }

  if (g_ldo)
    {
      esp_ldo_release_channel(g_ldo);
      g_ldo = NULL;
    }

  return OK;
}

int up_fbinitialize(int display)
{
  esp_err_t err;
  int ret;
  if (display != 0)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&g_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_ready)
    {
      nxmutex_unlock(&g_lock);
      return OK;
    }

  if (g_panel || g_io || g_bus || g_ldo)
    {
      ret = lcd_release();
      if (ret < 0)
        {
          nxmutex_unlock(&g_lock);
          return ret;
        }
    }

  esp_configgpio(LCD_BL_GPIO, OUTPUT);
  esp_gpiowrite(LCD_BL_GPIO, false);
  const esp_ldo_channel_config_t power =
    {
      .chan_id = 3,
      .voltage_mv = 2500,
    };
  const esp_lcd_dsi_bus_config_t bus =
    {
      .bus_id = 0,
      .num_data_lanes = 2,
      .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_XTAL,
      .lane_bit_rate_mbps = 1000,
    };
  const esp_lcd_dbi_io_config_t io =
    {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
    };
  const esp_lcd_dpi_panel_config_t dpi =
    {
      .virtual_channel = 0,
      .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_PLL_F240M,
      .dpi_clock_freq_mhz = 48,
      .in_color_format = LCD_COLOR_FMT_RGB565,
      .out_color_format = LCD_COLOR_FMT_RGB565,
      .num_fbs = 1,
      .video_timing =
        {
          .h_size = LCD_WIDTH,
          .v_size = LCD_HEIGHT,
          .hsync_pulse_width = 10,
          .hsync_back_porch = 120,
          .hsync_front_porch = 120,
          .vsync_pulse_width = 1,
          .vsync_back_porch = 20,
          .vsync_front_porch = 10,
        },
    };
  const ek79007_vendor_config_t vendor =
    {
      .mipi_config =
        {
          .dpi_config = &dpi,
          .lane_num = 2,
        },
    };
  ek79007_vendor_config_t panel_vendor = vendor;
  esp_lcd_panel_dev_config_t panel =
    {
      .reset_gpio_num = CONFIG_ESP32P4_BOARD_LCD_RST_GPIO,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
      .vendor_config = &panel_vendor,
    };

  syslog(LOG_INFO, "LCD: official DSI/EK79007 initialization\n");
  err = esp_ldo_acquire_channel(&power, &g_ldo);
  if (err != ESP_OK) goto fail;
  err = esp_lcd_new_dsi_bus(&bus, &g_bus);
  if (err != ESP_OK) goto fail;
  err = esp_lcd_new_panel_io_dbi(g_bus, &io, &g_io);
  if (err != ESP_OK) goto fail;
  panel_vendor.mipi_config.dsi_bus = g_bus;
  err = esp_lcd_new_panel_ek79007(g_io, &panel, &g_panel);
  if (err != ESP_OK) goto fail;
  err = esp_lcd_panel_reset(g_panel);
  if (err != ESP_OK) goto fail;
  err = esp_lcd_dpi_panel_get_frame_buffer(g_panel, 1, &g_plane.fbmem);
  if (err != ESP_OK) goto fail;

  /* Distinct from the Host vertical bars: horizontal RGB565 bands. */

  uint16_t *pixels = g_plane.fbmem;
  static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f};
  for (unsigned int y = 0; y < LCD_HEIGHT; y++)
    {
      for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
          pixels[y * LCD_WIDTH + x] = colors[y / (LCD_HEIGHT / 3)];
        }
    }

  err = esp_lcd_panel_draw_bitmap(g_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT,
                                   g_plane.fbmem);
  if (err != ESP_OK) goto fail;
  err = esp_lcd_panel_init(g_panel);
  if (err != ESP_OK) goto fail;
#ifdef CONFIG_ESP32P4_BOARD_LCD_COLORBAR
  err = esp_lcd_dpi_panel_set_pattern(g_panel, MIPI_DSI_PATTERN_BAR_VERTICAL);
  if (err != ESP_OK) goto fail;
#endif
  g_ready = true;
  esp_gpiowrite(LCD_BL_GPIO, true);
  syslog(LOG_INFO, "LCD: 1024x600 RGB565 framebuffer=%p ready\n",
         g_plane.fbmem);
  nxmutex_unlock(&g_lock);
  return OK;

fail:
  syslog(LOG_ERR, "LCD: initialization failed: 0x%x\n", err);
  lcd_release();
  nxmutex_unlock(&g_lock);
  return lcd_errno(err);
}

struct fb_vtable_s *up_fbgetvplane(int display, int vplane)
{
  return display == 0 && vplane == 0 && g_ready ? &g_vtable : NULL;
}

void up_fbuninitialize(int display)
{
  if (display == 0 && nxmutex_lock(&g_lock) == OK)
    {
      int ret = lcd_release();
      if (ret < 0)
        {
          syslog(LOG_ERR, "LCD: release failed: %d\n", ret);
        }

      nxmutex_unlock(&g_lock);
    }
}
