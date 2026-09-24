# tools/monitor — 定时采集 + 大模型跌倒判定 + 飞书告警

PC 端一条链路：板子每隔几秒采一帧 → 缩略图还原成 PNG → MiMo 视觉模型判断是否有人摔倒
→ 判定跌倒时推一张飞书告警卡片。板上固件不需要任何改动。

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
```

凭据只从环境变量读（`MIMO_API_KEY`、`FEISHU_WEBHOOK_URL`），不要写进源码提交。

## 模块

- `fall_watch.py` — 主循环与命令行入口，`--help` 有全部参数
- `board_console.py` — 裸 termios 串口（不碰 DTR/RTS）
- `thumb_image.py` — `THUMB:` base64 解析、校验、RGB565→PNG
- `ai_client.py` — 模型调用（`proxy` / `direct` / `mock`）、判定解析、飞书推送
- `mimo_proxy.py` — 本地 MiMo 代理，把 API Key 关在单独进程里

只用 Python 标准库，无需 pip 安装。

## 注意

- 一轮采集实测 10 秒量级（串口传 base64 就要 ~3.7 s），`--interval` 设得更小只会
  变成"尽可能快"，不会堆积。
- 判定的是"有人躺在地上"这个结果状态，不是摔倒的瞬间动作；画面只有 160×90 且无
  AE/AWB。准确率没有量化过，别当成成品指标用。
