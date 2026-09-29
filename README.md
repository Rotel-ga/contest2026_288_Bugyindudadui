# VelaP4X：ESP32-P4X openvela 板级适配与 AI 看护终端

本项目参加 openvela 2026 AI 硬件开发者大赛“新硬件平台适配”赛道。我们在 **ESP32-P4X-Function-EV-Board V1.6** 上完成 openvela 板级适配，并在同一块板上做成一台居家 AI 看护终端：中文触控桌面与 6 位 PIN 锁屏、摄像头跌倒监护（大模型判定 + 飞书告警）、拍照识物。

- 官方仓库：https://github.com/open-vela/contest2026_288_Bugyindudadui
- 提交分支：`dev-ai-contest-2026`
- 新功能源码基线：`79b565e814a5d8850edfbaa1a423a35be8eb92d7`（显示、触摸、桌面/锁屏、摄像头、跌倒监护、拍照识物）
- P0 板级验收基线：`35a953cc3673c0329b6a8de569604d492e1b64f0`（2026-09-17 最终复现）
- 目标开发板：ESP32-P4X-Function-EV-Board V1.6，实测芯片 ESP32-P4 revision v3.2
- 外设：官方 7 英寸 1024×600 MIPI-DSI 触摸屏（EK79007 + GT911）、SC2336 MIPI-CSI 摄像头模组

## 功能总览

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 板级基础（P0） | 完成 | out-of-tree ESP32-P4 芯片层与板级目录、Simple Boot `0x2000`、J20 USB 控制台、物理 UART0、NSH、GPIO4、Timer、JTAG |
| ES8311 I2C1（P1） | 完成 | `/dev/i2c1`，100 kHz，地址 `0x18`；仅控制通路，不含音频 |
| MIPI-DSI 显示 | 完成 | 移植 Espressif 官方 `esp_lcd`（DSI/DPI/EK79007），`/dev/fb0` 1024×600 RGB565，LVGL 上屏 |
| GT911 触摸 | 完成 | `/dev/input0`，I2C1 地址 `0x5D`，坐标 X/Y 镜像 |
| 桌面、锁屏与设置 | 完成 | 中文 LVGL 界面；锁屏可选滑动、6 位 PIN 或不锁屏；设置保存在 LittleFS `/data` |
| SC2336 摄像头 | 完成 | MIPI-CSI 2-lane RAW8 1280×720，ISP 转 RGB565，32 MiB PSRAM 帧缓冲 |
| 板端 JPEG | 完成 | 整数 DCT 软件编码 + 灰世界白平衡，1280×720 约 1.4 s |
| 跌倒监护 | 完成 | 面板按钮启停；PC 调用 MiMo 视觉模型判定，判定跌倒时推送飞书卡片；面板显示每次照片与状态 |
| 拍照识物 | 完成 | 本地实时预览；按钮冻结当前帧，PC 调用 MiMo 识别，中文结果分块回传屏幕 |
| `p4x_selftest` | 完成 | 板卡一键自检（人类可读 / JSON），并提供摄像头采集命令 |

AI 推理在云端完成：开发板负责采集、ISP、白平衡、JPEG、显示与交互，PC 脚本经 J20 USB 串口桥接并调用 MiMo。当前移植没有可用网络（本树 Wi-Fi 驱动不支持 ESP32-P4，板载以太网未启用），所以 PC 是监控与识物链路的一部分。

## 系统结构

```text
ESP32-P4X（openvela / NuttX）                     PC（Ubuntu，Python 标准库）           云服务
  app/desktop      桌面 · 锁屏 · 设置             tools/monitor/fall_watch.py  ──HTTPS──▶ MiMo 视觉模型
  app/fallguard    跌倒监护页面  ◀──fgctl──┐       tools/photo_identify/                 （mimo-v2.6-pro）
  app/photo_identify 拍照识物页面 ◀──pictl─┤          identify_watch.py
  app/p4x_selftest 自检 · CSI/ISP · JPEG  ─┼─ J20 USB Serial/JTAG ─▶ 校验 · 存档 · 告警策略 ──▶ 飞书群机器人
  LVGL · NSH · /dev/fb0 · /dev/input0 · /data（LittleFS）
  board/contest_board：esp_lcd、GT911、SC2336、DW-GDMA 共享中断、USB 帧传输
```

