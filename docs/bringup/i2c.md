# ES8311 I2C1 最小探测验收

## 1. 目标

本实验只验证 ESP32-P4X-Function-EV-Board V1.6 上板载 ES8311 的 I2C 控制通路，不实现完整音频播放或录音。

验收目标：

- ESP32-P4 revision v3.2 能启动该 I2C 实验镜像；
- I2C1 初始化成功；
- SDA 使用 GPIO7，SCL 使用 GPIO8；
- 注册 `/dev/i2c1`；
- 使用 `i2ctool` 扫描时，ES8311 的 7 位地址 `0x18` 稳定响应；
- 连续三次扫描结果一致。

本实验不包含：I2S 音频链路、ES8311 codec 驱动、audio upper-half、麦克风采集、功放或扬声器验证。

## 2. 硬件与参数

| 项目 | 值 | 说明 |
| --- | --- | --- |
| 开发板 | ESP32-P4X-Function-EV-Board V1.6 | 实物 PCB 丝印 |
| 芯片 | ESP32-P4 revision v3.2 | 通过 esptool 实测 |
| 控制器 | I2C1 / port 1 | 参照 Espressif BSP 默认 `BSP_I2C_NUM=1` |
| SDA | GPIO7 | Espressif BSP `BSP_I2C_SDA` |
| SCL | GPIO8 | Espressif BSP `BSP_I2C_SCL` |
| ES8311 扫描地址 | `0x18` | 7 位地址；Espressif codec 源码的 `0x30` 经过 `>> 1` 得到 `0x18` |
| 控制台 | J20 USB-Serial/JTAG | Linux 设备通常为 `/dev/ttyACM0` |
| 串口参数 | 115200 8N1 | 无硬件流控 |
| 烧录偏移 | `0x2000` | 当前 Simple Boot 镜像布局 |

不要把 codec 源码中的 `0x30` 直接作为 `i2ctool` 扫描地址。`i2ctool` 使用 7 位地址，因此目标是 `0x18`。

## 3. 代码与分支

实验分支：

```text
feat/p1-i2c-es8311
```

实验 worktree：

```text
/home/mi/openvela-contest/contest2026_288_Bugyindudadui-p1-i2c-es8311
```

基线：

```text
d611f97 (upstream/dev-ai-contest-2026)
```

主要代码：

```text
board/contest_board/src/esp32p4_board_i2c.c
board/contest_board/src/esp32p4_bringup.c
board/contest_board/src/esp32p4-function-ev-board.h
board/contest_board/src/CMakeLists.txt
board/contest_board/src/Makefile
board/contest_board/configs/i2c/defconfig
```

证据文件：

```text
docs/bringup/i2c_build.log
docs/bringup/i2c_config.txt
docs/bringup/i2c_chip_id.log
docs/bringup/i2c_flash.log
docs/bringup/i2c_image.sha256
docs/bringup/i2c_console_probe.log
docs/bringup/i2c_tool_help.log
docs/bringup/i2c_scan_3x.log
```

## 4. 验收前检查

以下命令在 openvela 工程根目录执行：

```bash
cd /home/mi/openvela-contest

ls -l /dev/ttyACM0
id -nG

readlink -f vendor/openvela/boards/contest2026_288_board
```

应满足：

- `/dev/ttyACM0` 存在；
- 当前用户属于 `dialout` 组；
- 正常情况下 `vendor/openvela/boards/contest2026_288_board` 指向正式参赛仓的 `board/contest_board`。

不要使用以下方式绕过设备权限：

```bash
sudo chmod 666 /dev/ttyACM0
```

## 5. 构建验收

### 5.1 保护原工作区并临时映射实验 worktree

openvela 工程通过 manifest linkfile 使用：

```text
vendor/openvela/boards/contest2026_288_board
```

如果该链接当前指向正式参赛仓，需要临时指向 I2C 实验 worktree 才能构建本分支。使用下面的单条命令，退出时会自动恢复原链接：

