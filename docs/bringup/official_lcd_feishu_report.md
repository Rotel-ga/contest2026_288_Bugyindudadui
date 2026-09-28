# ESP32-P4X openvela 官方显示驱动移植全记录

## 0. 交给其他 AI：先看这里，编译 / 烧录 / 验证

**2026-09-28 当前交接入口：**以第 45 节为准。当前分支为 `merge/esp32p4-desktop-camera`，组合修复已提交为 `e465751` 并推送至 Rotel-ga 同名远程分支；第 0 节早期分支要求与第 42～44 节“未提交/未推送”描述属于当时状态。最新 USB 中断发送版已编译，真机速度和连续采集仍待验证。

本节命令针对当前机器，使用 **Bash** 执行。工作区是 `/home/mi/Developer/openvela`，比赛 Git 仓库是其下的 `contest2026_288_Bugyindudadui`，不要在工作区根目录执行该项目的 Git 操作。

**交接要求：**在 `feat/esp32p4-official-lcd` 上继续，驱动主体尽量保持官方一致；修改官方文件时同步维护 `chip/esp_lcd/openvela.patch` 和来源校验。保留用户未提交改动，不要执行 `git reset --hard` 或 `git clean` 清理整个项目。下面的命令是操作说明，不代表已经执行烧录或真机验收。

**2026-09-24 最新进度：**板子已到，实测 ESP32-P4 rev v3.2、16MB Flash。官方 LCD 中断适配修复后，真实 framebuffer 红绿蓝白切换、横向 RGB 三条和 LVGL Widgets 控件页面已由用户确认，照片已归档。5 分钟移动色块和计数刷新已通过用户视觉确认及串口完整运行验证（第 27 节）；GT911 芯片探测、原始按下/移动/抬起事件及两轴镜像后的跟手方向已通过本轮日志和用户确认（第 29～30 节）；标准 LVGL 控件点击、开关和滑条已由用户确认，输入后端及事件日志支持本轮验收（第 31 节）；桌面、锁屏与设置交互已获用户确认（第 33 节）；随后发现的无串口读取时主板开关重启黑屏，经 USB 发送有界等待修复后，用户确认连续多次开关均成功显示锁屏（第 34.2 节）；长期稳定性及详细测试次数未统计；USB 复位/重新枚举及 ROM 摘要告警仍待核查。内部 RV32 库和公开 ESP32-P4 Quick App 镜像也已完成编译，但快应用首页尚未真机显示成功；后续已进行上板启动诊断，见第 37 节。最新显示证据见第 25 节，实际测试命令见第 26 节。

**阅读顺序：**先看本文件第 37 节的最新目标、官方方案与交接，再看第 34.2 节的独立启动验收和最新镜像，再看第 33 节的桌面完成状态、第 32 节的实现与构建命令，再看第 31 节控件验收、第 29 节触摸命令、第 27 节显示验收；第 28～31 节中的后续计划属于阶段历史，当前进度以第 34.2 节为准；第 25～26 节保存真机显示证据及实际命令，第 22 节记录 Quick App 固件编译，第 24 节记录官方板卡资料。此前章节是阶段历史，不能用早期“没有板子”“尚未出图”等结论覆盖后续验收。第 0.1～0.6 节为早期 LCD 流程，启动自检是否执行以当前 `ESP32P4_BOARD_LCD_BOOT_TEST` 配置为准。

**2026-09-27 方案交接更新（优先阅读）：**用户最终要求同一份固件中点击桌面“跌倒监护”图标启动真实快应用，并明确要求依据官方公开代码。官方 `xmsdemo/launcher → Intent/startActivity → vappxms/QuickActivity` 路径已核实；推荐优先评估此路径，不以 P62 私有 Launcher 为前提。核心 QuickApp 仍是官方预编译发布，不能称全部核心开源。快应用已上板尝试但首页白屏，最后完整日志停在 Bundle 创建；尚未完成组合固件。完整调研证据、当前状态和后续计划直接见本文件第 37 节，无需另读交接文档。此前“两份固件分段演示”建议已被用户否定，只可作为开发诊断。

### 0.1 核对分支、工具和串口

```bash
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"

git -C contest2026_288_Bugyindudadui status --short
git -C contest2026_288_Bugyindudadui branch --show-current
git -C contest2026_288_Bugyindudadui log -2 --oneline
command -v esptool
python3 -c 'import serial; print(serial.__version__)'
python3 -m serial.tools.list_ports -v
```

预期分支：`feat/esp32p4-official-lcd`。文档基线提交是 `7968744`，后续可以有新提交，不要为匹配本文而回退。若当前分支不同，检查未提交改动后再切换：

```bash
git -C /home/mi/Developer/openvela/contest2026_288_Bugyindudadui switch feat/esp32p4-official-lcd
```

板子接 J20 USB Serial/JTAG，实际串口可能是 `/dev/ttyACM0` 或其他编号。根据设备枚举结果确认；多块设备同时连接时不要自动选择第一项。

### 0.2 常规增量编译：配置没变且 HAL 存在时

```bash
bash <<'BASH'
set -euo pipefail
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"

./build.sh vendor/openvela/boards/contest2026_288_board/configs/lcd -j8 \
  2>&1 | tee /tmp/openvela-official-lcd-build-latest.log

test -s nuttx/nuttx.bin
sha256sum nuttx/nuttx.bin
wc -c nuttx/nuttx.bin
BASH
```

`pipefail` 保证构建失败不会被 `tee` 的成功返回值掩盖。成功需要同时满足退出码 0、日志出现 `Generated: nuttx.bin`、镜像非空。失败时不要直接烧录目录里可能残留的旧镜像。

**重要：**`build.sh` 发现 defconfig 变化会自动 `distclean`，该项目会连同忽略的 HAL checkout 一起删除。切配置、修改 defconfig 或 HAL 已丢失时使用下一节；不要反复运行失败构建，也不要改成没有工具链环境的裸 `make`。

### 0.3 切换配置 / HAL 丢失后的有序构建

执行前先检查 HAL 是否有未固化到补丁的自定义修改。配置变化可能清理它；有新增修改时先保存为补丁或备份。以下顺序是 **配置 → 恢复固定 HAL → 恢复 mbedTLS → build.sh 构建**，需要网络。

```bash
bash <<'BASH'
set -euo pipefail
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"

# -e：仅在已有配置与目标 defconfig 不同时清理并重新配置。
bash nuttx/tools/configure.sh -e \
  /home/mi/Developer/openvela/vendor/openvela/boards/contest2026_288_board/configs/lcd

bash contest2026_288_Bugyindudadui/board/contest_board/tools/prepare_esp_hal.sh

git -C contest2026_288_Bugyindudadui/board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls

./build.sh vendor/openvela/boards/contest2026_288_board/configs/lcd -j8 \
  2>&1 | tee /tmp/openvela-official-lcd-build-latest.log

test -s nuttx/nuttx.bin
sha256sum nuttx/nuttx.bin
wc -c nuttx/nuttx.bin
BASH
```

`prepare_esp_hal.sh` 固定 HAL 为 `b90b1837cb5ad24747deb4c895246037cc206ce5` 并应用兼容补丁。若提示补丁与当前树不匹配，应检查修改来源，不要直接强制覆盖。

### 0.4 烧录前核对实际配置

```bash
cd /home/mi/Developer/openvela
rg -n 'CONFIG_(ESP32P4_BOARD_LCD|ESP32P4_BOARD_LCD_COLORBAR|FB_UPDATE|ESPRESSIF_SPIRAM|ESPRESSIF_SIMPLE_BOOT|MM_KERNEL_HEAP)(=| is not set)' nuttx/.config
```

本分支默认真实画面模式应满足：

```text
CONFIG_ESP32P4_BOARD_LCD=y
CONFIG_FB_UPDATE=y
CONFIG_ESPRESSIF_SPIRAM=y
CONFIG_ESPRESSIF_SIMPLE_BOOT=y
# CONFIG_ESP32P4_BOARD_LCD_COLORBAR is not set
# CONFIG_MM_KERNEL_HEAP is not set
```

若 `COLORBAR=y`，屏幕显示的是 Host 内置图案，不能用来验证 framebuffer 或 LVGL。烧录偏移 `0x2000` 对应本项目当前 Simple Boot 镜像；改了启动方式后必须重新核对。

### 0.5 识别芯片并烧录

先结束占用目标串口的终端，再执行。把 `LCD_PORT` 改成上面确认过的设备节点。

```bash
bash <<'BASH'
set -euo pipefail
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"
LCD_PORT=/dev/ttyACM0

test -c "$LCD_PORT"
test -s nuttx/nuttx.bin
esptool --chip esp32p4 --port "$LCD_PORT" chip-id
esptool --chip esp32p4 --port "$LCD_PORT" --baud 921600 \
  --after hard-reset write-flash 0x2000 nuttx/nuttx.bin \
  2>&1 | tee /tmp/openvela-official-lcd-flash-latest.log
BASH
```

历史识别结果为 ESP32-P4 revision v3.2；以本次实际输出为准。烧录成功要看到写入 `0x2000` 及 hash 验证成功，不能只看进度条。

若遇到串口 `Permission denied`，本机历史调试使用 `dialout` 组。只有账户已经具备该组授权时，可通过以下方式启动拥有相应组权限的 shell，然后在新 shell 中重跑命令：

```bash
sg dialout -c 'bash --noprofile --norc'
```

若组切换也失败，应处理设备权限；不要直接放宽为所有用户可读写。

### 0.6 连接控制台、检查屏幕并运行 LVGL

```bash
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"
python3 -m serial.tools.list_ports -v
# 复位后可能重新枚举，按实际节点修改。
python3 -m serial.tools.miniterm /dev/ttyACM0 115200 --raw
```

退出 miniterm 使用 `Ctrl+]`。这是终端命令，不是 NSH 命令。进入 NSH 后逐条输入：

```text
uname -a
free
ls /dev/fb0
lvgldemo
```

验收观察点：

- 串口应有 `LCD: official DSI/EK79007 initialization` 和 `LCD: 1024x600 RGB565 framebuffer=... ready`。如果出现初始化错误，先记录错误码，不要把 ready 缺失解释为已经成功。
- 启动画面预期为**横向红、绿、蓝三条**，来自真实 PSRAM framebuffer。
- 运行 `lvgldemo` 后画面应切换并持续更新；只有背光或静态启动图案不算 LVGL 验收通过。
- 若出现竖彩条，先核对是否误开 Host pattern，以及板上是否仍是旧镜像。
- 保存本次源码提交、配置、镜像摘要、烧录日志、启动日志及屏幕现象，再进行多轮冷启动和复位。

**交接时的真实进度：编译与镜像生成已通过，新分支真机验证尚未执行。** 历史镜像大小 599,596 字节只用于识别当时产物，不能作为以后每次构建必须相等的断言。

### 0.7 当前 Quick App 固件：无板阶段构建与定位

从公开 openvela 工作区执行：

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_quickapp.sh -j8
```

本轮归档固件位于：

```text
/home/mi/Developer/openvela/artifacts/esp32p4-quickapp/nuttx.bin
```

同目录保存 `nuttx.elf`、`config`、`System.map` 和 `SHA256SUMS`。可只读复核：

```bash
cd /home/mi/Developer/openvela/artifacts/esp32p4-quickapp
sha256sum -c SHA256SUMS
ls -lh nuttx.bin nuttx.elf config System.map
```

构建脚本更新 `nuttx/nuttx.bin`；`artifacts/esp32p4-quickapp/` 是本轮归档快照，不会随每次构建自动更新。切换配置会覆盖工作区默认构建产物，应通过配置及摘要区分 LCD 镜像与 Quick App 镜像。

Quick App 配置使用 `ESP32P4_QUICKAPP_PREBUILT=y`，通过二进制包提供运行时及 LVGL；不能用公开 `CONFIG_QUICKAPP` / `CONFIG_GRAPHICS_LVGL` 是否开启来判断这份镜像有无运行时。该配置没有编入 `lvgldemo`，独立显示验收仍使用 LCD 配置。

当前只完成编译、链接和镜像检查；资源、字体和服务启动尚未准备完毕，不能将“镜像生成成功”表述为“快应用已上屏”。

---

记录日期：2026-09-24（北京时间）  
项目：VelaP4X / contest2026_288_Bugyindudadui  
开发分支：`feat/esp32p4-official-lcd`  
代码记录截至：`7968744283d98a6106ee4fe7f2066c3df945853e`  
文档用途：个人飞书文档库中的分支开发记录、后续真机调试及交接依据。

> 当前结论：官方 DSI / DBI / DPI / EK79007 显示链路已经接入 openvela，并完成 Make 编译、链接和镜像生成。当前没有完成该分支的烧录、真机出图、LVGL 动画、冷启动及退出恢复验证。历史手写驱动的彩条成功记录不能作为本分支的硬件验收结果。

## 1. 目标与最终原则

目标是在 ESP32-P4X-Function-EV-Board 上适配 EK79007 1024×600 MIPI-DSI 屏，通过 NuttX framebuffer 提供 `/dev/fb0`，继续承接 LVGL 和后续快应用显示。

本分支的明确要求是：**尽量与官方驱动一致，然后适配 openvela。**

具体执行方式：

- 保留官方 DSI 总线、DBI 命令传输、DPI 刷新和 EK79007 面板逻辑。
- 板级引脚、供电、分辨率和 NuttX framebuffer 接口放在独立封装中。
- 对官方文件的必要改动集中记录在 `openvela.patch`。
- 固定官方源码提交，保留版权和 Apache-2.0 SPDX 信息。
- 区分编译验证、代码来源验证和硬件运行验证。
- 当前移植覆盖本板需要的完整显示链路，不代表整套 esp_lcd 的全部总线后端和可选加速器都已适配。

## 2. 开始时的项目状态

原工作分支为 `feat/esp32p4-lcd-dsi`，工作区存在尚未提交的手写显示驱动、LCD 配置、HAL 兼容补丁以及进度说明。

历史进度记录描述：芯片为 ESP32-P4 rev v3.2，PSRAM 已进入堆，曾在补上 PHY 参考时钟分频配置后显示 DSI Host 内置彩条。该现象属于旧实现的历史记录，本次未重新实测。

初始审查发现：

1. 实际 `nuttx/.config` 开启了 `CONFIG_ESP32P4_BOARD_LCD_COLORBAR=y`，而 defconfig 没有显式写出该选项。旧 Kconfig 的默认值为 y，因此不能仅凭 defconfig 判断实际模式。
2. 旧实现混用了单节点循环链表和整帧结束重新启动 DMA 的策略。
3. 旧实现只在初始填图后回写缓存，没有通过 framebuffer `updatearea` 承接 LVGL 后续更新。
4. 初始化成功标志在硬件初始化完成前设置，失败后可能被后续调用误判为成功。
5. 旧释放路径没有先确认 DMA 停止就释放 framebuffer。
6. 历史记录中既有“彩条已显示”的更新，也有较早的“唯一剩余问题就是 DMA”的结论，需要明确时间和证据边界。

诊断判据也进行了澄清：DMA 的 `DST_TRANSCOMP` 与整帧回调使用的 `DMA_TFR_DONE` 不是同一事件；块完成传输量不适合直接当作累计活动计数器；未使能对应中断时，bridge 的屏蔽后状态为零不能单独证明没有 underrun。

用户最初提供的飞书进展链接访问返回 404，因此技术判断依据本地代码及本地记录，未声称读取到飞书正文。

## 3. 分支与提交保存

| 时间（北京时间） | 动作 | 结果 |
| --- | --- | --- |
| 2026-09-24 13:56 | 从原工作状态创建移植分支 | `feat/esp32p4-official-lcd` |
| 2026-09-24 13:56:53 | 保存此前未提交的手写驱动进展 | `e55ea1d8de02ca643cd9d1436d25875176593ba2` |
| 同阶段 | 建立手写基线备份分支 | `backup/esp32p4-handwritten-lcd` 指向 `e55ea1d` |
| 2026-09-24 14:12:45 | 保存可编译的官方移植版本 | `7968744283d98a6106ee4fe7f2066c3df945853e` |

提交说明：

```text
e55ea1d checkpoint: preserve experimental ESP32-P4 DSI bring-up
7968744 feat: port official Espressif DSI and EK79007 drivers to openvela
```

这两个是本地提交。本次没有执行远程推送、PR 创建或合并。原先手写进展的可恢复基线应使用备份分支或 `e55ea1d`，不能假定它已经提交在原分支上。

## 4. 官方源码版本选择过程

### 4.1 最初尝试

最初对照 IDF v5.5，随后尝试以 v5.5.2 的显示驱动实施移植。v5.5.2 对应提交为：

```text
30aaf64524299d3bde422ca9a2848090d1bc5d0f
```

编译显示其旧版像素类型、颜色转换接口和本地 HAL 不匹配。本地 HAL 的版本头标识为 IDF 6.1，固定提交时间位于 2026 年 7 月。

过程中曾为旧版接口做局部兼容并暂时裁剪可选功能。用户明确强调“尽量与官方一致”后，重新选择基础版本，撤掉旧版 API 兼容方案，恢复官方新版颜色转换和绘制钩子逻辑。

### 4.2 最终选用

| 组件 | 固定来源 | 选择依据 |
| --- | --- | --- |
| 官方 esp_lcd | ESP-IDF `d244a37c12c48d1b8a7a46d53ab93cf67c0856ca` | 与本地 HAL 同期的 IDF 6.1 开发线，接口更接近 |
| EK79007 | esp-iot-solution `e1a8f5c3e07d17218fbefeec996520536e2d282d` | 乐鑫官方面板组件，固定完整提交 |
| 底层 HAL | esp-hal-3rdparty `b90b1837cb5ad24747deb4c895246037cc206ce5` | 继续使用项目原有固定版本 |
| mbedTLS 子模块 | `582ff482038db6e4010dbf6f943d97b05ad06ea5` | HAL 固定子模块版本 |

同期版本不是“已证明完全兼容”的结论，最终仍需真机验证。没有为追逐最新版本而升级整个 HAL。

官方来源：

- [ESP-IDF esp_lcd 固定源码](https://github.com/espressif/esp-idf/tree/d244a37c12c48d1b8a7a46d53ab93cf67c0856ca/components/esp_lcd)
- [EK79007 固定源码](https://github.com/espressif/esp-iot-solution/tree/e1a8f5c3e07d17218fbefeec996520536e2d282d/components/display/lcd/esp_lcd_ek79007)
- [esp-hal-3rdparty 固定源码](https://github.com/espressif/esp-hal-3rdparty/tree/b90b1837cb5ad24747deb4c895246037cc206ce5)

## 5. 最终结构与数据通路

```text
LVGL / openvela 图形应用
            ↓
NuttX framebuffer：/dev/fb0、FBIO_UPDATE
            ↓
esp32p4_lcd.c：板级参数、生命周期、错误码转换
            ↓
官方 EK79007 + DBI 命令 + DPI 刷新 + DSI 总线驱动
            ↓
现有 esp-hal / NuttX 平台适配
            ↓