## 构建配置

| 配置 | 用途 | `nuttx.bin`（79b565e） |
| --- | --- | ---: |
| `nsh` | J20 USB NSH、Timer、GPIO | 227956 B |
| `uart0` | GPIO37/GPIO38 物理 UART0 控制台 | 229492 B |
| `i2c` | I2C1 与 i2ctool | 235572 B |
| `demo` | I2C1 + `p4x_selftest`（含摄像头采集与 JPEG） | 260508 B |
| `lcd` | MIPI-DSI 显示 + LVGL 演示 | 600136 B |
| `desktop` | 桌面、锁屏、设置、触摸 | 2819684 B |
| `desktop_camera` | **最终作品**：桌面 + 跌倒监护 + 拍照识物 + 摄像头 | 2839456 B |

镜像内嵌 git hash、编译时间和构建路径，不同构建的 SHA-256 不保证相同；以大小、构建判据和真机 `uname -a` 确认镜像身份。

## 获取工程

```bash
repo init \
  -u https://github.com/open-vela/contest2026_288_Bugyindudadui \
  -b dev-ai-contest-2026 \
  -m contest2026_288_Bugyindudadui.xml

repo sync -c -j8
```

manifest 建立以下映射：

| 仓库内路径 | openvela 工作区路径 |
| --- | --- |
| `board/contest_board` | `vendor/openvela/boards/contest2026_288_board` |
| `app/p4x_selftest` | `packages/demos/contest2026_288_p4x_selftest` |
| `app/desktop` | `packages/demos/contest2026_288_desktop` |

`app/desktop` 的 Makefile 同时编译 `app/fallguard` 与 `app/photo_identify`。缺少 `app/desktop` 映射时，`desktop` / `desktop_camera` 会在链接阶段报 `undefined reference to desktop_boot_main`。

## Clean build

`distclean` 会删除被忽略的 `esp-hal-3rdparty`，必须严格按以下顺序执行：

```text
distclean → 准备 HAL → 初始化 mbedTLS → build
```

```bash
cd /path/to/openvela
CONFIG=desktop_camera  # 可改为 nsh / uart0 / i2c / demo / lcd / desktop

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
  "vendor/openvela/boards/contest2026_288_board/configs/$CONFIG" -j8
```

成功标志为输出 `Generated: nuttx.bin`，产物是 `nuttx/nuttx.bin`。ESP HAL 固定在 `b90b1837cb5ad24747deb4c895246037cc206ce5`，兼容补丁为 `board/contest_board/patches/esp-hal-openvela-compat.patch`。如果本地还留着按旧补丁准备的 HAL，先 distclean（或删除 `board/contest_board/chip/esp-hal-3rdparty`）再准备，否则 `prepare_esp_hal.sh` 会提示 HAL 与补丁状态不一致。

`board/contest_board/tools/build_desktop.sh` 是 `desktop` 配置的便捷封装，需要用 `OPENVELA_ROOT=/path/to/openvela` 指明工作区；交付与复现以上面的标准命令为准。

## 芯片识别与烧录

J20 的 `/dev/ttyACMx` 编号会随重枚举变化，按实际节点设置；烧录前关闭所有串口终端和监控脚本：

```bash
PORT=/dev/ttyACM0

esptool --chip esp32p4 --port "$PORT" chip-id

esptool --chip esp32p4 \
  --port "$PORT" \
  --baud 921600 \
  write-flash 0x2000 nuttx/nuttx.bin
```

成功判据：识别 `ESP32-P4 revision v3.2`、写入 `0x00002000`、输出 `Hash of data verified.`

