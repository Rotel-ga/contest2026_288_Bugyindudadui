# openvela on ESP32-P4X

本项目面向 openvela 2026 AI 硬件开发者大赛“新硬件平台适配”赛道，目标是在 ESP32-P4X-Function-EV-Board V1.6 上完成可复现的 openvela 基础板级适配。

当前已在 ESP32-P4 revision v3.2 真机上完成：

- out-of-tree ESP32-P4 芯片层和板级目录接入；
- openvela clean build 和 ESP Simple Boot 镜像生成；
- J20 USB-Serial/JTAG 芯片识别和 `0x2000` 烧录；
- USB 控制台启动进入 NSH；
- `help`、`uname -a`、`free`、`ps`、`reboot` 真机验证。

当前达到 M1（NSH 跑通）。GPIO、Timer 定量测试、OpenOCD reset/halt、物理 UART0 和稳定性测试仍在推进，不将这些项目标记为已完成。

## 目录结构

```text
board/contest_board/     ESP32-P4X 芯片层、板级代码、配置和链接脚本
docs/bringup/            芯片识别、烧录和 NSH 原始验收日志
logs/                    按大赛规范导出的 AI Coding 日志
contest2026_288_Bugyindudadui.xml
                         repo manifest 和板级 linkfile 映射
```

`app/` 和 `quickapp/` 为组委会初始模板，本项目当前不以它们作为参赛成果。

## 获取工程

```bash
repo init -u https://github.com/open-vela/contest2026_288_Bugyindudadui \
  -b dev-ai-contest-2026 \
  -m contest2026_288_Bugyindudadui.xml
repo sync -c -j8
```

同步后，manifest 将 `board/contest_board` 映射到 `vendor/openvela/boards/contest2026_288_board`。

## 准备 ESP HAL

ESP32-P4 构建依赖固定版本的 `esp-hal-3rdparty`。该大型第三方仓不提交到专属参赛仓，使用仓内脚本克隆固定 commit 并应用 openvela API 兼容补丁：

```bash
cd contest2026_288_Bugyindudadui
./board/contest_board/tools/prepare_esp_hal.sh
```

固定版本：

```text
b90b1837cb5ad24747deb4c895246037cc206ce5
```

## 构建

主机要求 Linux x86_64。先安装 esptool，并确保其入口在 `PATH`：

```bash
python3 -m pip install --user esptool
cd ..

PATH="$HOME/.local/bin:$PATH" \
  ./build.sh vendor/openvela/boards/contest2026_288_board/configs/nsh
```

成功标志为 `Generated: nuttx.bin`，产物位于 `nuttx/nuttx.bin`。当前配置生成 ESP32-P4 Simple Boot RAM image，烧录偏移为 `0x2000`。

## 识别和烧录

将开发板 J20 USB Serial/JTAG 接口连接到主机，确认实际设备节点后执行：

```bash
esptool --chip esp32p4 --port /dev/ttyACM0 chip-id

esptool --chip esp32p4 \
  --port /dev/ttyACM0 \
  --baud 921600 \
  write-flash 0x2000 nuttx/nuttx.bin
```

当前实测硬件为 ESP32-P4 revision v3.2。不要根据贴纸跳过芯片识别，也不要在未核对镜像布局时更改烧录偏移。

## NSH 验收

通过 J20 的 `/dev/ttyACM0` 以 115200 波特率连接，执行：

```text
help
uname -a
free
ps
reboot
```

`reboot` 时 USB-Serial/JTAG 会短暂断开并重新枚举。原始真机证据见 `docs/bringup/`。

## 已知限制

- 当前只验证 J20 USB-Serial/JTAG 控制台，物理 UART0 GPIO37/GPIO38 尚未验收；
- GPIO、Timer 定量测试、JTAG reset/halt 和冷启动稳定性尚未完成；
- `esp-hal-3rdparty` 当前仍有一个 `nxsched_usleep` 声明兼容警告，但不阻塞构建和 NSH；
- 不提交 `esp-hal-3rdparty` 整仓、编译产物或公共 `nuttx/` 仓改动。

## AI Coding

项目在需求收敛、源码盘点、openvela API 兼容、构建排障、真机验收和文档整理阶段使用了 AI 辅助。按大赛规范导出的原始会话位于 `logs/`，其 manifest 记录会话文件和脱敏状态。
