# 摄像头定时巡检 + 大模型跌倒判定 + 飞书告警

本文记录 `tools/monitor/` 这条链路：板子每隔几秒采一帧，PC 把图像（默认板端 JPEG，也可用 RGB565 缩略图）
交给 MiMo 视觉模型判断有没有人摔倒，判定为跌倒时向飞书群推一张告警卡片。最终作品使用 `desktop_camera` 固件，
由跌倒监护页面的“开始 / 停止监控”按钮控制（`--panel-control`），见 [app/fallguard/README.md](../../app/fallguard/README.md)。

摄像头本身怎么通起来、增益和白平衡怎么调，见 [camera_csi.md](camera_csi.md)，本文不重复。

```
sudo usermod -aG dialout "$USER"
newgrp dialout
export MIMO_API_KEY="<your-api-key>"
export FEISHU_WEBHOOK_URL="<your-feishu-webhook-url>"
python tools/monitor/fall_watch.py --backend direct --interval 10
```


## 数据通路

```
SC2336 --MIPI-CSI--> ISP --> PSRAM 整帧 1280x720 RGB565 (1.8 MB)
  --> --jpeg（推荐）：板端灰世界 AWB + 整数 DCT JPEG 1280x720（约 1.4 s，约 0.11–0.26 MB）
      不加 --jpeg：板上 1/2 盒式平均 640x360 RGB565 缩略图
  --> base64 --> USB 串口（传图期间 USB 帧传输会话独占发送通路，普通日志不插行）
  --> PC: tools/monitor/board_console.py 读回控制台文本
  --> tools/monitor/jpeg_frame.py / thumb_image.py  校验长度、sum32、SOI/EOI 并落盘
  --> tools/monitor/ai_client.py    图像 -> MiMo 视觉模型 -> {fall_detected,...}
  --> 判定跌倒 --> 飞书自定义机器人 Webhook --> 群里的告警卡片
```

每一轮由 PC 通过 NSH 下发 `p4x_selftest --jpeg-capture`（或 `--camera-capture`）完成采集；
加 `--panel-control` 时，PC 先经 `fgctl` 等待面板“开始监控”，并把判定状态回传到面板。
面板控制与照片显示只在包含桌面的 `desktop_camera` 固件里可用；`demo` 固件只支持命令行直接采集。

## 文件

| 文件 | 职责 |
| --- | --- |
| `tools/monitor/fall_watch.py` | 主循环：采集 → 存图 → 送模型 → 告警，命令行入口 |
| `tools/monitor/board_console.py` | 裸 termios 串口，不碰 DTR/RTS（用 pyserial 会让设备重新枚举） |
| `tools/monitor/thumb_image.py` | 解析 `THUMB:` base64、校验、RGB565→PNG（只用 zlib/struct，不依赖 Pillow） |
| `tools/monitor/ai_client.py` | 模型调用（proxy/direct/mock 三种后端）+ 结果解析 + 飞书推送 |
| `tools/monitor/mimo_proxy.py` | 本地 MiMo 代理，把 API Key 关在单独进程里 |

全部只用 Python 标准库，无需 pip 安装任何东西。

## 配置

两个凭据都从环境变量读，**不写进源码**：

```bash
export MIMO_API_KEY="<your-api-key>"
export FEISHU_WEBHOOK_URL="<your-feishu-webhook-url>"
```

Webhook 等同于群机器人的密钥。若机器人开了关键词校验，注意卡片标题里含「跌倒」「告警」。

## 跑起来

两个终端（推荐，Key 只存在于代理进程）：

```bash
# 终端 1
export MIMO_API_KEY="<your-api-key>"
tools/monitor/mimo_proxy.py

# 终端 2
export FEISHU_WEBHOOK_URL="<your-feishu-webhook-url>"
tools/monitor/fall_watch.py --interval 10
```

单终端、不起代理：

```bash
export MIMO_API_KEY="<your-api-key>"
tools/monitor/fall_watch.py --backend direct --interval 10
```

先干跑一次，确认采集和成图没问题、且什么都不会发到群里：

```bash
tools/monitor/fall_watch.py --once --dry-run --backend mock
```

没有板子时，可以拿一份历史采集日志回放整条链路：

```bash
tools/monitor/fall_watch.py --from-log out/camera/latest.log --dry-run
```

典型输出：

```
port     : /dev/ttyACM0
backend  : proxy (http://127.0.0.1:8000/analyze)
interval : 10s   cooldown: 60s   confirm: 1
alerts   : Feishu webhook
[16:51:20] #1 正常 conf=0.10 色数=438 原因=画面中无人
[16:51:34] #2 跌倒 conf=0.82 色数=451 原因=画面中有人平躺在地面
  已发送飞书跌倒告警。
```

## 产物

都落在 `out/monitor/`（`out/` 已在 `.gitignore` 里）：

| 路径 | 说明 |
| --- | --- |
| `frames/<时间戳>-<帧号>.jpg` / `.png` | 送给模型的那张图（`--jpeg` 为 JPEG，缩略图模式为 PNG），逐帧留档 |
| `latest.jpg` / `latest.png` | 最近一帧，固定名，方便外部预览 |
| `logs/<时间戳>-<帧号>.log` | 该帧的完整控制台会话，出问题时可直接喂给 `decode_thumb.py` |
| `events.jsonl` | 每帧一行 JSON：判定、置信度、原因、模型原文、是否告警 |

`--keep`（默认 500）控制 `frames/` 和 `logs/` 各保留多少个文件，超出删最旧的。

## 常用参数