桌面设置分区位于 Flash 尾部 `0xF80000–0xFFFFFF`（512 KiB，LittleFS，挂载到 `/data`），不在固件镜像范围内，常规烧录不会覆盖；首次启动只格式化完全空白的分区。

## 使用最终作品（desktop_camera）

### 桌面与锁屏

- 通电后自动进入锁屏（默认滑动解锁），解锁后进入桌面与应用中心。
- 设置 → 锁屏方式：无需锁屏 / 滑动 / 6 位 PIN。设置 PIN 需输入两次；更改或关闭 PIN 前验证旧 PIN；连续 5 次输错需等待 30 秒。
- PIN 以 16 字节随机盐 + 迭代 SHA-256（8192 轮）保存在 `/data/desktop/settings.bin`，写入采用临时文件 + fsync + rename。PIN 只是界面访问限制，不是存储加密。
- 设置 → 关于桌面 → 退出桌面；之后可在 NSH 输入 `desktop` 重新启动。

### 跌倒监护

凭据只从环境变量读取，不要写进源码、日志或提交：

```bash
cd contest2026_288_Bugyindudadui
export MIMO_API_KEY=<你的 MiMo API Key>
export FEISHU_WEBHOOK_URL=<飞书群机器人 Webhook>   # 只在需要告警时设置

# 1. 不调模型、不发飞书的链路测试
python3 tools/monitor/fall_watch.py --jpeg --once --dry-run --backend mock

# 2. 面板按钮控制 + 真实模型识别（--dry-run 只关闭飞书告警）
python3 tools/monitor/fall_watch.py --port /dev/ttyACM0 --panel-control --jpeg \
  --backend direct --dry-run --interval 10 --capture-timeout 120
```

脚本就绪后，在板上打开 应用中心 → 跌倒监护，点“开始监控”；需要飞书告警时去掉 `--dry-run`。采集的 JPEG 保存在 `out/monitor/frames/`，每轮判定写入 `out/monitor/events.jsonl`。详细参数、时序与边界见 [`tools/monitor/README.md`](tools/monitor/README.md) 和 [`docs/bringup/fall_alert.md`](docs/bringup/fall_alert.md)。

### 拍照识物

先在跌倒监护页点“停止监控”、等本轮结束，再 Ctrl+C 退出 `fall_watch.py`；然后在板上打开 应用中心 → 拍照识物，等待实时画面出现：

```bash
python3 tools/photo_identify/identify_watch.py --port /dev/ttyACM0 --backend direct \
  --command-timeout 30 --capture-timeout 120 --max-completion-tokens 4096
```

点“拍照识别”后，当前显示的 480×270 画面被冻结并交给 PC，识别结果回到右侧提示框。`--backend mock` 可做不调模型的链路测试。两个脚本共用 J20 串口，同一时间只能运行一个；详见 [`tools/photo_identify/README.md`](tools/photo_identify/README.md)。

## 验证方法与结果

### 预检（自建 Skill）

```bash
cd contest2026_288_Bugyindudadui
python3 .claude/skills/esp32p4-repro-check/scripts/check_baseline.py --repo .
```

默认 `final` 档位检查 11 项：功能基线与 tree、必需文件、固件与主机工具相对基线未改动、三项 manifest 映射、配置契约、设备与入口 token、凭据不入源码、`esp_lcd` 官方源码可逆校验（19/19）和已存证据哈希。期望输出 `Summary: PASS=11 FAIL=0 RESULT=PASS`。`--profile p0` 用于复核 2026-09-17 的 P0 基线。

### 主机测试

```bash
export ASAN_OPTIONS=detect_leaks=0
for t in board/contest_board/tests/test_*.py tools/monitor/tests/test_*.py tools/photo_identify/tests/test_*.py; do
  python3 "$t" || echo "FAIL $t"
done
```

2026-09-29 在 `79b565e` 上 11/11 个文件通过（共享中断、USB 帧传输、三缓冲、照片预览、识物桥、面板邮箱、字库，以及 22 个 Python 用例）。

### 新功能验收（79b565e）

