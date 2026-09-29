> 2026-09-24：官方移植现位于 `feat/esp32p4-official-lcd`。
> 以下为手写驱动历史记录，不能作为新分支的当前状态。
> 当前来源、适配差异和验证范围见 [官方驱动移植说明](../../board/contest_board/chip/esp_lcd/README.md)。
> 手写基线已保存到 `backup/esp32p4-handwritten-lcd`（`e55ea1d`）。

# ESP32-P4X MIPI-DSI 显示驱动 — 进度与交接说明

> 最后更新：2026-09-24 01:05　分支：`feat/esp32p4-lcd-dsi`
> 目标板：ESP32-P4X-Function-EV-Board（rev v3.2）+ 7 寸 EK79007 1024×600 MIPI-DSI 屏
> 本文用于对话压缩后快速恢复上下文。

---

## 1. 总目标

在 openvela（NuttX 内核）上，从零适配 ESP32-P4 的 MIPI-DSI 显示驱动，注册 `/dev/fb0`，
最终跑 LVGL / 快应用上屏。openvela **只带底层 HAL**（`esp-hal-3rdparty`），**不带**
IDF 的 `esp_lcd` 上层驱动，所以这层必须自己写/移植（`chip/espressif/esp32p4_lcd.c`）。

五层路线：①显示驱动 ②图形栈 LVGL ③触摸 ④快应用引擎 ⑤跑通 rpk。
当前在 ①，代码完成、真机初始化通过，卡在"面板出图"最后一步。

---

## 2. 真机验证状态（ESP32-P4 rev v3.2，已烧录实测）

> ✅ **2026-09-24 01:39 屏幕点亮！** 手写 DSI bus 初始化**漏了 `mipi_dsi_ll_set_phy_pll_ref_clock_div(0, 1)`** → PHY PLL 虽锁定但锁在错误频率 → lane 比特率错 → DSI 链路无法同步 → 全黑。对照 IDF `esp_lcd_new_dsi_bus` 补上该调用 + 修正 escape时钟(18MHz,div≈7)/timeout时钟(10MHz,div≈13)/LP RX timeout=0x7FFF/clock lane 初始 AUTO/tx_eotp(true,false) 后，
> **host 内置彩条（COLORBAR=y，绕过 DMA）在真机 EK79007 上成功显示竖彩条**。物理层+DSI链路+面板初始化+host视频 全线打通。
>
> 曾一度误判为 DMA 问题（DMA 传输量=0），实为上游 DSI 链路未同步的下游连带现象。RGB565/16bpp 经官方 BSP 确认是 EK79007 默认、引脚(背光26/复位27)与 EK79007 DCS 序列也都经官方源码确认正确——均非问题。
>
> **下一步**：关掉 COLORBAR 走真实帧缓冲流，重测 DMA 是否把画面刷上屏，再绑 LVGL。


**已通（真机确认）**
- 板子可达：`esptool chip-id` → ESP32-P4 revision v3.2，USB-Serial/JTAG
- PSRAM：`free` 从 491KB → **34MB**（约 32MB PSRAM 进堆）✓
- DSI 驱动初始化全流程执行完成（串口打印 `LCD: DONE ok`）✓
- **D-PHY PLL 锁定**（`pll_locked=1`）✓
- **双 data lane 进入高速传输**（`lanes_stopped=0`）✓
- `/dev/fb0` 注册成功 ✓
- 系统稳定进 NSH ✓

**未通**
- EK79007 面板：**只有背光、无图像（黑屏）**

**寄存器诊断（关键）**
```
DIAG dma_amt0=0 amt1=0 (moving=0) fifo=0
DIAG lli_addr=0x4ff4a740 intr=0x10(=DST_TRANSCOMP, bit4)
DIAG pll_locked=1 lanes_stopped=0 lane_mbps=1000 dpi_div=5
DIAG brg_intr=0x0 (无 underrun)
```
结论：**物理层全通**（PLL/PHY/lane 都正常）。卡点在**数据通路**——
改成循环链表后 DMA 通道已装载 link-list（`lli_addr` 非0）、传输"完成"
（`DST_TRANSCOMP`），但 `trans_amount` 始终 0、`brg` 无 underrun ⇒
**DW-GDMA 没有把帧缓冲数据持续搬给 DSI bridge**，所以面板收不到像素 → 黑屏。

---

## 3. 真机抓到并修复的 Bug（都不报错、只能靠真机/诊断发现）

1. **rev v3.2 的 PHY PLL 参考时钟源**（导致启动直接卡死）
   - 现象：`esp32p4_lcd_clocks_enable` 处系统挂死（stage 日志停在 s1）。
   - 根因：给 `mipi_dsi_ll_set_phy_pllref_clock_source` 传了 `MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M`，
     该枚举只在 rev<300 有效；rev≥300 分支 switch 命中 `default: abort()`。
   - 修复：改用 `MIPI_DSI_PHY_PLLREF_CLK_SRC_XTAL`，`LCD_PHY_REF_CLK_HZ` 改 40000000。

