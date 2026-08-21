# 物理 UART0 控制台 Bring-up 文档

## 概述

在 ESP32-P4X-Function-EV-Board V1.6 上，新建独立配置 `configs/uart0`，将 NSH 控制台
从 J20 USB-Serial/JTAG 切换到芯片物理 UART0（GPIO37/GPIO38），并通过外接 3.3V
USB-UART 完成真机验收。`nsh` 与 `uart0` 两套配置并存、互不覆盖。

## 硬件核对

| 项目 | 结论 | 依据 |
| --- | --- | --- |
| UART0 TX | GPIO37 / U0TXD | 官方 J1 排针第 8 脚；`ESPRESSIF_UART0_TXPIN` 默认 37 |
| UART0 RX | GPIO38 / U0RXD | 官方 J1 排针第 10 脚；`ESPRESSIF_UART0_RXPIN` 默认 38 |
| GND | J1 第 9 脚 | 位于第8/10脚之间，就近取地 |
| 电平 | 3.3V | P4 IO 域 3.3V；USB-UART 的 LEVEL SEL 跳线必须设 3.3V |
| USB-UART 模块 | FT232RL（MCS-73 LV） | 枚举为 `/dev/ttyUSB0` |

> 说明：本板 v1.52+ 已移除板载 USB-to-UART，改用内置 USB-Serial/JTAG，因此物理
> UART0 验证必须外接 USB-UART 模块。

## 接线（TX/RX 交叉、共地、不接 VCC）

| ESP32-P4 J1 | FT232RL 模块 |
| --- | --- |
| 第8脚 GPIO37 / U0TXD | RX |
| 第10脚 GPIO38 / U0RXD | TX |
| 第9脚 GND | GND |

- 模块 VCC 不接（板子经 J20 自供电）。
- 接线前核对 V1.6 原理图。接线照片和 FT232RL 3.3V 电平设置照片已在群里分享。

## 配置改动清单

| 文件 | 操作 | 说明 |
| --- | --- | --- |
| `board/contest_board/configs/uart0/defconfig` | 新建 | 由 `nsh/defconfig` 复制修改 |

`uart0/defconfig` 相对 `nsh` 的关键差异：

- 新增 `CONFIG_ESPRESSIF_UART0=y`（启用 UART0，select UART0 串口驱动）
- 新增 `CONFIG_UART0_SERIAL_CONSOLE=y`（控制台指向 UART0）
- 移除 `CONFIG_ESPRESSIF_USBSERIAL=y`（不再用 USB-Serial-JTAG 作控制台）
- 引脚默认 37/38、波特率默认 `CONFIG_UART0_BAUD=115200`，无需显式设置

## 复现步骤

以下相对 openvela 工程根目录执行。

### 1. 准备 ESP HAL（必做）

```bash
bash board/contest_board/tools/prepare_esp_hal.sh
git -C board/contest_board/chip/esp-hal-3rdparty submodule update --init components/mbedtls/mbedtls
```

> 网络受限时可临时对当前终端设置 github 代理：
> `export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0='url.https://ghfast.top/https://github.com/.insteadOf' GIT_CONFIG_VALUE_0='https://github.com/'`
> 注意：勿对 `chip/esp-hal-3rdparty` 手动 distclean，一旦删除需重新克隆 + 初始化 mbedtls 子模块。

### 2. 构建 uart0

```bash
PATH="$HOME/.local/bin:$PATH" ./build.sh vendor/openvela/boards/contest2026_288_board/configs/uart0
```

成功标志：`Generated: nuttx.bin`。

### 3. 经 J20 烧录

```bash
PATH="$HOME/.local/bin:$PATH" esptool --chip esp32p4 --port /dev/ttyACM2 --baud 921600 \
  write-flash 0x2000 nuttx/nuttx.bin
```

成功标志：`Hash of data verified.`（同时验证 J20 仍可烧录）。

### 4. 从物理 UART0 打开控制台

```bash
sudo picocom -b 115200 /dev/ttyUSB0
```

按板子 reset 键，观察启动 banner 与 `nsh>`。

> 注意：`minicom` 默认开启硬件流控（RTS/CTS）会导致打开后空白；使用 `picocom`
> （默认无流控）或在 minicom 关闭 Hardware Flow Control。

## 最新基线（1d1d168）复测状态

