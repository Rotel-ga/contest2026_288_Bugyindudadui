# ESP32-P4 桌面、锁屏与设置

面向官方 1024×600 触摸屏的独立 LVGL 应用。默认滑动解锁；可在设置中
切换无需锁屏、滑动、6 位 PIN。开机启动与串口共存，不依赖 Quick App。

## 项目树

```text
app/desktop/
├── README.md                 # 范围、目录、命令、验收状态
├── Makefile / Make.defs      # NuttX 应用注册
├── desktop_main.c            # LVGL 生命周期、主循环和退出
├── desktop_boot.c            # 开机入口，初始化后启动桌面和 NSH
├── desktop.h                 # 模块接口与应用状态
├── core/
│   └── settings.c            # 配置读写、PIN 校验与错误处理
├── ui/
│   ├── common.c              # 字体、颜色、按钮、导航
│   ├── lockscreen.c          # 滑动锁屏、PIN 输入与设置密码
│   ├── home.c                # 桌面与设备信息
│   └── settings_page.c       # 平板分栏设置、锁屏选项
├── assets/
│   ├── desktop_font.c        # 从公开 Noto 字体生成的中文子集
│   └── FONT_LICENSE.txt      # 字体来源与许可证
└── tools/
    └── make_font.py          # 由界面实际文字生成 LVGL 字体
```

板级配套单独位于 `board/contest_board/`：`configs/desktop/defconfig`、
`tools/build_desktop.sh`、`src/esp32p4_desktop_storage.c`。驱动、挂载及
Flash 区间不写进页面代码。显示和触摸沿用已验收的板级驱动。

## 行为与边界

- 默认滑动解锁；关闭锁屏后开机直接进入桌面。
- 设置 PIN 时输入两次；更改/关闭 PIN 前验证旧 PIN。
- PIN 是本机界面访问限制，不代表设备加密、安全启动或串口访问保护。
- 设置只有成功写入持久存储后才生效；存储失败明确提示，不冒充已保存。
- 显示屏独立 USB 供电，主板电源开关关闭后的背光状态需要硬件验收。
- 单个 LVGL 实例管理全部页面；退出入口正常释放 LVGL，不靠强杀进程。

## 构建与运行

```bash
cd /home/mi/Developer/openvela
bash contest2026_288_Bugyindudadui/board/contest_board/tools/build_desktop.sh -j8
```

桌面固件开机自动启动。退出后在 NSH 输入 `desktop` 再次启动。
Flash 布局：固件从 `0x2000` 写入；桌面设置区为 `0xF80000` 起的
512 KiB（16 MiB Flash 尾部），挂载 LittleFS 到 `/data`。已只读确认该区
初始全为 0xFF；首次仅格式化全空白设置区。构建脚本检查镜像不重叠。
设置文件 `/data/desktop/settings.bin` 包含版本、模式、随机盐、PIN 摘要
和完整性校验；临时文件 fsync 后 rename 提交。不是加密存储。

字体由系统安装的 Noto Sans CJK SC（SIL OFL 1.1）生成，约 50 KiB 位图，
脚本依赖 Pillow。新增中文文案后执行 `python3 app/desktop/tools/make_font.py`
（以仓库为当前目录）再编译。

## 当前状态（2026-09-24）

**桌面、锁屏与设置第一版已实现并烧录；交互及关闭串口后的多次主板开关启动均获用户确认。**

曾出现只拨主板开关后背光黑屏、打开串口复位后恢复。USB 发送有界等待
修复后，关闭串口采集，用户连续多次拨主板开关均成功显示锁屏。屏幕始终
独立 USB 供电，具体次数未统计；详见总报告第 34.2 节。
日志已确认设置分区挂载、默认滑动设置跨复位读回、自动锁屏、滑动解锁、
再次锁定，以及设置/设备信息/控制中心的进入和返回。

当前已烧录且通过本轮开关启动验证的镜像 SHA-256：
`96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4`。
归档：工作区 `artifacts/desktop/standalone-console-fix/`。

证据边界：PIN 各分支、用户更改模式后的重启、物理开关熄屏未逐项留存完整
测试日志；总体完成按用户反馈记录，不等同于助手逐项实测。长期稳定性与
精确开关次数、整套双电源断电启动统计尚未完成。ROM 摘要和 USB 打开串口复位问题仍在。

触摸曾因 I2C 命令未初始化导致随机 ACK 期望电平，现已清零命令并明确 ACK=0；
修复后日志支持上述交互。设置和 PIN 的当前值以设备存储为准，不因文档更新重置。

下一阶段：补齐演示及可靠性记录，再接“桌面启动计算器快应用并返回”。
Quick App 与桌面的图形/输入生命周期仍待集成，不能直接叠加初始化 LVGL。
详细交接见 [总报告第 33 节](../../docs/bringup/official_lcd_feishu_report.md#33-桌面阶段完成确认与交接2026-09-24)。

之前的控件验收镜像保留在工作区
`artifacts/lcd-hardware-validation/touch-controls-confirmed/`。


### 独立启动修复与验证

2026-09-24：发现 USB 日志发送无界等待可能让未开串口的启动停住。
桌面配置现启用 `ESP32P4_USB_CONSOLE_BEST_EFFORT`，电脑不读取时允许丢弃
日志，保证发送不无限等待。已烧录验证镜像
`96da0e01e4a2f54dbcd6e3ec826bb32ccd0373a8f4f988e7fab36ca8ed5b2dd4`，
归档 `artifacts/desktop/standalone-console-fix/`；不开监视器时连续多次主板开关启动显示锁屏已获用户确认。
`i2c-ack-fixed` 保留为此前交互基线，最新镜像为上述 standalone-console-fix。