```bash
set -euo pipefail

link=/home/mi/openvela-contest/vendor/openvela/boards/contest2026_288_board
original=$(readlink "$link")
restore_link() { ln -sfn "$original" "$link"; }
trap restore_link EXIT

ln -sfn \
  /home/mi/openvela-contest/contest2026_288_Bugyindudadui-p1-i2c-es8311/board/contest_board \
  "$link"

PATH="$HOME/.local/bin:$PATH" \
  /home/mi/openvela-contest/build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/i2c distclean

bash \
  /home/mi/openvela-contest/contest2026_288_Bugyindudadui-p1-i2c-es8311/board/contest_board/tools/prepare_esp_hal.sh

git -C \
  /home/mi/openvela-contest/contest2026_288_Bugyindudadui-p1-i2c-es8311/board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls

PATH="$HOME/.local/bin:$PATH" \
  /home/mi/openvela-contest/build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/i2c
```

如果已经完成构建，只需要重建而不需要 `distclean`，可以使用同样的临时 link 保护方式直接执行最后一条 `build.sh` 命令。

### 5.2 构建成功标准

必须同时满足：

```text
命令退出码为 0
Generated: nuttx.bin
```

`prepare_esp_hal.sh` 会先执行补丁的 reverse-check；未打补丁时日志中可能出现一次“补丁未应用”或 `git apply --reverse --check` 失败提示。只有脚本随后正向应用成功、整体命令退出码为 0，并最终出现 `Generated: nuttx.bin`，才能判定构建成功。不要只凭该中间提示判定构建失败或成功。

构建后检查：

```bash
sha256sum /home/mi/openvela-contest/nuttx/nuttx.bin
stat -c 'image_size=%s' /home/mi/openvela-contest/nuttx/nuttx.bin
```

本次已验收镜像：

```text
image_size=235388
sha256=ac4f13a2ace3efb007aa7bca84824555bf51ce8e9a57417bb47bde87f243d4df
```

检查展开后的配置：

```bash
rg -n -S \
  'CONFIG_ESPRESSIF_I2C1=y|CONFIG_ESPRESSIF_I2C1_MASTER_MODE=y|CONFIG_ESPRESSIF_I2C1_SCLPIN=8|CONFIG_ESPRESSIF_I2C1_SDAPIN=7|CONFIG_I2C_DRIVER=y|CONFIG_SYSTEM_I2CTOOL=y|CONFIG_I2CTOOL_MINBUS=1|CONFIG_I2CTOOL_MAXBUS=1|CONFIG_I2CTOOL_DEFFREQ=100000' \
  /home/mi/openvela-contest/nuttx/.config
```

必须确认 I2C1 master、SCL 8、SDA 7、I2C character driver、i2ctool 和 bus 1 范围均存在。

## 6. 烧录验收

确认使用的是 I2C 实验镜像后执行：

```bash
cd /home/mi/openvela-contest
PATH="$HOME/.local/bin:$PATH" \
  esptool --chip esp32p4 \
  --port /dev/ttyACM0 \
  --baud 921600 \
  write-flash 0x2000 nuttx/nuttx.bin
```

烧录成功标准：

```text
Connected to ESP32-P4 on /dev/ttyACM0
Chip type: ESP32-P4 (revision v3.2)
Hash of data verified.
Hard resetting via RTS pin...
```

烧录期间不要执行擦除 eFuse、Secure Boot、Flash Encryption 或写入密钥等操作。

## 7. NSH 验收

烧录复位后，J20 设备可能短暂断开并重新枚举。设备重新出现后打开控制台：

```bash
picocom -b 115200 --noreset /dev/ttyACM0
```

按开发板复位键，或者在烧录自动复位后等待启动输出。应看到启动 banner 和：

```text
nsh>
```

在 NSH 执行：

```text
help
ls /dev
i2c
```

验收标准：

1. `help` 正常返回；
2. builtin app 列表中包含 `i2c`；
3. `ls /dev` 中包含：

```text
/dev/i2c1
gpio0
ttyACM0
```

4. `i2c` 显示 `dev` 子命令及扫描相关说明；
5. `i2c` 帮助中的 sticky 选项显示当前 bus 为 `1`、当前频率为 `100000`，例如：

```text
[-b bus] ... Default: 1 Current: 1
[-f freq] ... Default: 100000 Current: 100000
```

不同版本的 i2ctool 输出格式可能略有差异，以 `/dev/i2c1` 存在和扫描结果为准。