PSRAM → DW-GDMA → DSI Bridge → DSI Host → D-PHY → 面板
```

内置图案是另一条测试路径：

```text
DSI Host 内置图案发生器 → D-PHY → 面板
```

后者绕过真实 framebuffer 像素输入，不能用来证明 LVGL 已经上屏。

## 6. 官方驱动与适配文件

所有以下路径相对于比赛项目仓库根目录。

| 路径 | 内容 |
| --- | --- |
| `board/contest_board/chip/esp_lcd/esp_lcd_mipi_dsi_bus.c` | 官方总线、PHY、时钟和 Host 初始化 |
| `board/contest_board/chip/esp_lcd/esp_lcd_panel_io_dbi.c` | 官方 DCS 命令读写 |
| `board/contest_board/chip/esp_lcd/esp_lcd_panel_dpi.c` | 官方帧缓冲、DMA 连续刷新、绘制和事件接口 |
| `board/contest_board/chip/esp_lcd/esp_lcd_ek79007.c` | 官方面板初始化、复位、镜像等逻辑 |
| `board/contest_board/chip/esp_lcd/esp_lcd_panel_ops.c` | 官方通用面板 API |
| `board/contest_board/chip/esp_lcd/esp_lcd_panel_io.c` | 官方通用面板 IO API |
| `board/contest_board/chip/esp_lcd/*.h` | 对应公共与私有接口；部分其他面板头文件因官方聚合头依赖保留，不表示已移植那些面板 |
| `board/contest_board/chip/esp_lcd/lcd_nuttx.h` | 本地 OS 兼容定义 |
| `board/contest_board/chip/esp_lcd/openvela.patch` | 官方文件的全部本地差异 |
| `board/contest_board/chip/esp_lcd/upstream-sha256.json` | 19 个原始官方文件的摘要 |
| `board/contest_board/chip/esp_lcd/README.md` | 来源、差异、范围及验证说明 |
| `board/contest_board/chip/espressif/esp32p4_lcd.c` | openvela framebuffer 与板级封装 |

其他变更：

- `chip/espressif/Kconfig`：选择 `FB_UPDATE`；内置彩条默认改为 n。
- `chip/hal_esp32p4.mk`：接入官方源码、头文件目录和颜色格式 HAL。
- `chip/hal_esp32p4.cmake`：补充对应源码及 MIPI / DW-GDMA HAL；尚未单独执行 CMake 构建。
- `docs/bringup/lcd_dsi_progress.md`：保留旧记录，加上历史状态提示和新文档入口。
- `docs/bringup/official_lcd_build.txt`：保存构建和镜像摘要。

检查点提交还保存了原有 `configs/lcd/defconfig`、board bring-up 的 `fb_register(0, 0)`、Make/CMake 对适配入口的编译开关、PSRAM 相关 include 修复以及 HAL 兼容补丁。

## 7. 对官方文件的必要差异

| 差异 | 原因和边界 |
| --- | --- |
| FreeRTOS 私有 include / delay 替换 | 使用 NuttX 等待；本地 delay 宏只服务本目录使用的简单延时，不是通用 FreeRTOS 兼容层 |
| 中断退出让出处理 | 沿用 NuttX 中断返回时调度语义 |
| 删除 panel_io.h 中无关总线便利 include | 避免仅使用 DBI 却引入 SPI/I2C/I80/PARL 类型依赖，保留通用 IO API |
| PHY 等待有界化 | PLL 最多尝试 500 次、lane stop 最多 100 次，每次约延时 1 ms；调度会影响实际经过时间 |
| 显式帧缓冲对齐 | 按不小于 64 字节的缓存行对齐分配，检查分配结果位于外部 RAM；不能假设当前 heap_caps 实现保证全部 IDF capability 语义 |
| bridge 参考时钟显式开启 | 适配固定 HAL 所需 |
| 删除前停止 DMA | 屏蔽相关事件、清除重启回调、停输出和 DMA，并轮询通道停机；超时保留资源，避免 DMA 访问已释放内存 |
| DMA2D 可选路径隔离 | 保留官方实现于 `LCD_NUTTX_DMA2D` 条件编译块，当前为 0；尚未接入 async-color-convert 依赖，启用/停用 API 明确返回不支持 |
| 面板版本日志调整 | 以固定来源记录替代 IDF CMake 生成的组件版本宏 |

官方的 DSI 初始化主体、面板命令、单节点 SINGLY 链表及整帧回调重启、CPU 绘制、framebuffer 原地更新、YUV 转换 API 和绘制钩子仍保留。保留代码或接口不代表其所有配置已通过运行验证。

## 8. 板级配置

| 参数 | 当前值 |
| --- | --- |
| 板卡 | ESP32-P4X-Function-EV-Board；项目 README 标记板卡 V1.6 |
| 芯片 | 历史实测 ESP32-P4 rev v3.2；不要与 PCB 版本混淆 |
| 面板 | EK79007，1024×600 |
| 输入 / 输出颜色 | RGB565 / RGB565 |
| 帧缓冲 | 1 个，1,228,800 字节；stride 2048 字节 |
| DSI data lane | 2 |
| lane bit rate | 1000 Mbps |
| PHY 参考源 | XTAL |
| DPI 源 / 像素时钟 | PLL_F240M / 48 MHz |
| 水平 sync / back / front porch | 10 / 120 / 120 |
| 垂直 sync / back / front porch | 1 / 20 / 10 |
| 理论扫描频率 | 48,000,000 ÷ 1274 ÷ 631，约 59.71 Hz；不是实测值 |
| PHY 供电 | LDO 通道 3，2500 mV |
| 背光 | GPIO26，初始化成功后拉高 |
| 复位 | 默认 GPIO27，可由配置调整 |

## 9. framebuffer 初始化、更新和释放

初始化流程：

1. 检查 display 编号，通过 NuttX mutex 串行化生命周期操作。
2. 已成功初始化时直接返回；若存在上次遗留资源，先尝试清理。
3. 关闭背光，申请 PHY 供电。
4. 创建官方 DSI bus 和 DBI IO。
5. 创建 EK79007 面板，其内部创建 DPI 面板及 DMA / framebuffer 资源。
6. 调用官方复位接口，取得官方分配的 framebuffer。
7. 填入横向红、绿、蓝三条图案，调用官方 draw_bitmap 回写缓存。
8. 调用官方 panel_init，发送面板命令并启动扫描链路。
9. 若启用内置彩条选项，再调用官方 set_pattern 切换测试图案。
10. 仅在成功后设置 ready、打开背光，供 framebuffer 上层注册使用。

更新流程：LVGL NuttX 后端通过 `FBIO_UPDATE` 提交区域，适配层检查边界并将 framebuffer 内部指针传给官方 `esp_lcd_panel_draw_bitmap()`，进入原地更新和扫描行缓存回写路径。

释放流程：先取消 ready 和背光，再删除面板；DPI 删除路径阻止 DMA 回调重新启动并确认通道停止，之后才释放帧缓冲。正常情况下再释放 DBI、DSI bus 和 LDO。停止超时会保留面板资源并返回错误。以上失败及释放分支尚未真机注入故障验证。

## 10. 构建过程及遇到的问题

| 问题 | 处理 |
| --- | --- |
| 旧 IDF 像素格式及颜色 API 与 HAL 不匹配 | 最终改用同期 IDF 6.1 源码，撤掉旧版接口兼容方案 |
| build.sh 配置变化触发自动 distclean | HAL checkout 被清理，按固定版本恢复，并重放原兼容补丁 |
| 沙箱无法解析 GitHub | 请求网络执行权限后恢复 HAL / mbedTLS |
| 直接 make 缺少编译器环境 | 使用项目 build.sh 设置工具链环境；未改变项目工具链方案 |
| 临界区接口链接失败 | 补齐 NuttX spinlock 头文件 |
| `__containerof` 重复定义警告 | 本地兼容定义增加条件保护 |
| 最新官方 master 带来更新的异步拷贝依赖 | 比较后选择与 HAL 同期提交，减少额外依赖和 API 偏差 |
| 可选 DMA2D 尚未适配 | 保留源码，用本地编译开关隔离，避免静默假成功 |

初期编译出现过 HAL 头文件 `nxsched_usleep` 的既有警告。最终增量构建没有报告编译警告或错误，这不应扩大为“全仓所有配置均无警告”。

补丁文件中的空白上下文行在 Git diff whitespace 检查时会显示提示；这些是统一补丁的上下文标记，来源校验仍成功。没有把全分支 whitespace 检查记录成无条件通过。

## 11. 构建命令与恢复 HAL

工作区根目录：`/home/mi/Developer/openvela`。

通常构建：

```sh
PATH="$HOME/.local/bin:$PATH" ./build.sh vendor/openvela/boards/contest2026_288_board/configs/lcd -j8
```

当配置切换清理了 HAL 后，从比赛仓库根目录恢复：

```sh
bash board/contest_board/tools/prepare_esp_hal.sh
git -C board/contest_board/chip/esp-hal-3rdparty submodule update --init components/mbedtls/mbedtls
```

随后返回工作区根目录，使用同一份已生成配置再次运行构建命令。

最终实际配置确认：

```text
CONFIG_ESP32P4_BOARD_LCD=y
CONFIG_FB_UPDATE=y
CONFIG_ESPRESSIF_SPIRAM=y
# CONFIG_ESP32P4_BOARD_LCD_COLORBAR is not set
# CONFIG_MM_KERNEL_HEAP is not set
```

部分设置由 Kconfig 自动选择或默认值决定，savedefconfig 未必逐项保留。验证以生成的 `.config` 为准。当前单堆/flat 配置下完成构建，不代表独立 kernel heap 配置已支持。

## 12. 验证结果与证据

| 验证项 | 结果 |
| --- | --- |
| Make LCD 配置编译与链接 | PASS，退出码 0 |
| ESP32-P4 镜像生成 | PASS，`Generated: nuttx.bin` |
| 真实 framebuffer 默认配置 | 已确认，内置彩条关闭 |
| FB_UPDATE / PSRAM 配置 | 已确认 |
| 官方文件来源一致性 | PASS，19 个文件反向应用补丁后摘要一致 |
| 提交后再次校验来源 | PASS |
| CMake 独立构建 | 未执行 |
| 新分支烧录 | 未执行，当时无 `/dev/ttyACM0` |
| 新分支 PLL、出图、DMA 持续刷新 | 未验证 |
| LVGL 动画 / 防撕裂 | 未验证 |
| 冷启动、复位、退出和失败恢复 | 未验证 |
| 旧 nsh / uart0 / i2c / demo 基线回归 | 本次未重跑 |

构建产物记录：

```text
文件：nuttx/nuttx.bin
大小：599596 bytes
SHA-256：a2e82e74943a0a188d869a1c282d061dc75544ac310f1c93c2e8ffd67d85233b
```

该摘要对应本次记录的构建产物。后续构建包含版本/时间等信息，摘要可能变化。

持久证据：`docs/bringup/official_lcd_build.txt`。本次完整构建日志位于 `/tmp/openvela-official-lcd-build.log`，该临时路径可能被覆盖或清理，不作为长期唯一证据。

来源校验方法：复制 `upstream-sha256.json` 中列出的文件到临时目录，反向应用 `openvela.patch`，对恢复后的文件逐一计算 SHA-256，与 JSON 对照。验证副本即可，不应在编译目录直接反向应用补丁。

## 13. Quick App / `.rpk` 运行链路核查（2026-09-24 更新）

此前文档把“快应用引擎和 `.rpk` 安装链路是否存在”列为未知项。重新检查整个 openvela 工程后，结论已经明确：**工程包含 Quick App 运行时接口及预编译库、RPK 安装/卸载链路、Package Manager、Activity/Window/XMS 相关服务和 Launcher 示例；当前 ESP32-P4 LCD 配置尚未启用这些组件。本轮进一步检查发现，本地已有核心库均为其他架构，尚缺匹配 ESP32-P4 的 RV32 核心库或可编译源码，详见第 13.5 节。**

### 13.1 工程中已经存在的组件

| 组件 | 位置 | 已确认的能力 |
| --- | --- | --- |
| Quick App 容器 | `frameworks/runtimes/quickapp` | QuickJS + LVGL + Yoga + libuv；RPK 加载运行、路由、生命周期、GUI 组件、Feature、Inspector |
| 独立运行入口 | `frameworks/runtimes/quickapp/shell/vapp` | `vapp`，直接运行 RPK，适合先做单应用验证 |
| XMS 运行入口 | `frameworks/runtimes/quickapp/shell/xms` | `vappxms`，通过 `QuickActivity` / `QuickApplication` 接入系统服务 |
| Quick App API | `frameworks/runtimes/quickapp/include/quickapp.h` | 创建、启动、退出、路由、Manifest 查询、RPK 安装和卸载 API |
| 包管理服务 | `frameworks/runtimes/services/pm` | `pm install`、`pm list`、`pm uninstall`；包信息查询、安装任务和卸载任务 |
| Activity / Window 服务 | `frameworks/runtimes/services/am`、`frameworks/runtimes/services/wm` | 应用活动管理、窗口管理、输入分发和 LVGL driver proxy |
| 系统服务 | `frameworks/runtimes/services/system_server` | 为 `vappxms`、Package Manager、Activity Manager、Window Manager 提供系统服务承载 |
| Launcher 示例 | `frameworks/runtimes/services/xmsdemo/launcher` | XMS demo Launcher，manifest 中声明 `action.system.HOME` |
| 快应用示例 | `packages/apps`、`packages/fe/examples` | Vela Quick App 工程、Manifest、UX/JS/CSS 和 packager 依赖 |
| 本项目示例映射 | `contest2026_288_Bugyindudadui/quickapp/hello_quickapp` | 映射至 `packages/apps/contest2026_288_hello_quickapp` 的初始模板 |

### 13.2 `.rpk` 安装的实际流程

工程中 `frameworks/runtimes/services/pm/src/PackageInstaller.cpp` 会识别 `.rpk` 后缀，交给异步安装任务。流程是：

```text
pm install <file.rpk>
       ↓
PackageManager / PackageInstaller
       ↓
InstallTask
       ↓
posix_spawn pmsInstaller
       ↓
app_verify_init()
       ↓
app_verify_unzip()
       ↓
移动到 /data/app/<package-name>
       ↓
PackageParser 读取 manifest.json
       ↓
packages.list 保存安装信息
```

`InstallMain.cpp` 默认使用 `/data/data/tmp/<rpk-name>` 作为临时解包目录、`/data/app/<package-name>` 作为安装目录、`/data/data/<package-name>` 作为应用数据目录。`PackageParser::parseQuickAppManifest()` 将 Quick App 默认入口设置为 `appType=QUICKAPP`、`execfile=vappxms`、`entry=QuickActivity`。安装、解包、Manifest 解析和包信息管理代码已经存在；还需要验证 `app_verify_*`、文件系统和预编译库在 ESP32-P4 配置下可用。

### 13.3 Quick App Kconfig 依赖

`frameworks/runtimes/quickapp/Kconfig` 中，启用 `QUICKAPP` 至少需要：

```text
LIBUV
PROTOBUF_C
INTERPRETERS_QUICKJS 或 INTERPRETERS_WAMR
GRAPHICS_LVGL
UIKIT
LIBUV_EXTENSION
UIKIT_FONT_MANAGER
LIB_YOGA
LV_USE_LIBPNG
LV_USE_NUTTX_LIBUV
UTILS_CURL
LIB_PNG
FEATURE_FRAMEWORK
LIBASH
```

本项目当前 LCD 配置已经启用 LVGL 和 framebuffer，但当前 `.config` 中确认未启用 `INTERPRETERS_QUICKJS`、`QUICKAPP`、`QUICKAPP_VAPP`、`QUICKAPP_VAPP_XMS`、`SYSTEM_PACKAGE_SERVICE`、`SYSTEM_SERVER`、`SYSTEM_ACTIVITY_SERVICE` 和 `SYSTEM_WINDOW_SERVICE`。`QUICKAPP_VAPP` 适合先做独立 RPK 验证；`QUICKAPP_VAPP_XMS` 还依赖 `SYSTEM_SERVER`；Package Manager 还依赖 `ANDROID_BINDER`、`SYSTEM_SERVER` 和 `LIB_RAPIDJSON`。

补充：`QUICKAPP_VAPP` 和 `QUICKAPP_VAPP_XMS` 都依赖 `LIB_FREETYPE`。当前 LCD `.config` 还未开启 `HAVE_CXX`，需结合目标库的 ABI 配齐 C++ 运行库。上述 `depends on` 不会因为手写 `CONFIG_QUICKAPP=y` 就自动全部满足，应核对最终生成配置和链接结果。Quick App Kconfig 默认 JS heap 为 4 MiB、应用线程栈为 128 KiB；这些只是默认配置，不是已测总内存需求，还要计入 framebuffer、字体、图片、其他线程及文件系统占用。

### 13.4 面向 ESP32-P4 的两阶段接入方案

**阶段 A：独立 `vapp` 最小闭环**

```text
MIPI-DSI → /dev/fb0 → LVGL
                    ↓
          QUICKAPP + QuickJS
                    ↓
       vapp hap://app/<package-name>
                    ↓
          Quick App 页面上屏
```

先取得匹配目标架构及依赖版本的库或核心源码，再在独立配置中启用 `INTERPRETERS_QUICKJS`、`QUICKAPP`、`QUICKAPP_VAPP` 及其依赖，不引入完整 XMS。首次上屏优先部署已解包的最小应用和字体，通过 `vapp hap://app/<package-name>` 验证 QuickJS、LVGL、Yoga、文件系统和 PSRAM。

这个 URI 启动方式来自工作区 `docs/zh-cn/contest_2026/quickapp/quickapp_manual.md`；公开 shell 入口的用法也是 `<package-uri>`。它是下一步验证方案，不代表 ESP32-P4 已经支持运行。直接传入 `.rpk` 路径的行为应以取得的目标库为准，首次页面上屏后再分别验证 RPK 加载、验签解包与 PM 安装，避免把所有环节同时作为首次启动条件。

**阶段 B：完整桌面和已安装应用**

```text
system_server
 ├── PackageManager（安装、卸载、扫描 /data/app）
 ├── ActivityManager
 └── WindowManager
          ↓
       Launcher
          ↓
   点击已安装应用图标
          ↓
       vappxms
          ↓
      Quick App 页面
```

阶段 B 需要阶段 A 通过后，再启用 `SYSTEM_SERVER`、`SYSTEM_PACKAGE_SERVICE`、`SYSTEM_ACTIVITY_SERVICE`、`SYSTEM_WINDOW_SERVICE`、`ANDROID_BINDER`、`QUICKAPP_VAPP_XMS` 及 Launcher demo。

### 13.5 公开工作区状态复核（内部源码补充前）

“没有快应用引擎和 RPK 链路”的判断已经撤销。当前更准确的状态是：

| 能力 | 工程源码/库 | ESP32-P4 LCD 配置 | 目标板验证 |
| --- | ---: | ---: | ---: |
| QuickJS | 有 | 未启用 | 未验证 |
| Quick App runtime | 有接口及其他架构预编译库，尚缺 RV32 核心库或源码 | 未启用 | 未验证 |
| `vapp` / `vappxms` | 有 shell 源码及预编译接入声明，目标入口及链接待接入 | 未启用 | 未验证 |
| RPK 解包/安装 | 有，`app_verify_init/unzip` | 未启用 | 未验证 |
| Package Manager | 有 | 未启用 | 未验证 |
| Activity / Window / System Server | 有 | 未启用 | 未验证 |
| Launcher demo | 有 | 未启用 | 未验证 |
| MIPI-DSI framebuffer | 已接入 | 已配置 | 新分支未验证 |
| LVGL framebuffer 后端 | 有 | 已配置 | 新分支未验证 |

本轮对工作区找到的四套 `libquickapp.a` 使用 `LC_ALL=C readelf -h` 检查实际归档成员，结果如下；架构判断不是根据目录名推测。

| 核心库目录（相对 openvela 工作区） | ELF 架构 |
| --- | --- |
| `vendor/openvela/boards/vela/libs/ap_cmake` | ELF64 / AArch64 |
| `vendor/openvela/boards/vela/libs/armv7a_cmake` | ELF32 / ARM |
| `vendor/openvela/boards/vela/libs/vela_cmake` | ELF64 / x86-64 |
| `vendor/sifli/boards/sf32lb52/libs/nsh_cmake` | ELF32 / ARM |

当前板级配置为 RV32；上述库不能直接用于该目标。本地 `frameworks/runtimes/quickapp/src/` 不存在，也未找到 RISC-V 版核心库。这说明**当前工作区缺少目标核心实现**，不代表上游或其他渠道一定没有可用版本。应先向维护方或比赛支持渠道确认匹配当前 RV32 ISA/ABI 的库包或可编译源码，以及配套 LVGL、QuickJS、C++ 运行库版本。

构建还存在独立接入工作：`frameworks/runtimes/quickapp/Makefile` 在 `QUICKAPP_VAPP=y` 时声明 `PROGNAME += vapp`，但该分支没有加入入口源文件，且注释明确依赖预编译版本；CMake 则通过 `nuttx_add_prebuilt()` 声明核心库及应用库。现有其他板卡在板级脚本中接入预编译库。拿到目标库后需为 ESP32-P4 明确库路径、链接和入口注册，不能把菜单选项开启当作接入成功。

除了 `quickapp` 核心，还要核对 `gui_wrapper`、`quickappfeatures`、`quickapp_inspector` 及所需 `vapp` 入口实现的目标版本和依赖。其余待验证项包括内存与 C++ ABI、文件系统和字体、`app_verify_*`、触摸输入及 Launcher 的 1024×600 布局。

### 13.6 给后续 AI 的任务顺序

**更新：**用户已提供内部源码，来源核实已完成，以下第 2 步的“确认来源”由第 18 节替代。当前应按第 18 节开展依赖与版本适配，不再等待现成 RV32 库。

1. 优先按第 15 节完成官方 LCD 的真机出图、LVGL 动态更新及启动可靠性验收，保存可恢复基线。
2. 可与 LCD 验收同步确认 RV32 Quick App 核心库或源码来源、配套版本及 ABI。这个依赖未解决前，不把完整目标运行时编译作为已可执行的下一步。
3. 取得目标实现后，从 LCD defconfig 派生独立配置，补齐 Make/CMake 的库链接和入口注册，逐步开启 C++、QuickJS、Quick App 阶段 A 依赖及 `QUICKAPP_VAPP`。
4. 建立应用资源、字体和可写数据目录，明确文件系统及部署方式；检查 PSRAM 剩余量和运行期内存需求。
5. 部署已解包的最小应用，用 `vapp hap://app/<package-name>` 验证页面真实输出和持续更新，再验证直接 RPK 加载及验签解包。
6. 接入并验证触摸坐标、方向和事件，再进行需要点击操作的场景验收。
7. 阶段 A 通过后开启 `SYSTEM_SERVER`、PM/AM/WM、Binder 和 `QUICKAPP_VAPP_XMS`，验证安装、查询和卸载。
8. 启用 Launcher，完成点击启动、返回桌面、卸载和重启后恢复。

如果暂时没有匹配 RV32 的核心实现，可以继续 LCD、触摸及板级验收，并在已有匹配库的模拟器上开发快应用页面；模拟器通过不能替代 ESP32-P4 运行验证。

**不要把 `apps/interpreters/quickjs` 的通用 QuickJS 命令行解释器误认为完整 Quick App runtime。** 前者只是通用 JS 引擎/解释器；后者是 `frameworks/runtimes/quickapp`，依赖 LVGL、Yoga、Feature、libuv 和预编译 Quick App 核心库。

### 13.7 当前缺少的文件系统与资源条件

当前 LCD `.config` 未开启 `FS_TMPFS`、`FS_ROMFS`、`FS_LITTLEFS`、`FS_FAT`，也未开启网络。板级 `esp32p4_bringup.c` 有 `/proc` 挂载和受配置控制的临时文件系统挂载，但尚未建立 Quick App 所需的应用、字体和数据存储方案，不能直接照搬模拟器的 adb 部署步骤。

首次验证至少要明确：

- 应用和字体存放位置，以及运行时访问路径；本地指南示例使用 `/data/app/<package-name>` 和 `/data/font`。
- `/data` 等可写目录如何提供，资源通过固件打包还是实际可用的文件传输通道进入板子。
- 可以先采用固件内置只读资源与 RAM 可写数据目录，具体挂载和资源布局需实施验证；RAM 文件系统占用必须计入内存预算。
- 需要断电保留应用安装和用户数据时，再接持久文件系统，并验证重启恢复。

这是阶段 A 的前置工作，不应等运行 `vapp` 后才排查缺文件、缺字体或目录不可写。

## 14. 未完成事项与当前限制

- 首要未完成项是真机验证，当前不能宣布屏幕驱动已经稳定可用。
- 已找到 Quick App 内部核心源码，可以自行交叉编译；尚无本项目 RV32 编译及运行结果。目标依赖、跨版本接口、构建接入和文件系统/字体部署仍待完成，见第 18 节。
- DMA2D 异步颜色转换/拷贝依赖尚未移植，不能仅把宏改为 1 就认为支持。
- NuttX 层只暴露单 framebuffer，尚未接入 page flipping、防撕裂和完整帧同步方案。
- 官方多缓冲、YUV、绘制钩子等代码保留，但对应运行场景未验收。
- 触摸、Quick App 运行时与 rpk 上屏不在本分支本次完成范围；工程中已有对应运行时和安装链路，但当前 LCD 配置未启用，也没有 ESP32-P4 目标板验证。
- 电源管理、cache-disabled ISR、独立 kernel heap 配置均未验证。
- heap_caps 的内存区域语义、DMA 描述符及中断链路还需结合真机地址和回调行为检查；不能因为函数同名就假定和 IDF 运行时完全相同。
- 新分支使用有界 PHY 等待，没有继承旧实现“反复重置 PHY 并重试 20 次”的调试策略，冷启动可靠性需要重新测试。

## 15. 下一轮真机验收顺序

1. 连接板子，确认串口、芯片型号和烧录参数，对新镜像保存烧录及启动日志。
2. 检查初始化日志、`/dev/fb0` 和启动横向 RGB 三条图案。此图案必须来自真实 framebuffer。
3. 运行 `lvgldemo widgets`，确认 UI 替换启动图案并持续变化，排除只显示启动图案的情况。
4. 记录 underrun、DMA 完成事件和画面异常；需要诊断时优先通过官方事件回调累计计数，在普通任务中低频输出，避免每帧中断大量打印或根据单次寄存器值过度推断。当前未接入这些统计，缺少错误日志不能直接证明没有 underrun。
5. 建议首轮做 10 次断电冷启动、10 次复位及至少 10 分钟动态显示观察，记录成功/失败次数、画面及日志。这些是建议验收门槛，尚未执行。
6. 单独测试重复初始化、错误恢复和释放路径。当前 `apps/examples/lvgldemo/lvgldemo.c` 正常运行会进入无限循环；demo 退出与 `up_fbuninitialize()` 不是同一验收项。应增加专门测试入口，先解除上层映射和使用，再测试注销/释放/重新注册，不能在 LVGL 仍使用 framebuffer 时直接释放。
7. 若真实流失败，单独启用官方 Host 竖彩条定位 PHY / Host / 面板链路，随后切回真实流继续验证。
8. 稳定后再扩展双缓冲、帧同步、防撕裂及触摸。

## 16. 后续文档更新模板

每次继续调试建议追加以下信息，避免新旧结论混用：

| 字段 | 内容 |
| --- | --- |
| 日期与代码提交 | 待填 |
| 本次变更目的 | 待填 |
| 是否修改官方文件 / 对应补丁 | 待填 |
| 构建配置、结果、镜像摘要 | 待填 |
| 真机屏幕现象和串口日志位置 | 待填 |
| 测试次数、通过次数 | 待填 |
| 新发现 / 已排除项 | 待填 |
| 下一步 | 待填 |

本文为本地准备好的飞书正文。生成本文时会话未提供飞书文档创建/写入接口，尚未上传或创建远程文档；目标位置为用户个人文档库。

## 17. 本轮代码复核记录（2026-09-24）

本轮只读取代码及配置、检查现有产物、校验补丁并更新本文；没有重新编译、烧录或执行硬件验收。

| 核查项 | 本轮结果 |
| --- | --- |
| 分支 / HEAD | `feat/esp32p4-official-lcd` / `7968744` |
| 官方来源复核 | 在临时副本反向应用 `openvela.patch`，19/19 个 SHA-256 与清单一致 |
| 现有镜像 | 599,596 字节，SHA-256 为 `a2e82e74943a0a188d869a1c282d061dc75544ac310f1c93c2e8ffd67d85233b`，与第 12 节一致 |
| 更新路径 | `lv_nuttx_fbdev.c` 提交 `FBIO_UPDATE`；板级 `lcd_update()` 调用官方 `draw_bitmap()`，进入 framebuffer 扫描行缓存回写路径 |
| 串口枚举 | 当前环境未找到 `/dev/ttyACM*` 或 `/dev/ttyUSB*`；这不判断板子在其他环境是否可用 |
| Quick App 核心库 | 找到四套 `libquickapp.a`，实际为 ARM/AArch64/x86-64；本地未找到 RV32 核心库，核心 `src/` 目录也不存在 |
| 本轮文档保存 | 更新前本文为 Git 未跟踪文件；本轮编辑不包含 Git 提交或远程发布 |

**当前行动顺序：接板完成真实 RGB 和 LVGL 动态验收 → 保存 LCD 基线；同步落实 RV32 Quick App 核心实现 → 独立配置接入依赖与存储/字体 → URI 启动最小应用 → RPK 安装链路、触摸和 Launcher。**


## 18. 用户提供内部源码后的结论更新（2026-09-24）

用户提供另一份本机工程后，已只读核实该工程包含 Quick App 核心源码，不再需要把取得现成 RV32 二进制库作为唯一途径。此前“缺核心源码”的结论仅适用于比赛公开工作区，不能扩大为用户本机没有源码。

### 18.1 已确认的源码与构建证据

以下路径相对用户提供的内部工程根目录；为保留报告可分享性，不在这里复制内部源码正文。

| 证据 | 核实结果 |
| --- | --- |
| `frameworks/runtimes/quickapp/src/api.cpp` | 有 `QApplicationCreate()`、`QApplicationStart()` 的实际实现 |
| `frameworks/runtimes/quickapp/src/` | 有 framework、GUI、JSE、PM 等目录；统计到 403 个 C/C++ 源码及头文件 |
| `frameworks/runtimes/quickapp/inspector/` | 有实际源码及构建规则 |
| `frameworks/runtimes/quickapp/src/Makefile` | 加入核心源码，并在 `QUICKAPP_VAPP=y` 时加入 `shell/vapp/main.cpp` |
| `frameworks/runtimes/quickapp/src/CMakeLists.txt` | 有核心源码收集、GUI 子目录及 protobuf 生成规则 |
| `frameworks/runtimes/quickapp/deps/js-framework/` | 目录及 Make/CMake 文件存在；Node 依赖安装和生成步骤尚未运行验证 |

这证明已具备开展源码移植的基础，不代表全部依赖完整或 RV32 编译已通过。本轮没有修改内部工程、复制其源码到比赛仓库、运行内部工程构建或发布源码。

### 18.2 当前真正需要解决的差异

1. **公共接口版本不同。** 内部与公开版本的 `quickapp.h` 不同，创建参数及客户端结构体有新增字段。必须配套使用所选源码版本的头文件、shell 和依赖接口，不能仅复制 `src/` 并保留全部旧头文件。
2. **配置符号不同。** 内部 Quick App 依赖 `LIB_CURL`，公开版本使用 `UTILS_CURL`；需按目标树实际 Kconfig 映射，而不是原样复制产品 defconfig。
3. **依赖需要逐项核对。** 两边 LVGL 版本头均为 9.1.0，但这不能证明厂商扩展完全兼容；QuickJS、UIKit、ASH、Feature、Yoga 等也需核对。Makefile 引用的 `deps/uvws` 在本轮检查中不存在，是否仍为所选功能必需应结合实际编译确认。
4. **主机工具与目标代码分别构建。** 构建规则包含主机端 JS 工具、Node 依赖和 protobuf 生成，不能将所有编译器统一替换为 RV32 交叉编译器。
5. **硬件与资源缺口仍然存在。** LCD 真机验收、文件系统、字体、内存预算、触摸和后续安装链路继续按前文推进。

### 18.3 更新后的执行顺序

1. 保留当前 LCD 配置和可恢复基线，继续真实 framebuffer/LVGL 真机验收。
2. 按用户要求，内部源码只留在内部工程，在其中建立独立 RV32 构建配置和输出目录；禁止将内部源码、内部头文件或源码副本放入公开 openvela 工程。只交付经检查的编译产物及不含内部实现的构建说明。
3. 在内部工程建立最小 `vapp` 配置，先检查依赖与代码生成，再使用匹配 ESP32-P4 ISA/ABI 的工具链编译目标库。内部完成公开 API 兼容适配；导出前检查 ELF 架构、未解析符号和调试信息，避免产物携带内部源码内容。
4. 链接出目标镜像后，准备字体与已解包的最小应用，验证 URI 启动及页面更新；随后验证 RPK 安装、触摸及 Launcher。

**当前缺口已由“没有核心源码”变为“已有源码，但尚未完成面向 ESP32-P4 的依赖适配、交叉编译和真机运行”。**


## 19. RV32 首轮配置失败及修正（2026-09-24）

首轮一键命令只生成了 CMake 构建文件，没有生成 Quick App 库；`Configuring done` / `Generating done` 不能记为编译通过。实际配置缺少 Quick App，且 QEMU 基础配置默认开启 FPU，不能直接作为 ESP32-P4 的库配置。

已通过 Kconfig 依赖解析定位并修正：

| 未生效组件 | 缺失前置条件 / 修正 |
| --- | --- |
| FreeType、PNG | 开启 `ARCH_SETJMP_H` |
| libuv extension | 开启 `NETUTILS_CODECS`、`CODECS_HASH_MD5` |
| TCP | 开启 `SCHED_LPWORK` |
| 整数 ABI | 显式关闭 `ARCH_FPU` |

内部专用配置片段已更新，使用 `setconfig` 解析后确认 `QUICKAPP=y`、`QUICKAPP_VAPP=y`、`ARCH_FPU=n`。脚本增加严格配置片段检查、Quick App 核心编译规则和实际 `-march` / `-mabi` 检查，并提供 `--configure-only` 模式。配置解析成功仍不代表完整编译或 ESP32-P4 集成成功。

编译产物改为先保留在内部交付目录，取消自动复制到 openvela；待公开接口、依赖及目标兼容检查完成后再交付二进制。内部源码和头文件仍不得复制到公开工程。


补充验证：已生成 Quick App 核心 `src/api.cpp` 的实际编译规则，参数为 `-march=rv32imac_zicsr_zifencei -mabi=ilp32`。另发现内部 CMake 构建不提供 `olddefconfig` 目标，已从一键脚本删除该调用，改由 CMake 配置阶段解析；`savedefconfig` 目标虽存在，但配置片段模式下明确拒绝执行，因此脚本也已删除该调用，配置通过专用片段持久保存。启用 LVGL POSIX 文件后端，并将 POSIX/Quick App bundle 缓存大小显式设为 0，消除未开启后端时默认整数配置为空的问题。上述结果仍属于配置和规则验证，尚无完整库编译通过结论。


## 20. 后续单文件编译修复记录（2026-09-24）

- LVGL POSIX 文件后端：初次启用后 `LV_FS_POSIX_LETTER=0` 触发编译错误。专用配置采用内部已有 Quick App 配置的 `/` 标识（47）、路径 `/` 和默认驱动标识 0，失败文件已重新编译通过。
- QuickJS：RV32 软件浮点工具链不提供 `FE_DOWNWARD` / `FE_UPWARD`。内部源码新增仅用于 NuttX RISC-V 软件浮点的 printf 路径，禁用不适用的动态舍入补偿与 `fesetround()` 调用。原失败对象已使用实际目标命令编译通过，未解析符号中没有 `fesetround`。直接引用内部 NuttX dtoa 实现的主机测试通过 12 项正负临界/相邻值检查；不代表完整 JS 数值兼容或目标板运行验收。
- ASH：架构检测原先只有 x86/ARM 分支，触发 `Architecture is not supported`。内部头文件补充基于 `__riscv_xlen` 的 RV32/RV64 识别与通用位宽宏。两个失败的 C wrapper 文件及 `variable_segment.cpp` 已按实际 RV32 命令编译通过；真实交叉编译器的 RV32/RV64 宏检查通过。

以上修改只发生于内部源码/专用配置，未复制内部源码或头文件到公开工程。验证范围为已列出的失败对象与定向测试，完整 Quick App 库、最终链接及 ESP32-P4 运行仍待验证。


后续 ZIP 编译修复：`feature/modules/zip_impl.cpp` 无条件使用 `fopen64`，但当前 NuttX 未开启 `FS_LARGEFILE`，没有该别名。内部源码改为 NuttX 分支使用标准 `fopen`，其他平台保持原行为，并显式包含 `stdio.h`。原失败对象已按实际 RV32 命令编译通过；符号检查确认使用 `fopen`、不再依赖 `fopen64`。未改变全局文件偏移 ABI，未扩大为大文件支持或 RPK 解包运行验收。


后续属性依赖修复：`configuration_impl.cpp` 调用 `uv_property_set`，其声明和实现受 `KVDB` 控制，原配置未开启；`uv_getlocale` 实现也依赖属性服务。专用配置已增加 `KVDB`、本地 `KVDB_SERVER`、`UNQLITE`、`NET_LOCAL`/`NET_LOCAL_STREAM` 及 `FS_LOCK_BUCKET_SIZE=16`，使用既有 TMPFS。通过 CMake 目标生成 JIDL 接口后，语言配置模块、`uv_locale`、`uv_property`、属性客户端/共享内存/服务端均编译通过，生成 `libframework_utils.a` 和 `libapps_kvdbd.a`。该验证没有启动服务或验收持久化；后续运行需准备 TMPFS、数据库目录及 kvdbd 启动顺序。整体依赖不止最初列出的五个 Quick App 核心库，最终交付需以链接结果核对。


后续 uORB 依赖修复：默认启用的电池模块包含 `system/state.h`，该头文件在内部工程存在，但 include 路径由 `UORB` 控制。定位模块也使用同一事件机制。专用配置补齐 `SENSORS`、`USENSOR`、`UORB`；通过 CMake 目标验证电池、定位、`uv_topic` 编译，并生成 `libuorb.a`。未启用或验收任何具体电池/GNSS 物理设备；后续需要实际数据发布者才能提供对应硬件能力。


后续 Canvas 2D 依赖修复：内部 Canvas 实现无条件使用 LVGL 矩阵与矢量绘图 API，原配置未开启，导致不完整类型及大量接口未声明错误。专用片段补齐 `LV_USE_VECTOR_GRAPHIC`、`LV_USE_MATRIX`、`LV_USE_FLOAT`、`LV_USE_THORVG`、`LV_USE_THORVG_INTERNAL`，采用软件矢量后端，未开启硬件 FPU。原失败 Canvas 2D 对象与包含该后端的 `liblvgl.a` 已通过 RV32 构建。`LV_USE_FLOAT` 改变 LVGL 精确数值类型，最终 openvela 侧必须同步匹配该配置，不能与原 LCD-only 配置直接混用；实际绘制、速度和内存尚未真机验收。


## 21. 内部 RV32 Quick App 完整构建通过（2026-09-24）

本轮由助手继续完成实际构建，退出码 0，日志出现 `build completed successfully`，最终 `nuttx` ELF 链接及 `System.map`、`nuttx.hex` 生成通过。此固件属于内部 `rv-virt:quickapp_export` 验证配置，不能烧录到 ESP32-P4。

最后一轮链接修正包括：

- 开启 `NETDEV_LATEINIT`，避免当前无板级网卡初始化实现的验证配置引用 `riscv_netinitialize`；不表示已接入物理网络设备。
- 开启 `NET_SOCKOPTS`、`LIBC_NETDB`，提供 socket 选项及地址解析接口。
- 开启 `ALLOW_MIT_COMPONENTS`，编入现有 `strptime` 实现。
- 使用本地已有 Newlib 数学源码（`LIBM_NEWLIB`），补齐 `lrint`、`hypot`、`log1p` 等接口，仍为 RV32 软件浮点 ABI。
- 内部 RISC-V `setjmp.h` 补齐 `extern "C"`，使 C++ ThorVG 引用正确的 C 符号。
- GUI 阶段还补齐 `LV_USE_QRCODE`，完整 `gui_wrapper` 与 LVGL 库已通过构建。

### 21.1 产物完整位置与查找方法

本次内部工程根目录为 `/home/mi/2t/p62-dev`。交付目录的完整路径为：

```text
/home/mi/2t/p62-dev/out/quickapp-rv32-delivery-20260924-162932-490649/
```

**交付目录与 `quickapp-rv32-export` 编译目录平级，不在编译目录里面，也不在公开 openvela 工程内。**

```text
/home/mi/2t/p62-dev/out/
├── quickapp-rv32-export/                         # 原始编译输出
├── quickapp-rv32-delivery-20260924-162932-490649/  # 检查后的五个库和摘要
└── quickapp-rv32-link-check.log                  # 本次成功日志
```

在编译用的同一台机器、同一环境中执行：

```bash
cd /home/mi/2t/p62-dev/out/quickapp-rv32-delivery-20260924-162932-490649
ls -lh
sha256sum -c SHA256SUMS
```

用户反馈找不到目录后，助手再次只读检查：目录存在，解析后的路径与上述完整路径一致，包含以下文件。若文件管理器中未显示，可按 `Ctrl+L` 粘贴完整路径；编辑器文件树可能需要刷新或检查忽略目录过滤。若终端仍提示不存在，先核对机器及挂载环境：

```bash
hostname
ls -ld /home/mi/2t/p62-dev/out/quickapp-rv32*
```

### 21.2 交付文件与验证结果

目录包含：

```text
libquickapp.a
libgui_wrapper.a
libquickappfeatures.a
libquickapp_inspector.a
libapps_vapp.a
SHA256SUMS
```

本次复核的精确文件大小如下；后续重新构建可能变化，不应把它们作为所有版本必须相等的断言。

| 文件 | 大小（字节） |
| --- | ---: |
| `libquickapp.a` | 3,579,736 |
| `libgui_wrapper.a` | 4,934,274 |
| `libquickappfeatures.a` | 303,024 |
| `libquickapp_inspector.a` | 12,930 |
| `libapps_vapp.a` | 90,208 |
| `SHA256SUMS` | 422 |

以上为静态库文件大小，不等于最终固件大小或运行内存占用。

五个库均通过 ELF32/RISC-V/soft-float ABI、指令集检查；去除调试信息后检查没有 `.debug_*`、`.zdebug_*` 或 `.gnu.lto_*` 段，SHA-256 清单复核全部通过。指令集属性对应 RV32 IMAC、Zicsr、Zifencei 及其工具链标注的子扩展。

验证边界：链接仍有一个 RWX LOAD segment 警告；没有执行模拟器或 ESP32-P4 运行验收。五个库不是自包含运行时，链接依赖还包括 QuickJS、ASH、Feature、libuv、LVGL、数学库、KVDB 等；公开工程的版本、配置、接口和入口注册需匹配。本轮没有将内部源码、头文件或产物复制到公开工程。

日志保存在内部工程 `out/quickapp-rv32-link-check.log`。此前用户运行的 `quickapp-rv32-build-all.log` 仍可能保留旧失败记录，本次通过以新的 link-check 日志为准。

### 21.3 内部库交付时的阶段状态（已由第 22 节更新）

- **已完成：**内部 RV32 配置构建、最终链接、五个库的二进制检查和内部交付目录准备。
- **未完成：**公开 openvela 的依赖版本与配置匹配、公开接口兼容、`vapp` 入口及库链接接入、ESP32-P4 固件构建和真机运行。
- **源码边界：**内部源码和内部头文件保留在内部工程；后续只交付经核对的二进制产物，不复制内部源码。
- **接入顺序：**核对库依赖与 LVGL/QuickJS/C++ ABI → 准备公开工程独立配置 → 接入库和入口 → 编译 ESP32-P4 镜像 → 准备字体、文件系统和最小应用 → 真机验证。


## 22. 公开 openvela 的 ESP32-P4 Quick App 镜像生成通过（2026-09-24）

本节更新第 21 节的公开集成状态：已将配套二进制接入本机公开工作区，通过 ESP32-P4 Make 构建、最终链接和镜像生成。内部源码及头文件没有复制。

- 新建 `board/contest_board/configs/quickapp`，保留原 LCD 配置；使用 `ESP32P4_QUICKAPP_PREBUILT` 选择二进制运行时，不再由公开 Quick App/LVGL 选项编译另一套版本。
- `board/contest_board/prebuilt/quickapp-rv32` 包含 29 个配套归档与摘要，包括 libc++/libc++abi 21.1.8、LVGL、QuickJS、ASH、Feature、libuv 等；公开工程仍构建 ESP32-P4 内核、板级驱动、文件系统、网络栈和 NSH。
- `app/quickapp_prebuilt` 注册 `vapp`、`kvdbd` 的 C 入口，构建助手自动建立应用链接。
- rev3 链接脚本补齐 C++ 构造函数边界，并将 libc++ `__lcxx_override` 代码段纳入 Flash text，满足镜像对齐要求。
- 内部重编 LVGL framebuffer 对象，启用 `FBIO_UPDATE` 后交付库变体；反汇编确认提交 0x2807 ioctl，承接板级 PSRAM 缓存回写。

构建入口：

```bash
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_quickapp.sh -j8
```

脚本检查归档摘要、建立应用链接、配置并按需恢复固定 HAL；切配置前仍需保留未固化的 HAL 改动。只验证 Make，该二进制包的板级 CMake 接入未实现。

```text
固件：/home/mi/Developer/openvela/artifacts/esp32p4-quickapp/nuttx.bin
大小：3507376 bytes
SHA-256：46e7d65c00f1a99e4ef25fac742d6b9c6af637c016cdd20869173c72df220f57
ELF、配置、System.map：同一 artifacts 目录
持久验证摘要：docs/bringup/quickapp_prebuilt_build.txt
```

已检查入口、构造函数边界、29 个归档的摘要和调试/LTO 段，12 项系统类型大小与 9 项字段偏移比对一致。抽样 ABI 检查不代表所有跨版本接口语义已证明兼容。

当前可宣称已生成包含官方 LCD、Quick App 二进制和 NSH 入口的 ESP32-P4 固件。未烧录或运行；没有内置应用/字体资源镜像，没有建立 `/data` 挂载和 kvdbd 自动启动，没有触摸或 Launcher。后续需准备资源、服务启动和真机验收。Flash header 为 4MB，镜像加 0x2000 偏移未超过 4MiB，实际容量及资源布局仍需核对。

本轮未提交 Git、推送或发布。原 LCD 配置未修改，原镜像暂存于 `/tmp/esp32p4-lcd-baseline/`，临时目录不是长期唯一证据。


## 23. 快应用方向的无板阶段计划（后续优先级调整见第 24 节）

| 层级 | 最新证据 | 剩余工作 |
| --- | --- | --- |
| ESP32-P4 基础板级 | 已有历史真机基线 | Quick App 配置运行后的基础回归 |
| 官方 LCD / framebuffer | 已接入，LCD 配置和 Quick App 配置均已生成镜像 | 真实 RGB、动态刷新、冷启动和复位验收 |
| 内部 RV32 运行时 | 完整构建、链接及库检查通过 | 固化内部构建脚本和必要修改，长期维护二进制来源 |
| 公开 openvela 集成 | 29 个配套归档接入，ESP32-P4 镜像生成通过 | 运行验证，必要时继续处理跨版本接口语义差异 |
| 最小快应用 | 仍为项目初始模板 | 补齐构建/打包、字体、动态页面、页面跳转和数据读写 |
| 文件系统与属性服务 | TMPFS/ROMFS 支持及 kvdbd 入口已编入 | 资源镜像、`/data` 挂载、数据库目录和服务启动顺序 |
| 触摸与 Launcher | 未接入验收 | 基本快应用运行后再接触摸及完整桌面系统 |

### 23.1 没有板子时优先做

1. 把临时目录中的内部构建脚本、配置及必要源码适配固化到内部工程；公开侧维护二进制包清单和构建说明，不复制内部源码或头文件。
2. 完成最小快应用：中文文字、计时变化、按钮、两个页面和一次数据读写，形成可部署产物。
3. 设计并实现只读应用/字体资源与可写数据目录，准备 `/data`、KVDB 数据目录及启动顺序；TMPFS 读写不等于断电持久化。
4. 选择可用的模拟器图形配置验证应用及运行时生命周期。ESP32-P4 镜像不能当作通用 QEMU 固件运行，模拟器通过也不替代 LCD/PSRAM/DMA 真机验证。
5. 准备拿板后的烧录与分层验收流程：LCD 真实 RGB → LVGL 持续刷新 → vapp 最小页面 → 触摸 → RPK 安装与 Launcher。

### 23.2 交接边界

- 本轮没有板子，没有执行烧录、硬件刷新、触摸或应用运行验证。
- 公开工程生成的是包含运行时的 ESP32-P4 固件，尚未附带开机即可展示的应用和字体。
- 内部库已在本机公开工作区落盘，但尚未提交 Git、推送或对外发布。
- 本文及第 22 节的构建记录应随以后镜像变化更新；历史内部五库交付目录与当前公开侧 29 库配套包是不同阶段的产物。


## 24. 官方板卡核查与独立桌面阶段目标（2026-09-24）

用户提供官方板卡说明后，本轮读取官方网页并对照乐鑫 BSP 与本地 NuttX 输入驱动。当前阶段先完成“打开电源进入桌面，图标可点击、普通页面可返回”，暂不以快应用、PM/AM/WM 或完整 Launcher 为前置条件。此节是资料核查和实施计划，没有新增桌面或触摸驱动实现，也没有真机验收。

### 24.1 官方资料与现有配置对照

来源：

- [ESP32-P4X-Function-EV-Board 用户指南](https://documentation.espressif.com/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)
- [乐鑫 BSP 板级头文件](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h)
- [乐鑫 BSP 显示与触摸初始化实现](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_p4_function_ev_board/esp32_p4_function_ev_board.c)

上述 BSP 链接为 master，本轮作为硬件适配参考，尚未固定为本项目触摸驱动的上游基线。BSP 目录名不含 X；收到实物后仍需核对板卡和屏幕配件版本。

| 项目 | 官方指南 / BSP 核查结果 | 本项目状态 |
| --- | --- | --- |
| 显示配件 | 可选 7 英寸、1024×600 MIPI-DSI 电容触摸屏 | 当前显示驱动按 1024×600 配置 |
| LCD 复位 | GPIO27 → 屏幕适配板 J6 的 RST_LCD | 与现有代码一致 |
| 背光 | GPIO26 → 屏幕适配板 J6 的 PWM | 与现有代码一致 |
| 显示排线 | 反向线序，连接主板 MIPI-DSI 与屏幕适配板 J3 | 等待实物核对 |
| 触摸控制器 | 官方 BSP 创建 GT911 驱动 | 本项目尚未接入 |
| 触摸 I²C | SDA GPIO7、SCL GPIO8 | 当前 I2C1 已使用这组引脚，需共享总线而非重复配置 |
| 触摸独立复位 / 中断 | 官方 1024×600 配置均为 GPIO_NUM_NC | 需确认实际复位关联和定时读取方案 |
| 触摸方向初值 | swap_xy=0、mirror_x=1、mirror_y=1 | 仅作初始参考，必须与实际显示方向校准 |

GPIO27 是 LCD 复位引脚，不能未经原理图核实就把它当作独立 GT911 复位控制。BSP 对触摸复位有共享注释，但配置值为 NC，最终应按实际连接处理。

### 24.2 现有 GT9xx 驱动能复用的范围

公开 openvela 已有：

```text
nuttx/drivers/input/gt9xx.c
nuttx/include/nuttx/input/gt9xx.h
CONFIG_INPUT_GT9XX
```

驱动提供 I²C 数据读取和触摸设备注册接口，板级需提供 `irq_attach`、`irq_enable`、`set_power` 回调。注册路径示例为 `/dev/input0`，可继续接 LVGL 的 NuttX 触摸后端。

目前的关键适配点是：现有驱动按中断通知上层读取数据，而官方 BSP 的该屏配置未使用独立触摸中断 GPIO。需要先核对屏幕适配板原理图，再决定使用可用中断线或适配定时读取；不能只开启 Kconfig 就宣称触摸可用。触摸 I²C 地址、复位时序和方向也尚未实测。

### 24.3 第一版桌面范围

建议从 LCD 配置派生独立桌面配置，先用公开 LVGL 源码实现桌面和普通页面，不要求依赖内部 Quick App 二进制包。桌面及触摸适配的配置和源码尚未创建。

首版效果：

```text
上电 → 系统与显示初始化 → LVGL 桌面
                             ↓ 点击图标
                         普通测试页面
                             ↓ 返回按钮
                           回到桌面
```

桌面包含背景、项目名称、3～4 个图标和必要状态文字；没有实际数据来源的电量、Wi-Fi 状态不显示为已可用。保留串口控制台。图标先打开普通 LVGL 页面，后续再接快应用，避免一开始同时引入两套图形生命周期。

### 24.4 无板可做与拿板必验

| 无板阶段可以完成 | 必须拿板验证 |
| --- | --- |
| 独立桌面程序、图标和页面切换 | 官方显示链路真实出图和持续刷新 |
| 模拟器布局及按钮事件验证 | GT911 总线通信、触摸按下/移动/抬起 |
| GT911 板级适配、定时读取及错误处理的代码准备 | 屏幕方向与触摸坐标的一致性 |
| 开机启动桌面的入口和失败日志 | 多轮冷启动、复位、持续操作稳定性 |
| 显示和输入测试程序、验收清单 | PSRAM/DMA/cache 行为及画面异常 |

拿板先确认显示屏、适配板及反向排线齐备。按官方指南连接 GPIO26/27 和供电；官方推荐屏幕适配板外接 USB 供电，使用该供电方式时指南说明无需再连接主板至适配板的 5V/GND 杜邦线。具体接法以对应硬件版本说明为准。

当前优先顺序：独立 LVGL 桌面及模拟器验证 → GT911 适配准备 → 开机入口 → 拿板验收 LCD 和触摸 → 再接快应用与桌面返回生命周期。

### 24.5 保留此前资源准备的真实状态

此前计算器方向已生成约 97 KiB 的资源 ROMFS，并起草 `qastart` 资源/服务准备入口，但该工作没有完成运行验收，不能认为开机计算器已可用。第 22 节归档仍是当时通过检查的固件快照，不应把后续开发目录的构建结果自动视为已更新归档。后续恢复快应用准备时应重新核对入口是否进入镜像、资源是否被链接保留、挂载、KVDB 就绪、字体及应用加载。


## 25. 真机显示验证更新（2026-09-24）

用户已连接开发板，实测 ESP32-P4 rev v3.2、16MB Flash，设备 USB 序列号为 `E8:F6:0A:E3:A9:17`；端口曾在 ttyACM0/ttyACM1 间重新枚举。主板与显示屏分别 USB 供电。

本轮修复官方 LCD bridge 和 DW-GDMA 的中断接入：使用 NuttX `esp_os_intr_alloc` / `esp_os_intr_alloc_intrstatus` 及对应释放入口，使 IRQ 映射被本项目中断分发逻辑登记。差异已同步到 LCD 与 HAL 补丁。修复后任务能持续运行，DMA 帧完成计数约每秒 60 次。GPIO26 的额外输入采样和输出矩阵实验按用户要求撤回，保持原纯输出配置。

真机证据：

- 用户确认 Host 竖彩条能显示；该路径不证明 framebuffer 刷新。
- 真实 framebuffer 曾出现固定黑/蓝分区，未将此解释为动态撕裂。
- 颜色诊断任务连续两轮写入全屏红、绿、蓝、白，每色保持 5 秒；用户确认颜色依次出现，随后确认横向红绿蓝三条正常。
- 每次缓存回写返回 0，日志最终 DMA 帧完成计数为 2519；采样时 bridge 原始状态为 0x2，没有观察到欠载位 0x1，但不等于经过完整欠载统计验收。
- 颜色自检结束后执行 `lvgldemo widgets`，成功打开 `/dev/fb0`，参数为 1024×600、RGB565、fblen 1228800、stride 2048。采集窗口内没有 FBIO_UPDATE 错误；演示使用默认字体替代未启用的 Montserrat 16/24。
- 用户随后确认屏幕出现 LVGL 控件页面，完成首次真实 UI 上屏确认；动画是否持续变化、长时间显示稳定性仍待验证。触摸、桌面、冷启动可靠性尚未验收。

颜色验证固件及配置：`artifacts/lcd-hardware-validation/color-test-confirmed/`；SHA-256 为 `a3ffc6897ae0a7fcc348acfb2f011444c5b4db9d1b12e630e4002a1c0307dbe6`。LVGL 日志：`artifacts/lcd-hardware-validation/lvgl-20260924-180050.log`。

未解决事项：USB 监视器连接期间出现 CHIP_USB_UART_RESET 与重新枚举；ROM 报 SHA-256 comparison failed 后继续启动，需要独立核查 Simple Boot 镜像格式与 ROM 行为。当前代码保留一次性颜色诊断任务，常规版本应在验收后移除或置于诊断配置开关下，不能在其执行期间启动另一个 framebuffer 绘制程序。


## 26. 真机测试命令与 LVGL 上屏复现（2026-09-24）

本节区分主机 Bash 命令与板端 NSH 命令。路径以 `/home/mi/Developer/openvela` 为工作区根目录。此次显示验证使用 **LCD 配置**，不是 Quick App 配置；Host 彩条关闭，GPIO26 保持纯输出，LCD/DMA 中断映射修复已生效。

### 26.1 最重要的板端命令

真正让屏幕出现照片中控件页面的命令是：

```text
lvgldemo widgets
```

它在板子的 `nsh>` 提示符下执行，不是在电脑 Bash 中执行，也不是 `vapp`。页面是 LVGL Widgets Demo 的 Profile 页，不是自有桌面或快应用。

**执行时机：**当前测试固件启动后约 2 秒开始红→绿→蓝→白全屏测试，每色 5 秒，重复两轮。必须等串口出现下面这行，确认诊断任务不再改写 framebuffer：

```text
LCD TEST: horizontal RGB restored sync=0 frames=...
```

然后输入 `lvgldemo widgets`。不要在颜色诊断运行期间启动 LVGL，也不要在已经运行的 demo 上重复启动第二个实例。demo 正常路径是持续循环；退出串口终端不会自动结束板上 demo。

### 26.2 主机端：识别设备、编译和检查

实际用过的设备查询：

```bash
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"
python3 -m serial.tools.list_ports -v
esptool --chip esp32p4 --port /dev/ttyACM0 flash-id
```

本次识别到的序列号为 `E8:F6:0A:E3:A9:17`，Flash 为 16MB。端口曾在 `/dev/ttyACM0` 和 `/dev/ttyACM1` 间变化，每次应按枚举结果填写；不要盲用固定编号。受限工具环境无法看到设备时，本轮通过沙箱外访问确认设备存在。

当前配置已是 LCD、HAL 存在时，实际编译命令为：

```bash
set -o pipefail
PATH="$HOME/.local/bin:$PATH" ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/lcd -j8 \
  2>&1 | tee /tmp/lcd-rebuild-verify.log
```

切配置或 HAL 缺失时先按第 0.3 节有序恢复。必须确认构建退出码 0、出现 `Generated: nuttx.bin`，再检查：

```bash
rg -n 'CONFIG_(ESP32P4_BOARD_LCD|ESP32P4_BOARD_LCD_COLORBAR|FB_UPDATE|EXAMPLES_LVGLDEMO|ESPRESSIF_SIMPLE_BOOT)(=| is not set)' nuttx/.config
sha256sum nuttx/nuttx.bin
wc -c nuttx/nuttx.bin
```

真实流要求 `COLORBAR` 关闭，其余上述功能开启。`nuttx/nuttx.bin` 是可被后续构建覆盖的当前产物，不能把它自动等同于已经验收的镜像。

### 26.3 主机端：烧录已确认的颜色测试镜像

本轮通过颜色及控件页面验证的固件归档位于：

```text
artifacts/lcd-hardware-validation/color-test-confirmed/nuttx.bin
SHA-256: a3ffc6897ae0a7fcc348acfb2f011444c5b4db9d1b12e630e4002a1c0307dbe6
```

下面是与实际烧录参数一致、显式使用归档镜像的复现命令。先结束占用串口的监视器，按实际枚举修改 `LCD_PORT`：

```bash
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"
LCD_PORT=/dev/ttyACM0
(cd artifacts/lcd-hardware-validation/color-test-confirmed && sha256sum -c SHA256SUMS)
esptool --chip esp32p4 --port "$LCD_PORT" --baud 921600 \
  --after hard-reset write-flash 0x2000 \
  artifacts/lcd-hardware-validation/color-test-confirmed/nuttx.bin
```

本次必须看到 `Hash of data verified.`。烧录写入校验与随后 ROM 的 `SHA-256 comparison failed` 是不同检查；后者仍是未解决问题，不能因烧录校验成功而忽略。

手动连接控制台可用以下命令；这是复现建议，本次成功上屏实际使用的是下一节的 Python 脚本：

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 115200 --raw --dtr 0 --rts 0
```

退出终端为 `Ctrl+]`。即使预置 DTR/RTS，本轮仍观察到打开串口伴随 USB 复位/重新枚举，不保证连接完全无扰动。掉线后先核对新端口，再等本次启动的颜色自检结束。

### 26.4 本次实际执行过的测试脚本

| 阶段 | 当时在主机执行的命令 | 行为与结果 |
| --- | --- | --- |
| 初次烧录后查询 | `python3 /tmp/capture-p4-lcd.py` | 查询 uname/free/fb0；早期 LCD 初始化失败，无 fb0；此脚本写串口无超时，不推荐复用 |
| 白屏/真实流早期诊断 | `python3 /tmp/flash-read-real.py` | 烧录后立即采集；曾固定 ttyACM0，受枚举变化和复位干扰，不作为推荐入口 |
| 按序列号烧录 | `python3 /tmp/p4-flash-only.py` | 自动找到本板，烧录当前 nuttx.bin 后关闭端口 |
| 只读采集 | `python3 /tmp/p4-read-only.py` | 预置 DTR/RTS、按序列号重连，记录初始化与 DMA 计数；仍观察到 USB 复位 |
| 静态状态查询尝试 | `python3 /tmp/p4-static-check.py` | 原计划查询 uname/free/fb0/uptime，实际在查询前断开，不能记为完成连续运行验证 |
| 重新编译后的烧录验证 | `python3 /tmp/p4-rebuild-flash-verify.py` | 归档镜像、烧录、采集，证据在 rebuild-20260924-175454 目录 |
| 两轮颜色测试采集 | `python3 /tmp/p4-color-read.py` | 红绿蓝白依次显示，日志 sync=0，最终恢复横向 RGB |
| **成功启动 LVGL 页面** | **`python3 /tmp/p4-run-lvgl.py`** | **等到 horizontal RGB restored 后，向 NSH 发送 `lvgldemo widgets`，再采集约 12 秒日志** |

脚本 `/tmp` 路径是当时的临时入口，可能被清理或修改。部分实际脚本已保存为 `docs/bringup/lcd_hardware_20260924/historical_scripts/` 历史副本；副本仍含本机绝对路径，部分会写 Flash，只用于审阅和复核操作，不能不检查就批量执行。最新的颜色测试 task 代码在板级 `esp32p4_lcd.c` 中。

### 26.5 上屏证据与照片判读

本次成功操作顺序有日志直接支持：

```text
LCD TEST: horizontal RGB restored sync=0 frames=2519
HOST: launched lvgldemo widgets after RGB self-test
nsh> lvgldemo widgets
The framebuffer device was opened successfully
xres: 1024
 yres: 600
fblen: 1228800
stride: 2048
bpp: 16
```

上面摘录缩略了日志前缀，完整记录见 [LVGL 日志](lcd_hardware_20260924/lvgl_widgets.log)；两轮填色记录见 [颜色测试日志](lcd_hardware_20260924/color_test.log)。

用户随后提供实拍照片，已保存为 [LVGL Widgets 上屏照片](lcd_hardware_20260924/lvgl_widgets.jpg)。照片可见 LVGL v9.1.0 标识、Profile/Analytics/Shop 标签、头像、文字、按钮、输入框、滑条和开关，整体页面完整，未见此前大片黑蓝分区。拍摄条纹、反光和保护膜痕迹不能仅凭照片归为刷新故障。

**可记录通过：真实 RGB 填色切换、正常横向 RGB 三条、LVGL 控件页面首次完整上屏。** Profile 页面静止不等于刷新卡死；持续动画、触摸、长期稳定性和冷启动仍需后续独立测试。当前没有因为照片而新增任何驱动修改或再次烧录。


## 27. 五分钟 LVGL 动态刷新验收（2026-09-24）

新增 `app/lcd_refresh` 和 NSH 命令 `lcdrefresh [seconds]`，默认 300 秒，显示移动色块、每秒计数及变色横条，不依赖触摸。`ESP32P4_LCD_REFRESH_TEST=y` 已加入 LCD 配置；启动颜色诊断改由 `ESP32P4_BOARD_LCD_BOOT_TEST` 控制，默认关闭，避免抢占 framebuffer。LCD 时序和 GPIO26 纯输出配置保持不变。

主机编译入口（助手脚本新建，脚本语法检查通过；本轮实际构建使用原 build.sh 命令）：

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_lcd.sh -j8
```

脚本建立 lcd_refresh 应用链接，并按配置→HAL 恢复→构建的顺序处理。配置变化可能清理 HAL，新增的 HAL 改动须先保存。

本次实际主机操作为 `python3 /tmp/p4-flash-only.py` 烧录、`python3 /tmp/p4-run-refresh.py` 采集；后者等待 NSH 和 framebuffer ready 后发送以下板端命令：

```text
lcdrefresh 300
```

实际结束日志：

```text
REFRESH COMPLETE seconds=300 updates=14999; visual confirmation required
release_cb: Done
nsh> free
... free 32801704 ...
nsh> uptime
00:05:01 up  0:05 ...
```

用户观察反馈“正常，不影响视觉观感”。结合 300 秒连续进度日志、无采集到的 framebuffer 更新错误/崩溃、正常退出并响应 NSH，本轮动态显示测试通过。14999 是应用更新循环次数，不是面板实测帧数；板级 framebuffer 保留分配，不能仅凭退出后 heap 数字判定内存泄漏。

证据：完整日志 [refresh_300s.log](lcd_hardware_20260924/refresh_300s.log)。本轮已烧录镜像、配置、构建/烧录日志及源码快照归档至工作区 `artifacts/lcd-hardware-validation/refresh-test/`，内有 SHA256SUMS。后续仅调整了测试源文件注释和格式，尚未重新烧录；不要假定当前源码快照的字节内容与最后格式调整完全相同。

当前显示基线可用于下一轮 GT911 探测和接入。尚未完成触摸、桌面、多次启动退出、冷启动统计和长期稳定性；USB 复位/重新枚举、ROM 摘要告警仍作为独立问题保留。本轮无 Git 提交或推送。


## 28. 接下来的实施计划：触摸 → 轻量桌面 → 开机进入（2026-09-24）

### 28.1 当前起点与阶段目标

当前已具备可恢复的显示基线：真实 framebuffer 颜色测试正常，LVGL Widgets 页面完整上屏，`lcdrefresh 300` 完成五分钟动态刷新并正常返回 NSH，用户确认视觉正常。保留 `artifacts/lcd-hardware-validation/refresh-test/` 中的固件、配置、源码快照和摘要；它是本轮验收基线，不等同于已提交的 Git 版本。

下一阶段目标是：**通电进入一个简单横屏桌面，点击图标进入普通 LVGL 页面，再通过返回按钮回到桌面。** 首版不依赖快应用、应用安装管理或完整系统 Launcher。快应用二进制和资源准备成果保留，等显示与触摸桌面闭环后继续接入。

### 28.2 按顺序实施与验收

以下为制定时的计划；GT911 探测与原始事件已有后续进展，见第 29～30 节。次数和时长是建议门槛，不是已完成结果。

| 顺序 | 任务 | 实施要点 | 完成标准 |
| --- | --- | --- | --- |
| 1 | GT911 硬件探测 | 复用 I2C1 的 SDA GPIO7/SCL GPIO8；读取芯片 ID 和配置，核对实际地址，候选通常为 0x5D/0x14；确认适配板复位/中断实际连接 | 保存实际地址、产品 ID、读取结果和错误日志；不能只凭地址 ACK 宣称触摸正常 |
| 2 | 原始触摸事件 | 对照现有 `gt9xx.c` 的读、poll 和按下/抬起语义；官方屏未配置独立 INT GPIO，需要实现合适的轮询通知或使用经核实的中断连接 | 输入节点可读取按下、连续移动、抬起；静止按住不被误判成连续点击，无触摸时不产生假事件 |
| 3 | LVGL 触摸接入与校准 | 接入 NuttX 触摸后端；以实际横屏方向确定交换轴及镜像，官方 BSP 的镜像参数仅供参考 | 点击中心、四角及边缘标记位置正确；滑动连续，松手后状态恢复；例如连续 20 次点击/拖动无明显漏报或卡住 |
| 4 | 轻量桌面 | 从已验收 LCD 配置派生独立桌面配置；一个图形主程序管理 LVGL，不同时运行 demo、颜色自检或另一个图形程序 | 桌面显示项目标题和 3～4 个图标；点击进入普通测试页，返回后可再次进入；不用假电量/假联网状态 |
| 5 | 开机进入桌面 | 明确显示、输入、应用初始化顺序；保留串口；桌面失败时输出明确日志，便于回到诊断流程 | 无需输入 NSH 命令即可进入桌面，输入和页面切换可用；建议多次软件复位和断电冷启动分别记录成功次数 |
| 6 | 固化桌面基线 | 保存配置、库及源码版本、镜像摘要、照片/视频和串口日志，补齐自动构建/应用链接流程 | 建议连续操作及显示至少 30 分钟，完成重复进出页面测试；形成可复现的开机桌面演示 |

制定本计划时第 1 步是立即开始的具体工作；当前探测已通过，最新下一步见第 30 节。不要同时改动 LCD 时序、背光路由和触摸方向；一次变更对应一个明确观察点。用户要求撤回的 GPIO26 输入采样/矩阵实验不应在后续任务中未经说明再次引入。

### 28.3 触摸适配需要特别核对的代码行为

现有 `nuttx/drivers/input/gt9xx.c` 可作为基础，但不能认为注册 `/dev/input0` 就完成接入：它的读取逻辑包含自动上报上次触点抬起和重复坐标过滤，需要结合定时读取方式验证按住、移动和抬起，必要时做正确的状态机适配。轮询方案应控制频率和 CPU 占用，避免持有长临界区或与显示共享资源发生阻塞。

显示已经通过五分钟测试，触摸问题应先在输入日志和坐标测试页定位。不开启无关外设，不通过修改显示时序修正输入坐标。

### 28.4 并行跟踪的可靠性问题

- **USB 串口复位/重新枚举：**按设备序列号定位端口，记录打开/关闭终端与复位的时间关系；区分主动烧录复位、监视器控制线影响和设备自主复位。验证期间保持一个串口所有者。
- **ROM SHA-256 告警：**分别核查 ROM 所见镜像头、Simple Boot 段布局和写入校验。Flash 写入 Hash 成功不能代替 ROM 摘要检查；不得以关闭告警冒充问题解决。
- **启动与内存：**五分钟正常不能替代冷启动或泄漏验证；比较同一配置下多次启动/退出的内存和任务状态。
- **来源可复现：**官方 LCD 改动同步 `openvela.patch` 与摘要校验，HAL 修改同步兼容补丁；配置切换前保留未固化修改。

这些问题不应被文档标记为已解决；进入“开机可靠桌面”验收前必须明确其结论或限制。

### 28.5 桌面之后再做

桌面及触摸验收后，再把其中一个图标接到快应用。先明确唯一 LVGL 所有者、应用显示区域、事件循环和退出恢复方式，不能直接从运行中的桌面叠加调用会再次初始化 LVGL 的 `vapp_main()`。内部运行时适配仍在内部完成，公开工程只接收产物。

后续再依次验证计算器资源与字体、KVDB 和数据目录、快应用启动与返回，最后按需求增加 RPK 安装/卸载、持久存储、PM/AM/WM 及完整 Launcher。

### 28.6 每步记录要求

每轮记录：源码/补丁状态、实际配置、构建结果、烧录镜像 SHA-256、主机和 NSH 命令、设备序列号、观察现象、次数/时长和下一项未完成工作。主机脚本与板端命令分开写；没有运行或没有视觉确认的步骤明确标为待验证。


## 29. GT911 实机探测通过与可见触摸测试入口（2026-09-24）

按用户提供的 [ESP32-P4X 官方指南](https://documentation.espressif.com/esp-dev-kits/zh_CN/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html) 核对屏幕与供电接法。沿用 GPIO7/8 的 I2C1、GPIO26 背光及 GPIO27 LCD 复位，未修改显示时序。新增诊断命令 `gtprobe`，用 16 位寄存器地址读取 GT911；现有 i2ctool 的 `-r` 仅支持 8 位寄存器地址。

实际串口结果：

```text
GTPROBE addr=0x5d ID=39 31 31 00 firmware=0x1060 resolution=1024x600 vendor=0x00
GTPROBE config_version=0x59 configured=1024x600 max_contacts=5 module_switch=0x0d status=0x80
GTPROBE addr=0x14 ID read failed errno=5
GTPROBE COMPLETE identified=1 result=PASS; touch events not tested
```

因此芯片通信已通过，地址为 0x5D；0x14 不响应不能视为该屏触摸故障。证据见 [gt911_probe.log](lcd_hardware_20260924/gt911_probe.log)，初始探测固件归档于工作区 `artifacts/lcd-hardware-validation/gt911-probe/`。

**旧命令 `python3 /tmp/p4-run-gtprobe.py` 仅发送 `gtprobe`，不启动 LVGL、不采集触摸事件。** 打开串口仍观察到 USB 复位，设备会回到启动画面；该脚本运行后点屏幕不会产生可见反馈。用户报告看到类似之前的割裂画面，尚不能据此判定显示驱动回归或认定它就是正常色带，需用下面的新测试页区分。

新增 `gtprobe 300` 可见诊断：独占一个 LVGL 实例，显示 `GT911 RAW TOUCH TEST`、PRESSED/RELEASED、原始坐标、DOWN/MOVE/UP 计数和触点圆点。采集新报告后清除 0x814E ready 状态；无新报告保留按住状态，不伪造松手。它尚不是 `/dev/input0` 驱动，也未接入 LVGL 输入设备系统；坐标方向待校准。一次只用一根手指测试。

主机编译（已执行成功，助手维护脚本）：

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_lcd.sh -j8
```

首次新增命令时出现 `gtprobe: command not found`：增量构建未刷新应用注册表。现已在构建入口加入显式 register，并检查最终镜像含 `gtprobe_main` 与日志字符串。envsetup 不兼容 nounset/errexit 的部分已在子 shell 初始化阶段处理。

本次可见测试镜像已烧录并通过写入 Hash 校验，归档于 `artifacts/lcd-hardware-validation/gt911-visible-test/`；SHA-256：`753a296a296bda07dc8ab62860903ba39eb902567597820c5fbdeeb02bd5daea`。

主机启动并保存日志（本轮实际使用；不要同时打开第二个串口程序）：

```bash
cd /home/mi/Developer/openvela
python3 contest2026_288_Bugyindudadui/app/lcd_refresh/tools/run_gttouch.py
```

该脚本按序列号 E8:F6:0A:E3:A9:17 找板，等待 NSH，发送板端命令 `gtprobe 300`。它不烧录。日志写入工作区 `artifacts/lcd-hardware-validation/gttouch-*.log`。已有 NSH 终端时可直接执行：

```text
gtprobe 300
```

待用户操作验收：轻点中心→静止长按约 3 秒→拖动→松开→四角点按。观察圆点、坐标及事件计数，记录是否镜像/交换轴。300 秒后退出；如需再测，重新执行命令。当前仅芯片 ID/配置探测可标记通过，可见页和手指事件须结合本轮日志与用户反馈后再验收。ROM 摘要告警及 USB 复位仍未解决。


### 29.1 手指方向相反：两轴镜像修正

用户已确认可触摸，但滑动方向与圆点相反。原始采集日志已经出现 DOWN、连续 MOVE、UP，证明触摸报告可读；尚不代表坐标方向通过。官方 BSP `bsp_touch_new()` 使用 `swap_xy=0, mirror_x=1, mirror_y=1`。现按零起点像素边界设置 `screen_x=1023-raw_x`、`screen_y=599-raw_y`，保留原始串口坐标，并在页面同时显示 screen/raw。未改 LCD 时序，也未写触摸配置。

方向修正镜像 SHA-256：`9206d80d004f083493351e2045aa00ffc269c0503a75f331e1a3c393baf98935`（以归档 SHA256SUMS 为准）。可见标题改为 `GT911 TOUCH TEST`，说明文字 `Screen mapping: mirror X + Y, no axis swap`。编译与 nxstyle 检查通过；首次烧录被上一轮串口采集占用阻止，确认并结束该采集进程后重新烧录。上一轮采集被提前结束，不能记录为完整 300 秒通过。

下一次验收：分别左右、上下滑动，圆点应与手指同向；点中心与四角检查位置，静止长按与松开检查状态。后续用户已反馈“现在好了”，方向修正通过本轮确认，详见第 30 节。此处仍为诊断应用，后续需接入 NuttX 输入驱动和 LVGL 输入设备。


## 30. 触摸跟手确认与下一阶段工作（2026-09-24）

### 30.1 最新实测结论

用户先确认“可以触摸”，随后报告滑动方向相反；加入 X/Y 两轴镜像并重新编译烧录后，用户确认“现在好了”。因此可以记录：**GT911 通信正常，原始触摸事件可读，诊断页方向修正后用户确认正常。**

方向修正后的串口日志阶段快照见 [gt911_mirror_user_confirmed.log](lcd_hardware_20260924/gt911_mirror_user_confirmed.log)。保存时已记录 2 次 DOWN、72 次 MOVE、2 次 UP，两次按下均有对应松开；这些是该快照内的计数，不是完整 300 秒统计，也不是全部验收动作计数。用户操作与日志共同支持本轮结果，尚未单独统计四角精度、20 次点击、长按稳定性或 300 秒结束结果。

当前固件基线位于工作区 `artifacts/lcd-hardware-validation/gt911-mirror-test/`，SHA-256 为 `9206d80d004f083493351e2045aa00ffc269c0503a75f331e1a3c393baf98935`。第 29 节编译入口和主机采集命令仍有效；板端 `gtprobe` 仅探测芯片，`gtprobe 300` 才启动可见触摸测试。测试正在采集时不要再打开第二个串口程序。

| 环节 | 当前状态 |
| --- | --- |
| LCD / framebuffer / LVGL 显示 | 颜色、控件页面及五分钟动态刷新已通过 |
| GT911 I2C 和产品配置 | 已通过，地址 0x5D，1024×600 |
| 原始触摸事件和圆点跟手方向 | 本轮通过，用户确认正常 |
| 标准 NuttX 输入节点与 LVGL 控件操作 | 后续已通过本轮用户确认，见第 31 节 |
| 简单桌面、页面进入与返回 | 待实现 |
| 开机自动桌面、多次冷启动 | 待实现和验收 |
| Quick App 真机启动 | 已有编译准备，待后续接入 |

### 30.2 下一步：让标准 LVGL 控件真正可操作

当前圆点由诊断程序直接读取 I2C 后移动，还没有形成 `GT911 → NuttX 输入节点 → LVGL 输入设备 → 控件事件` 的完整流程。立即开展的下一项工作是补齐这条输入路径，让按钮、开关和滑条接收正常的 LVGL 事件。

1. 基于现有 GT9XX 驱动实现适合本板的轮询及按下/移动/抬起状态处理，注册 `/dev/input0`。没有新报告时保留当前状态，避免把静止长按变成反复点击；总线错误需要记录且不可伪造点击。
2. 将已验证的 X/Y 镜像映射放在明确的一层，避免驱动与 LVGL 后端重复翻转。保留原始坐标诊断入口。
3. 接入 LVGL 的 NuttX 输入后端，制作按钮计数、开关和滑条测试页。只允许一个图形应用持有 LVGL，一个输入消费者读取 GT911；不同时运行 `gtprobe 300` 与控件测试。
4. 真机验收：一次点击计数加一，长按不会产生重复点击，拖动滑条连续，松手后立即恢复；补测中心、四角和边缘位置。建议记录 20 次点击/拖动，次数尚未完成。

### 30.3 随后：简单桌面与开机进入

控件操作通过后，派生独立桌面配置，显示项目标题和 3～4 个图标。首版图标进入普通 LVGL 页面，并提供返回桌面按钮；所有页面共用一个 LVGL 实例。完成反复进入/返回后，再配置开机自动启动并保留串口诊断。按原计划补做多次复位、断电冷启动和持续操作测试，保存固件、配置、照片/视频及日志。

桌面完成后再接计算器快应用。内部源码仍只在内部工程编译，公开 openvela 接收编译产物。Quick App 的 LVGL 所有权、事件循环和退出回桌面需单独设计，不从运行中的桌面直接重复初始化运行时。

USB 打开串口引发复位及 ROM SHA-256 告警继续保留为未解决项；当前跟手确认不覆盖这两项可靠性问题。本轮仅更新文档和证据，没有新增驱动修改、烧录、Git 提交或推送。


## 31. 标准 LVGL 控件交互验收与简单桌面计划（2026-09-24）

### 31.1 已完成的输入流程

用户确认“这一步已经验证了”。最终运行日志显示标准 NuttX 触摸节点已由 LVGL 打开，按钮、开关、滑条均产生实际事件：

```text
GT911 /dev/input0 ready; polling, mirror X/Y
touchscreen /dev/input0 open success, maxpoint 1
CONTROLS START seconds=300 input=/dev/input0
CONTROLS CLICK count=6
CONTROLS SWITCH state=OFF changes=4
CONTROLS SLIDER value=79 changes=39
```

以上为不同时间的结果摘录。完整日志见 [touch_controls_confirmed.log](lcd_hardware_20260924/touch_controls_confirmed.log)。本轮可记录 **GT911 → NuttX `/dev/input0` → LVGL 输入后端 → 标准按钮/开关/滑条交互通过用户确认**；6/4/39 是日志中的点击数、开关变化数、滑条值变化数，不是预设测试次数。用户要求提前退出，未等待 `CONTROLS COMPLETE`，不能记作完整 300 秒或长期稳定性通过。四角精度、长按次数与重复进入退出仍需专门统计。

板级新增 `src/esp32p4_touch.c`，复用 NuttX touchscreen upper half 的事件队列、read/poll 和 `TSIOC_GETMAXPOINTS`。独立内核任务轮询 GT911，仅发布一个主触点；没有新报告时保留状态，X/Y 镜像仅在板级转换一次。开启 `ESP32P4_BOARD_TOUCH`、`INPUT_TOUCHSCREEN` 和 `LV_USE_NUTTX_TOUCHSCREEN`。旧 `gtprobe` 在 `/dev/input0` 存在时拒绝读取，避免争用芯片报告。没有修改通用 GT9XX 驱动，也没有改 LCD 时序。

### 31.2 本轮修复与限制

第一次标准控件固件虽然注册了 `/dev/input0`，但日志出现 `CONTROLS FAIL: framebuffer or input missing`，原因是 LVGL 沿用触摸未启用时的增量对象。清理 LVGL 对象后，共用应用 archive 被清除，又暴露其他应用 `.built` 标记未失效导致的链接缺符号；使应用归档标记失效并重新构建后，最终 ELF 已包含 `lv_nuttx_touchscreen_create`，真机成功打开设备并收到控件事件。

早期固件还出现 `GT911 transfer error=-5`。新增 status/points/ack 分阶段日志后，最终本轮交互日志未再采集到该错误，但尚未独立证明其根因或长期消失，不记为已经彻底修复。持续总线错误时驱动目前保留按下状态，后续仍需设计异常恢复；这轮未进行故障注入。轮询任务持续运行，省电启停、多点触摸不在本轮验收内。USB 控制线复位和 ROM 摘要告警仍保留。

新增驱动与测试应用 nxstyle 检查通过，最终构建成功。仓库整体 `git diff --check` 仍报告既有 `openvela.patch` 中的三个空白行问题，不将整仓检查宣称为通过。

### 31.3 固件、命令与退出方式

验收镜像归档：工作区 `artifacts/lcd-hardware-validation/touch-controls-confirmed/`，含配置、构建日志、烧录日志、交互日志及源码副本。

```text
SHA-256: d1d7cba1d16f3b72f7a112b0f2c1ad45e2104164546cd4878ec5a9b480a32b41
```

主机编译：

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_lcd.sh -j8
```

最终成功构建日志为 `/tmp/p4-controls-build3.log`，已归档。变更 LVGL 编译选项后需确认相关对象重新编译；仅看到链接成功不足以证明选项已生效。

主机启动交互测试并保存日志：

```bash
cd /home/mi/Developer/openvela
python3 contest2026_288_Bugyindudadui/app/lcd_refresh/tools/run_touchcontrols.py
```

已有串口 NSH 会话时直接执行以下板端命令，二者选其一，不重复打开串口：

```text
touchcontrols 300
```

该测试正常计时结束会清理 LVGL 并返回 NSH。本轮用户要求提前退出，而当前程序没有提前退出按钮，控制台也未启用 Ctrl+C 信号；因此先结束本轮主机采集进程，再执行以下主机命令复位（不写入 Flash）：

```bash
/home/mi/.local/bin/esptool --chip esp32p4 --port /dev/ttyACM0 run
```

端口必须按设备序列号确认；已执行复位退出，本次不等同于应用自然退出或资源释放验收。退出后未再启动图形程序；只读日志已确认 `GT911 /dev/input0 ready` 后出现 `NuttShell (NSH)` 和 `nsh>`，见 [退出后的启动日志](lcd_hardware_20260924/touch_controls_exit.log)。采集已结束，串口已关闭。

### 31.4 下一步：一个能进入和返回的简单桌面

下一项目标是：**先手动启动桌面，点击图标进入普通页面，再点击返回回到桌面。** 在这个流程验证之后，才配置开机自动进入，便于分开定位页面问题和启动问题。

1. 从已验收配置派生独立桌面配置，保留当前控件测试固件作为回退基线；固化应用链接和构建入口。
2. 一个 LVGL 程序管理所有页面。桌面放项目标题及 3 个入口，例如“控制”“设备信息”“触摸测试”；设备信息读取真实数据，不显示虚构联网/电量。
3. 点击入口切换普通 LVGL 页面，每页提供返回按钮。增加串口诊断可用的退出方式；退出须清理 LVGL 输入与显示对象，避免只能复位离开。
4. 验收桌面→页面→返回，连续多次操作无重复点击、无触摸丢失，退出后可再次启动。记录事件、内存与任务状态，确认不重复初始化 LVGL。
5. 上述通过后配置开机启动：初始化显示和输入后启动桌面，保留 NSH；分别统计复位与断电冷启动结果，再做持续运行测试。

本轮不启动 Quick App 集成。桌面导航和开机启动稳定后，再把一个图标接到计算器快应用，明确运行时与桌面的 LVGL 所有权及退出恢复方式。内部源码继续只在内部工程构建，公开工程接收产物。


## 32. 独立桌面、锁屏与设置实现（2026-09-24，实现及调试记录）

用户要求默认滑动解锁，设置可关闭锁屏或改用 PIN，通电自动进入对应页面，
断电熄屏；文件分模块并在目录根部写 README。应用放在 `app/desktop/`，
根部 [README](../../app/desktop/README.md) 包含项目树、各模块职责与命令。
板级配套为 `configs/desktop/defconfig`、`tools/build_desktop.sh` 与
`src/esp32p4_desktop_storage.c`。桌面页面不依赖内部源码或 Quick App。

### 32.1 当前实现

- 中文横屏界面：滑动锁屏、桌面三个入口、分栏设置、设备信息和控制示例。
- 默认滑动；设置提供无需解锁、滑动、6 位 PIN。设置 PIN 两次确认，
  更改已有 PIN 或关闭它前验证旧 PIN；连续输错后等待。PIN 为界面锁，
  不等于加密存储或串口访问控制。
- `desktop_boot_main` 初始化 NSH 并启动桌面任务；保留控制台。设置中的
  “关于桌面 → 退出桌面”正常释放输入与显示，NSH 可用 `desktop` 重启。
- 字体使用公开 Noto Sans CJK SC 子集，来源与 OFL 许可证在 assets 中。

### 32.2 设置存储

最初候选 0x380000 区域发现已有数据，程序拒绝格式化；已只读备份，未擦除。
后续只读确认 Flash 尾部 `0xF80000..0xFFFFFF` 共 512 KiB 全为 0xFF，
将该区设为独立 LittleFS 分区挂载 `/data`，镜像配置改为实测 16 MiB。
首次只允许格式化整区全空白的分区，已存在但损坏的卷不自动重建。

设置 `/data/desktop/settings.bin` 用临时文件、fsync、rename 提交，含版本、
解锁模式、随机盐、迭代 SHA-256 PIN 摘要及校验。读取失败显示错误页，
不直接进入桌面。日志已证实分区挂载成功、默认滑动设置保存后在下一次复位
读回 mode=1；用户修改模式后的持久性和 PIN 仍待专项验证。

### 32.3 锁屏/图标无响应的定位

用户先报告锁屏能显示但不能滑动，随后能进入桌面但图标点不了。硬件日志
出现 `I2C1 message=0 error=0x400`（NACK）。检查发现 `esp_i2c_sendstart()`
的命令结构未清零，`ack_exp` 没设置；接收命令也包含未初始化字段。已将
restart/write/read/end 命令完整初始化，显式设置 ACK 期望为 0。此缺陷与
调用栈变化后时好时坏的触摸表现一致；没有改变触摸镜像或 LCD 时序。

修复版实际日志已记录：滑动解锁→桌面→锁屏→再次解锁→设置→返回→控制中心。
本轮这些操作期间未再采集到 ACK 错误，长期稳定性尚不宣称通过。
HAL 的 `nxsched_usleep` 兼容问题改为 `nxsig_usleep`，已同步
`esp-hal-openvela-compat.patch` 并通过反向应用检查。

### 32.4 构建、产物与待完成验收

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_desktop.sh -j8
```

固件从 0x2000 烧录，构建入口检查不覆盖设置分区。当前已烧录修复版归档
`artifacts/desktop/i2c-ack-fixed/`，SHA-256：
`a04ef08ca77e7c87f537034c670dc41ffab6d62ce01c095d161013be7a91230e`。
开机自动运行，无需 `gtprobe` 或 `touchcontrols`。临时连续采集入口为
`python3 /tmp/p4-desktop-monitor.py`；它只读串口，但打开端口仍可能复位。

本节调试时列出的验收项如下；用户后续总体完成确认见第 33 节，逐项测试日志尚未全部补齐：
关闭锁屏后复位直接桌面、恢复滑动、设置测试 PIN、错误
密码拒绝与正确密码进入、旧 PIN 验证后关闭锁屏、退出再启动、物理电源开关
关机熄屏。主板与屏幕独立 USB 供电，最后一项必须由用户观察确认。
ROM 摘要告警和 USB 复位仍未解决，不能把当前桌面首次运行等同于最终完成。


## 33. 桌面阶段完成确认与交接（2026-09-24）

### 33.1 完成状态与证据范围

用户在桌面交互修复后反馈“好了”，随后明确“现在都做好了，更新文档”。
据此将本轮 **独立桌面、锁屏与设置第一版** 标记为用户总体确认完成。
这次只更新文档与日志快照，没有重新编译、烧录、复位，也没有变更用户的锁屏设置。

| 功能 | 实现/确认情况 | 已保存证据 |
| --- | --- | --- |
| 开机自动进入锁屏 | 默认滑动模式已实现；用户确认锁屏出现 | 启动日志 `settings load=0 mode=1`、`PAGE lock`、`READY` |
| 滑动解锁与再次锁定 | 用户明确确认能进入桌面 | 多次 `swipe unlock`、`PAGE home`、`PAGE lock` |
| 桌面图标和页面返回 | I2C 修复后交互正常，用户总体确认完成 | 设置、设备信息、控制中心进入及返回日志 |
| 平板式分栏设置 | 已实现，无需解锁/滑动/PIN 三种方式 | 页面代码及设置页进入日志 |
| PIN 与锁屏方式持久保存 | 已实现，纳入用户总体完成确认 | 默认滑动模式跨复位读回有日志；各 PIN 分支及模式切换后的重启未单独留存完整日志 |
| 物理开关开机/熄屏 | 纳入本轮目标和用户总体完成确认 | 助手未单独观察断电熄屏，未保存独立视频或逐次冷启动记录 |
| 退出及重新启动 | 已提供设置内退出与 NSH `desktop` 入口 | 不据总体确认推算重复退出次数或内存稳定性 |

保留证据边界：用户总体完成确认不等于助手逐项执行了所有测试。
不虚构 PIN 测试次数、断电次数、持续运行时长；ROM 摘要告警与 USB 打开串口
引发复位仍是已知未解决问题。此前的 I2C ACK 随机字段缺陷已修复，后续长期
稳定性仍需独立统计。

### 33.2 文件位置、构建与使用

应用根目录：`app/desktop/`，项目树与文件职责见
[桌面 README](../../app/desktop/README.md)。页面、设置/PIN、字体和启动入口
分文件维护；板级存储及驱动在 `board/contest_board/`。

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_desktop.sh -j8
```

编译当前产物为工作区 `nuttx/nuttx.bin`，以后会被其他构建覆盖。
本轮已烧录的桌面修复镜像保存在：

```text
/home/mi/Developer/openvela/artifacts/desktop/i2c-ack-fixed/nuttx.bin
SHA-256: a04ef08ca77e7c87f537034c670dc41ffab6d62ce01c095d161013be7a91230e
```

通电自动运行桌面，无需输入测试命令。默认滑动解锁；用户更改过设置后，以
`/data/desktop/settings.bin` 中保存的方式为准。设置区位于 Flash 尾部
`0xF80000..0xFFFFFF`，常规固件烧录不能覆盖或擦除该区。
退出入口为“设置 → 关于桌面 → 退出桌面”；退出后在 NSH 执行 `desktop`
重新启动。不要同时运行其他 LVGL 测试程序。

本次文档更新时保存的交互快照：
[desktop_completion_snapshot.log](lcd_hardware_20260924/desktop_completion_snapshot.log)。
快照包含多轮启动和调试历史；应按最后一次启动段判断修复版结果，不能把早期
NACK 错误归到后续修复版。归档目录里的原始镜像和 SHA256SUMS 保持原样。

### 33.3 下一阶段

桌面已作为接下来工作的基础。先整理可复现的演示记录：开机、解锁、图标进入
与返回、锁屏方式切换、重新开机、关机熄屏；同时补齐少量冷启动和持续操作统计。
这些属于证据与可靠性补齐，不重复从 LCD 点屏开始。

之后进入 **桌面启动一个计算器快应用并返回桌面**：先明确桌面与 Quick App
运行时的 LVGL 所有权、输入归属、事件循环和退出恢复，再连接一个真实应用入口。
保持内部源码不拷入公开仓库，只接收内部编译产物。Quick App 真机运行与返回
目前尚未完成，本轮文档更新不将它标为通过。


## 34. 主板电源开关重启黑屏：重新打开启动验收（2026-09-24）

用户在第 33 节总体完成确认后报告：只关闭/打开主板电源开关，屏幕 USB 等
其余连接保持不变，重新上电后只有背光、没有画面。助手打开串口触发
`CHIP_USB_UART_RESET` 后，日志显示存储挂载、LCD、GT911 与桌面初始化正常，
到达 `DESKTOP PAGE lock mode=1` 和 `DESKTOP READY`；用户明确确认画面已恢复。

**结论：交互功能的既有结果保留，但“拨主板开关后可靠自动出图”未通过，
不能将第 33 节的总体确认作为电源开关/冷启动问题已经解决的证据。**
屏幕持续供电时的主板重启与 USB 复位不同，目前尚无无扰动的故障启动日志，
根因未确定，不直接归咎于供电或屏幕时序。尚未为此修改显示参数或重新烧录。

下一步：保持串口采集，用户复现只拨主板开关；记录断开/重新连接和 ROM reset
原因，分清实际电源启动与主机打开串口引起的二次复位。若二次复位掩盖故障，
需增加可在故障后读取的启动诊断或采用独立串口采集，再决定修复位置。
临时入口 `/tmp/p4-power-cycle-monitor.py`，日志写入工作区
`artifacts/desktop/power-cycle-*.log`；该脚本不写 Flash，但打开串口可能复位。


### 34.1 串口读取依赖与修复候选

用户确认：打开串口后画面恢复；连续采集开启时只拨主板电源开关，能直接显示
锁屏。这提示是否有 USB 主机读取会影响启动。代码确认 `esp_usbserial_write`
原先无限等待硬件 TX FIFO，可阻塞早期 syslog；正常应用 printf 也可能在
64 字节软件发送队列满时等待 TX 中断。未打开串口读取时，这两条路径均存在
阻塞风险。根因关联需要关闭监视器后的实测确认，不以开启采集的成功代替。

新增 `ESP32P4_USB_CONSOLE_BEST_EFFORT`，只由桌面配置选择：发送软件队列主动
排空；硬件 FIFO 最多等待约 1 ms，超时后丢弃无法发送的日志，FIFO 可用时
恢复发送。接收/NSH 保留。代价是电脑不读取或读取过慢时日志可能缺失。
没有改 LCD 时序、背光 GPIO 或设置数据。

修复候选已编译烧录，Flash Hash 校验成功，烧录后串口保持关闭。归档：
`artifacts/desktop/standalone-console-fix/`，SHA-256：
`96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4`。
待验收：不开监视器，用户只拨主板开关重复验证锁屏与触摸；随后检查串口
仍能接收命令。此处为烧录后待验收状态，后续开关出图确认见第 34.2 节。


### 34.2 关闭串口后的多次主板开关验证通过（2026-09-24）

烧录 USB 发送有界等待修复版、关闭助手串口采集后，用户反馈：
“我连续打开了好几次，成功再次显示锁屏”。据此记录：**本轮不依赖串口
监视器读取、只拨主板电源开关的重复启动出图验证通过用户确认。**

测试条件为屏幕继续独立 USB 供电、其他接线保持不变，主板电源开关反复
关闭再打开。具体次数与每次断电时长未统计，不虚构为固定次数。用户本次
明确确认的是锁屏再次显示，不据此新增触摸、PIN、整套双电源断电启动或
关机熄屏的独立验收结果。之前已有的交互验证继续保留。

对比修复前不开监视器黑屏、开监视器恢复，以及修复后不开监视器多次出图，
结合代码中存在的 USB TX 无界等待，本轮证据支持该阻塞是本次故障原因。
本项可关闭为“在上述条件下修复并验证”，长期运行、更多启动组合仍独立跟踪。
ROM 摘要告警与打开 USB 串口可能引起复位并未被本次修复解决。

**最新已验收启动镜像：**

```text
/home/mi/Developer/openvela/artifacts/desktop/standalone-console-fix/nuttx.bin
SHA-256: 96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4
```

此镜像取代 `i2c-ack-fixed` 作为当前桌面启动基线；旧镜像保留用于追溯。
本轮只更新文档与用户确认记录，没有重新烧录、复位、打开串口或更改锁屏设置。
后续可以继续桌面与计算器快应用生命周期集成；同时补齐演示视频及长时间
运行统计，不再将“必须打开串口才能启动”当作正常使用步骤。


## 35. 当前固件确实运行在 openvela 上的证据（2026-09-24）

详见 [openvela 适配证据报告](openvela_adaptation_evidence.md)。已核对 manifest
来源、NuttX 内核启动链、标准设备/文件系统接口、最终 ELF 符号，并由当前 ELF
重新生成镜像，与已烧录 standalone-console-fix 镜像逐字节一致。证据归档在
工作区 `artifacts/desktop/openvela-proof/`。本轮没有连接串口、烧录或复位。

准确结论：公开 openvela/NuttX 在 ESP32-P4 上运行，底层复用并适配 Espressif
HAL/官方 LCD；桌面使用 openvela 的 LVGL NuttX 后端。当前完成的是 custom
board/chip 与桌面适配，尚不代表 Quick App 真机与完整 Launcher 已完成，也
不声称已合入官方主线。esptool 和 ROM 的 ESP32 字样是芯片工具/启动信息，
不决定上层运行的操作系统。


## 36. 跌倒监护 RPK 与第一步资源/服务验证（2026-09-24）

用户提供 `com.openvela.fallguard.debug.0.1.0.rpk`，已保存到
`quickapp/fall_guard/delivery/`，SHA-256 为
`7a9c2419f016c5619f38d6210db686a9018bd90df7c8925f8d293ec748db1616`。
包名 com.openvela.fallguard，designWidth=1024，包含三个页面和视频占位图片，
没有字体。独立 fallguard 配置已接入解包 JS 与公开 Noto 字体子集；原始包不改，
尚未验收 RPK 安装/签名。运行时支持读取 JS，无需先转换字节码。

内部专用 RV32 导出配置补编 LVGL 触摸后端，并保留 FB_UPDATE；只导出
liblvgl.a，未拷出内部源码或头文件。测试固件链接通过，包含 qastart、vapp、
kvdbd、qa_resource_start 和 lv_nuttx_touchscreen_create。

用户要求一步一步推进，因此第一步仅执行 `qastart --prepare`，不启动页面。
首次笼统的超时已通过公开包装层诊断缩小为：

```text
QA resources copied; starting KVDB
QA KVDB entered pid=6
QA KVDB returned result=2 errno=2
QA KVDB diagnostic pid=6 state=2 result=2 errno=2
QA path=/tmp/kvdbd stat_errno=2
QA path=/data/persist.db stat_errno=2
QA path=/etc/build.prop size=21 mode=100777
Quick App preparation: I/O error
```

结论：资源复制已成功；服务创建后立即返回，尚未达到共享区就绪。
errno=2 提示路径不存在，但不能仅凭最终 errno 判定具体失败调用；下一步
需要服务内部错误日志来区分 socket bind、共享文件 open 或持久库初始化。
已核对导出库中的路径包含 /etc/build.prop、/tmp/kvdbd、/data/persist.db。

日志归档工作区 `artifacts/fallguard/kvdb-step1/`。此步骤尚未通过，不能认为
快应用已运行或 RPK 有问题。采集完成后恢复已验证桌面固件
`standalone-console-fix/nuttx.bin`，不会保留无锁屏的独立测试固件给用户日常使用。
后续顺序：KVDB 准备通过 → 应用首次出图 → 触摸/路由 → 桌面启动与返回。


### 36.1 第一步通过：资源与 KVDB 共享区就绪（2026-09-24）

继续定位发现内部库与公开 NuttX 的 open 标志 ABI 不同：内部 O_CREAT=0x40、
O_RDWR=2、O_CLOEXEC=0x80000；公开系统分别为 0x04、3、0x400。
实际导出 libframework_utils.a 的 kvdb_shm_init 反汇编显示调用 open 时传入
0x80042。公开系统未将此识别为创建文件，因此 /tmp/kvdbd 不存在，符合
先前服务返回 result=2 / errno=2 的证据。

新增公开适配 `app/quickapp_prebuilt/qa_file_abi.c`，将指定库的 open 引用
重命名为 qa_prebuilt_open 后转换标志。只对 fallguard 配置里的
libframework_utils.a 和 libunqlite.a 生效，原始导出库保持不变，派生库位于
`prebuilt/fallguard-abi/`，生成脚本记录源与派生哈希。未修改公共 open 定义，
未拷入内部源码或头文件。本地测试验证访问模式、创建、截断/追加、非阻塞、
未知标志拒绝，nxstyle 通过。

本轮执行且通过的板端命令：

```text
qastart --prepare
QA resources copied; starting KVDB
QA KVDB entered pid=6
QA open failed path=/dev/urandom source=0x80000 target=0x401 errno=2
Quick App resources and KVDB ready; /data is volatile.
nsh>
```

证据：[fallguard_kvdb_ready.log](lcd_hardware_20260924/fallguard_kvdb_ready.log)。
通过范围为资源准备和 KVDB 共享区 magic 就绪；未执行真实属性读写往返，
未验证长期服务循环，未启动快应用 UI。/dev/urandom 缺失提示仍待处理，
不把可选失败忽略为所有依赖均正常。

固件/配置/源码与日志归档：工作区 `artifacts/fallguard/kvdb-ready/`，镜像
SHA-256：`2157e30116543486b00fd0cd5244fda9f3042c133bd778a7f952e78b21729249`。
按照一步一步验证的约定，本轮完成后恢复已验证桌面固件，不把测试配置留作
用户日常启动镜像。

下一步：检查其余运行库的文件操作 ABI 和随机设备依赖，再验证跌倒监护
首次加载/上屏。当前 open 转换仅覆盖 KVDB 相关两个库，不能宣称全部 RV32
运行库与公开内核 ABI 一致。桌面图标启动与返回仍在后续阶段。


## 37. 官方公开快应用桌面启动：调研结论、方案与 AI 交接（2026-09-27）

**本节直接续写于原总报告，是当前交接依据。下一位 AI 只需阅读本文件第 0 节和第 37 节，再按需回查历史证据，不需要另找交接文档。**

此前 P62 调研和独立 vapp 测试为阶段历史；本节明确最终目标、官方公开依据、推荐方案和未解决问题。推荐方案尚未实施，不代表已经生成桌面与快应用组合固件。

### 37.1 用户最终目标与不可改变的约束

最终演示必须在**同一份固件**中完成：

```text
打开主板电源 → 锁屏 → 解锁 → 桌面显示“跌倒监护”图标
→ 点击图标 → 真正运行 com.openvela.fallguard → 返回桌面
```

用户希望方案简单，不要求应用商店、动态安装卸载、多应用后台，但明确拒绝
把“两份固件切换演示”或“只在串口输入 qastart”当作最终结果。

- 启动管理方案优先采用**官方公开 open-vela 仓库**的代码和接口。P62 私有
  Launcher/proxyquickapp 不作为本轮官方开源方案的依据。
- 内部源码 `/home/mi/2t/p62-dev` 不拷到公开工程。已使用的内部 RV32 库必须如实
  说明来源；如需调整，内部构建、只导出产物。
- 保留已验证桌面源码、配置、固件及尾部设置分区。不能直接从当前桌面按钮
  调用会再次初始化 LVGL 的独立 vapp_main。
- 用户要求一步一步验证；明确测试固件是否会暂时没有桌面。没有真机验收就不
  宣称应用上屏、返回或组合固件完成。

### 37.2 官方公开证据：已核实的事实

2026-09-27 实际从 GitHub 官方 open-vela 组织下载核验了 README、Launcher、
QuickActivity、vapp 和 PackageParser；同时阅读了本机公开工作区对应源码。
下面链接的 dev 分支可能后续变化，表内 HEAD 为本机此次核对版本。

| 官方项目/源码 | 本机路径或 HEAD | 关键事实 |
| --- | --- | --- |
| [QuickApp README](https://github.com/open-vela/frameworks_runtimes_quickapp/blob/dev/README.md) | `frameworks/runtimes/quickapp`，`4eecf624f1c8c7b0f17eb72918619da6f416d41a` | 官方提供 vapp 独立模式和 vappxms 系统集成模式；核心以预编译库发布 |
| [Launcher HomeActivity](https://github.com/open-vela/frameworks_runtimes_services_xmsdemo/blob/dev/launcher/src/HomeActivity.cpp) | `frameworks/runtimes/services/xmsdemo`，`2bbd05996709e7d7e70f0e0d1732019c4773af0d` | 查询包列表，点击入口后构造 Intent 并调用 startActivity |
| [PackageParser](https://github.com/open-vela/frameworks_runtimes_services_pm/blob/dev/src/PackageParser.cpp) | `frameworks/runtimes/services/pm`，`380cd048f3618385c845d9cea598c6170520786a` | 快应用默认执行文件 vappxms，默认入口 QuickActivity |
| [QuickActivity](https://github.com/open-vela/frameworks_runtimes_quickapp/blob/dev/shell/xms/quick_activity.cpp) | `frameworks/runtimes/quickapp/shell/xms/quick_activity.cpp` | 接入系统主循环、窗口根对象；转发显示/隐藏/返回，销毁时停止并等待 UI 清理 |
| [System Server](https://github.com/open-vela/frameworks_runtimes_services_system_server) | `frameworks/runtimes/services/system_server`，`2f41945917478cae568b09b8c5e8ad500f1ff889` | 提供应用、包、窗口等服务；SYSTEM_SERVER 配置依赖 Binder 与 libuv |
| [独立 vapp 入口](https://github.com/open-vela/frameworks_runtimes_quickapp/blob/dev/shell/vapp/main.cpp) | `frameworks/runtimes/quickapp/shell/vapp/main.cpp` | 自行初始化 LVGL/设备与事件循环，适合独立诊断，不可直接重入运行中的桌面 |

本机 `apps/frameworks/runtimes/quickapp` 与 `frameworks/runtimes/quickapp` 是工作区
映射路径，不要将它们误当成两套完全独立源码。

**开源范围必须准确表述**

官方公开的是 Launcher 示例、系统应用/包/窗口管理代码、QuickApp Shell
集成层及 API 头文件；README 明确核心 quickapp、gui_wrapper、quickappfeatures、
quickapp_inspector 以预编译静态库发布，不包含全部核心源码。

可以说“基于官方公开的启动管理与集成代码”，不能说“整套快应用核心完全开源”。
现有 RV32 核心由内部源码编译导出，这一点不因选择官方 XMS 路径而改变。
尚未确认官方公开发布物中有能直接替换当前内部产物的兼容 RV32 核心套件。

### 37.3 官方系统集成启动链

```text
Launcher: PackageManager.getAllPackageInfo()
  → 展示应用入口（官方示例是名称按钮，可改成我们自己的图标）
  → 点击：Intent(packageName) + startActivity(intent)
  → 系统应用/包管理解析目标
  → 快应用 execfile=vappxms，entry=QuickActivity
  → QuickApplication 注册 QuickActivity
  → QuickActivity::onCreate()
      使用系统 main loop 和窗口 root 创建 ShellApp
  → 快应用显示/路由/返回/退出
```

源码确认的生命周期：

| 动作 | 官方 QuickActivity 行为 |
| --- | --- |
| 创建 | MessageLoop_CreateForUV；ShellApp::Create；setUILoop；绑定 getWindow()->getRoot() |
| 恢复 | show 或处理新 Intent 路由 |
| 停止显示 | hide |
| 系统返回 | backpress |
| 应用请求退出 | Observer 收到 QAPP_EVENT_REQ_EXIT 后调用 stopApplication |
| 销毁 | stop → waitUIDestroy → release/reset → 销毁 message loop 和 GUI 上下文 |

这是公开可读的参考路径，不是本板已完成的能力；仍需核对依赖、线程和资源清理。
官方示例并未自动提供我们的横屏图标布局、锁屏设置或视频功能。

### 37.4 当前推荐方案（尚未实施）

以**官方 XMS + vappxms/QuickActivity**作为系统集成路径，保留当前桌面的
外观与功能。先做一个内置应用，避免同时扩大到应用商店/后台管理。

1. 盘点本板启用 XMS 的最小配置：Binder、libuv、系统应用/包/窗口服务、
   应用运行库与 QuickActivity。SYSTEM_SERVER_LITE 是公开可见的单实例渲染
   选项，但尚未验证本板适用，不能先宣称已选定或可直接用。
2. 建立独立 `desktop_quickapp` 组合配置，保留原 `desktop` 回退配置。
3. 将现有桌面接入系统 Activity/窗口生命周期，借鉴 xmsdemo Launcher 的
   查询/Intent 启动方式，增加跌倒监护图标。不是在当前 C 回调中直接粘贴
   startActivity；当前桌面还不是 XMS Activity，需要适配。
4. 注册跌倒监护包，让包管理能解析到 vappxms 和 QuickActivity。先核对官方
   PM 的实际资源布局/注册与安装要求，不凭 README 的简略命令猜测安装步骤。
5. 统一一套匹配的 LVGL 和设备管理。现有公开桌面与内部运行库的 LVGL 提交
   不同；需明确头文件、库与配置一致，不能盲目链接两套图形实现。
6. 保留 LittleFS `/data` 和 `/data/desktop/settings.bin`。组合路径不使用
   qastart 的 TMPFS 覆盖 /data；应用资源与用户数据另建目录。
7. 保持应用内容区 1024×600，接好系统返回/回桌面控件、启动失败处理和重复
   启动保护。退出完成再恢复可操作桌面。

这是推荐方向，不是完成承诺或已经生成的组合固件。如果依赖盘点表明 XMS
对当前板级成本明显过大，应先向用户说明取舍，再考虑基于官方公开 Shell/API
写精简宿主；不能静默改回 P62 私有管理方案。

### 37.5 当前实际工程与阻塞

**已验证桌面**

- 源码：队伍仓库 `app/desktop/`。
- 配置：`board/contest_board/configs/desktop/`。
- 固件：工作区 `artifacts/desktop/standalone-console-fix/nuttx.bin`。
- SHA-256：`96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4`。
- 显示、触摸、桌面交互和不开串口的多次主板开关出图已获用户确认。

**跌倒监护应用与独立验证**

- 完整应用源码目前在 `/home/mi/Developer/AIOT-RPK/openvela`。
- 原始 RPK 已保存：`quickapp/fall_guard/delivery/com.openvela.fallguard.debug.0.1.0.rpk`。
- RPK SHA-256：`7a9c2419f016c5619f38d6210db686a9018bd90df7c8925f8d293ec748db1616`。
- 应用设计宽度 1024，视频只预留、真实跌倒识别和通知未接入。
- `qastart --prepare` 已通过资源及 KVDB 共享区就绪检查。
- 内部/公开 open 标志 ABI 差异已有转换，后续扩到 openat/fcntl/pipe2；随机
  设备已加入测试配置。这不是全部运行库 ABI 已认证。
- 后续执行 qastart，用户看到白屏。最后完整日志停在 `QA_STAGE bundle_create enter`
  之后，尝试打开 base.rpk 失败；加载器源码存在解包目录回退，不能凭这条错误
  就归因于包缺失，更不能宣称已执行到页面脚本。
- 已额外编译 Bundle 精细诊断版，但没有对应的新运行日志确认更细停点。
  改用 XMS 不会自动消除此运行时阻塞，必须继续定位。

**当前板子与构建产物必须重新确认**

本次更新仅整理本地文档，没有连接板子。此前最后核对的烧录日志显示 Bundle 诊断固件
已写入成功，之后未查到更新的桌面恢复记录。不能直接断言板上仍是稳定桌面。
`nuttx/nuttx.bin` 是可被构建覆盖的当前产物，不等同于桌面备份。
执行任何烧录前先核对固件摘要、配置与串口，不能把测试固件当作组合固件。

### 37.6 下一位 AI 的执行顺序和验收门槛

| 顺序 | 工作 | 验收 |
| --- | --- | --- |
| 1 | 核对最后烧录/运行记录，定位 Bundle 白屏 | 应用创建继续，真实首页出现 |
| 2 | 列出官方 XMS 最小依赖和缺失产物 | 可复核配置/库清单，明确改动范围 |
| 3 | 组合配置接入系统窗口与桌面 | 现有锁屏、设置和触摸不退化 |
| 4 | 应用登记、图标与 Intent 启动 | 点击图标出现真实快应用首页 |
| 5 | 返回、退出、重复启动和失败恢复 | 回桌面后可操作、能再次启动，无残留实例 |
| 6 | 固化演示 | 同一固件开机→图标→应用→返回；记录真实次数和日志 |

调试可分层，但最终不接受两份固件切换替代图标启动。若测试固件暂时没有
锁屏，要先明确告知；测试后恢复约定的桌面基线并核对写入结果。

### 37.7 原飞书文档与本次更新范围

## 38. 组合固件推进记录（2026-09-27）

### 38.1 用户确认与推进方式

用户已重新烧录并确认 `standalone-console-fix` 桌面基线在板上生效。桌面此前已经多轮验证，本阶段不重复进行完整桌面验收；该镜像作为随时可恢复的回退基线。

本阶段采用逐步推进方式：每完成一个实际操作，立即记录命令、产物、SHA-256、真机现象和回退方法；任何组合/诊断镜像烧录前先保存桌面基线信息，失败后恢复桌面镜像，不把诊断固件当作日常固件。

### 38.2 第一步：只读核对三套配置（已完成，未改代码、未烧录）

当前三个相关配置的职责已经确认：

| 配置 | 当前作用 | 关键选项 |
| --- | --- | --- |
| `configs/desktop` | 已验证桌面基线 | `ESP32P4_BOARD_LCD=y`、`ESP32P4_DESKTOP=y`、LVGL、GT911 由桌面板级入口承接 |
| `configs/quickapp` | 独立 Quick App 运行时镜像 | `ESP32P4_QUICKAPP_PREBUILT=y`，没有 `ESP32P4_DESKTOP` |
| `configs/fallguard` | 独立跌倒监护诊断镜像 | `ESP32P4_QUICKAPP_PREBUILT=y`、`ESP32P4_QUICKAPP_FALLGUARD=y`、触摸、网络和临时文件系统 |

独立 Quick App 构建入口为：

```bash
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_quickapp.sh -j8
```

该命令更新工作区临时产物：

```text
/home/mi/Developer/openvela/nuttx/nuttx.bin
```

此前归档快应用镜像为：

```text
/home/mi/Developer/openvela/artifacts/esp32p4-quickapp/nuttx.bin
SHA-256: 46e7d65c00f1a99e4ef25fac742d6b9c6af637c016cdd20869173c72df220f57
```

该镜像是独立运行时/诊断镜像，不是最终桌面加快应用组合固件，不能直接作为最终演示镜像。

### 38.3 当前结论与下一步

仅将 `ESP32P4_QUICKAPP_PREBUILT=y` 加到桌面配置还不足以完成最终目标；还需要确认桌面应用、Quick App 资源、`qastart/vapp` 入口以及官方 XMS/QuickActivity 生命周期的连接方式。下一步先检查桌面代码的图标/事件入口和 Quick App 入口，随后创建独立的组合配置，不修改已验证的 `desktop` 配置。

### 38.4 第二步：桌面事件与 Quick App 入口核对（已完成，只读）

桌面当前主页只有“设置”“控制中心”“关于设备”三个入口；尚无“跌倒监护”图标，也没有调用 Quick App 的事件回调。桌面应用由 `desktop_main` 持有自己的 LVGL 生命周期和循环，`desktop_boot` 负责开机创建桌面任务。

当前 Quick App 入口由 `app/quickapp_prebuilt/qastart.c` 提供：

```text
qastart --prepare  → 资源复制与 KVDB 准备
qastart            → vapp_main("hap://app/com.openvela.fallguard")
```

`qastart` 通过 `vapp_main()` 直接进入独立 vapp 入口；该入口不是官方 XMS/QuickActivity 系统集成入口，并且会由自身管理 Quick App 的 LVGL/显示生命周期。因此不能直接在当前桌面 LVGL 回调中无保护地调用 `vapp_main()`，否则可能形成两套 LVGL/事件循环和 framebuffer 生命周期冲突。

本步确认的工程连接关系：

- `app/desktop/Make.defs` 只注册桌面应用；
- `app/quickapp_prebuilt/Make.defs` 在 `ESP32P4_QUICKAPP_PREBUILT=y` 时注册 `qastart`、`vapp`、`kvdbd`；
- 板级 `src/Makefile` 在 `ESP32P4_DESKTOP=y` 时加入桌面存储，在 `ESP32P4_BOARD_TOUCH=y` 时加入触摸；
- 因此“同时链接桌面和 Quick App”在构建层面可行，但“桌面按钮启动并安全返回”仍需要明确宿主/Activity 生命周期，不能仅靠配置合并解决。

本步未修改源码、未编译、未烧录、未操作板子。回退方式为不创建组合配置，继续使用已验证的 `configs/desktop` 和 `standalone-console-fix` 镜像。

### 38.5 第三步：创建组合配置骨架（已完成，未编译、未烧录）

新增实验配置：

```text
board/contest_board/configs/desktop_quickapp/defconfig
```

该配置同时请求：

```text
CONFIG_ESP32P4_DESKTOP=y
CONFIG_ESP32P4_QUICKAPP_PREBUILT=y
CONFIG_ESP32P4_QUICKAPP_FALLGUARD=y
CONFIG_ESP32P4_BOARD_LCD=y
CONFIG_ESP32P4_USB_CONSOLE_BEST_EFFORT=y
```

并保留桌面/触摸、LVGL、framebuffer、LittleFS、网络、临时文件系统、KVDB 诊断所需的基础选项。它是新的实验配置，不改写 `configs/desktop/defconfig`、`configs/quickapp/defconfig` 或 `configs/fallguard/defconfig`。

当前只创建了配置文件，尚未运行 `configure.sh`、尚未编译、尚未烧录。下一步先让 NuttX 对该配置做配置解析并检查最终 `.config`，若解析失败只删除该实验配置即可回退；已验证桌面配置和镜像不受影响。

### 38.6 第三步配置解析结果：工具链环境缺失（未编译、未烧录）

尝试命令：

```bash
cd /home/mi/Developer/openvela
bash nuttx/tools/configure.sh -e \
  vendor/openvela/boards/contest2026_288_board/configs/desktop_quickapp
```

结果：配置工具多次报告：

```text
riscv64-unknown-elf-gcc: 未找到
ERROR: No readable Make.defs file exists at /home/mi/Developer/openvela/nuttx
```

根因是本次命令所在 shell 尚未加载项目构建环境，不能据此判断 `desktop_quickapp` 选项冲突。该命令没有进入编译或烧录阶段；但它使工作区 `nuttx/.config` 处于不完整的组合配置状态。下一步必须先恢复已验证的桌面配置，再加载 `build/envsetup.sh` 或项目规定的工具链环境，重新进行配置解析。回退基线仍是归档镜像 `artifacts/desktop/standalone-console-fix/nuttx.bin`，未被改写。

随后已加载 `build/envsetup.sh`，确认工具链存在于：

```text
/home/mi/Developer/openvela/prebuilts/tools/linux/x86_64/riscv64-unknown-elf-gcc
```

并修复了构建树中的 `nuttx/Make.defs` 链接，使其指向板级公共 `scripts/Make.defs`。在带工具链环境下再次尝试切换配置时，新的明确阻塞为：

```text
Quick App binary bundle is missing; see prebuilt/quickapp-rv32/README.md
```

这不是板子问题，也没有发生编译或烧录。原因是组合配置会在 `Make.defs` 阶段检查预编译 Quick App bundle；当前工作区的 vendor 映射/`BOARD_DIR` 解析与该检查路径不匹配，虽然源码树下实际存在 `prebuilt/quickapp-rv32/libapps_vapp.a`。下一步要先用项目 Quick App 构建脚本的完整准备流程修复 bundle 路径/映射，再继续配置；不直接改公共 `Make.defs` 绕过校验。

后续定位发现：最初手写的组合 `defconfig` 缺少桌面基线中的 `CONFIG_ARCH_BOARD_CUSTOM_DIR` 和 `CONFIG_ARCH_CHIP_CUSTOM_DIR`，导致 `BOARD_DIR` 退回 NuttX 默认目录；同时 `nuttx/Make.defs` 曾被配置脚本建立为错误的相对符号链接（从 `nuttx/` 解析不到 vendor 路径）。现已处理：组合配置改为从完整 `configs/desktop/defconfig` 继承后叠加 Quick App 选项，`nuttx/Make.defs` 改为指向实际板级 `scripts/Make.defs` 的绝对链接。

### 38.7 构建树恢复与 HAL 恢复（已完成，仍未烧录）

按项目标准 `build.sh` + `build/envsetup.sh` 重新生成桌面配置时，构建树先正常完成配置、应用注册和 context 准备；随后发现切换配置清理了 `chip/esp-hal-3rdparty`。已执行：

```bash
bash contest2026_288_Bugyindudadui/board/contest_board/tools/prepare_esp_hal.sh
```

HAL 已恢复到固定提交：

```text
b90b1837cb5ad24747deb4c895246037cc206ce5
```

兼容补丁涉及的三个文件已出现工作区修改；脚本输出中的“patch failed”是因为固定 HAL 中对应兼容内容已经存在/部分已应用，需以 `git status` 和实际 diff 为准，不能重复强行应用。当前 `esp_err.h` 已存在；mbedTLS 子模块尚未在本步骤确认，后续构建脚本会按既有流程检查/初始化。

截至本记录：没有烧录、没有改变板上固件；组合配置和 HAL 恢复仍属于主机侧构建准备。

### 38.8 HAL 与比赛版本一致性确认（已完成）

按比赛 README 和 `prepare_esp_hal.sh` 的固定要求恢复，没有切换到其他版本：

```text
esp-hal-3rdparty HEAD: b90b1837cb5ad24747deb4c895246037cc206ce5
mbedTLS 子模块：已初始化
compat patch：已应用
```

实际检查命令：

```bash
bash contest2026_288_Bugyindudadui/board/contest_board/tools/prepare_esp_hal.sh
git -C contest2026_288_Bugyindudadui/board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls
```

`git status` 中仅保留兼容补丁对应的三个 HAL 工作区修改：`dw_gdma.c`、`os.h`、`os.c`；这是比赛版本所需的 openvela 适配状态，不是换用新 HAL 产生的差异。当前尚未烧录。

### 38.9 组合链接第一轮失败：两份 LVGL 与 work queue 缺失

组合配置已完成桌面和 Quick App 应用注册，但最终链接失败。主要错误为：

```text
fallguard-abi/liblvgl.a 与 openvela staging/libapps.a 中的 LVGL 符号重复定义
undefined reference: work_queue / work_cancel
```

这确认了报告中关于“一套匹配的 LVGL 所有权”的风险：桌面已链接公开 openvela LVGL，独立 RV32 运行库又携带内部 `liblvgl.a`，两者不能同时进入同一最终 ELF。

已做主机侧修复（尚未重新编译、尚未烧录）：

- `board/contest_board/scripts/Make.defs` 在 `ESP32P4_DESKTOP=y` 时从 Quick App 预编译库列表排除 `liblvgl.a`；
- `desktop_quickapp/defconfig` 增加 `CONFIG_SCHED_WORKQUEUE=y` 和 `CONFIG_SCHED_LPWORK=y`，为当前网络库依赖提供 NuttX work queue。

下一步重新执行组合编译，观察是否还有 ABI/符号依赖错误。若链接继续要求内部 LVGL 私有符号，则需改为官方 QuickActivity 宿主或重新评估运行库导出边界，不能用重复链接强行通过。

### 38.10 组合固件编译通过并归档（尚未真机验证）

在保持比赛固定 HAL `b90b1837cb5ad24747deb4c895246037cc206ce5`、恢复 mbedTLS 子模块后，采用已记录的组合链接实验配置：保留 Quick App 预编译库中的 LVGL 扩展，并在桌面组合链接中加入 `-z muldefs`，使公开桌面 LVGL 的公共符号先满足桌面、预编译库补充 Quick App 专用符号。该方案仅用于当前实验组合，后续真机若出现图形生命周期/ABI 问题需回退评估，不能视为已经证明两套 LVGL 完全兼容。

直接 `make -C nuttx -j8` 已成功完成最终链接和镜像生成，日志包含：

```text
LD: nuttx
MKIMAGE: NuttX binary
Generated: nuttx.bin
```

归档产物：

```text
artifacts/desktop-quickapp/20260928-combination-v1/nuttx.bin
SHA-256: 7806e40bda7a5328222bfd2cbadbb40b981694fa23daf27d509b971fa0c8f588
大小：4028584 字节
```

归档同时保存 `config`、`nuttx.elf`、`build.log`、`build-meta.txt` 和 `SHA256SUMS`。最终 `.config` 静态核对确认同一镜像同时启用：

```text
CONFIG_ESP32P4_DESKTOP=y
CONFIG_ESP32P4_QUICKAPP_PREBUILT=y
CONFIG_ESP32P4_QUICKAPP_FALLGUARD=y
CONFIG_ESP32P4_BOARD_TOUCH=y
CONFIG_SCHED_WORKQUEUE=y
CONFIG_SCHED_LPWORK=y
```

截至本记录：组合镜像只完成主机编译、链接和归档，尚未烧录、尚未确认开机桌面、尚未确认点击图标启动快应用，也尚未确认返回桌面。桌面回退镜像仍为 `artifacts/desktop/standalone-console-fix/nuttx.bin`，SHA-256 为 `96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4`。

### 38.11 组合镜像首次烧录完成（桌面/快应用功能尚待真机验收）

按目标板序列号 `E8:F6:0A:E3:A9:17` 重新枚举到 `/dev/ttyACM0`，确认芯片为 ESP32-P4 revision v3.2 后，烧录：

```bash
esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  --after hard-reset write-flash 0x2000 \
  artifacts/desktop-quickapp/20260928-combination-v1/nuttx.bin
```

写入范围为 `0x00002000` 至 `0x003d9fff`，最终显示：

```text
Wrote 4028584 bytes
Hash of data verified.
Hard resetting via RTS pin...
```

烧录日志保存于 `/tmp/desktop-quickapp-flash.log`。当前板上已是组合镜像；尚未把串口启动日志和屏幕现象记录为通过，下一步先验证桌面是否正常启动，再尝试 `qastart --prepare`/`qastart` 诊断入口。若组合镜像无法稳定启动，立即恢复桌面回退镜像 `standalone-console-fix/nuttx.bin`。

### 38.12 组合镜像首次真机启动与 Quick App 准备结果

烧录后串口重新枚举为 `/dev/ttyACM0`。启动采集到桌面运行日志和 GT911 触摸事件，其中包含：

```text
DESKTOP PAGE controls
GT911 event=1 ...
GT911 event=4 ...
```

这支持本轮结论：组合镜像至少能够启动桌面，桌面 LVGL 和触摸输入没有因组合链接立即失效。

随后在同一份组合镜像、同一桌面会话中执行：

```text
qastart --prepare
```

实际结果：

```text
Quick App preparation: Not a directory
```

因此当前仅能标记“组合镜像编译/烧录成功、桌面启动和触摸事件可见”；不能标记资源准备、KVDB、快应用首页或返回桌面通过。下一步检查 `qastart` 使用的资源路径与桌面 LittleFS `/data` 挂载布局，修复后重新编译/归档/烧录。当前板子仍运行组合镜像；如需立即恢复日常桌面，使用 `artifacts/desktop/standalone-console-fix/nuttx.bin`，SHA-256 为 `96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4`。

### 38.13 修复组合模式下 `/data` 挂载冲突（已修改，待编译验证）

根因定位：桌面启动时已由 `board_desktop_storage_initialize()` 将持久 LittleFS 分区挂载到 `/data`，而 `qastart` 原逻辑无条件执行 `mount(NULL, "/data", "tmpfs", ...)`，导致组合镜像中出现 `Not a directory`。

已在 `app/quickapp_prebuilt/qastart.c` 做最小兼容修改：

- 若 `/data` 已是目录，则打印 `QA using existing /data mount` 并复用桌面 LittleFS；
- 只有独立运行且 `/data` 不存在时才回退挂载 TMPFS；
- `/etc` 同样优先复用已有目录，避免组合模式重复挂载；
- 不改变独立 Quick App 没有桌面存储时的原有路径。

本步尚未编译、尚未烧录。下一步直接增量编译组合配置，随后重新归档和烧录，先验收 `qastart --prepare`，再处理桌面图标与应用生命周期。

### 38.14 `/data` 修复版组合镜像编译归档

修复 `qastart.c` 后，未重新运行会清理 HAL 的配置流程，直接执行：

```bash
make -C nuttx -j8
```

编译、链接和 `Generated: nuttx.bin` 均成功。归档：

```text
artifacts/desktop-quickapp/20260928-combination-v2-datafix/nuttx.bin
SHA-256: c3564bdf28fec524711415e6df5f628af0f7f3c786e5f516a4d2853652206e06
大小：4028680 字节
```

该镜像仍使用比赛固定 HAL `b90b1837cb5ad24747deb4c895246037cc206ce5`；归档保存 `config`、`nuttx.elf`、`build.log`、`build-meta.txt` 和 `SHA256SUMS`。尚未烧录，下一步烧录后复测桌面和 `qastart --prepare`。

### 38.15 官方 XMS 路线准备中的 HAL 版本复核

在继续官方 XMS 配置前，已先恢复并核对比赛版本 HAL：

```text
esp-hal-3rdparty HEAD: b90b1837cb5ad24747deb4c895246037cc206ce5
mbedTLS 子模块 HEAD: 582ff482038db6e4010dbf6f943d97b05ad06ea5
兼容补丁工作区修改：dw_gdma.c、nuttx/include/platform/os.h、nuttx/src/platform/os.c
```

`HAL_MATCHES_CONTEST_VERSION` 核对通过。后续 `desktop_xms` 配置、官方 System Server/PackageManager/QuickActivity 调试均基于该固定版本，不切换 HAL。

### 38.16 官方 XMS 配置依赖解析进度（仅主机配置，未编译、未烧录）

已清理固定 `com.openvela.fallguard` 扫描实验，避免应用中心写死包名。新建并开始解析：

```text
board/contest_board/configs/desktop_xms/defconfig
```

该配置基于已验证桌面，追加官方 XMS/QuickActivity 依赖。当前已确认 Kconfig 能识别并进入解析的依赖包括：

```text
DRIVERS_BINDER
LIBUV / LIBUV_EXTENSION
LIB_RAPIDJSON
PROTOBUF_C
INTERPRETERS_QUICKJS
FEATURE_FRAMEWORK
LIBASH
LIB_FREETYPE / LIB_YOGA / LV_USE_LIBPNG
UTILS_CURL / LIB_ZLIB / CRYPTO_MBEDTLS
FS_SHMFS / KVDB / UIKIT / LV_USE_NUTTX_LIBUV
```

解析过程中已修复 `UTILS_CURL → LIB_CURL → LIB_ZLIB + CRYPTO_MBEDTLS` 以及 Binder 对 `CXX_LOCALIZATION/TLS` 的依赖条件。当前最终 `.config` 仍未启用 `ANDROID_BINDER`、`SYSTEM_SERVER`、`SYSTEM_PACKAGE_SERVICE`、`SYSTEM_WINDOW_SERVICE`、`SYSTEM_ACTIVITY_SERVICE` 和 `QUICKAPP_VAPP_XMS`，说明还存在 Kconfig 依赖或入口选择未满足，不能把“写入 defconfig”误记为 XMS 已启用。

本步未编译、未烧录，也未修改稳定桌面镜像。下一步继续定位这些官方符号为何被 Kconfig 裁掉，直到它们真实出现在最终 `.config`，再恢复固定 HAL 后尝试编译。

本轮进一步确认官方 Binder/XMS 的真实依赖入口：

- `ANDROID_BINDER` 依赖 `CXX_LOCALIZATION`、`LIBC_LOCALE`、TLS 槽位，并选择 Android libutils；
- Binder 驱动是 `DRIVERS_BINDER`，ServiceManager 是 `ANDROID_SERVICEMANAGER`，需要 `TIMER_FD` 和 `EVENT_FD`；
- `QUICKAPP` 还需要 QuickJS、Feature Framework、libuv extension、UIKIT、Yoga、PNG、curl、ASH 等；
- `SYSTEM_SERVER` 依赖 Binder、libuv；PM/WMS/AMS 再分别依赖 RapidJSON、SHMFS、KVDB、LVGL NuttX libuv 等。

已将这些依赖写入 `desktop_xms/defconfig`，Kconfig 的配置刷新已能完成到应用注册/context 阶段；当前仍被切配置后 HAL checkout 缺失和 `.config` 依赖裁剪阻塞，尚未进入 XMS 编译或烧录。没有把 defconfig 中“请求启用”误记为最终 `.config` 已启用。

此前已创建的飞书精简报告为：[《ESP32-P4 接入 openvela：从黑屏到触摸桌面与快应用运行时》](https://feishu.cn/docx/SC9Vd3c0Koxur1x8FzgcZUGSnah)。

用户本次要求继续维护当前 `official_lcd_feishu_report.md`，因此调研结论、当前状态和后续步骤均完整写在本节。之前另建的交接文档不再作为主入口，后续直接续写本文件。飞书没有同步修改；本轮没有更改应用/驱动、编译、烧录或操作板子。

## 39. 快应用最小演示方案：统一包名到资源路径（2026-09-27）

用户确认先做最小目标：桌面点击一个图标，启动真实跌倒监护 RPK；暂不做动态安装、应用商店和多应用后台。最终仍要求同一固件完成桌面→快应用→返回桌面。

本轮新增统一应用目录层：

```text
app/quickapp_prebuilt/qa_app_catalog.h
app/quickapp_prebuilt/qa_app_catalog.c
```

目录层登记应用包名、标题、入口 URI 和资源根目录；当前唯一内置应用是 `com.openvela.fallguard`，但资源路径由函数生成：

```text
/data/app/<package_name>
```

Bundle/运行时核心不写死具体包名；包名只在内置应用描述表登记。函数拒绝 `..`、斜杠、反斜杠、空格和空包名，主机单元测试已通过：包名解析、路径生成、manifest 文件路径生成和路径穿越拒绝均通过。

`qastart` 已改为从应用目录查询入口 URI，再传给运行时；后续桌面图标将使用同一目录层，不在桌面代码和 Bundle 代码中各自复制路径规则。本轮只完成目录层和主机测试，尚未把桌面图标、LVGL 释放/恢复、应用退出返回接入。

真实 RPK 约束保持不变：资源来自用户提供的 `com.openvela.fallguard.debug.0.1.0.rpk` 解包内容，不能用仿制 LVGL 页面替代。当前真实 RPK 测试此前停在 Bundle 创建阶段；下一步是让调用方传入由包名解析出的资源根目录，继续定位 Bundle 创建白屏。

### 39.1 最小宿主方案（待实现）

不直接在桌面点击回调中调用会重新初始化 LVGL 的独立 `vapp_main()`。推荐增加宿主状态：

```text
桌面点击
  → desktop_suspend_for_quickapp()
  → 释放桌面 LVGL、/dev/fb0、/dev/input0
  → qa_host_start("com.openvela.fallguard")
  → 独立快应用使用自己的 LVGL/libuv
  → 应用内“返回桌面”触发 qastart 退出
  → desktop_resume_after_quickapp()
```

桌面和快应用不能同时持有同一 framebuffer、触摸节点或全局 LVGL。失败时宿主应恢复桌面并打印明确原因。此方案先支持一个内置应用，后续才考虑官方公开 XMS/vappxms 的 PackageManager/Activity/Window 集成。


## 40. 当前跌倒监护独立测试固件编译完成（2026-09-28）

在恢复比赛固定 HAL `b90b1837cb5ad24747deb4c895246037cc206ce5`、mbedTLS 子模块后，使用 `build_fallguard.sh -j8` 编译通过。最终 `.config` 和 System.map 确认包含：

```text
CONFIG_ESP32P4_QUICKAPP_PREBUILT=y
CONFIG_ESP32P4_QUICKAPP_FALLGUARD=y
CONFIG_ESP32P4_BOARD_TOUCH=y
qastart_main
vapp_main
qa_resource_start
lv_nuttx_touchscreen_create
```

当前镜像是**独立快应用验证固件**，没有 `CONFIG_ESP32P4_DESKTOP`，因此不会自动显示桌面锁屏；不能把它烧录后当作最终桌面固件。它用于下一次上板验证：`qastart --prepare`、真实 RPK 启动和首页显示。归档：`artifacts/fallguard/current-build/`。

镜像 SHA-256：`cc952d016caae8f2f2eab6bd06d1915006ec3f69323220ff0e493078e3364f6e`。当前电脑随后没有检测到 USB 串口，本轮没有烧录。桌面回退镜像仍为 `artifacts/desktop/standalone-console-fix/nuttx.bin`。

下一步：板子重新连接后，先烧录这份独立测试固件，执行 `qastart --prepare`，再执行 `qastart`。只有真实 RPK 首页出图后，才开始把桌面释放/恢复和图标接入组合配置。

## 41. 稳定桌面分支与团队标准编译入口（2026-09-28）

稳定桌面现在位于标准比赛仓库路径：

```text
/home/mi/Developer/openvela/contest2026_288_Bugyindudadui
```

当前分支：`stable/esp32p4-desktop`。原先包含快应用和显示实验的工作区已保留在：

```text
/home/mi/Developer/openvela/contest2026_288_Bugyindudadui-experiment-20260928
```

另有原始复件 `contest2026_288_Bugyindudadui（复件）`，未被修改。

稳定桌面遵循团队仓库的根目录构建入口。完整 openvela 工作区中执行：

```bash
cd /home/mi/Developer/openvela

# 首次构建或切换配置时，按团队标准先清理并准备固定 HAL
PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/desktop \
  distclean

cd contest2026_288_Bugyindudadui
bash board/contest_board/tools/prepare_esp_hal.sh
git -C board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls
cd ..

# 团队统一入口：从 openvela 根目录编译
PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/desktop \
  -j8
```

成功标志是 `Generated: nuttx.bin`。产物位于：

```text
/home/mi/Developer/openvela/nuttx/nuttx.bin
```

本次按上述根目录入口实测成功：镜像大小 `718480` 字节，SHA-256 为 `e85f3865b280e42ae09b9ecb40b5d698ffbc48b186e05581a96e957f07be31ac`。构建日志确认编译了 `desktop_boot`、`esp32p4_touch`、`esp32p4_desktop_storage` 和 LVGL 桌面应用。

`board/contest_board/tools/build_desktop.sh` 是便捷封装，但交付和复现以本节的团队标准 `./build.sh` 命令为准。`chip/esp-hal-3rdparty` 是被 `.gitignore` 忽略的第三方依赖，不提交到比赛仓库；其他开发者执行 `prepare_esp_hal.sh` 和 mbedTLS 子模块初始化即可得到相同固定版本。

## 42. 桌面与摄像头合并最新状态（2026-09-28）

### 当前 Git 状态

当前工作区位于：

```text
/home/mi/Developer/openvela/contest2026_288_Bugyindudadui
```

当前分支：

```text
merge/esp32p4-desktop-camera
```

当前 HEAD 是本地合并提交：

```text
2b9070b merge: integrate image-develop into desktop branch
```

该提交的两个父分支分别是：

```text
stable/esp32p4-desktop
  b8b97cd feat: add native LVGL fallguard app

rotel/image-develop
  a178ae8 feat(monitor): consume the board's JPEG with fall_watch --jpeg
```

该合并提交尚未推送。当前还有未提交的组合配置和底层实验修改：

```text
board/contest_board/configs/desktop_camera/
board/contest_board/chip/espressif/esp_irq.c
board/contest_board/chip/hal_esp32p4.mk
```

构建过程中生成的 `.built` 文件不应提交：

```text
app/desktop/.built
app/p4x_selftest/.built
```

### 合并内容边界

本次合并的目标是把桌面和摄像头代码放进同一个分支，暂时不做桌面与摄像头页面联动。当前合入内容包括：

- 稳定桌面、锁屏、设置、应用中心和原生 LVGL 跌倒监护页面；
- `app/p4x_selftest/` 摄像头自检、SC2336 CSI、ISP 和软件 JPEG 代码；
- 摄像头验证工具和监控脚本；
- 摄像头 `demo` 配置；
- 摄像头分支对 ESP32-P4 HAL、FreeRTOS 兼容层和 IRQ 的修改。

当前没有把摄像头画面接到 `app/fallguard/` 页面，也没有实现实时预览、页面内单帧采集或摄像头退出恢复桌面。

### 两份独立固件已经分别编译成功

同一块板子当前应轮流烧录两份独立固件，不能把两个完整 `nuttx.bin` 写到同一个 `0x2000` 地址后期待同时运行。

稳定桌面配置：

```text
vendor/openvela/boards/contest2026_288_board/configs/desktop
```

归档固件：

```text
artifacts/merge-desktop-camera/desktop/nuttx.bin
```

SHA-256：

```text
0700891e6bd2f820c7eee07885aae29543d8feef36440584d1f6320d934bd6e0
```

摄像头配置：

```text
vendor/openvela/boards/contest2026_288_board/configs/demo
```

归档固件：

```text
artifacts/merge-desktop-camera/camera-demo/nuttx.bin
```

SHA-256：

```text
057de0e123263c4840f17ede6fbc50744b3b32634a0777c084fe52f2890531cb
```

桌面验证时烧录 `desktop/nuttx.bin`；摄像头验证时烧录 `camera-demo/nuttx.bin`。两个人共用一块板时，按验证目标轮流烧录即可。

### 单固件组合尝试结果

为了探索一份固件同时包含桌面和摄像头代码，新增了未提交配置：

```text
board/contest_board/configs/desktop_camera/defconfig
```

该配置同时包含：

```text
CONFIG_ESP32P4_DESKTOP=y
CONFIG_INIT_ENTRYPOINT="desktop_boot_main"
CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST=y
CONFIG_ESPRESSIF_SPIRAM=y
CONFIG_I2C_TRACE=y
```

最终组合 ELF 中确认存在：

```text
desktop_boot_main
p4x_selftest_main
p4x_camera_capture_csi
esp_cam_new_csi_ctlr
fallguard_show
```

组合固件可以链接并生成，但烧录后出现：

```text
大部分屏幕黑色
底部蓝色横向撕裂条纹
背光正常
桌面不能正常显示
```

已经测试过的组合镜像包括：

```text
artifacts/merge-desktop-camera/desktop-camera/nuttx.bin
SHA-256: ad24b28a12c454dc8703004deded3488a5ece8a0720c99b0b72bd8ff14bf6746

artifacts/merge-desktop-camera/desktop-camera-fixed/nuttx.bin
SHA-256: 4b62410cf922b841704f69ef173a5ff12bf1b95cc3e1feee601a18d6d3096351
```

两份都出现相同撕裂，说明问题发生在摄像头底层静态链接和共享资源层，即使尚未执行摄像头命令，也会影响 LCD 启动。

### 已确认的底层冲突

#### 1. DW-GDMA 共享源和资源

LCD 的 MIPI DSI 刷新和摄像头 CSI/ISP 都使用 DW-GDMA。合并初期的 `hal_esp32p4.mk` 重复加入了：

```text
dw_gdma_hal.c
dw_gdma.c
color_hal.c
```

构建日志出现过重复目标警告。后续已尝试让共享源只编译一次，并让摄像头专用源受 `CONFIG_LVX_USE_DEMO_CONTEST2026_288_P4X_SELFTEST` 条件控制，但组合固件仍然撕裂，因此重复源不是唯一根因。

#### 2. 全局 IRQ 分发变化

`image-develop` 修改了 `board/contest_board/chip/espressif/esp_irq.c`，让没有 NuttX IRQ 映射的 CPU interrupt 转发到 ESP-IDF handler table。该逻辑可能影响 LCD DSI/DMA 的共享中断。当前合并分支有未提交的恢复稳定桌面 IRQ 行为的实验修改，尚未证明摄像头中断仍可用。

#### 3. HAL 和 FreeRTOS 兼容层

摄像头分支修改了被共享的 `esp-hal-3rdparty` 内容：

```text
components/upper_hal_dma/src/dw_gdma.c
nuttx/include/platform/os.h
nuttx/src/platform/os.c
```

这些修改会影响 DMA 中断申请、任务创建和平台延时，不是纯摄像头页面代码。摄像头 HAL、CSI/ISP HAL 和 FreeRTOS 兼容层静态链接进桌面镜像后，可能在摄像头未启动时就改变 LCD 的运行环境。

#### 4. PSRAM 和缓存带宽

LCD framebuffer 约为：

```text
1024 × 600 × 2 ≈ 1.17 MiB
```

摄像头 1280×720 RGB565 帧约为：

```text
1280 × 720 × 2 ≈ 1.76 MiB
```

两者还要共享 LVGL、字体、ISP、JPEG、DMA 描述符和系统堆。即使底层链接问题修好，实时预览仍需限制分辨率、帧率和缓冲数量。

### 当前正确的推进顺序

现在不要继续用撕裂的 `desktop_camera` 固件做演示。应按以下顺序修复：

```text
1. 保留 stable/desktop 和 demo 两份独立固件作为回退
2. 清理 desktop_camera 的构建生成物
3. 让共享 GDMA/color 源只编译一次
4. 让摄像头 HAL 只在摄像头配置中进入构建
5. 将摄像头 IRQ 兼容从全局 esp_irq.c 中隔离
6. 使 desktop_camera 在摄像头未启动时与 desktop 完全一致
7. 同一固件中先做 NSH 单帧采集
8. 再做跌倒监护页面单帧采集
9. 最后才做 320×180、5 FPS 的低帧率预览
```

“方案一”指：同一个固件，开机只运行桌面，进入跌倒监护页面时按需启动摄像头，离开页面时停止并释放摄像头。该方案可以降低运行时资源冲突，但不能跳过共享 GDMA、IRQ、HAL 和构建隔离修复。

### 当前其他 AI 接手时的注意事项

- 不要把 `desktop_camera` 当前镜像当作稳定桌面镜像；它已经在真机上出现底部蓝色撕裂。
- 不要覆盖或重置 `stable/esp32p4-desktop`；它是已推送的稳定桌面回退基线。
- 不要提交 `chip/esp-hal-3rdparty/`、`nuttx/nuttx.bin`、`nuttx/.config` 或 `.built` 文件。
- 当前 merge 分支的修复仍未提交、未推送，修改前应先查看 `git status`。
- 摄像头分支原配置是 `demo`，桌面分支原配置是 `desktop`；两者分别编译成功不代表组合配置可以同时运行。
- 真正的单固件联动需要新增 `camera_session` 生命周期层，不能直接把 `p4x_camera_csi.c` 当作 LVGL 后台服务。


## 43. 组合固件冲突修复：共享中断与摄像头启停（2026-09-28）

当前分支仍为 `merge/esp32p4-desktop-camera`。用户确认独立桌面和摄像头已分别在同一块板上验证，本轮目标是修复组合模式；用户自行烧录，本文不宣称真机联合验收通过。

### 43.1 已实施修复

- 恢复合并时删除的 I2C READ、END 命令码，修复 GT911/SC2336 共用 I2C 读取路径；修正 TRACE 的 RV32 时间格式参数。
- 原 `esp_os_intr_alloc_intrstatus()` 通过单个 source→IRQ 槽返回句柄，无法表达 DW-GDMA 多通道共享源；现在由 `esp_alloc_native_irq()` 保存每次申请的独立 IDF 句柄，启停与释放按实际句柄处理。分配时先禁用中断，发布映射后再依 flags 启用。
- 原 `esp_os_intr_free()` 把 IDF 原生句柄强转为另一种结构体读取 IRQ。现在按句柄查找并释放对应注册项，不会因摄像头释放而误取 LCD 句柄。
- HAL 中断经 `riscv_doirq()` 和 NuttX 公共 demultiplexer 分发，保留中断上下文和调度处理；移除绕过 NuttX 的直接 IDF handler 回调。demultiplexer 使用实际触发的 CPU vector；修正 edge/level 判断。
- ISP 中断也接入相同适配。DW-GDMA 删除时先移除通道 ISR，再释放它引用的 group。
- FreeRTOS 兼容临界区从空操作改为保存/恢复 IRQ 状态的递归自旋锁；队列数据复制使用 IRQ-safe 自旋锁，避免 ISR 获取 mutex；超时换算使用 NuttX tick 配置。
- CSI stop 与 completion 重启 DMA 的路径串行化；停止后到来的 completion 不再重新启动该通道。采集入口串行化，按实际启用/启动状态清理；stop 失败保留 DMA 所有内存并报告需复位。
- Make/CMake 中共享 DW-GDMA/color 源去重，摄像头专用源与 FreeRTOS include 受摄像头配置控制。HAL 改动已完整同步到 `board/contest_board/patches/esp-hal-openvela-compat.patch`。

以上是代码确认的问题和对应修复。第 42 节“相同撕裂说明静态链接/共享资源就是根因”的判断仍需真机证据；本轮不将该推断记为已定位的唯一原因。

### 43.2 验证与组合镜像

- 最终 `desktop_camera` 完整重编译成功：`Generated: nuttx.bin`，无重复目标警告。
- 主机回归测试直接提取实际 IRQ 所有权与分发函数，用模拟分配器验证独立句柄、ISR 上下文、100 次重复申请/释放、失败回滚、释放摄像头后 LCD 回调仍有效、边沿/电平 ACK。ASan/UBSan 通过；容器 ptrace 限制下禁用 LeakSanitizer。
- HAL patch 反向检查通过；`git diff --check` 通过。
- ELF 确认同时包含 `desktop_boot_main`、`p4x_selftest_main`、`p4x_camera_capture_csi`、`esp_cam_new_csi_ctlr`、`fallguard_show` 和新 IRQ 适配函数。
- 构建仍有既有格式/宏警告、未启用的 ISP AE/AF/AWB/Histogram 队列接口声明警告；不宣称全仓零警告或这些可选功能已支持。
- CMake 同步了源清单，本轮实际固件使用 Make 构建，未做 CMake 构建验收。

归档（相对 openvela 根目录）：

```text
artifacts/merge-desktop-camera/20260928-shared-irq-repair/desktop_camera/nuttx.bin
大小：737944 字节
SHA-256：f1bd80c6a76ee9ece5ca6d15b5357c46ff6c8cc72c31bfe169199702ec302fc9
```

同目录保存 ELF、最终 config、System.map、build.log、源码差异和校验清单。`nuttx/nuttx.bin` 当前也是该组合版本。未提交、未推送、未烧录。

回归命令：

```bash
cd /home/mi/Developer/openvela
ASAN_OPTIONS=detect_leaks=0 python3   contest2026_288_Bugyindudadui/board/contest_board/tests/test_shared_irq.py
```

### 43.3 用户烧录和联合验收

下面命令由用户执行；先确认板接 J20 及实际串口编号，关闭占用串口的终端。

```bash
cd /home/mi/Developer/openvela
export PATH="$HOME/.local/bin:$PATH"
python3 -m serial.tools.list_ports -v

# 按枚举结果设置串口
LCD_PORT=/dev/ttyACM0
FW_DIR=artifacts/merge-desktop-camera/20260928-shared-irq-repair/desktop_camera
(cd "$FW_DIR" && sha256sum -c SHA256SUMS) && esptool --chip esp32p4 --port "$LCD_PORT" --baud 921600   --after hard-reset write-flash 0x2000 "$FW_DIR/nuttx.bin"
```

成功需出现 `Hash of data verified.`。复位重新枚举后连接：

```bash
python3 -m serial.tools.miniterm /dev/ttyACM0 115200 --raw
```

NSH 中逐条执行，先确认锁屏/桌面/触摸，再启动摄像头：

```text
free
p4x_selftest --camera
p4x_selftest --camera-capture
free
p4x_selftest --camera-capture
free
```

观察 `stage wait ret=0`、`on_trans_finished` 非零及有效图像统计，同时查看桌面是否继续刷新、触摸是否正常。重复至少 3 次，验证摄像头结束后仍可切页/拖动控件，随后断电重启复测。需要 JPEG 链路时再运行 `p4x_selftest --jpeg-capture`。

当前仅集成桌面与 NSH 摄像头命令，不把摄像头预览接进跌倒监护页面。若仍有黑屏/撕裂，保留完整启动日志与首次采集日志，分别判断发生在摄像头启动前、采集中还是释放后；不能直接宣布问题彻底解决。


## 44. 组合固件串口传图损坏修复（2026-09-28）

用户实测 RGB565 连续六次长度不足，第七次未捕获的 Base64 错误导致监控退出；JPEG 也报 Incorrect padding。本轮读取保存日志确认每次板端 `stage wait ret=0`、completion=5、PASS one frame。`0x5d` GT911 I2C TRACE 插入 THUMB/jpg 行，属于确认的协议污染；USB BEST_EFFORT 超时丢字符是同时存在的可靠性缺口。

修复：

- desktop_camera 关闭 I2C_TRACE，保留桌面、触摸、摄像头和无主机读取时的独立启动。
- 新增任务上下文的 USB frame begin/write/end。传图期间独占硬件发送通路，普通控制台日志被丢弃；不暂停触摸和桌面任务。等待 FIFO 时让出 CPU，不在关中断期间等待主机；按照实际写入长度推进，不丢弃图像字节。连续 2 秒无发送进展返回 ETIMEDOUT，结束会话恢复普通日志。
- RGB565 与 JPEG 的头、Base64 和尾全部走同一传输会话。加入前导换行防止上一条日志的残行污染头。JPEG selftest 与真实采集共用互斥锁。
- thumb_image 严格解析独立 payload 行，必须有 footer，保留长度和 checksum 校验；Base64 异常统一包装为 ThumbError，使监控可以记录失败并重试，不放宽损坏图像校验。

验证：6 项解析测试通过（含现场失败日志），USB 发送函数主机 ASan/UBSan 测试通过（短写、等待、日志隔离、主机断开超时及释放），git diff --check 通过。容器禁用 LeakSanitizer。组合配置完整编译及最终增量复核成功；仍有既有 HAL/可选 ISP 警告。尚未烧录，真机成功率待用户验收。

本次构建使用根目录 build.sh desktop_camera。切换配置前临时保留现有固定 HAL，执行 distclean 后恢复，校验已应用的 HAL 补丁；不是重新下载 HAL 的全新依赖复现。

新镜像：`artifacts/merge-desktop-camera/20260928-transport-fix/nuttx.bin`（相对 openvela 根目录），738004 字节。
SHA-256：`11e8b274c712b15782a48b6f3ff326eab8cd60c3c9f91a9032c0b24529df51e3`。
归档含 ELF/config/System.map、完整及最终构建日志、源码差异、SHA256SUMS。分支保持 merge/esp32p4-desktop-camera，未提交、未推送、未烧录。

用户烧录：

```bash
cd /home/mi/Developer/openvela
PORT=/dev/ttyACM0
esptool --chip esp32p4 --port "$PORT" --baud 921600 \
  --after hard-reset write-flash 0x2000 \
  artifacts/merge-desktop-camera/20260928-transport-fix/nuttx.bin
```

先关闭其他读取串口的终端，在比赛仓库执行（不发送告警）：

```bash
python tools/monitor/fall_watch.py --jpeg --once --dry-run --backend mock
python tools/monitor/fall_watch.py --once --dry-run --backend mock
python tools/monitor/fall_watch.py --jpeg --interval 10 --dry-run --backend mock
```

验收 JPEG/RGB565 都能保存图像且校验通过、连续采集至少 10 次，传图时触摸/桌面正常；断开读取端后桌面继续运行，重新运行监控可恢复传输。必须使用新固件，脚本异常捕获不能修复旧固件已丢失的数据。

### 44.1 真机第一次完整 JPEG 与接收等待修正

2026-09-28 20:22:09 的日志确认 26,381 字节 JPEG 完整解析成功，存在 jpeg_sw: end、PASS one frame 和 NSH 提示符；mock 结果不是实际跌倒识别验收。20:22:59 的第二帧头声明 99,871 字节，30 秒截止时仍在 jpg payload 中，未收到 end/PASS。随后握手零字节，尚不能据此确定整板卡死原因。

主机读取原先固定 sleep(0.03)，与小 USB 包背压叠加时接收速率低。board_console.py 改为 select 等待数据就绪，保留实时日志；采集预算耗尽后额外排空最多 30 秒，避免立即关闭读取端。八项主机测试通过（含小包分段接收及损坏日志解析）；尚待同一固件复位后真机重试，不宣称速度及超时恢复已在板上验证。此次仅修改主机脚本，无新固件。


### 44.2 USB 发送从 tick 轮询改为中断唤醒

20:29:10 真机帧为 139436 字节 JPEG，实时日志最终收到 end/PASS/NSH，图像链路完成，但接收约 1.9 KB/s。主机 select 改动未消除此限速，因此此前将主要瓶颈归于主机 30 ms sleep 的判断不充分。

本轮修改 esp_usbserial_frame_write：取消 FIFO 忙时 nxsig_usleep(1000)（系统 tick=10 ms），改为 SERIAL_IN_EMPTY 中断 post semaphore 唤醒任务。开启中断后重查 FIFO，防止就绪竞争；普通日志不能关闭传图中断；传输结束禁用该中断。保留 2 秒无进展等待限制和日志隔离。主机发送单元测试模拟 ISR、短写、无主机超时通过，组合配置 build.sh 增量编译成功，未烧录，吞吐待真机验证。

归档：artifacts/merge-desktop-camera/20260928-usb-irq-tx/nuttx.bin
大小：738260 字节，SHA-256：043e97388b7e2d102437a415c13944d9bc460476f9bf4f802220d24274206ba7。


## 45. 组合修复提交与远程分支推送记录（2026-09-28）

### 45.1 当前仓库与提交

本地仓库：`/home/mi/Developer/openvela/contest2026_288_Bugyindudadui`。
当前本地分支：`merge/esp32p4-desktop-camera`。

用户明确要求提交并推送至 Rotel-ga 仓库的同名新分支，已完成：

```text
远程名：rotel
远程地址：git@github.com:Rotel-ga/contest2026_288_Bugyindudadui.git
远程分支：merge/esp32p4-desktop-camera
提交：e4657510a300026d76a273a04871a06fe55769e3
说明：fix: integrate desktop camera IRQ ownership and reliable USB frame transport
```

[远程分支](https://github.com/Rotel-ga/contest2026_288_Bugyindudadui/tree/merge/esp32p4-desktop-camera) · [提交详情](https://github.com/Rotel-ga/contest2026_288_Bugyindudadui/commit/e4657510a300026d76a273a04871a06fe55769e3)

实际推送命令：

```bash
git push -u rotel HEAD:refs/heads/merge/esp32p4-desktop-camera
```

Git 返回 `[new branch] HEAD -> merge/esp32p4-desktop-camera`，本地分支已跟踪 `rotel/merge/esp32p4-desktop-camera`。推送后本地 HEAD 与远程跟踪引用均为上述完整提交。未修改远程 `dev-ai-contest-2026`，未创建或合并 PR，未执行强制推送。

### 45.2 提交范围与检查

提交包含 22 个文件的修改：组合配置、I2C 读取回归修复、共享中断句柄管理、CSI 启停与兼容层修复、USB 图像独占传输和中断唤醒、主机接收/握手/解码处理、回归测试及本开发报告截至第 44.2 节的记录。

推送前重新确认：

- 监控脚本 9 项单元测试通过；
- 共享中断测试通过：独立句柄、ISR 上下文、100 次申请/释放、失败回滚；
- USB 发送测试通过：短写、日志隔离、ISR 唤醒、主机不接收时超时及释放；
- HAL 兼容补丁反向检查通过；
- `git diff --cached --check` 通过。

构建产物、`esp-hal-3rdparty` checkout、本地运行日志及 `app/desktop/.built`、`app/p4x_selftest/.built` 未提交。两个 `.built` 文件仍保留在本地。

### 45.3 最新固件与下一步

最新组合固件仍为：

```text
artifacts/merge-desktop-camera/20260928-usb-irq-tx/nuttx.bin
大小：738260 字节
SHA-256：043e97388b7e2d102437a415c13944d9bc460476f9bf4f802220d24274206ba7
```

该路径相对 openvela 根目录。此镜像在代码提交前生成，源码修复已纳入 e465751，但不应把镜像内版本字符串宣称为该新提交。构建使用团队根目录 `./build.sh .../configs/desktop_camera -j8` 入口。

用户自行烧录，AI 本轮没有操作板子。旧的 tick 轮询发送版已有两次完整 JPEG 日志证据，但速度约 1.9 KB/s；最新中断唤醒版目前只完成编译和主机测试，尚未取得用户的真机吞吐及连续采集结果。后续聚焦同一组合固件的 JPEG 接收速度、连续多次采集、超时后恢复以及桌面/触摸并行稳定性。mock 输出不代表真实跌倒识别验收。

本第 45 节及顶部交接提示是用户随后要求补充的本地文档记录，尚未纳入 e465751，也未再次提交或推送。


## 46. 原生面板按钮控制电脑监控脚本（2026-09-28）

用户要求先实现原生“开始监控”按钮与 fall_watch.py 的控制链路，目前只绑定 JPEG/mock/dry-run，不接入真实 AI 或飞书发送。

实现：板端新增 panel_control 状态邮箱，按钮更新 requested 与 revision；NSH 新增 fgctl query / fgctl ack，PC 在同一个串口连接中轮询并确认。按钮请求存储在内存，传图时不会因日志抑制丢失。每次 ack 携带 revision，旧确认不能覆盖后来的停止操作。板端 UI 只在 LVGL 线程更新，页面切换保留监控请求；卡片显示 PC offline、Waiting for PC、Capturing (mock)、Frame OK (mock)、Capture error / retry 等状态（使用现有字体支持的 ASCII）。PC 联系超时设为 180 秒，以兼容当前长采集及恢复预算。

PC 新增 --panel-control，启动后等待按钮；不加 --once 时，开始/停止控制采集循环；加 --once 时，每个开始请求只采一帧，脚本继续等待下一次停止→开始操作。停止在当前帧结束后生效，不中断 JPEG。仅允许 mock + dry-run；串口连接只由该脚本持有。电脑程序必须预先运行，板子不会自行启动电脑进程。

验证：14 项 PC 单元测试通过，覆盖等待按钮、确认时遇到停止、帧内停止以及单次请求去重；板端实际状态邮箱源文件的主机测试通过（旧 ack 拒绝、停止确认、连接过期）；组合配置根目录 build.sh 编译成功，fgctl 注册与 ELF 符号核对通过，git diff --check 通过。未操作板子，真机按钮控制待用户验证。

归档：`artifacts/merge-desktop-camera/20260928-panel-control/nuttx.bin`（相对 openvela 根目录）。
大小：739012 字节；SHA-256：`99d5d9ee7fe618bb8521ab39f96f25177c558e107ed0cc3a111e2d53e0fbc99c`。

用户烧录：

```bash
cd /home/mi/Developer/openvela
PORT=/dev/ttyACM0
esptool --chip esp32p4 --port "$PORT" --baud 921600 \
  --after hard-reset write-flash 0x2000 \
  artifacts/merge-desktop-camera/20260928-panel-control/nuttx.bin
```

先验证每次点击一次采集（脚本保持等待）：

```bash
cd /home/mi/Developer/openvela/contest2026_288_Bugyindudadui
tools/monitor/fall_watch.py --port /dev/ttyACM0 --panel-control --jpeg --once --dry-run --backend mock --capture-timeout 120
```

循环验证：去掉 --once，加 --interval 10；点击开始进入循环，停止后当前帧完成即不再采集。返回应用中心再进入页面，请求状态应保持。Ctrl+C 结束电脑监听；连接丢失时程序可能报错退出，需用户恢复串口后重新启动，尚未实现自动重连。

本轮实现与本地记录尚未提交或推送；远程 e465751 不含此按钮控制功能。


### 46.1 面板控制开放真实识别后端

用户要求按钮触发真实识别。已移除 panel-control 对 mock/dry-run 的限制，允许 direct/proxy，仍禁止与 from-log 合用。PC 按选定后端执行真实脚本；direct 从环境变量 MIMO_API_KEY 取密钥。--dry-run 仅禁止飞书发送，不禁止模型调用。未实际请求模型或发送飞书。

新增 fall 状态回传，页面显示 Monitoring / No fall detected / Fall detected，检测到跌倒时状态转为“疑似跌倒”；模型错误回传 error，不能显示为正常。停止仍在本轮采集/识别结束后生效。16 项主机测试通过，其中 direct API 使用模拟 HTTP 响应验证 JPEG 请求及跌倒结果回传。组合固件编译成功，真机按钮、模型请求与告警尚待验证。

固件：artifacts/merge-desktop-camera/20260928-panel-direct/nuttx.bin
大小：739056；SHA-256：d44f6a3ce08888354eeeca4e225acdaa0183fdde101e3aef1049d90b443bdfef。

启动真实识别、暂不发飞书：

```bash
tools/monitor/fall_watch.py --port /dev/ttyACM0 --panel-control --jpeg --backend direct --dry-run --interval 10 --capture-timeout 120
```

运行前在用户终端配置 MIMO_API_KEY。需要飞书告警时配置 FEISHU_WEBHOOK_URL 并去掉 --dry-run；密钥和 webhook 不写进源码。电脑需要保持运行，板子按钮不会自行启动电脑上的进程。此更新未提交/推送。
