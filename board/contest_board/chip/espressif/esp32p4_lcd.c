/****************************************************************************
 * boards/contest_board/src/esp32p4_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 MIPI-DSI display driver for the EK79007 1024x600 panel on the
 * ESP32-P4X-Function-EV-Board.
 *
 * Bring-up strategy (see docs): milestone A brings the physical link up and
 * shows the DSI host built-in colour-bar test pattern (no GDMA / framebuffer
 * streaming required).  Milestone B (DW-GDMA framebuffer streaming) feeds the
 * PSRAM framebuffer to the bridge for real content.  This file implements
 * milestone A plus the NuttX framebuffer registration scaffolding.
 *
 * Init sequence (mirrors the ESP-IDF esp_lcd MIPI-DSI flow, re-implemented
 * against the esp-hal HAL/LL layers):
 *   LDO 2.5V (VDD_MIPI_DPHY) -> DSI bus/PHY/DPI clocks -> mipi_dsi_hal_init
 *   -> configure PHY PLL -> wait PLL lock -> EK79007 DCS init -> DPI timing
 *   -> host video mode -> bridge -> colour-bar pattern.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>
#include <nuttx/video/fb.h>
#include <arch/board/board.h>

#ifdef CONFIG_ESP32P4_BOARD_LCD

/* esp-hal MIPI-DSI HAL / LL and supporting components */

#include "esp_ldo_regulator.h"
#include "hal/mipi_dsi_hal.h"
#include "hal/mipi_dsi_ll.h"
#include "hal/lcd_types.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/dw_gdma.h"
#include "esp_cache.h"
#include "soc/soc.h"
#include "esp_gpio.h"
#include "hal/dw_gdma_ll.h"
#include "esp_rom_sys.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* EK79007 1024x600 panel timing (60Hz):
 * FPS = DPI_CLK / (hsw+hbp+hact+hfp) / (vsw+vbp+vact+vfp)
 *     = 48M / (10+120+1024+120) / (1+20+600+10) = ~60Hz
 */

#define LCD_H_RES              1024
#define LCD_V_RES              600
#define LCD_HSYNC              10
#define LCD_HBP                120
#define LCD_HFP                120
#define LCD_VSYNC              1
#define LCD_VBP                20
#define LCD_VFP                10

#define LCD_DPI_CLK_MHZ        48
#define LCD_LANE_NUM           2
#define LCD_LANE_BITRATE_MBPS  1000.0f

/* PHY PLL reference: use the internal 20MHz source (no divider). */

#define LCD_PHY_REF_CLK_HZ     40000000

/* DPI pixel clock is derived from PLL_F240M (240MHz). */

#define LCD_DPI_SRC_CLK_MHZ    240

/* Framebuffer: RGB565 to halve PSRAM footprint (1024*600*2 = 1.2MB). */

#define LCD_BPP                16
#define LCD_COLOR_FMT          LCD_COLOR_FMT_RGB565
#define LCD_FB_STRIDE          (LCD_H_RES * (LCD_BPP / 8))
#define LCD_FB_SIZE            (LCD_FB_STRIDE * LCD_V_RES)

/* Optional panel hardware reset GPIO. ESP32-P4X-Function-EV-Board wires the
 * 7" screen adapter's LCD_RST to GPIO27 and the backlight PWM to GPIO26.
 */

#ifndef CONFIG_ESP32P4_BOARD_LCD_RST_GPIO
#  define CONFIG_ESP32P4_BOARD_LCD_RST_GPIO 27
#endif

#define LCD_BL_GPIO            26

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  uint8_t cmd;
  uint8_t data[4];
  uint8_t data_bytes;
  uint16_t delay_ms;
} ek79007_init_cmd_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* EK79007 vendor-specific default initialisation sequence (from the
 * ESP-IDF esp_lcd_ek79007 component).  0xB2 selects the lane count and
 * must be sent first; 0x11 (sleep-out) needs a 120ms settle delay.
 */

static const ek79007_init_cmd_t g_ek79007_init[] =
{
  {0xb2, {0x10}, 1, 0},   /* PAD_CONTROL: 0x10 = 2-lane */
  {0x80, {0x8b}, 1, 0},
  {0x81, {0x78}, 1, 0},
  {0x82, {0x84}, 1, 0},
  {0x83, {0x88}, 1, 0},
  {0x84, {0xa8}, 1, 0},
  {0x85, {0xe3}, 1, 0},
  {0x86, {0x88}, 1, 0},
  {0x11, {0x00}, 0, 120}, /* sleep-out */
};