## 8. ES8311 扫描验收

先确认 i2ctool 的实际帮助：

```text
i2c
```

扫描命令为：

```text
i2c dev 0x03 0x77
```

该命令扫描 7 位地址范围 `0x03` 到 `0x77`。执行三次：

```text
i2c dev 0x03 0x77
i2c dev 0x03 0x77
i2c dev 0x03 0x77
```

成功标准：

- 三次命令均回到 `nsh>`；
- 三次结果均出现 `0x18`；
- 三次结果一致；
- 若出现其他地址，也必须记录，不能直接全部称为 ES8311；
- 本板本次实测只出现 `0x18`。

典型成功输出：

```text
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- 18 -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
...
70: -- -- -- -- -- -- -- --
nsh>
```

注意：扫描仅用于确认设备地址响应，不执行 ES8311 寄存器写入。不要使用 `i2c set`、`i2c verf` 或其他写寄存器命令作为本阶段验收步骤。

## 9. 结果判定矩阵

| 验收层级 | 必要结果 | 判定 |
| --- | --- | --- |
| 构建 | 退出码 0，生成 `nuttx.bin` | 软件构建通过 |
| 参数 | `.config` 明确 I2C1/SCL8/SDA7 | 配置通过 |
| 烧录 | 芯片为 P4 v3.2，`Hash of data verified.` | 镜像写入通过 |
| 启动 | 出现 banner 和 `nsh>` | 固件启动通过 |
| 设备节点 | 存在 `/dev/i2c1` | 板级 I2C 注册通过 |
| 工具 | builtin app 包含 `i2c` | i2ctool 接入通过 |
| 地址 | 三次扫描均响应 `0x18` | ES8311 I2C 探测通过 |

只有完成最后一行，才能将本实验描述为“已在真机确认 ES8311 I2C 响应”。仅有编译成功、`/dev/i2c1` 存在或底层源文件存在，都不能替代地址扫描证据。

## 10. 故障排查

### 没有 `/dev/i2c1`

检查：

```text
CONFIG_I2C_DRIVER=y
CONFIG_ESPRESSIF_I2C1=y
CONFIG_ESPRESSIF_I2C1_MASTER_MODE=y
```

并确认 `board_i2c_init()` 已在 bring-up 中执行。不要把 I2C0 的默认配置当成 I2C1。

### 扫描没有 `0x18`

按以下顺序检查：

1. SDA/SCL 是否接反或被外部设备占用；
2. 是否实际烧录了 I2C 配置生成的镜像；
3. `.config` 是否仍为 SDA7/SCL8；
4. 板卡是否上电，ES8311 电源和复位条件是否正常；
5. 是否在正确的 J20 控制台执行了 bus 1 扫描；
6. 保存完整 NSH 原始日志后再重启并重复扫描。

不要因为扫描失败就猜测新的 GPIO、I2C 控制器或地址，也不要直接改用 `0x30` 扫描。

### `picocom` 立即退出

如果从脚本或非交互终端启动 `picocom`，可能出现 `STDIN is not a TTY` 并立即退出。这是采集方式问题，不是设备启动失败。交互式验收应在真实终端运行 `picocom`；自动采集可使用带延时的管道并保存原始输出，但必须检查输出中是否有 `nsh>`。

## 11. 本次验收结果

本次在 ESP32-P4X-Function-EV-Board V1.6、ESP32-P4 revision v3.2 真机上完成：

- J20 `/dev/ttyACM0` 芯片识别；
- I2C 实验镜像烧录到 `0x2000`；
- `Hash of data verified.`；
- 启动进入 NSH；
- `/dev/i2c1` 存在；
- `i2c` builtin app 可用；
- 三次 `i2c dev 0x03 0x77` 扫描均发现 `0x18`；
- 未发现其他响应地址。

原始日志见本目录中的 `i2c_*` 证据文件。

## 12. 当前边界

本结果只证明：在当前板卡、当前镜像、当前 I2C1/SDA7/SCL8 配置下，7 位地址 `0x18` 的设备能稳定响应 I2C 扫描。

它不证明：

- ES8311 全部寄存器配置正确；
- I2S 音频数据链路可用；
- 麦克风采集可用；
- 功放、扬声器或音频播放可用。
