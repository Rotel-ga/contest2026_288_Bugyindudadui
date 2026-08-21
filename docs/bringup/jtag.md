# JTAG reset/halt Bring-up 文档

## 概述

在 ESP32-P4X-Function-EV-Board V1.6 上，通过 J20 板载 USB-Serial/JTAG 接口，
使用 Espressif 版 OpenOCD（`openocd-esp32`）识别 ESP32-P4 target，并完成
`reset` 与 `halt`。退出 OpenOCD 后开发板仍可正常复位进入 NSH。

> 本任务不修改板级代码，仅使用 openocd-esp32 自带的 ESP32-P4 配置与 J20
> 内置 USB-JTAG 通道，因此不涉及 defconfig / 源码改动。

## 工具环境

| 项目 | 结论 | 依据 |
| --- | --- | --- |
| OpenOCD 类型 | Espressif `openocd-esp32`（非 upstream） | 任务硬性要求 |
| OpenOCD 版本 | `Open On-Chip Debugger v0.12.0-esp32-20260703 (2026-07-03-13:49)` | `openocd --version` |
| 安装根目录 | `~/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260703/` | 本机实测 |
| openocd 可执行 | `<安装根目录>/bin/openocd` | 本机实测 |
| scripts 目录 | `<安装根目录>/share/openocd/scripts` | 本机实测 |
| board cfg | `board/esp32p4-builtin.cfg` | 走 J20 内置 USB-JTAG |
| target cfg | `target/esp32p4.cfg` | ESP32-P4 target 定义 |
| 调试通道 | J20 USB-Serial/JTAG（内置） | `esp32p4-builtin.cfg` source `interface/esp_usb_jtag.cfg` |

> 说明：`board/esp32p4-builtin.cfg` 内部 `source interface/esp_usb_jtag.cfg` +
> `source target/esp32p4.cfg`，即使用芯片内置 USB-JTAG，不需要外接 FTDI 调试器。

## 执行命令

以下路径为本机实际安装位置，非占位符。若在其他机器执行，请按实际 openocd-esp32
安装目录替换。

```bash
OCD_ROOT=$HOME/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260703

"$OCD_ROOT/bin/openocd" \
  -s "$OCD_ROOT/share/openocd/scripts" \
  -c 'set ESP_RTOS hwthread; set ESP_ONLYCPU 1' \
  -f board/esp32p4-builtin.cfg \
  -c 'init; reset halt; esp appimage_offset 0x2000' \
  2>&1 | tee docs/bringup/openocd.log
```

### 参数说明

| 参数 | 作用 |
| --- | --- |
| `-s <scripts>` | 指定 Espressif 版脚本搜索目录，确保加载的是 esp-openocd 而非 upstream |
| `set ESP_RTOS hwthread` | 以硬件线程方式呈现，配合 NuttX 调试 |
| `set ESP_ONLYCPU 1` | 只调试 1 个核（P4 为双核，先单核简化 bring-up） |
| `-f board/esp32p4-builtin.cfg` | 走 J20 板载 USB-JTAG（非外接 FTDI） |
| `init` | 初始化 OpenOCD 与 JTAG 扫描链 |
| `reset halt` | 复位后立即暂停 CPU（验收核心动作） |
| `esp appimage_offset 0x2000` | 告知镜像烧录偏移，与本项目 `0x2000` 一致 |

## 复现步骤

以下步骤相对 openvela 工程根目录执行。

### 1. 确认串口与 USB-JTAG 权限（首次）

J20 枚举为 `/dev/ttyACMx`。当前用户需在 `dialout` 组：

```bash
sudo usermod -aG dialout $USER   # 加组后需重新登录或 newgrp dialout 生效
```