> 本交付分支 `feat/esp32p4-jtag-uart0-final` 基于官方最新基线 `1d1d168`（已含 Timer
> follow-up）。以下项目**已在该基线上重新真机验证**（构建 commit `e1b39fe`）：

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 最新基线上 `uart0` clean build | ✅ 通过 | distclean → prepare_esp_hal → mbedtls 子模块 → build，`Generated: nuttx.bin`，证据 `uart0_build.log` |
| `nuttx/.config` 关键项核对 | ✅ 通过 | 见下方"关键配置核对"，全部命中，证据 `uart0_config.txt` |
| J20 烧录 + hash 校验 | ✅ 通过 | `Hash of data verified.`，见下方"烧录记录"，证据 `uart0_flash.log`、`uart0_image.sha256` |
| UART0 reboot 前后**连续原始日志** | ✅ 通过 | 单串口连续采集，含 BOOT→BASIC→TIMER→REBOOT→AFTER REBOOT，证据 `uart0_serial_raw.log` |
| UART0 timer 回归（sleep/usleep/uptime） | ✅ 通过 | `sleep 1`=1.0100s、`sleep 10`=10.0100s、`usleep 500000`=0.5100s，见 `uart0_serial_raw.log` |

### 关键配置核对（构建后 grep `nuttx/.config`）

```bash
grep -E 'CONFIG_(ONESHOT|ONESHOT_COUNT|UART0_SERIAL_CONSOLE|ESPRESSIF_UART0|ESPRESSIF_USBSERIAL|SCHED_TICKLESS|ALARM_ARCH|TIMER_ARCH)' nuttx/.config
```

至少应确认：

```plaintext
CONFIG_ONESHOT=y
CONFIG_ONESHOT_COUNT=y
CONFIG_UART0_SERIAL_CONSOLE=y
CONFIG_ESPRESSIF_UART0=y
# CONFIG_ESPRESSIF_USBSERIAL is not set
# CONFIG_SCHED_TICKLESS is not set
# CONFIG_ALARM_ARCH is not set
```

> `CONFIG_ESPRESSIF_UART0` 在当前 Kconfig 中默认启用，可能不会显式出现在 `defconfig`
> 文件中，请以最终展开的 `nuttx/.config` 为准，不要为让 defconfig 出现该行而改 Kconfig。

### 烧录记录

| 字段 | 值 |
| --- | --- |
| 烧录 commit | `e1b39fe`（`feat/esp32p4-jtag-uart0-final`，基于 `1d1d168`） |
| 芯片 revision | ESP32-P4 revision v3.2（`esptool` 实测，USB mode: USB-Serial/JTAG） |
| 镜像 sha256 | `0b09327d59cd8ffdcb6102cf78e3ef69fd7a5045a96725e08886af91a7c7ea57` |
| 镜像大小 | 229492 bytes |
| 烧录偏移 | `0x2000`（ESP32-P4 Simple Boot RAM image） |
| esptool 校验 | `Hash of data verified.`（见 `uart0_flash.log`） |

## 验收标准对照

| 验收项 | 状态 | 证据 |
| --- | --- | --- |
| J20 仍可用于烧录 | ✅ 通过 | 本固件经 J20 烧录，`Hash of data verified` |
| UART0 见完整 banner 和 NSH | ✅ 通过 | `uart0_log.txt` 开机日志到 `nsh>` |
| 能执行基础命令 | ✅ 通过 | `help`/`uname -a`/`free`/`ps` 输出见日志 |
| reboot 后 UART0 再次出现输出 | ✅ 通过 | 连续原始日志中 `nsh> reboot` → `reboot status=0` → `ESP-ROM...` → `*** Booting NuttX ***` → `NuttShell (NSH)`，见 `uart0_serial_raw.log` |
| USB console 与 UART0 配置不互相覆盖 | ✅ 通过 | `nsh` 与 `uart0` 为两套独立 defconfig，各自可构建 |

## 关键日志摘录

以下摘自最新基线（`1d1d168`，构建 commit `e1b39fe`）UART0 连续原始日志 `uart0_serial_raw.log`：

```plaintext
*** Booting NuttX ***
NuttShell (NSH)
nsh> uname -a
NuttX 0.0.0 dd92bcf4257 Aug 21 2026 20:14:31 risc-v contest2026_288_board
nsh> time "sleep 1"
1.0100 sec
nsh> time "sleep 10"
10.0100 sec
nsh> time "usleep 500000"
0.5100 sec
nsh> reboot
reboot status=0
ESP-ROM:esp32p4-eco7-20260109
*** Booting NuttX ***
NuttShell (NSH)
nsh>
```

完整连续日志见 `docs/bringup/uart0_serial_raw.log`（含 help/free/ps/uptime 全量输出）。

## 结论

**成功**：新建 `configs/uart0` 将控制台切至物理 UART0（GPIO37/GPIO38 @115200），
经 J20 烧录后，通过外接 FT232RL USB-UART 在 3.3V 电平下从物理串口进入 NSH，
基础命令与 reboot 均正常，且不影响原 `nsh`（USB console）配置。

## 分支与提交

- 分支：`feat/esp32p4-jtag-uart0-final`
- 基线 commit：`1d1d168`（upstream/dev-ai-contest-2026，已含官方 Timer follow-up）
- 交付物：`configs/uart0/defconfig`、`docs/bringup/uart0.md`、`docs/bringup/uart0_log.txt`
- 接线照片和 FT232RL 3.3V 电平设置照片已在群里分享。