| 验收项 | 结果 |
| --- | --- |
| 七套配置 clean build | 7/7 生成 `nuttx.bin`（2026-09-29） |
| 主机测试 | 11/11 文件通过 |
| MIPI-DSI 显示 | 1024×600 RGB565；四色全屏与 LVGL 控件上屏经真机确认；LVGL 连续刷新 300 s、14999 次更新无错误 |
| GT911 触摸 | ID `911`、固件 `0x1060`、5 点；方向修正后跟手 |
| 桌面 / 锁屏 / 设置 | 开机锁屏、滑动解锁、再次锁定、设置进入与返回有日志；PIN 设置、校验、更改经团队真机确认 |
| 摄像头采集 | 166 条寄存器读回 `mismatches=0`（2026-09-29 12/12 次） |
| 跌倒监护 | 2026-09-29 两次会话 11/11 轮完成真实模型判定；2 轮判定跌倒（置信度 0.72、0.82）并推送飞书；单轮约 12 s |
| 板端 JPEG | 1280×720 编码 1370–1450 ms；单帧 113910–263942 B |
| 拍照识物 | 真机端到端识别成功：480×270 当前帧 → MiMo → 中文结果（如 267 B）分块回传屏幕 |

### P0 板级验收（35a953c，2026-09-17）

| 验收项 | 结果 |
| --- | --- |
| 四配置 clean build（nsh / uart0 / i2c / demo） | PASS，12/12 判据 |
| ESP32-P4 v3.2 识别、`0x2000` 烧录与 hash 校验 | PASS |
| J20 USB 与物理 UART0 进入 NSH | PASS |
| Timer 定量延时、GPIO4 软件链路 | PASS |
| I2C 完整扫描仅 `0x18` | PASS 3/3 |
| `p4x_selftest` human / JSON | PASS 3/3 + 3/3 |
| JTAG reset/halt/PC | PASS |
| 完整启动 / reboot / 冷启动 | 22/22、11/11、10/10 |
| GPIO4 外部电平 | SKIP（无外部测量夹具） |

## 目录结构

```text
app/desktop/            LVGL 桌面、锁屏、设置、字库；fgctl / pictl 命令入口
app/fallguard/          跌倒监护页面、面板控制邮箱、照片预览
app/photo_identify/     拍照识物页面与识物请求桥
app/p4x_selftest/       板卡自检、SC2336 CSI/ISP 采集、软件 JPEG
board/contest_board/    ESP32-P4X 芯片层与板级代码、官方 esp_lcd 移植、七套配置、构建脚本、主机测试
tools/camera/           串口采集、缩略图解码、白平衡增益计算
tools/monitor/          跌倒监护 PC 脚本（MiMo 判定 + 飞书告警）
tools/photo_identify/   拍照识物 PC 脚本
docs/bringup/           构建、烧录、各外设 bring-up 记录与原始证据
.claude/skills/esp32p4-repro-check/  项目自建复现检查 Skill
logs/                   按大赛规范导出的 AI Coding 日志
contest2026_288_Bugyindudadui.xml    repo manifest 与 linkfile 映射
LICENSE                 Apache License 2.0
```

`quickapp/hello_quickapp` 保留为组委会初始模板，不作为参赛成果。

## 文档与证据索引

- 显示驱动移植与桌面开发全过程：[`docs/bringup/official_lcd_feishu_report.md`](docs/bringup/official_lcd_feishu_report.md)、[`board/contest_board/chip/esp_lcd/README.md`](board/contest_board/chip/esp_lcd/README.md)
- 桌面、锁屏与设置：[`app/desktop/README.md`](app/desktop/README.md)
- 摄像头：[`docs/bringup/camera_csi.md`](docs/bringup/camera_csi.md)
- 跌倒监护：[`docs/bringup/fall_alert.md`](docs/bringup/fall_alert.md)、[`tools/monitor/README.md`](tools/monitor/README.md)、[`app/fallguard/README.md`](app/fallguard/README.md)
- 拍照识物：[`tools/photo_identify/README.md`](tools/photo_identify/README.md)、[`app/photo_identify/README.md`](app/photo_identify/README.md)
- P0 各项：`docs/bringup/gpio.md`、`timer_*.log`、`uart0.md`、`i2c_post_merge_summary.md`、`jtag.md`、`p4x_selftest.md`
- Skill 预检记录：`docs/bringup/esp32p4_repro_check_preflight.log`（P0）、`docs/bringup/esp32p4_repro_check_final_preflight.log`（最终）