> 权限说明（务必区分两类访问）：
> - `dialout` 组**只解决串口设备**（`/dev/ttyACMx`）访问，即 esptool 烧录 / picocom 串口。
> - **OpenOCD 走 libusb 直接访问 J20 的 USB-JTAG 通道**，不经过 `/dev/ttyACMx`，因此
>   `dialout` 组对它无效。若无对应 udev 规则，OpenOCD 可能报 `LIBUSB_ERROR_ACCESS`。
> - 正确做法：安装 Espressif OpenOCD 自带的 udev 规则（`contrib/60-openocd.rules`）到
>   `/etc/udev/rules.d/`，然后重新加载并**拔插开发板**使规则生效：
>   ```bash
>   sudo cp "$OCD_ROOT/share/openocd/contrib/60-openocd.rules" /etc/udev/rules.d/
>   sudo udevadm control --reload-rules && sudo udevadm trigger
>   # 重新拔插 J20 USB 线
>   ```
> - 临时应急方案：直接 `sudo` 执行 OpenOCD（本文 `run_openocd_jtag.sh` 即以 sudo 运行）。
>   长期建议用 udev 规则，避免每次 sudo。

### 2. 确认 chip-id（可选，确认板子在线）

```bash
PATH="$HOME/.local/bin:$PATH" esptool --chip esp32p4 --port /dev/ttyACM2 chip-id
```

### 3. 启动 OpenOCD 并 reset/halt

执行「执行命令」小节中的整条命令，日志同时写入 `docs/bringup/openocd.log`。

### 4. 退出 OpenOCD 后确认可复位进 NSH

退出 OpenOCD（Ctrl-C），复位开发板，通过 `/dev/ttyACMx` 以 115200 连接确认进入 NSH：

```bash
picocom -b 115200 --noreset /dev/ttyACM2
```

## 验收标准对照

| 验收项 | 状态 | 证据 |
| --- | --- | --- |
| OpenOCD 确认识别 ESP32-P4 target | ✅ 通过 | `JTAG tap: esp32p4.tap0/1`、`Examined RISC-V core`、`Chip revision v3.2`、`Examination succeed` |
| 能执行 `reset halt` | ✅ 通过 | `Reset cause (24) - (JTAG CPU reset)` |
| 日志中能看到目标暂停 | ✅ 通过 | `targets` 表 State 列 `halted`；`reg pc` 读出 `pc (/32): 0x4fc00b10`（halted 才可读） |
| 退出后可复位进 NSH | ✅ 通过 | 按 reset 键后经 `/dev/ttyACM2` 115200 正常进入 `nsh>` |
| 未把 upstream OpenOCD 失败误判为 P4 不支持 | ✅ 规避 | 全程使用 openocd-esp32 v0.12.0-esp32 |

### 关键日志摘录（docs/bringup/openocd.log）

```plaintext
Info : JTAG tap: esp32p4.tap0 tap/device found: 0x00012c25 (mfg: 0x612 (Espressif Systems), part: 0x0012)
Info : [esp32p4] Examined RISC-V core
Info : [esp32p4]  XLEN=32, misa=0x40903127
Info : [esp32p4] Chip revision v3.2
Info : [esp32p4] Examination succeed
Info : [esp32p4] Reset cause (24) - (JTAG CPU reset)
=== TARGET STATE AFTER reset halt ===
 0* esp32p4            esp32p4    little esp32p4.tap1       halted
=== PC (readable only when halted) ===
pc (/32): 0x4fc00b10
```

## 当前验证状态

### 已完成（真机验证通过）

- 确认本机已安装 Espressif `openocd-esp32 v0.12.0-esp32-20260703`
- 定位 `board/esp32p4-builtin.cfg`、`target/esp32p4.cfg` 实际路径
- OpenOCD 通过 J20 内置 USB-JTAG 识别 ESP32-P4 target（Chip revision v3.2）
- `reset halt` 成功，`targets` 显示 State=`halted`，`reg pc` 读出 `0x4fc00b10`
- 退出 OpenOCD 后按 reset 复位，开发板正常重新进入 NSH
- 完整日志已采集至 `docs/bringup/openocd.log`

### 结论

**成功**：Espressif openocd-esp32 通过 J20 内置 USB-JTAG 完成 ESP32-P4（rev v3.2）
的 target 识别与 `reset halt`，目标暂停状态有显式证据（halted + pc），退出后
开发板可正常复位进入 NSH。JTAG 通道可用。

## 分支与提交

- 分支：`feat/esp32p4-jtag-uart0-final`
- 基线 commit：`1d1d168`（upstream/dev-ai-contest-2026，已含官方 Timer follow-up）