static mipi_dsi_hal_context_t g_dsi_hal;
static esp_ldo_channel_handle_t g_ldo_phy;
static uint8_t *g_fbmem;
static dw_gdma_channel_handle_t g_dma_chan;
static dw_gdma_link_list_handle_t g_link_list;

/* NuttX framebuffer descriptors */

static struct fb_videoinfo_s g_videoinfo =
{
  .fmt     = FB_FMT_RGB16_565,
  .xres    = LCD_H_RES,
  .yres    = LCD_V_RES,
  .nplanes = 1,
};

static struct fb_planeinfo_s g_planeinfo =
{
  .fbmem   = NULL,
  .fblen   = LCD_FB_SIZE,
  .stride  = LCD_FB_STRIDE,
  .display = 0,
  .bpp     = LCD_BPP,
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int esp32p4_fb_getvideoinfo(struct fb_vtable_s *vtable,
                                   struct fb_videoinfo_s *vinfo);
static int esp32p4_fb_getplaneinfo(struct fb_vtable_s *vtable, int planeno,
                                   struct fb_planeinfo_s *pinfo);

/****************************************************************************
 * Private Data (vtable)
 ****************************************************************************/

static struct fb_vtable_s g_fb_vtable =
{
  .getvideoinfo = esp32p4_fb_getvideoinfo,
  .getplaneinfo = esp32p4_fb_getplaneinfo,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp32p4_lcd_clocks_enable
 *
 * Description:
 *   Enable and route the MIPI-DSI bus, PHY-config, PHY-PLL-ref and DPI
 *   clocks.  Must run inside the RCC atomic environment.
 ****************************************************************************/

static void esp32p4_lcd_clocks_enable(uint32_t dpi_div)
{
  PERIPH_RCC_ATOMIC()
    {
      /* DSI system bus clock + module reset */

      mipi_dsi_ll_enable_bus_clock(0, true);
      mipi_dsi_ll_reset_register(0);

      /* PHY configuration clock (20MHz internal source) */

      mipi_dsi_ll_enable_phy_config_clock(0, true);
      mipi_dsi_ll_set_phy_config_clock_source(0,
                                              MIPI_DSI_PHY_CFG_CLK_SRC_PLL_F20M);

      /* PHY PLL reference clock — use XTAL (valid on ESP32-P4 rev >= v3;
       * the internal PLL_F20M source is only valid on the legacy rev < v3
       * register layout and would hit abort() here).
       */

      mipi_dsi_ll_set_phy_pllref_clock_source(0,
                                              MIPI_DSI_PHY_PLLREF_CLK_SRC_XTAL);
      mipi_dsi_ll_set_phy_pll_ref_clock_div(0, 1);   /* no division on ref */
      mipi_dsi_ll_enable_phy_pllref_clock(0, true);

      /* DPI pixel clock: PLL_F240M / dpi_div = 48MHz */

      mipi_dsi_ll_set_dpi_clock_source(0, MIPI_DSI_DPI_CLK_SRC_PLL_F240M);
      mipi_dsi_ll_set_dpi_clock_div(0, dpi_div);
      mipi_dsi_ll_enable_dpi_clock(0, true);
    }
}

/****************************************************************************
 * Name: esp32p4_lcd_panel_reset
 ****************************************************************************/

static void esp32p4_lcd_panel_reset(void)
{
  /* Turn the backlight on (GPIO26, driven high = full brightness). */

  esp_configgpio(LCD_BL_GPIO, OUTPUT);
  esp_gpiowrite(LCD_BL_GPIO, true);

#if CONFIG_ESP32P4_BOARD_LCD_RST_GPIO >= 0
  /* EK79007 hardware reset (active low): assert 10ms, release, wait 20ms. */

  esp_configgpio(CONFIG_ESP32P4_BOARD_LCD_RST_GPIO, OUTPUT);
  esp_gpiowrite(CONFIG_ESP32P4_BOARD_LCD_RST_GPIO, false);
  up_mdelay(10);
  esp_gpiowrite(CONFIG_ESP32P4_BOARD_LCD_RST_GPIO, true);
  up_mdelay(20);
#endif
}

/****************************************************************************
 * Name: esp32p4_lcd_panel_init
 *
 * Description:
 *   Send the EK79007 DCS initialisation sequence over the DSI command
 *   (generic/DBI) channel.
 ****************************************************************************/

static void esp32p4_lcd_panel_init(void)
{
  int i;

  for (i = 0; i < (int)(sizeof(g_ek79007_init) /
                        sizeof(g_ek79007_init[0])); i++)
    {
      const ek79007_init_cmd_t *c = &g_ek79007_init[i];

      mipi_dsi_hal_host_gen_write_dcs_command(&g_dsi_hal, 0, c->cmd, 1,
                                              c->data, c->data_bytes);
      if (c->delay_ms > 0)
        {
          up_mdelay(c->delay_ms);
        }
    }
}

/****************************************************************************
 * Name: esp32p4_fb_getvideoinfo
 ****************************************************************************/

static int esp32p4_fb_getvideoinfo(struct fb_vtable_s *vtable,
                                   struct fb_videoinfo_s *vinfo)
{
  *vinfo = g_videoinfo;
  return OK;
}

/****************************************************************************
 * Name: esp32p4_fb_getplaneinfo
 ****************************************************************************/

static int esp32p4_fb_getplaneinfo(struct fb_vtable_s *vtable, int planeno,
                                   struct fb_planeinfo_s *pinfo)
{
  if (planeno != 0)
    {
      return -EINVAL;
    }

