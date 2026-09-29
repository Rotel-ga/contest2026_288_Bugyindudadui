# 原生 LVGL 跌倒监护应用

`fallguard` 是运行在 openvela/NuttX 桌面内的原生 LVGL 页面模块。它与桌面共用同一个 LVGL 实例、`/dev/fb0` 和 `/dev/input0`，不启动第二套图形运行时，也不依赖 Quick App、RPK 或 qastart。源码由 `app/desktop/Makefile` 一并编译，入口为 桌面 → 应用中心 → 跌倒监护，最终固件使用 `desktop_camera` 配置。

## 页面功能

- **开始 / 停止监控**：按钮写入 `panel_control.c` 的状态邮箱（requested + 递增 revision）。PC 端 `tools/monitor/fall_watch.py --panel-control` 通过 NSH 命令 `fgctl query` / `fgctl ack` 轮询并确认，旧 revision 的确认会被拒绝；停止在当前一轮结束后生效。
- **状态卡片**：`PC offline`、`Waiting for PC`、`Monitoring`、`No fall detected`、`Fall detected`、`Monitor error / retry`、`Stopping after frame`；页面状态随之显示“正常 / 检测中 / 疑似跌倒”。模型出错时显示错误，不会显示为“正常”。
- **照片区**：每次成功采集后，`camera_preview.c` 从同一帧生成 480×270 等比缩小的照片（JPEG 模式复用编码器的灰世界增益），显示为 `Photo #n`；这是按采集更新的照片，不是连续视频。停止后保留最后一张，切页返回仍显示。
- **模拟摔倒**：只切换页面状态，不经过摄像头和模型，用于演示。
- 返回应用中心。

## 使用

板端打开页面后，在 PC 上运行（凭据只从环境变量读取）：

```bash
export MIMO_API_KEY=<你的 MiMo API Key>
python3 tools/monitor/fall_watch.py --port /dev/ttyACM0 --panel-control --jpeg \
  --backend direct --dry-run --interval 10 --capture-timeout 120
```

去掉 `--dry-run` 并设置 `FEISHU_WEBHOOK_URL` 后才会推送飞书告警。完整参数、时序与已知边界见 [tools/monitor/README.md](../../tools/monitor/README.md) 和 [docs/bringup/fall_alert.md](../../docs/bringup/fall_alert.md)。