2. **PSRAM 使能触发的两个 esp-hal 集成编译 bug**（早先，已进 compat patch）
   - `esp_allocateheap.c` 缺 `#include <nuttx/kmalloc.h>` → `kumm_addregion` 链接失败。
   - `dw_gdma.c`（IDF 原版）依赖 `freertos/FreeRTOS.h`，openvela 移植层不提供 →
     改用 `platform/os.h` + 3 个 port 宏映射（portMUX_TYPE→OS_SPINLOCK_TYPE 等）。
   - `os.c` 的 `nxtask_init` 旧签名 → 应用仓库自带 compat patch。
   - 以上均已写入 `patches/esp-hal-openvela-compat.patch`（`prepare_esp_hal.sh` 自动重放）。

3. **启动早期 fb_register 竞争**：无延时时 `esp32p4_lcd_initialize` 在 bringup 太早跑，
   `/dev/fb0` 注册失败；加启动延时后正常（说明 init 应延后或做健壮化，最终应改成
   注册在合适时机，而不是靠 delay）。

4. **bridge 参考时钟**：里程碑B重写时漏掉 `mipi_dsi_brg_ll_enable_ref_clock`，已补回。

---

## 4. 驱动初始化序列（已对齐 IDF esp_lcd，当前实现）

`chip/espressif/esp32p4_lcd.c :: esp32p4_lcd_initialize()`：
1. `esp_ldo_acquire_channel`(chan 3, 2500mV) 给 VDD_MIPI_DPHY 供电
2. `esp32p4_lcd_clocks_enable`：DSI bus clk / reset / phy cfg clk(PLL_F20M) /
   **pllref clk = XTAL** / dpi clk(PLL_F240M, div=5→48MHz)
3. `mipi_dsi_hal_init` + `configure_phy_pll`(40MHz ref, 1000Mbps) + 等 PLL 锁定
4. 完整 host/PHY bring-up（对齐 IDF `esp_lcd_new_dsi_bus`）：
   等 lanes stopped、命令模式、clock lane LP、`set_switch_time(50,104,46,128)`、
   rx_crc/rx_ecc/tx_eotp、timeout/escape clk div(=bitrate/8/10≈12)、
   `set_max_read_time(6000)`、`set_stop_wait_time(0x3f)`、
   命令 LP 速率模式(gen/dcs short wr/rd np=0/1/2 + long + mrps)、
   **cmd_ack=true / te_ack=false**（对齐 DBI IO）
5. `esp32p4_lcd_panel_reset`：背光 GPIO26 拉高 + **LCD_RST GPIO27** 复位(低10ms→高20ms)
6. `esp32p4_lcd_panel_init`：EK79007 DCS 序列（PAD_CONTROL 0xB2=0x10 2lane、
   0x80~0x86、sleep-out 0x11 延时120ms）
7. DPI host 配置：vcid、color_coding(RGB565)、timing_polarity、
   lp_h/v_timing、frame_ack、lp_command、burst、packet_pixel_num、trunks0、null0、h/v 时序
8. bridge：**enable_ref_clock**、num_pixel_bits、input_color_format(RGB565)、
   underrun_discard、flow_controller=DMA、burst_len256、empty_threshold(1024-256)、enable、update
9. 分配 PSRAM 帧缓冲（RGB565, 1.2MB）+ 填 8 条竖彩条测试图 + `esp_cache_msync` 回写
10. `esp32p4_lcd_dma_setup`：DW-GDMA 通道(src=MEM/dst=PERIPH_DSI, flow=SELF)+
    **循环链表(CIRCULAR, 1项, is_last=false)** 指向帧缓冲，dst=`MIPI_DSI_BRG_MEM_BASE(0x50105000)`
11. 使能：DMA enable_ctrl → video_mode(true) → clock_lane AUTO → bridge dpi_output(true)
12. `fb_register(0,0)` 出 `/dev/fb0`

EK79007 硬参数：1024×600 / DPI 48MHz / 2 lane 1000Mbps / hsync10 hbp120 hfp120 / vsync1 vbp20 vfp10 / 60Hz。
接线：PWM 背光=GPIO26，LCD_RST=GPIO27（P4X-Function-EV-Board + 屏适配板）。

---

## 5. 最后卡点 & 下一步（重点）

**卡点**：DW-GDMA 把 PSRAM 帧缓冲搬到 DSI bridge FIFO 这一步——通道已装载循环链表、
传输报"完成"(DST_TRANSCOMP)，但实际没有持续搬数据（`trans_amount` 恒 0，bridge 无 underrun）。
即 **DMA↔DSI bridge 的数据流没真正建立**。

**下一步排查方向**（按优先级）：
1. **DW-GDMA 描述符/传输是否真配对**：确认 `lli_config_transfer` 写入的 block size
   （`LCD_FB_SIZE*8/64=153600`）、src/dst 地址、传输宽度是否正确落到描述符；
   确认描述符内存 cache 一致性（虽在内部 RAM 0x4ff…）。
