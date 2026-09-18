# VelaP4X：ESP32-P4X openvela 板级适配与一键自检平台

本项目面向 openvela 2026 AI 硬件开发者大赛“新硬件平台适配”赛道，在 **ESP32-P4X-Function-EV-Board V1.6** 上完成可复现的 openvela 基础板级适配、自检应用和验收证据。

- 官方仓库：https://github.com/open-vela/contest2026_288_Bugyindudadui
- 提交分支：`dev-ai-contest-2026`
- 功能验收基线：`35a953cc3673c0329b6a8de569604d492e1b64f0`
- 自建 Skill / LICENSE：PR [#12](https://github.com/open-vela/contest2026_288_Bugyindudadui/pull/12)
- 目标开发板：ESP32-P4X-Function-EV-Board V1.6
- 实测芯片：ESP32-P4 revision v3.2

## 已完成功能

### P0：基础板级能力

- out-of-tree ESP32-P4 芯片层、比赛板级目录和 manifest `<linkfile>` 接入；
- openvela clean build、`nuttx.bin` 和 ESP Simple Boot 镜像生成；
- J20 USB Serial/JTAG 控制台；
- 物理 UART0 控制台：GPIO37/U0TXD、GPIO38/U0RXD；
- NSH 启动及 `help`、`uname -a`、`free`、`ps`、`uptime`、`reboot`；
- GPIO4 注册为 `/dev/gpio0`，完成低→高→低软件写入和 readback；
- Timer 定量延时验证；
- Espressif OpenOCD/JTAG target 识别、`reset halt` 和 PC 读取；
- Simple Boot 偏移 `0x2000` 烧录及 `Hash of data verified.`。

### P1：ES8311 I2C 最小探测

- I2C1 SDA：GPIO7；
- I2C1 SCL：GPIO8；
- 设备节点：`/dev/i2c1`；
- 总线频率：100 kHz；
- ES8311 7 位地址：`0x18`；
- 最终基线完整扫描 3/3，仅发现 `0x18`。

该能力只证明 I2C 控制通路和地址响应，不代表 I2S、codec audio upper-half、录音、播放、功放或扬声器已经支持。

### `p4x_selftest` 板卡自检

项目提供独立 `demo` 配置和 NSH 命令：

```text
p4x_selftest
p4x_selftest --json
```

自检覆盖：

- 系统信息；
- 500 ms Timer 定量检查；
- GPIO4 软件低→高→低及 readback；
- GPIO4 物理项（无外部仪器时为 `SKIP`）；
- `/dev/i2c1`、100 kHz、ES8311 `0x18` 一字节读取。

无外部 GPIO 测量夹具时，预期结果为：

```text
PASS=4 FAIL=0 SKIP=1 RESULT=PASS
```

最终验证结果：

- human 模式：3/3 PASS；
- JSON 模式：3/3 可解析且结果一致；
- 每轮均为 4 PASS、0 FAIL、1 SKIP。

## 最终复现结果

2026-09-17 在官方功能基线 `35a953c` 上完成最终复现。

### 四配置 clean build

| 配置 | 结果 | `nuttx.bin` 大小 | 构建耗时 |
| --- | --- | ---: | ---: |
| `nsh` | PASS | 227700 bytes | 2m15s |
| `uart0` | PASS | 229492 bytes | 3m50s |
| `i2c` | PASS | 235388 bytes | 2m44s |
| `demo` | PASS | 237592 bytes | 2m36s |

每套配置均同时满足：

1. 构建退出码为 0；
2. 输出包含 `Generated: nuttx.bin`；
3. `nuttx/nuttx.bin` 非空。

镜像包含 git hash、编译时间和构建路径，因此不同构建的 SHA-256 不保证相同。最终验收同时使用镜像大小、构建判据和真机 `uname -a` 版本串确认镜像身份。

### 真机与稳定性

| 验收项 | 结果 |
| --- | --- |
| ESP32-P4 revision v3.2 识别 | PASS |
| `0x2000` 烧录和 hash 校验 | PASS |
| J20 USB 控制台进入 NSH | PASS |
| 物理 UART0 进入 NSH | PASS |
| Timer 定量延时 | PASS |
| GPIO4 软件链路 | PASS |
| I2C 完整扫描仅 `0x18` | PASS 3/3 |
| `p4x_selftest` human | PASS 3/3 |
| `p4x_selftest --json` | PASS 3/3 |
| JTAG reset/halt/PC | PASS |
| 完整启动 | PASS 22/22 |
| `reboot` | PASS 11/11 |
| 冷启动 | PASS 10/10 |
| 运行期 assert/panic/abort | 0 |
| GPIO4 外部电平 | SKIP，无外部测量夹具 |
| Timer 长时间连续运行统计 | 未执行 |

## 可用配置

```text
board/contest_board/configs/nsh
board/contest_board/configs/uart0
board/contest_board/configs/i2c
board/contest_board/configs/demo
```

| 配置 | 用途 |
| --- | --- |
| `nsh` | J20 USB NSH、Timer、GPIO |
| `uart0` | GPIO37/GPIO38 物理 UART0 控制台 |
| `i2c` | I2C1 和 i2ctool |
| `demo` | I2C1 + `p4x_selftest` |

## 目录结构

```text
app/p4x_selftest/
    板卡一键自检应用

board/contest_board/
    ESP32-P4X 芯片层、板级代码、四套配置和构建脚本

docs/bringup/
    构建、烧录、GPIO、Timer、I2C、UART0、JTAG、自检和原始证据

.claude/skills/esp32p4-repro-check/
    项目自建复现检查 Skill

logs/
    按大赛规范导出的 AI Coding 日志

contest2026_288_Bugyindudadui.xml
    repo manifest 和 linkfile 映射

LICENSE
    Apache License 2.0
```

`quickapp/hello_quickapp` 保留为组委会初始模板，不作为本次参赛成果。

## 获取工程

```bash
repo init \
  -u https://github.com/open-vela/contest2026_288_Bugyindudadui \
  -b dev-ai-contest-2026 \
  -m contest2026_288_Bugyindudadui.xml

repo sync -c -j8
```

manifest 将建立以下关键映射：

```text
board/contest_board
  → vendor/openvela/boards/contest2026_288_board

app/p4x_selftest
  → packages/demos/contest2026_288_p4x_selftest
```

## Clean build

`distclean` 会删除被忽略的 `esp-hal-3rdparty`，必须严格执行：

```text
distclean → 准备 HAL → 初始化 mbedTLS → build
```

示例：

```bash
cd /path/to/openvela
CONFIG=demo  # 可改为 nsh / uart0 / i2c

PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  "vendor/openvela/boards/contest2026_288_board/configs/$CONFIG" \
  distclean

cd contest2026_288_Bugyindudadui
bash board/contest_board/tools/prepare_esp_hal.sh

git -C board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls

cd ..
PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  "vendor/openvela/boards/contest2026_288_board/configs/$CONFIG"
```

成功标志：

```text
Generated: nuttx.bin
```

产物：

```text
nuttx/nuttx.bin
```

ESP HAL 固定版本：

```text
b90b1837cb5ad24747deb4c895246037cc206ce5
```

## 芯片识别与烧录

J20 的 `/dev/ttyACMx` 编号可能随重枚举变化，必须按实际节点设置：

```bash
PORT=/dev/ttyACM0

esptool --chip esp32p4 --port "$PORT" chip-id

esptool --chip esp32p4 \
  --port "$PORT" \
  --baud 921600 \
  write-flash 0x2000 nuttx/nuttx.bin
```

成功判据：

```text
ESP32-P4 revision v3.2
Wrote ... at 0x00002000
Hash of data verified.
```

## 最小验收

### NSH

```text
help
uname -a
free
ps
uptime
time "sleep 1"
time "sleep 10"
time "usleep 500000"
ls /dev
reboot
```

### GPIO4

```text
gpio -o 1 /dev/gpio0
gpio -o 0 /dev/gpio0
```

软件 write/readback 不能替代 GPIO4 外部电平测量。没有万用表、LED、逻辑分析仪或示波器时，物理项必须记录为 `SKIP`。

### I2C

```text
ls /dev/i2c1
i2c dev 0x03 0x77
```

完整扫描至少执行 3 次，预期只发现 7 位地址 `0x18`。

### Selftest

```text
p4x_selftest
p4x_selftest --json
```

### 物理 UART0

| ESP32-P4X J1 | 3.3 V USB-UART |
| --- | --- |
| GPIO37 / U0TXD | RX |
| GPIO38 / U0RXD | TX |
| GND | GND |

要求：

- TX/RX 交叉；
- 共地；
- USB-UART 选择 3.3 V；
- 不连接 VCC；
- 波特率 115200。

## 自建 Skill

项目提供 `esp32p4-repro-check`：

```bash
python3 .claude/skills/esp32p4-repro-check/scripts/check_baseline.py \
  --repo .
```

该 Skill 会以只读方式检查：

- 冻结功能基线是否为当前 HEAD 的祖先；
- 基线 Git tree；
- BSP/selftest 受保护路径是否相对基线保持不变；
- manifest linkfile；
- `demo = i2c + selftest` 配置关系；
- 板级初始化和设备节点契约；
- 已存 selftest 证据哈希。

最终材料 commit 上实测：

```text
PASS=9 FAIL=0 RESULT=PASS
```

## 证据索引

仓内主要证据：

- GPIO：`docs/bringup/gpio.md`、`gpio_log.txt`
- Timer：`docs/bringup/timer_*.log`
- UART0：`docs/bringup/uart0.md`、`uart0_serial_raw.log`
- I2C：`docs/bringup/i2c_post_merge_summary.md`、`i2c_post_merge_*.log`
- JTAG：`docs/bringup/jtag.md`、`openocd.log`
- selftest：`docs/bringup/p4x_selftest.md`、`p4x_selftest_serial_raw.log`、`p4x_selftest_serial_report.json`
- Skill preflight：`docs/bringup/esp32p4_repro_check_preflight.log`

这些文件记录了不同开发阶段的明确 commit 基线；历史日志不会被改写为最终基线结果。

最终基线的完整复现结果、照片和演示过程已纳入比赛技术报告与 Demo 视频。

## AI Coding 日志

仓库中已归集：

```text
logs/Rotel-ga/
```

当前包含 21 个 canonical session、4475 个 events，官方校验为 `ALL OK`。2026-09-03 之后的部分 Kiro 会话因收集能力不完整未上传，技术报告中已如实说明。

## 已知限制

- 未实现完整 ES8311 音频、I2S、录音、播放、功放或扬声器；
- SPI2 Gate 为 STOP，默认引脚未引出 J1 且与 MicroSD/Ethernet 资源冲突；
- 未实现 MIPI-DSI/LVGL、摄像头/ISP/H.264、ESP32-C6 Wi-Fi/BLE；
- 未启用 Secure Boot、Flash Encryption，未写入真实密钥或 eFuse；
- GPIO4 外部电平未实测，物理项为 `SKIP`；
- Timer 长时间连续运行统计未执行；
- `/dev/timer0` 不存在，Timer 证据来自系统时基；
- Simple Boot 启动时 ROM 会打印 `SHA-256 comparison failed`，随后正常继续启动；最终 22/22 次均进入 NSH；
- 不提交 `esp-hal-3rdparty` 整仓、构建产物或公共 `nuttx/` 仓补丁。

## License

本项目使用 Apache License 2.0，详见 [LICENSE](LICENSE)。
