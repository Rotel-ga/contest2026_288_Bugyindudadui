# 拍照识物：独立 PC 脚本

两个应用的统一使用步骤、切换与排障见 [开发报告第 51 节](../../docs/bringup/official_lcd_feishu_report.md#51-两个原生应用使用手册2026-09-28)。

先在板上停止跌倒监控并等待当前轮结束，再退出跌倒监控脚本，然后进入拍照识物页面。使用包含 pictl 的新组合固件，在本机环境设置 MIMO_API_KEY，然后运行：

```bash
python3 tools/photo_identify/identify_watch.py --port /dev/ttyACM0 --backend direct --command-timeout 30 --capture-timeout 120 --max-completion-tokens 4096
```

不调用模型的链路测试用 `--backend mock`。不需要飞书 Webhook，不发送告警；本脚本只做物品识别，不改跌倒识别提示词或模型判断规则。

点击“拍照识别”时复制屏幕当前显示的 480×270 RGB565 帧，保持画面直到结果返回或超时。后台摄像头继续采集，但不会用后来的帧替换当前请求。PC 校验后保存 PNG，再调用模型；结果最多 900 字节 UTF-8，通过短命令分段传回，完整校验后更新提示框。请求超时为 180 秒，退出页面取消请求，过期答案拒绝。每次只允许一个待处理请求。

照片：`out/photo_identify/frames/`、`out/photo_identify/latest.png`；传输日志和模型结果：`out/photo_identify/logs/`。识别使用屏幕帧分辨率而非 1280×720 原始帧。

识物脚本和跌倒脚本共用串口，两个新版脚本不能同时运行，后启动者会提示占用；切换应用前 Ctrl+C 退出当前脚本。串口锁是协作锁，其他串口终端仍需手动关闭。

板端命令：pictl q 查询；pictl f <id> 读取快照；pictl b/c/e 分段回传文本。图片包含请求编号、长度和 sum32；结果包含长度、顺序偏移、sum32 和 UTF-8 检查。不是文件路径或任意命令执行接口。

当前字库包含 GB2312 常用简体字符和页面文本。模型提示要求简体纯文本，罕见字、表情等仍可能缺字。真实模型识别、板上显示与相机并行稳定性需真机验证。

识物生成额度默认 4096，可通过 `--max-completion-tokens 8192` 调整。该设置独立于跌倒脚本的 MIMO_MAX_COMPLETION_TOKENS；此前独立脚本写死 512 的问题已修复。finish_reason=length 时拒绝截断结果，提示增大额度；不自动重试产生额外模型请求。
