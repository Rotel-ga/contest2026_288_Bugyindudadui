# tools/monitor — 定时采集 + 大模型跌倒判定 + 飞书告警

两个应用的统一使用步骤、切换与排障见 [开发报告第 51 节](../../docs/bringup/official_lcd_feishu_report.md#51-两个原生应用使用手册2026-09-28)。

PC 端一条链路：板子每隔几秒采一帧 → 缩略图还原成 PNG → MiMo 视觉模型判断是否有人摔倒
→ 判定跌倒时推一张飞书告警卡片。普通命令行采集使用现有摄像头固件；面板按钮控制需要包含 `fgctl` 的面板固件。

完整说明（数据通路、时序限制、已知边界、排障表）见
[`docs/bringup/fall_alert.md`](../../docs/bringup/fall_alert.md)。

## 三条命令

```bash
# 离线自检：不连板子、不调模型、不发飞书
tools/monitor/fall_watch.py --from-log out/camera/latest.log --backend mock --dry-run

# 本地代理 + 循环监控
export MIMO_API_KEY=sk-...                  # 终端 1
tools/monitor/mimo_proxy.py
export FEISHU_WEBHOOK_URL=https://...       # 终端 2
tools/monitor/fall_watch.py --interval 10

# 单进程，不起代理
tools/monitor/fall_watch.py --backend direct --interval 10

# JPEG 传输：板端整帧 1280×720 编 JPEG（AWB 在板端做，--no-awb 关）
tools/monitor/fall_watch.py --jpeg --once --dry-run --backend mock
```

凭据只从环境变量读（`MIMO_API_KEY`、`FEISHU_WEBHOOK_URL`），不要写进源码提交。

## 模块

- `fall_watch.py` — 主循环与命令行入口，`--help` 有全部参数
- `board_console.py` — 裸 termios 串口（不碰 DTR/RTS）
- `thumb_image.py` — `THUMB:` base64 解析、校验、RGB565→PNG
- `jpeg_frame.py` — `jpg:` base64 解析，校验长度/sum32/SOI-EOI/SOF 尺寸，落 `.jpg`
- `ai_client.py` — 模型调用（`proxy` / `direct` / `mock`）、判定解析、飞书推送
- `mimo_proxy.py` — 本地 MiMo 代理，把 API Key 关在单独进程里

只用 Python 标准库，无需 pip 安装。

## 注意

- 一轮实测：缩略图模式约 17 s，`--jpeg` 模式约 13 s（整帧 1280×720；其中传感器
  配置 ~7 s、板端 AWB+编码 ~1.3 s、传输 ~1.7 s）。`--interval` 设得比一轮更小只会
  变成"尽可能快"，不会堆积。
- 判定的是"有人躺在地上"这个结果状态，不是摔倒的瞬间动作；传感器无 AE、只有
  静态白平衡。准确率没有量化过，别当成成品指标用。


## 手动启动，面板按钮控制

```bash
tools/monitor/fall_watch.py --port /dev/ttyACM0 --panel-control --jpeg --backend direct --dry-run --interval 10 --capture-timeout 120
```

终端手动运行后等待面板开始/停止按钮。使用真实模型识别，dry-run 仅禁止飞书告警。停止在本轮完成后生效；Ctrl+C 退出脚本。不使用后台 service，不登录自动启动。不传 --panel-control 时保持直接采集行为。终端需配置 MIMO_API_KEY。


## 板端照片预览（本地新增，待真机验收）

包含 `camera_preview` 的组合固件可在跌倒监护页面的原“视频画面预留”框显示每次成功采集的照片，480×270 等比例居中，按采集更新，不是连续视频。停止后保留最后一张，切页返回可再次显示。电脑脚本继续使用上面的手动面板控制命令，不需要后台 service。

此功能需要烧录 `artifacts/merge-desktop-camera/20260928-camera-preview/nuttx.bin`（相对 openvela 根目录）。该镜像 741032 字节，SHA-256 为 `ae680d8fbf2d1bbe1e41c3899f51d6958030e2413bca24ce8da30ca0fc284abd`。代码和编译检查已通过，尚未确认真机显示成功。

板上预览直接从采集帧生成，电脑 JPEG 保存位置不变。操作命令及验收项见 [开发报告第 47 节](../../docs/bringup/official_lcd_feishu_report.md#47-原生跌倒监护页面显示每次采集照片2026-09-28)。