  *pinfo = g_planeinfo;
  return OK;
}

/****************************************************************************
 * Name: esp32p4_lcd_dma_done_cb
 *
 * Description:
 *   DW-GDMA "full transfer done" callback.  Re-arms the link-list transfer
 *   so the panel is refreshed continuously from the framebuffer.
 ****************************************************************************/

static bool esp32p4_lcd_dma_done_cb(dw_gdma_channel_handle_t chan,
                                    const dw_gdma_trans_done_event_data_t *edata,
                                    void *user_data)
{
  dw_gdma_block_markers_t markers =
  {
    .is_valid = true,
    .is_last  = true,
  };

  dw_gdma_lli_set_block_markers(dw_gdma_link_list_get_item(g_link_list, 0),
                                markers);
  dw_gdma_channel_use_link_list(chan, g_link_list);
  dw_gdma_channel_enable_ctrl(chan, true);
  return false;
}

/****************************************************************************
 * Name: esp32p4_lcd_dma_setup
 *
 * Description:
 *   Allocate the DW-GDMA channel and a single-item link list that streams
 *   the PSRAM framebuffer into the DSI bridge FIFO (MIPI_DSI_BRG_MEM_BASE).
 *   DMA acts as the flow controller; the bridge applies back-pressure via
 *   credits.  The transfer is re-armed in the done callback for continuous
 *   refresh.
 ****************************************************************************/

static int esp32p4_lcd_dma_setup(void)
{
  dw_gdma_channel_alloc_config_t alloc =
  {
    .src =
    {
      .block_transfer_type      = DW_GDMA_BLOCK_TRANSFER_LIST,
      .role                     = DW_GDMA_ROLE_MEM,
      .handshake_type           = DW_GDMA_HANDSHAKE_HW,
      .num_outstanding_requests = 5,
    },
    .dst =
    {
      .block_transfer_type      = DW_GDMA_BLOCK_TRANSFER_LIST,
      .role                     = DW_GDMA_ROLE_PERIPH_DSI,
      .handshake_type           = DW_GDMA_HANDSHAKE_HW,
      .num_outstanding_requests = 2,
    },
    .flow_controller = DW_GDMA_FLOW_CTRL_SELF,
    .chan_priority   = 1,
  };

  dw_gdma_link_list_config_t llcfg =
  {
    .num_items = 1,
    .link_type = DW_GDMA_LINKED_LIST_TYPE_CIRCULAR,
  };

  dw_gdma_block_transfer_config_t xfer =
  {
    .src =
    {
      .addr        = (uint32_t)(uintptr_t)g_fbmem,
      .burst_mode  = DW_GDMA_BURST_MODE_INCREMENT,
      .burst_items = DW_GDMA_BURST_ITEMS_512,
      .burst_len   = 16,
      .width       = DW_GDMA_TRANS_WIDTH_64,
    },
    .dst =
    {
      .addr        = MIPI_DSI_BRG_MEM_BASE,
      .burst_mode  = DW_GDMA_BURST_MODE_FIXED,
      .burst_items = DW_GDMA_BURST_ITEMS_256,
      .burst_len   = 16,
      .width       = DW_GDMA_TRANS_WIDTH_64,
    },
    .size = (size_t)LCD_FB_SIZE * 8 / 64,   /* transfer size in 64-bit words */
  };

  dw_gdma_event_callbacks_t cbs =
  {
    .on_full_trans_done = esp32p4_lcd_dma_done_cb,
  };

  dw_gdma_block_markers_t markers =
  {
    .is_valid = true,
    .is_last  = false,
  };

  if (dw_gdma_new_channel(&alloc, &g_dma_chan) != 0)
    {
      lcderr("ERROR: dw_gdma_new_channel failed\n");
      return -EIO;
    }

  if (dw_gdma_new_link_list(&llcfg, &g_link_list) != 0)
    {
      lcderr("ERROR: dw_gdma_new_link_list failed\n");
      return -EIO;
    }

  dw_gdma_channel_register_event_callbacks(g_dma_chan, &cbs, NULL);

  dw_gdma_lli_config_transfer(dw_gdma_link_list_get_item(g_link_list, 0),
                              &xfer);
  dw_gdma_lli_set_block_markers(dw_gdma_link_list_get_item(g_link_list, 0),
                                markers);
  dw_gdma_channel_use_link_list(g_dma_chan, g_link_list);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp32p4_lcd_initialize
 ****************************************************************************/

int esp32p4_lcd_initialize(void)
{
  mipi_dsi_hal_config_t halcfg;
  uint32_t dpi_div;
  uint32_t esc_div;
  int timeout;
  int ret;
  static bool s_inited = false;

  /* up_fbinitialize()/this routine can be invoked more than once (e.g. when
   * something opens /dev/fb0 again).  Re-running the full DSI bring-up would
   * reset the PHY and, if the PLL fails to re-lock, kill the already-working
   * display (observed as the picture fading to black).  Make it idempotent.
   */

  if (s_inited)
    {
      syslog(LOG_ERR, "LCD: GUARD hit, skip re-init\n");
      return OK;
    }

  s_inited = true;   /* mark immediately so any re-entry is blocked */

  syslog(LOG_ERR,"LCD: begin\n");
  syslog(LOG_ERR,"LCD: reset_reason=0x%x "
         "(1=PWRON 3=CORE_SW 7=MWDT 9=RWDT c=CPU_SW d=CPU_RWDT 10=SYS_RWDT "
         "12=SUPER_WDT 16=USB_JTAG 1a=CPU_LOCKUP)\n",
         (unsigned)esp_rom_get_reset_reason(0));

  /* 1. Power the MIPI D-PHY via internal LDO channel 3 @ 2.5V. */

  esp_ldo_channel_config_t ldocfg =
  {
    .chan_id    = 3,
    .voltage_mv = 2500,
  };

  if (esp_ldo_acquire_channel(&ldocfg, &g_ldo_phy) != 0)
    {
      syslog(LOG_ERR,"LCD: FAIL ldo\n");
      lcderr("ERROR: failed to acquire MIPI DPHY LDO channel\n");
      return -EIO;
    }


  /* 2. Compute divisors and enable the DSI clock tree.
   *    escape clock must stay <= 20MHz (lane byte clock = bitrate/8).
   */

  dpi_div = LCD_DPI_SRC_CLK_MHZ / LCD_DPI_CLK_MHZ;                 /* 240/48 = 5  */
  esc_div = (uint32_t)((LCD_LANE_BITRATE_MBPS / 8.0f) / 20.0f) + 1; /* ~8 */

  esp32p4_lcd_clocks_enable(dpi_div);


  /* 3. Initialise the DSI HAL (powers host+PHY, resets PHY, enables clock
   *    lane, forces PLL) and configure the PHY PLL for the lane bit rate.
   */

  halcfg.bus_id            = 0;
  halcfg.num_data_lanes    = LCD_LANE_NUM;
  halcfg.lane_bit_rate_mbps = LCD_LANE_BITRATE_MBPS;
  mipi_dsi_hal_init(&g_dsi_hal, &halcfg);

  /* Populate hal->expect_dpi_clock_freq_mhz / real_dpi_clock_freq_mhz.
   * The DPI timing helpers (set_horizontal/vertical_timing) divide by these
   * fields; if left at 0 the timing math yields NaN/garbage and the DSI
   * bridge never scans out (black screen, DMA stalls at 0 bytes).
   */

  mipi_dsi_hal_host_dpi_calculate_divider(&g_dsi_hal,
                                          (float)LCD_DPI_SRC_CLK_MHZ,
                                          (float)LCD_DPI_CLK_MHZ);

  /* Configure the PHY PLL and wait for it to lock.
   *
   * On a COLD boot the DSI reference/config clocks have only just been
   * enabled and are not yet stable, so the DW MIPI D-PHY PLL frequently
   * fails to lock on the first attempt (works after a warm reset because
   * the clocks were already running).  Give the clocks a moment to settle,
   * then retry the whole configure+wait sequence several times.
   */

  up_mdelay(10);   /* let the freshly-enabled DSI clocks settle */

  {
    int attempt;
    bool locked = false;

    for (attempt = 1; attempt <= 20 && !locked; attempt++)
      {
        /* Re-run the PHY reset each attempt.  configure_phy_pll only writes
         * the PLL M/N divider; it does NOT restart the PLL.  A cold-boot PLL
         * that fails to lock stays stuck unless the PHY is reset (rstz +
         * force_pll) first — which is what mipi_dsi_hal_init does.
         */

        mipi_dsi_hal_init(&g_dsi_hal, &halcfg);
        mipi_dsi_hal_configure_phy_pll(&g_dsi_hal, LCD_PHY_REF_CLK_HZ,
                                       LCD_LANE_BITRATE_MBPS);

        for (timeout = 2000; timeout > 0; timeout--)
          {
            if (mipi_dsi_phy_ll_is_pll_locked(g_dsi_hal.host))
              {
                locked = true;
                break;
              }

            up_udelay(10);
          }

        if (!locked)
          {
            up_mdelay(5);   /* clocks settle further, then reset+reconfigure */
          }
      }

    if (!locked)
      {
        syslog(LOG_ERR,"LCD: FAIL pll lock\n");
        lcderr("ERROR: MIPI DSI PHY PLL failed to lock\n");
        return -ETIMEDOUT;
      }

    syslog(LOG_ERR,"LCD: pll locked (attempt %d)\n", attempt - 1);
  }

  /* 4. Complete DSI host/PHY bring-up (mirrors ESP-IDF esp_lcd_new_dsi_bus).
   *    These PHY timing / EOTp / clock settings are essential for the D-PHY
   *    to actually transmit valid packets — without them the panel receives
   *    nothing and stays black.
   */

  /* wait for the data lanes to settle in stop state */

  for (timeout = 1000; timeout > 0; timeout--)
    {
      if (mipi_dsi_phy_ll_are_lanes_stopped(g_dsi_hal.host, LCD_LANE_NUM))
        {
          break;
        }

      up_udelay(10);
    }

  /* start in command mode; clock lane stays LP until the DPI stream is up */

  mipi_dsi_host_ll_enable_video_mode(g_dsi_hal.host, false);
  mipi_dsi_host_ll_set_clock_lane_state(g_dsi_hal.host,
                                        MIPI_DSI_LL_CLOCK_LANE_STATE_HS);

  /* PHY HS<->LP switch timing (data hs2lp, lp2hs, clk hs2lp, lp2hs) */

  mipi_dsi_phy_ll_set_switch_time(g_dsi_hal.host, 50, 104, 46, 128);

  /* packet integrity + end-of-transmission */

  mipi_dsi_host_ll_enable_rx_crc(g_dsi_hal.host, true);
  mipi_dsi_host_ll_enable_rx_ecc(g_dsi_hal.host, true);
  mipi_dsi_host_ll_enable_tx_eotp(g_dsi_hal.host, true, false);

  /* timeout / escape clock dividers (source = HS byte clock).
   * Match IDF: timeout clock target 10MHz, escape clock target 18MHz.
   */

  mipi_dsi_host_ll_set_timeout_clock_division(g_dsi_hal.host,
      (uint32_t)roundf(LCD_LANE_BITRATE_MBPS / 8.0f / 10.0f));   /* ~13 */
  esc_div = (uint32_t)roundf(LCD_LANE_BITRATE_MBPS / 8.0f / 18.0f); /* ~7 */
  mipi_dsi_host_ll_set_escape_clock_division(g_dsi_hal.host, esc_div);
  mipi_dsi_host_ll_set_timeout_count(g_dsi_hal.host, 0, 0x7FFF, 0, 0, 0, 0, 0);

  /* read/stop wait times */

  mipi_dsi_phy_ll_set_max_read_time(g_dsi_hal.host, 6000);
  mipi_dsi_phy_ll_set_stop_wait_time(g_dsi_hal.host, 0x3f);

  /* Send all init commands in low-power mode (DBI command channel). */

  /* DBI IO config (from esp_lcd_new_panel_io_dbi): no tear-effect ack,
   * enable command ack for reliable command delivery.
   */

  mipi_dsi_host_ll_enable_te_ack(g_dsi_hal.host, false);
  mipi_dsi_host_ll_enable_cmd_ack(g_dsi_hal.host, true);

  for (int np = 0; np <= 2; np++)
    {
      mipi_dsi_host_ll_set_gen_short_wr_speed_mode(g_dsi_hal.host, np,
                                                   MIPI_DSI_LL_TRANS_SPEED_LP);
      mipi_dsi_host_ll_set_gen_short_rd_speed_mode(g_dsi_hal.host, np,
                                                   MIPI_DSI_LL_TRANS_SPEED_LP);
      mipi_dsi_host_ll_set_dcs_short_wr_speed_mode(g_dsi_hal.host, np,
                                                   MIPI_DSI_LL_TRANS_SPEED_LP);
      mipi_dsi_host_ll_set_dcs_short_rd_speed_mode(g_dsi_hal.host, np,
                                                   MIPI_DSI_LL_TRANS_SPEED_LP);
    }

  mipi_dsi_host_ll_set_gen_long_wr_speed_mode(g_dsi_hal.host,
                                              MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_dcs_long_wr_speed_mode(g_dsi_hal.host,
                                              MIPI_DSI_LL_TRANS_SPEED_LP);
  mipi_dsi_host_ll_set_mrps_speed_mode(g_dsi_hal.host,
                                       MIPI_DSI_LL_TRANS_SPEED_LP);

  syslog(LOG_ERR,"LCD: phy/host cfg ok\n");

  /* 5. Reset panel and push the EK79007 DCS init sequence. */

  esp32p4_lcd_panel_reset();
  esp32p4_lcd_panel_init();

  /* 6. DPI video-mode timing / colour coding. */

  mipi_dsi_host_ll_dpi_set_vcid(g_dsi_hal.host, 0);
  mipi_dsi_host_ll_dpi_set_color_coding(g_dsi_hal.host, LCD_COLOR_FMT, 0);
  mipi_dsi_host_ll_dpi_set_timing_polarity(g_dsi_hal.host, false, false,
                                           false, false, false);

  /* Permit low-power transitions during blanking periods. */

  mipi_dsi_host_ll_dpi_enable_lp_horizontal_timing(g_dsi_hal.host, true, true);
  mipi_dsi_host_ll_dpi_enable_lp_vertical_timing(g_dsi_hal.host, true, true,
                                                 true, true);
  mipi_dsi_host_ll_dpi_enable_frame_ack(g_dsi_hal.host, true);
  mipi_dsi_host_ll_dpi_enable_lp_command(g_dsi_hal.host, true);
  mipi_dsi_host_ll_dpi_set_video_burst_type(g_dsi_hal.host,
                                    MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES);
  mipi_dsi_host_ll_dpi_set_video_packet_pixel_num(g_dsi_hal.host, LCD_H_RES);
  mipi_dsi_host_ll_dpi_set_trunks_num(g_dsi_hal.host, 0);
  mipi_dsi_host_ll_dpi_set_null_packet_size(g_dsi_hal.host, 0);

  mipi_dsi_hal_host_dpi_set_horizontal_timing(&g_dsi_hal, LCD_HSYNC, LCD_HBP,
                                              LCD_H_RES, LCD_HFP);
  mipi_dsi_hal_host_dpi_set_vertical_timing(&g_dsi_hal, LCD_VSYNC, LCD_VBP,
                                            LCD_V_RES, LCD_VFP);


  syslog(LOG_ERR,"LCD: panel+dpi ok\n");

  /* 7. Bridge: per-line pixel bits, DMA flow control, burst/credit. */

  mipi_dsi_brg_ll_set_num_pixel_bits(g_dsi_hal.bridge,
                                     LCD_H_RES * LCD_V_RES * LCD_BPP);
  mipi_dsi_brg_ll_set_input_color_format(g_dsi_hal.bridge, LCD_COLOR_FMT);

  /* NOTE: bridge horizontal/vertical timing is already programmed (with the
   * refresh-rate compensation) by mipi_dsi_hal_host_dpi_set_*_timing above.
   */

  mipi_dsi_brg_ll_set_multi_block_number(g_dsi_hal.bridge, 1);
  mipi_dsi_brg_ll_set_underrun_discard_count(g_dsi_hal.bridge, LCD_H_RES);
  mipi_dsi_brg_ll_set_flow_controller(g_dsi_hal.bridge,
                                      MIPI_DSI_LL_FLOW_CONTROLLER_DMA);
  mipi_dsi_brg_ll_set_burst_len(g_dsi_hal.bridge, 256);
  mipi_dsi_brg_ll_set_empty_threshold(g_dsi_hal.bridge, 1024 - 256);
  mipi_dsi_brg_ll_enable_ref_clock(g_dsi_hal.bridge, true);
  mipi_dsi_brg_ll_enable(g_dsi_hal.bridge, true);
  mipi_dsi_brg_ll_update_dpi_config(g_dsi_hal.bridge);


  /* 8. Allocate the PSRAM framebuffer (RGB565) and flush it to memory so
   *    the DMA sees a clean buffer.
   */

  g_fbmem = kmm_memalign(64, LCD_FB_SIZE);
  if (g_fbmem == NULL)
    {
      syslog(LOG_ERR,"LCD: FAIL fb alloc\n");
      lcderr("ERROR: failed to allocate %d byte framebuffer\n", LCD_FB_SIZE);
      return -ENOMEM;
    }

  /* Bring-up test pattern: fill the framebuffer with 8 vertical colour bars
   * (RGB565) so the real GDMA->bridge->panel path shows visible content.
   */

  {
    static const uint16_t bars[8] =
    {
      0xFFFF, 0xFFE0, 0x07FF, 0x07E0,   /* white, yellow, cyan, green   */
      0xF81F, 0xF800, 0x001F, 0x0000,   /* magenta, red, blue, black    */
    };
    uint16_t *fb = (uint16_t *)g_fbmem;
    int x;
    int y;

    for (y = 0; y < LCD_V_RES; y++)
      {
        for (x = 0; x < LCD_H_RES; x++)
          {
            fb[y * LCD_H_RES + x] = bars[(x * 8) / LCD_H_RES];
          }
      }
  }

  esp_cache_msync(g_fbmem, LCD_FB_SIZE,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  g_planeinfo.fbmem = g_fbmem;


  syslog(LOG_ERR,"LCD: fb fill ok\n");

  /* 9. Set up the DW-GDMA framebuffer -> bridge stream. */

  ret = esp32p4_lcd_dma_setup();
  if (ret < 0)
    {
      return ret;
    }


  syslog(LOG_ERR,"LCD: dma ok\n");

  /* 10. Enable the pipeline. */

#ifdef CONFIG_ESP32P4_BOARD_LCD_COLORBAR
  mipi_dsi_host_ll_enable_video_mode(g_dsi_hal.host, true);
  mipi_dsi_host_ll_set_clock_lane_state(g_dsi_hal.host,
                                        MIPI_DSI_LL_CLOCK_LANE_STATE_HS);

  /* Bring-up test: show the DSI host built-in colour bar (no DMA stream). */

  mipi_dsi_brg_ll_enable_dpi_output(g_dsi_hal.bridge, false);
  mipi_dsi_brg_ll_update_dpi_config(g_dsi_hal.bridge);
  mipi_dsi_host_ll_dpi_set_pattern_type(g_dsi_hal.host,
                                        MIPI_DSI_PATTERN_BAR_VERTICAL);
  lcdinfo("EK79007 %dx%d MIPI-DSI up (colour-bar test); /dev/fb0 ready\n",
          LCD_H_RES, LCD_V_RES);
#else
  /* Real framebuffer streaming — order mirrors ESP-IDF dpi_panel_init:
   * start the DMA first, then enable video mode + HS clock lane, then turn
   * on the bridge DPI output.
   */

  mipi_dsi_host_ll_dpi_set_pattern_type(g_dsi_hal.host, MIPI_DSI_PATTERN_NONE);
  dw_gdma_channel_enable_ctrl(g_dma_chan, true);
  mipi_dsi_host_ll_enable_video_mode(g_dsi_hal.host, true);
  mipi_dsi_host_ll_set_clock_lane_state(g_dsi_hal.host,
                                        MIPI_DSI_LL_CLOCK_LANE_STATE_HS);
  mipi_dsi_brg_ll_enable_dpi_output(g_dsi_hal.bridge, true);
  mipi_dsi_brg_ll_update_dpi_config(g_dsi_hal.bridge);
  lcdinfo("EK79007 %dx%d MIPI-DSI up (framebuffer stream); /dev/fb0 ready\n",
          LCD_H_RES, LCD_V_RES);
#endif

  syslog(LOG_ERR,"LCD: DONE ok\n");

  /* ---- diagnostics: is the pipeline actually running? ---- */

  {
    dw_gdma_dev_t *gdma = DW_GDMA_LL_GET_HW(0);
    int chid = 0;
    uint32_t amt0;
    uint32_t amt1;
    uint32_t fifo;
    bool locked;
    bool stopped;

    dw_gdma_channel_get_id(g_dma_chan, &chid);
    amt0 = dw_gdma_ll_channel_get_trans_amount(gdma, chid);
    up_mdelay(50);
    amt1 = dw_gdma_ll_channel_get_trans_amount(gdma, chid);
    fifo = dw_gdma_ll_channel_get_fifo_remain(gdma, chid);
    locked = mipi_dsi_phy_ll_is_pll_locked(g_dsi_hal.host);
    stopped = mipi_dsi_phy_ll_are_lanes_stopped(g_dsi_hal.host, LCD_LANE_NUM);

    syslog(LOG_ERR, "DIAG chid=%d dma_amt0=%lu amt1=%lu (moving=%d) fifo=%lu\n",
           chid, (unsigned long)amt0, (unsigned long)amt1,
           (amt0 != amt1), (unsigned long)fifo);
    syslog(LOG_ERR, "DIAG lli_addr=0x%lx intr=0x%lx\n",
           (unsigned long)dw_gdma_ll_channel_get_current_link_list_item_addr(
                                                                gdma, chid),
           (unsigned long)dw_gdma_ll_channel_get_intr_status(gdma, chid));

    /* dump the actual DMA descriptor in memory */

    {
      volatile uint32_t *d = (volatile uint32_t *)(uintptr_t)
          dw_gdma_ll_channel_get_current_link_list_item_addr(gdma, chid);
      if (d != NULL)
        {
          syslog(LOG_ERR, "DIAG desc sar=0x%lx dar=0x%lx block_ts=%lu "
                 "llp=0x%lx ctrl_lo=0x%lx ctrl_hi=0x%lx\n",
                 (unsigned long)d[0], (unsigned long)d[2],
                 (unsigned long)(d[4] & 0x3fffff),
                 (unsigned long)d[6], (unsigned long)d[8],
                 (unsigned long)d[9]);
        }

      /* also read the live channel registers (block_ts programmed) */

      syslog(LOG_ERR, "DIAG chen=0x%lx ch_block_ts=%lu\n",
             (unsigned long)gdma->chen0.val,
             (unsigned long)(gdma->ch[chid].block_ts0.val & 0x3fffff));
    }
    syslog(LOG_ERR, "DIAG pll_locked=%d lanes_stopped=%d(0=HS active) "
           "lane_mbps=%d dpi_div=%lu\n",
           locked, stopped, (int)g_dsi_hal.lane_bit_rate_mbps,
           (unsigned long)dpi_div);
    syslog(LOG_ERR, "DIAG brg_intr=0x%lx (bit0=underrun) fb=%p\n",
           (unsigned long)mipi_dsi_brg_ll_get_interrupt_status(g_dsi_hal.bridge),
           g_fbmem);
  }

  return OK;
}

/****************************************************************************
 * Name: up_fbinitialize
 ****************************************************************************/

int up_fbinitialize(int display)
{
  static bool s_fb_inited = false;

  /* up_fbinitialize() is called from multiple paths (board bring-up AND the
   * NX/graphics start-up).  Re-running the DSI bring-up resets the PHY and
   * kills the working display (picture fades to black).  Only ever run it
   * once.  Set the flag BEFORE calling so a re-entrant call is also blocked.
   */

  if (s_fb_inited)
    {
      return OK;
    }

  s_fb_inited = true;
  return esp32p4_lcd_initialize();
}

/****************************************************************************
 * Name: up_fbgetvplane
 ****************************************************************************/

struct fb_vtable_s *up_fbgetvplane(int display, int vplane)
{
  if (display != 0 || vplane != 0)
    {
      return NULL;
    }

  return &g_fb_vtable;
}

/****************************************************************************
 * Name: up_fbuninitialize
 ****************************************************************************/

void up_fbuninitialize(int display)
{
  if (g_fbmem != NULL)
    {
      kmm_free(g_fbmem);
      g_fbmem = NULL;
    }
}

#endif /* CONFIG_ESP32P4_BOARD_LCD */
