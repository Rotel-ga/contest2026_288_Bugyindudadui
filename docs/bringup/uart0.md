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
- 接线前核对 V1.6 原理图；接线照片见 `docs/bringup/`（`uart0_wiring*.jpg`）。

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

## 验收标准对照

| 验收项 | 状态 | 证据 |
| --- | --- | --- |
| J20 仍可用于烧录 | ✅ 通过 | 本固件经 J20 烧录，`Hash of data verified` |
| UART0 见完整 banner 和 NSH | ✅ 通过 | `uart0_log.txt` 开机日志到 `nsh>` |
| 能执行基础命令 | ✅ 通过 | `help`/`uname -a`/`free`/`ps` 输出见日志 |
| reboot 后 UART0 再次出现输出 | ✅ 通过 | `reboot` 后重新打印 ESP-ROM banner → NSH |
| USB console 与 UART0 配置不互相覆盖 | ✅ 通过 | `nsh` 与 `uart0` 为两套独立 defconfig，各自可构建 |

## 关键日志摘录

```plaintext
*** Booting NuttX ***
NuttShell (NSH)
nsh> uname -a
NuttX 0.0.0 dd92bcf4257 Aug 14 2026 18:49:58 risc-v contest2026_288_board
```

完整日志见 `docs/bringup/uart0_log.txt`。

## 结论

**成功**：新建 `configs/uart0` 将控制台切至物理 UART0（GPIO37/GPIO38 @115200），
经 J20 烧录后，通过外接 FT232RL USB-UART 在 3.3V 电平下从物理串口进入 NSH，
基础命令与 reboot 均正常，且不影响原 `nsh`（USB console）配置。

## 分支与提交

- 分支：`zhangyuxuan-openvela`
- 交付物：`configs/uart0/defconfig`、`docs/bringup/uart0.md`、`docs/bringup/uart0_log.txt`、接线照片