| 参数 | 作用 |
| --- | --- |
| `--jpeg` | 让板子整帧编 JPEG（1280×720，板端 AWB）再传，推荐；不加时传 RGB565 缩略图 |
| `--no-awb` | 关闭 JPEG 路径的板端灰世界白平衡 |
| `--panel-control` | 等待面板“开始监控”并回传状态（需 `desktop_camera` 固件） |
| `--interval 10` | 采集间隔秒数，从一轮开始算到下一轮开始 |
| `--scale 4` | 缩略图模式下送模型前的整数倍最近邻放大 |
| `--stretch` | 每通道拉伸到满量程，画面偏暗时用（采集路径没有自动曝光） |
| `--gain 0x80 0x00 0x10` | 透传给 `p4x_selftest --camera-capture` 的 SC2336 增益 |
| `--cooldown 60` | 两次告警之间的最小间隔，防止刷群 |
| `--confirm 2` | 要求连续 N 帧都判定跌倒才告警，压误报 |
| `--min-confidence 0.6` | 低于该置信度的正例直接忽略 |
| `--notify-failures 5` | 连续 N 次采集失败时发一条文字提醒（画面断了也要有人知道） |
| `--notify-start` | 启动时发一条文字消息，用来确认 Webhook 通 |
| `--dry-run` | 完全不碰飞书，只打印判定 |
| `--backend mock` | 不调模型，返回固定结论；配合 `MONITOR_MOCK_FALL=1` 可离线打通告警链路 |

`--confirm` 和 `--cooldown` 是两件事：前者压"单帧看错"，后者压"同一次事故反复报"。

## 时序：间隔不是想设多小就多小

每一轮都要重新配置传感器（166 条模式表写入与读回，约 7 秒），再加编码和串口传输。
2026-09-29 用 `desktop_camera` 固件、`--jpeg --backend direct` 实测 11 轮：采集+编码+传输约 10.2 秒
（板端 JPEG 编码 1370–1450 ms，帧 113910–263942 字节），模型判定约 1–2 秒，单轮合计约 11–12 秒。
缩略图模式（640×360 RGB565，460800 字节）串口传输本身约 7 秒，单轮更长。

`--interval` 小于一轮耗时时，脚本不会堆积任务，而是打印
`本轮耗时 …s，已超过 --interval …s，立即开始下一轮` 并接着跑——间隔实际退化为"尽快"。

想真正做到每秒级，必须换掉传输层（以太网），见 camera_csi.md 的「后续工作」。

## 已知边界

不要在材料里包装掉这几条。

1. **判的是"有人躺在地上"，不是"摔倒的那一瞬间"。**
   每隔十几秒一帧的采样率下，跌倒过程几乎必然落在两帧之间。这套东西能可靠发现的是
   跌倒之后的**结果状态**。要检测动作本身需要连续视频，当前传输带宽不支持。
2. **传感器没有 AE/AWB。** `--jpeg` 送整帧 1280×720（缩略图模式为 640×360），但曝光固定、
   ISP 只有静态白平衡，JPEG 路径另做板端灰世界校正。人离得远、光线变化、逆光都会明显掉准确率。
   system prompt 里已经写明"可能偏暗偏色、辨认不清时必须判 false"，这是在用保守换误报少。
3. **误报和漏报都没有量化。** 没有标注集，也没有跑过召回率/准确率，仓里只有人工摆拍的
   功能验证。不要对外声称任何准确率数字。
4. **告警卡片里不带图。** 自定义机器人 Webhook 传不了图片（要上传图片得用应用凭据走
   `im/v1/images`），所以卡片里给的是 PC 上那张图的本地路径。
5. **一个板子只能被一个进程占着。** `fall_watch.py` 跑起来后，`capture_camera.py`
   或串口终端再去开同一个设备会互相抢输出。

## 排障

| 症状 | 原因 / 处置 |
| --- | --- |
| `no Espressif serial device found` | 设备没枚举出来或被别的程序占用，`--port /dev/ttyACM0` 显式指定 |
| `capture timed out after 120s` | 板子没在跑 NSH，或固件不带 `p4x_selftest`；先手工敲一次命令确认 |
| `payload truncated` / 长度不足 | 串口在丢数据或有日志插行：确认用的是本仓库构建的固件（USB 帧传输会话），`desktop_camera` 不要开 `I2C_TRACE`；偶发一次下一轮会重试 |
| `near-constant frame (N colours)` | 传感器在出纯色，多半是 `ang` 增益落在低 3 位 `0b100` 的禁用值上 |
| `checksum mismatch` | 串口传输出错，同上，偶发可忽略 |
| `代理分析失败：MIMO_API_KEY is not set` | 代理进程没拿到 Key，注意 `export` 要在启动代理的那个终端里 |
| `模型未按约定返回 JSON` | 模型没守格式；先看 `events.jsonl` 里的 `raw` 字段确认它究竟回了什么 |
| `飞书拒绝消息：code=19024` | 机器人开了关键词校验但消息不含关键词，或 Webhook 填错 |
| 一直判"正常"但人确实躺着 | 先看 `out/monitor/latest.jpg`（缩略图模式为 `latest.png`）：如果人眼都看不出，就是画质问题不是模型问题，缩略图模式可加 `--stretch`，或调增益 |

## 后续工作

- **以太网取整帧**：分辨率和帧率的根，其它优化都受它限制。
- **先做一次人体存在判断再调模型**：无人时不必调用，省配额也省延迟。
- **告警带图**：换成飞书应用凭据即可上传图片，卡片里直接显示现场画面。
- **建一个小标注集**：哪怕 50 张正负样本，也能把"边界 3"从"未量化"变成有数字。
