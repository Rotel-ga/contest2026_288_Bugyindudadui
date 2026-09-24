# Official Espressif LCD driver port for openvela

This directory contains the official DSI bus, DBI command transport, DPI
framebuffer driver, panel operations and EK79007 panel driver. The board
adapter is `../espressif/esp32p4_lcd.c`.

## Pinned upstream sources

- ESP-IDF: `d244a37c12c48d1b8a7a46d53ab93cf67c0856ca` (2026-06-29/30,
  IDF 6.1 development line, selected to match the project's July 2026 HAL).
  https://github.com/espressif/esp-idf/tree/d244a37c12c48d1b8a7a46d53ab93cf67c0856ca/components/esp_lcd
- EK79007: esp-iot-solution `e1a8f5c3e07d17218fbefeec996520536e2d282d`.
  https://github.com/espressif/esp-iot-solution/tree/e1a8f5c3e07d17218fbefeec996520536e2d282d/components/display/lcd/esp_lcd_ek79007
- Existing HAL remains pinned to `b90b1837cb5ad24747deb4c895246037cc206ce5`.

The files are compiled in place. `openvela.patch` contains **all differences**
from the original files listed in `upstream-sha256.json`. Reverse-applying
that patch to a temporary copy and checking the hashes verifies provenance.
`lcd_nuttx.h` and this README are port-owned files, not upstream files.
Original Espressif copyright and Apache-2.0 SPDX notices are retained.

## Deliberate port differences

1. Replace private FreeRTOS includes/delays with `lcd_nuttx.h` and NuttX waits.
   NuttX handles rescheduling on interrupt return.
2. Omit unrelated SPI/I2C/I80/PARL convenience includes from panel_io.h;
   the complete generic panel IO API and DBI implementation remain.
3. Bound PHY lock and lane-stop waits so a hardware failure returns an error.
   PHY programming, frequency calculations, clock lane policy, packet timing
   and panel command sequence otherwise remain upstream.
4. Explicitly align PSRAM framebuffers to cache lines and verify their memory
   range because this HAL's NuttX heap_caps shim does not enforce all IDF caps.
5. Enable the bridge reference clock explicitly for the pinned HAL.
6. Stop DMA rearming/interrupts and wait for channel disable before releasing
   framebuffer memory. A timeout retains resources rather than freeing memory
   still accessible to DMA.
7. Keep the official DMA2D hook implementation under `LCD_NUTTX_DMA2D` (0).
   Its async-color-convert dependency is not ported yet; enable/disable DMA2D
   API calls return ESP_ERR_NOT_SUPPORTED. CPU copy, no-copy cache writeback,
   draw hooks, YUV conversion and the DW-GDMA scanout remain official logic.
8. Replace CMake-generated EK79007 version macros with an identification log;
   the exact upstream revision is recorded above.

This is a complete **DSI/DBI/DPI/EK79007 display path**, not a port of every
esp_lcd backend or optional accelerator. The initial board adapter exposes one
RGB565 framebuffer; upstream multi-buffer APIs are retained but not exposed as
NuttX page flipping yet. Power-management/cache-disabled ISR modes are not
validated or enabled by the LCD configuration.

## Board adapter

- LDO channel 3, 2500 mV; backlight GPIO26; reset GPIO27.
- 1024x600 RGB565, 48 MHz pixel clock, two DSI lanes at 1000 Mbps.
- H: 10/120/120, V: 1/20/10 (sync/back/front porch).
- `up_fbinitialize` owns lifecycle; board bring-up calls `fb_register(0, 0)`.
- `CONFIG_FB_UPDATE` routes LVGL dirty rectangles through official
  `esp_lcd_panel_draw_bitmap` no-copy/cache-writeback logic.
- Default real framebuffer pattern: horizontal red/green/blue bands.
- Optional `CONFIG_ESP32P4_BOARD_LCD_COLORBAR`: official Host vertical bars;
  this bypasses framebuffer pixel output and cannot validate LVGL output.

## Build and validation

Normal build from workspace root:

```sh
PATH="$HOME/.local/bin:$PATH" ./build.sh vendor/openvela/boards/contest2026_288_board/configs/lcd -j8
```

The workspace build script automatically distcleans when defconfig changes,
which deletes the ignored HAL checkout. After configuring/cleaning, restore
HAL with `board/contest_board/tools/prepare_esp_hal.sh`, initialize its mbedTLS
submodule, and rerun the same build. Do not edit optional board settings without
accounting for this behavior.

Validation performed in this session: LCD configuration compiles and links;
ESP32-P4 image generated. Make integration tested; CMake source integration
updated but a separate CMake build was not run. No serial board was available,
so PLL lock, actual scanout, LVGL animation, cold boot and teardown still need
hardware validation. Build success does not prove display output.

Hardware acceptance order:

1. Boot and confirm /dev/fb0 plus horizontal RGB bands (real framebuffer).
2. Run `lvgldemo`, confirm changing UI, not merely the startup pattern.
3. Repeat cold boot and software reset; inspect underrun/error messages.
4. If real output fails, use Host pattern as a separate PHY/panel test.
5. Validate repeated initialization/error recovery and then page flipping.

The old handwritten implementation is preserved at local checkpoint `e55ea1d`
and branch `backup/esp32p4-handwritten-lcd`.