这些文件记录了各自开发阶段的 commit 基线；历史日志不会改写成后续基线的结果。完整复现证据（构建日志、镜像、真机串口日志与照片）保留在团队本地证据库，并纳入比赛技术报告与演示视频。

## AI Coding 日志

仓库已归集 `logs/Rotel-ga/`（21 个会话 / 4475 个 events）、`logs/zqzhang2023/`（6 / 159）、`logs/Zyxx2003/`（1 / 174）。前两者通过官方 `validate-log.py` 校验；`logs/Zyxx2003/` 中的 1 个 Kiro CLI 会话不符合事件 schema，按“请勿手动删改”的规则原样保留。2026-09-03 之后的开发会话采集不完整，未上传，技术报告中已如实说明。

## 已知限制

- AI 推理在云端（MiMo），依赖 PC 串口桥接；开发板暂无可用网络。
- 跌倒监护每轮约 12 s 采一帧，只能发现“有人倒地”的结果状态，不能识别跌倒动作；没有标注数据集，准确率、误报率、漏报率未量化，只能作为辅助提醒。
- 传感器无 AE/AWB：采集走 ISP 静态白平衡，JPEG 路径另做板端灰世界校正；光照变化会影响识别。
- 跌倒监护与拍照识物共用摄像头和 J20 串口：切换应用前需先停止并退出对应的 PC 脚本，同一时间只运行一个脚本。
- 打开 USB 串口可能使板子复位；ROM 启动时打印 Simple Boot `SHA-256 comparison failed` 后继续正常启动。
- `demo` 配置仍启用 `CONFIG_I2C_TRACE=y`。I2C 命令字未初始化的根因已修复，`desktop_camera` 不再需要它；`demo` 去掉该项前需真机回归。
- 新增配置只验证了 Make 构建，CMake 未验收。
- 未实现完整 ES8311 音频 / I2S；SPI2 Gate 为 STOP（默认引脚未引出且与 MicroSD / Ethernet 冲突）；未实现 Wi-Fi / BLE / 以太网。
- 未启用 Secure Boot、Flash Encryption，未写入真实密钥或 eFuse；PIN 不代表存储加密或串口访问控制。
- GPIO4 外部电平未实测（`SKIP`）；Timer 长时间连续运行统计未执行；`/dev/timer0` 不存在，Timer 证据来自系统时基。
- 组合固件长时间运行、两应用反复切换的稳定性统计尚未完成。
- 不提交 `esp-hal-3rdparty` 整仓、构建产物或公共 `nuttx/` 仓补丁。

## 第三方组件与许可

| 组件 | 来源 | 许可 |
| --- | --- | --- |
| `board/contest_board/chip/esp_lcd/` | ESP-IDF `components/esp_lcd`（`d244a37c`）与 esp-iot-solution EK79007（`e1a8f5c3`），差异见 `openvela.patch` | Apache-2.0，保留原版权与 SPDX 声明 |
| SC2336 1280×720 寄存器表 | espressif/esp-video-components | Apache-2.0 |
| ESP HAL | espressif/esp-hal-3rdparty（`b90b1837`，构建时下载，不入仓） | Apache-2.0 |
| `app/desktop/assets/desktop_font.c` | 由 Noto Sans CJK SC 生成的子集，见 `app/desktop/assets/FONT_LICENSE.txt` | SIL OFL 1.1 |

## License

本项目代码使用 Apache License 2.0，详见 [LICENSE](LICENSE)；第三方字体资源按其 SIL OFL 1.1 许可分发。
