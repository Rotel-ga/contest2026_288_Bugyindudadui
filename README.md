# openvela on ESP32-P4X

本项目面向 openvela 2026 AI 硬件开发者大赛“新硬件平台适配”赛道，在 ESP32-P4X-Function-EV-Board V1.6 上实现可复现的 openvela 基础板级适配。实测芯片为 ESP32-P4 revision v3.2。

当前功能开发已冻结，主线转入最终证据、回归和作品材料收口。

## 当前状态

### P0：基础板级能力

已完成并通过真机验证：

- out-of-tree ESP32-P4 芯片层、板级目录和 manifest `<linkfile>` 接入；
- openvela clean build、`nuttx.bin` 和 ESP Simple Boot RAM image 生成；
- `esptool --chip esp32p4` 芯片识别、`0x2000` 烧录和写入哈希校验；
- J20 USB-Serial/JTAG 控制台和物理 UART0（GPIO37/GPIO38）配置；
- NSH 启动及 `help`、`uname -a`、`free`、`ps`、`reboot`；
- GPIO4 `/dev/gpio0`、Timer 和 OpenOCD/JTAG reset/halt。

最终提交前仍会在合入后的唯一 head 上统一重跑三配置 clean build、双控制台、GPIO4 物理证据、Timer 长稳、冷启动 10 次和 reboot 10 次，避免把不同历史提交上的证据混为同一基线。

### P1：ES8311 I2C 最小探测

[PR #9](https://github.com/open-vela/contest2026_288_Bugyindudadui/pull/9) 已合入 I2C1 最小探测能力，post-merge 基线为 `a0a7451b9a3e78e1b4f5a135288fa8fc5ea43558`：

- SDA：GPIO7；
- SCL：GPIO8；
- 设备节点：`/dev/i2c1`；
- 总线频率：100 kHz；
- ES8311 7 位地址：`0x18`；
- 4 次完整有效扫描结果一致，均只发现 `0x18`。

该能力正式冻结为本次作品的 P1。它只证明 I2C 控制通路和地址响应，不代表 I2S、codec audio upper-half、录音、播放、功放或扬声器已经支持。

### SPI2 Gate：STOP

V1.6 原理图核对表明，默认 SPI2 GPIO15/GPIO29/GPIO30/GPIO31 未引出到 J1，且分别占用板载 MicroSD/Ethernet 资源。本次不创建 SPI 实现分支、不接线、不烧录 SPI 镜像，也不把通用 Kconfig 默认 GPIO 当作板级接线事实。

## 目录结构

```text
board/contest_board/     ESP32-P4X 芯片层、板级代码、配置和链接脚本
docs/bringup/            构建、烧录、控制台、I2C 和真机验收原始证据
logs/Rotel-ga/           按大赛规范导出的 AI Coding 日志
contest2026_288_Bugyindudadui.xml
                         repo manifest 和板级 linkfile 映射
```

`app/` 和 `quickapp/` 为组委会初始模板，本项目不以它们作为参赛成果。

## 获取工程

```bash
repo init -u https://github.com/open-vela/contest2026_288_Bugyindudadui \
  -b dev-ai-contest-2026 \
  -m contest2026_288_Bugyindudadui.xml
repo sync -c -j8
```

同步后，manifest 将 `board/contest_board` 映射到 `vendor/openvela/boards/contest2026_288_board`。

## Clean build

主机要求 Linux x86_64，并需安装 `esptool`：

```bash
python3 -m pip install --user esptool
```

可用配置为 `nsh`、`uart0` 和 `i2c`。`distclean` 会删除被忽略的 `esp-hal-3rdparty`，因此顺序必须是 **distclean → 准备 HAL → 初始化 mbedTLS → build**：

```bash
cd /path/to/openvela
CONFIG=i2c  # 或 nsh / uart0

PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/$CONFIG \
  distclean

cd contest2026_288_Bugyindudadui
bash board/contest_board/tools/prepare_esp_hal.sh

git -C board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls

cd ..
PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/$CONFIG
```

成功判据是构建真实退出状态为 0，且输出包含：

```text
Generated: nuttx.bin
```

产物位于 `nuttx/nuttx.bin`。HAL 固定版本为：

```text
b90b1837cb5ad24747deb4c895246037cc206ce5
```

## 识别和烧录

连接开发板 J20 USB Serial/JTAG 接口，确认实际设备节点后执行：

```bash
esptool --chip esp32p4 --port /dev/ttyACM0 chip-id

esptool --chip esp32p4 \
  --port /dev/ttyACM0 \
  --baud 921600 \
  write-flash 0x2000 nuttx/nuttx.bin
```

烧录通过标准包括 ESP32-P4 revision v3.2、偏移 `0x2000` 和 `Hash of data verified.`。不要根据板卡贴纸跳过芯片识别，也不要在未核对镜像布局时修改烧录偏移。

## 最小验收

### NSH

J20 使用 `/dev/ttyACM0`、115200 波特率。物理 UART0 使用 GPIO37/GPIO38，并应与 J20 证据分开记录。

```text
help
uname -a
free
ps
reboot
```

### I2C

```text
ls /dev/i2c1
i2c dev 0x03 0x77
```

有效扫描必须是完整命令和完整输出；被采集工具截断的命令保留在原始日志中，但不计入有效次数。

## 证据索引

- I2C post-merge 总结：`docs/bringup/i2c_post_merge_summary.md`；
- I2C clean build：`docs/bringup/i2c_post_merge_build.log`；
- 芯片识别和烧录：`docs/bringup/i2c_post_merge_chip_id.log`、`i2c_post_merge_flash.log`；
- I2C 配置、镜像哈希和 4 次扫描：`i2c_post_merge_config.txt`、`i2c_post_merge_image.sha256`、`i2c_post_merge_scan*.log`；
- UART0 首次失败构建：`docs/bringup/uart0_build_failure_hal_api_mismatch.log`；
- UART0 刷新依赖后的成功重试：`docs/bringup/uart0_build_retry_success.log`。

失败日志不会被删除或伪装成成功；README 和总结只引用可追踪的原始证据。

## AI Coding 日志

项目在需求收敛、源码盘点、API 兼容、构建排障、真机验收和文档整理阶段使用了 AI 辅助。仓库只保留原始 transcript 来源目录和 `cwd` 均属于 `/home/mi/openvela-contest` 的比赛会话；其他工作区会话只在本地受限归档中保存，不进入远端。

当前 `logs/Rotel-ga` 包含 21 个 canonical session、4475 个 events。官方校验结果为 `ALL OK`，凭据扫描为 0 命中：

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py \
  logs/Rotel-ga
```

## 已知限制

- 不支持完整 ES8311 音频、I2S、录音、播放、功放或扬声器；
- SPI2 Gate 为 STOP，不提供 SPI 回环或外设支持；
- 不承诺 MIPI-DSI/LVGL、摄像头/ISP/H.264、ESP32-C6 Wi-Fi/BLE；
- 不操作 Secure Boot、Flash Encryption、真实密钥或任何 eFuse；
- 最终提交 head 的 10 次冷启动、10 次 reboot、GPIO4 物理测量和 Timer 长稳统计仍需统一归档；
- `esp-hal-3rdparty` 仍有一个 `nxsched_usleep` 声明兼容警告，但不阻塞构建和已验证功能；
- 不提交 `esp-hal-3rdparty` 整仓、构建产物或公共 `nuttx/` 仓补丁。