2. **DSI 外设握手**：`dst.role=ROLE_PERIPH_DSI` 的握手外设号是否正确、bridge 是否真的
   在向 DMA 发请求（读 bridge FIFO 状态 / DMA channel 更多状态位）。
3. **中断/连续刷新**：单次 SINGLY 时 `DST_TRANSCOMP` 只触发一次说明"传输完成中断"回调
   （`on_full_trans_done`）可能没被 openvela 的 `esp_intr_alloc` shim 接上；已改 CIRCULAR
   规避，但仍需确认 DMA 是否真在循环搬运。
4. **最稳妥**：完整移植 IDF `esp_lcd`（`esp_lcd_mipi_dsi_bus.c`/`esp_lcd_panel_dpi.c`/
   `esp_lcd_panel_io_dbi.c`/`esp_lcd_ek79007.c`）+ 依赖(`esp_clk_tree`/`esp_async_fbcpy`)，
   用 `platform/os.h` shim 适配（就像已成功适配的 `dw_gdma.c`）。树里目前无这些源与依赖。
5. **对照法**：在有 ESP-IDF 的机器上烧官方 EK79007 例程，确认屏/接线 OK 并拿到能亮的参照。

---

## 6. 改动文件清单（`feat/esp32p4-lcd-dsi`，**尚未提交**）

```
新增  board/contest_board/chip/espressif/esp32p4_lcd.c   DSI 驱动 + GDMA + framebuffer 层(含调试插桩)
新增  board/contest_board/configs/lcd/defconfig          独立 LCD 配置(PSRAM+FB+LVGL+DSI, COLORBAR 关)
改    board/contest_board/chip/espressif/Kconfig          ESP32P4_BOARD_LCD / COLORBAR / RST_GPIO(默认27)
改    board/contest_board/chip/espressif/Make.defs        条件编译 esp32p4_lcd.c + dw_gdma
改    board/contest_board/chip/espressif/CMakeLists.txt    同上
改    board/contest_board/chip/hal_esp32p4.mk             纳入 mipi_dsi_hal/periph + dw_gdma_hal/dw_gdma 源
改    board/contest_board/chip/espressif/esp_allocateheap.c  +kmalloc.h include(PSRAM 修复)
改    board/contest_board/src/esp32p4_bringup.c            fb_register(0,0)
改    board/contest_board/patches/esp-hal-openvela-compat.patch  os.c + dw_gdma.c 适配
```
注意：`demo/i2c/nsh/uart0` 四套已验收基线配置**未改动**。
esp-hal 树的改动（dw_gdma.c/os.c）已固化进 compat patch，不进本仓提交。

**当前含调试代码**（需清理或保留供续调）：
- `esp32p4_lcd_initialize` 开头 `up_mdelay(2500)`（等 USB 枚举，最终要去掉/改健壮）
- 各阶段 `syslog(LOG_ERR,"LCD: ...")` 日志
- 末尾 `DIAG ...` 寄存器诊断块
- 帧缓冲填的是**测试彩条**（非黑），用于点亮验证
- `configs/lcd/defconfig` 里 `# CONFIG_ESP32P4_BOARD_LCD_COLORBAR is not set`（真实流模式）

---

## 7. 构建 / 烧录 / 验证命令（本环境）

```bash
# 构建(不能 distclean，否则删 esp-hal 要重跑 prepare；切 sim 后需 make clean 清跨架构残留)
cd /home/mi/Developer/openvela
PATH="$HOME/.local/bin:$PATH" ./build.sh vendor/openvela/boards/contest2026_288_board/configs/lcd

# 串口需权限：用户不在 dialout 组，用 sg 切组(usermod -aG dialout mi 已做)
sg dialout -c "PATH=\"$HOME/.local/bin:$PATH\" esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 --after hard-reset write-flash 0x2000 nuttx/nuttx.bin"

# 读串口(USB-serial-jtag 复位后会重枚举，用重连循环读)：
sg dialout -c "python3 - <<'PY'
import serial,time,glob
end=time.time()+14; buf=b''
while time.time()<end:
    ps=sorted(glob.glob('/dev/ttyACM*'))
    if not ps: time.sleep(0.1); continue
    try: s=serial.Serial(ps[0],115200,timeout=0.2)
    except Exception: time.sleep(0.1); continue
    try:
        while time.time()<end:
            d=s.read(4096)
            if d: buf+=d
    except Exception: pass
    try: s.close()
    except Exception: pass
import sys; sys.stdout.buffer.write(buf[-1500:])
PY"
```

真机验证要点：`free`(看 34MB=PSRAM) / `ls /dev/fb0`(存在) / 串口看 `DIAG` 行判断 DMA 是否搬数据。

---

## 8. 一句话总结

**物理层已完全点通（PLL 锁定、lane HS、PSRAM、fb0、稳定启动），真机修了 3 个隐藏 bug；
唯一剩下的是"DW-GDMA 把帧缓冲持续搬给 DSI bridge"这条数据流没建立起来 → 面板黑屏。
下一步用完整移植 IDF esp_lcd/dw_gdma 或逻辑分析仪对照来攻这最后一环。**
