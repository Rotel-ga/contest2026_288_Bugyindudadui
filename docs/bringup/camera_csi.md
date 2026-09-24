# ESP32-P4X SC2336 MIPI-CSI 摄像头采集与导出

本文记录 `p4x_selftest --camera-capture` 的完整使用方法：如何构建、烧录、在板上采集一帧，
以及如何在 PC 端把串口里的 base64 缩略图还原成 PNG。面向接手继续做图像识别的同学。

- 开发板：ESP32-P4X-Function-EV-Board V1.6
- 芯片：ESP32-P4 revision v3.2
- 传感器：SC2336（MIPI-CSI 2 lane）
- 配置：`board/contest_board/configs/demo/defconfig`
- 命令：`p4x_selftest --camera-capture [<dig_fine> <dig_coarse> <ang>]`

## 数据通路

```
SC2336 --MIPI-CSI(2 lane, 336 Mbps/lane)--> CSI bridge
  --> ISP (RAW8 BGGR -> demosaic -> WBG 白平衡 -> RGB565)
  --> DW-GDMA --> PSRAM 帧缓冲 (1280x720x2 = 1.8 MB)
  --> 板上 1/8 降采样 (160x90) --> base64 --> 串口
  --> PC 端 tools/camera/decode_thumb.py --> PNG
```

整帧 1.8 MB 留在 PSRAM 里，串口只送 160×90 的缩略图（28800 字节，约 3.7 秒）。
本配置没有挂可写文件系统（`FS_TMPFS`/`FS_FAT`/`FS_ROMFS` 均关闭），所以整帧**无法落盘**，
应用会打印 `frame not saved to ...; use the THUMB lines above instead` 并继续——这不是失败。
要把整帧传出去必须先做以太网，见文末「后续工作」。

## 前置条件

硬件：SC2336 模组接到板上 MIPI-CSI 排线座；USB 线接 J20（USB Serial/JTAG）。

配置：`demo/defconfig` 里与摄像头相关的四项，**都不要动**。

| 配置项 | 值 | 为什么需要 |
| --- | --- | --- |
| `CONFIG_ESPRESSIF_SPIRAM` | `y` | 1.8 MB 帧缓冲放不进内部 RAM |
| `CONFIG_MM_REGIONS` | `2` | 否则 PSRAM 不进堆，`malloc` 拿不到 |
| `CONFIG_I2C_TRACE` | `y` | **功能必需，不是调试残留**，详见「已知边界」第 1 条 |
| `CONFIG_ARCH_INTERRUPTSTACK` | `2048` | 默认值；曾怀疑不足并调到 8192，实测用量恒 468 字节，已回退 |

## 构建

`distclean` 会删掉被 gitignore 的 `esp-hal-3rdparty`，所以**顺序不能颠倒**：

```bash
cd /path/to/openvela-contest

# 1) 清理
PATH="$HOME/.local/bin:$PATH" ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/demo distclean

# 2) 恢复 HAL（distclean 刚把它删了）
cd contest2026_288_Bugyindudadui
bash board/contest_board/tools/prepare_esp_hal.sh
git -C board/contest_board/chip/esp-hal-3rdparty \
    submodule update --init components/mbedtls/mbedtls
cd ..

# 3) 构建
PATH="$HOME/.local/bin:$PATH" ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/demo
```

成功判据是 `Generated: nuttx.bin`，并且要**自己检查退出码**——包装脚本末尾的提示不足以判定成功。

## 烧录

```bash
esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 \
  write-flash 0x2000 out/.../nuttx.bin
```

偏移固定 `0x2000`（Simple Boot）。成功判据是 `Hash of data verified.`。

芯片复位会让 USB Serial/JTAG 重新枚举，**烧录后要等 2~3 秒再开串口**，
否则会看到 `FATAL: read zero bytes from port`。

## 采集

### 用脚本（推荐）

```bash
cd contest2026_288_Bugyindudadui
tools/camera/capture_camera.py                       # 用内置默认增益
tools/camera/capture_camera.py --gain 0x80 0x00 0x10 # 显式指定
tools/camera/capture_camera.py --port /dev/ttyACM0 --timeout 240
```

不传 `--port` 时按 Espressif VID `303a` 自动识别。脚本只用标准库、不依赖 pyserial：
pyserial 打开该设备时会拨动 DTR/RTS，导致设备重新枚举。

输出：

- `out/camera/capture-<时间戳>.log` — 完整会话留档
- `out/camera/latest.log` — 固定名，供解码脚本默认读取

### 手工

也可以直接在 NSH 里敲，但串口里 I2C trace 刷屏很厉害，建议重定向到文件再解码：

```
nsh> p4x_selftest --camera-capture 0x80 0x00 0x10
```

关键输出：

```
camera_capture: SC2336 ID 0xcb3a
camera_capture: verify summary mismatches=0
camera_capture: gain 0x3e07 = 0x80 ...
camera_capture: stage wbg r=184 g=153 b=256 ret=0
camera_capture: frame px=921600 distinct=709 diff_from_first=921002 ...
camera_capture: thumb begin w=160 h=90 fmt=rgb565le bytes=28800
THUMB:<base64>            (每行 72 个 base64 字符)
camera_capture: thumb end sum32=0x...
camera_capture: PASS one frame output=...
```

## 解码

```bash
tools/camera/decode_thumb.py                    # 默认读 out/camera/latest.log
tools/camera/decode_thumb.py out/camera/capture-20260924-153000.log
```

产物全部落在 `out/camera/`（该目录已在 `.gitignore` 里）：

| 文件 | 说明 |
| --- | --- |
| `thumb.rgb565` | 原始 RGB565 小端裸数据，160×90×2 = 28800 字节 |
| `thumb.png` | 直接转换的 PNG，所见即传感器所出 |
| `thumb-stretched.png` | 每通道各自拉伸到满量程，便于看暗部结构 |

脚本会校验 base64 长度与板上打印的 `sum32`，并打印一张 ASCII 预览；
`distinct` 颜色数是判断"是不是真图像"的主判据：

- `< 16` 近常量 ⇒ 不是真实图像（报错退出）
- `< 200` 有结构但层次偏少 ⇒ 可能欠曝，调增益
- 否则合格

实测参考值（室内桌面，三次不同采集）：

| 采集 | 整帧 distinct | 缩略图 distinct |
| --- | --- | --- |
| 白平衡校正前 | 2009 | 725 |
| 白平衡校正后 A | 935 | 438 |
| 白平衡校正后 B | 709 | 297 |

色数**随场景内容变化很大**，不要把某个具体数字当阈值——同样是校正后的真实图像，
上面 A、B 两次就差了 141 色（438 vs 297）。它只用来区分"真图像"与"纯色/近常量"。

**校正后色数下降是正常的，不是退化。** 当前用的 normalised 增益三项都 ≤1.0x
（0.719/0.598/1.000），衰减两个通道必然压缩色调范围、让原本不同的值并到一起。
换句话说这是"不 clip"换来的代价。低对比场景校正后有可能掉到 200 以下，
此时先看 `thumb-stretched.png` 有没有结构，再决定是不是真的欠曝。

解码与渲染**合在一次运行里完成**，且运行前会先删掉旧产物。早先分成两步时，
很容易只跑渲染不跑解码而看到上一次的图——症状是输出与上次逐字相同。

## 增益与白平衡调优

### 增益

默认值在 `app/p4x_selftest/p4x_camera_csi.c`：

```c
#define SC2336_GAIN_DIG_FINE    0x80   /* 0x3e07 */
#define SC2336_GAIN_DIG_COARSE  0x00   /* 0x3e06 */
#define SC2336_GAIN_ANG         0x10   /* 0x3e09 */
```

命令行三个参数可临时覆盖，无需重新编译，方便扫增益。

⚠️ **模拟增益 `ang` 的取值有约束**：低 3 位为 `0b100` 的值（`0x04`/`0x0c`/`0x14`/`0x1c`）
会让传感器恒输出常量 `0x39e7`（画面纯色）。实测可用的是 `0x00`/`0x08`/`0x10`/`0x18`/`0x1f`。
这是实测经验规律，**不是从手册推出来的**，换传感器批次请重新验证。

### 白平衡

raw Bayer 的绿色感光点是红蓝的两倍，不校正就明显偏绿。当前用 ISP WBG 做**静态**灰世界校正：

```c
#define SC2336_WB_GAIN_R  184   /* 0.719x */
#define SC2336_WB_GAIN_G  153   /* 0.598x */
#define SC2336_WB_GAIN_B  256   /* 1.000x */
```

增益是 12 位定点、`256 = 1.0x`（Q4.8），小于 256 表示衰减。
校正后实测 R/G=0.996、B/G=1.016。

换了光源或场景后重算：

```bash
tools/camera/calc_wb.py            # 读 out/camera/thumb.rgb565
```

脚本给两套值：

- **normalised**（推荐）：最大增益锁在 1.0x，不产生 clip，但整体变暗。仓里用的就是这套。
- **grey-world**：G 固定 1.0x、抬 R 和 B，保持亮度但会 clip，脚本会同时预测 clip 比例。

把打印出来的三行 `#define` 粘回 `p4x_camera_csi.c` 重新编译即可。

两个用法提示：

- 拿**校正前**的帧跑，才能得到需要的校正量。实测在未校正帧上得到 `188/166/256`，
  与仓里固化的 `184/153/256` 同量级（差异来自拟合用的场景不同）；
  同一帧的 grey-world 方案会 clip 掉 R 51.9% / B 39.3%，这就是选 normalised 的依据。
- 拿**已校正**的帧跑，会得到接近 `256/256/256`（≈1.0x）的结果——这正是"已经平衡、
  无需再动"的信号，不要把它再粘回代码。

实现上两点容易踩：`esp_isp_wbg_configure()` 必须置 `update_once_configured = 1`，
否则它会去等一个永远不来的 VSYNC；`bayer_order` 用 `COLOR_RAW_ELEMENT_ORDER_BGGR`
（它是枚举首成员，值为 0，所以"没设"和"设对了"看起来一样）。

## 已知边界

以下四项是当前交付的真实边界，不要在材料里包装掉。

1. **`CONFIG_I2C_TRACE=y` 是功能必需的，机制未解释。**
   关掉它，166 条模式表写到 `0x3200` 就会失败（`errno=5`，控制器报 NACK）。
   已经用有界二分把原因缩到这一项（先 `{DEBUG_FEATURES, I2C_TRACE}`，再 `{I2C_TRACE}`）。
   三条替代方案**全部证伪**：把 SCCB 间隔加到 5 ms、中断栈加到 8192、
   把 `esp_i2c.c` 的 `GET_STATUS()` 改成无条件。
   已知 `SC2336_SCCB_GAP_US 5000` 的节流修好了**读**侧（verify mismatches 10→0），
   但**写**侧为什么还依赖 trace，目前没有解释。
   还没做的判别性实验：把 tracedump 改成只在失败时打印——仍正常说明必需的是 trace 的
   *记录* 部分，再次失败说明必需的是打印带来的 *延时*。
2. **没有 AWB / AE。** 白平衡是对某一个场景的静态拟合，换光源会失准；曝光也不自动。
3. **`ang` 取值约束是实测经验规律**，非手册推导，见上。
4. **GPIO4 物理电平与 Timer 长稳未测**（缺外部夹具），与摄像头无关，但同属本作品的已知边界。

## 排障

| 症状 | 原因 / 处置 |
| --- | --- |
| `FATAL: read zero bytes from port` | 复位后 USB 重枚举，等 2~3 秒再开串口 |
| 画面纯色、`distinct` 只有个位数 | `ang` 落在低 3 位 `0b100` 的禁用值上，换 `0x10` |
| `distinct` 偏低、画面偏暗 | 抬 `dig_fine`（如 `0x80` → `0xc0`） |
| 明显偏绿 | WBG 没生效：确认 `stage wbg ... ret=0`，且 `update_once_configured=1` |
| `verify summary mismatches` 非 0 | SCCB 节流不足，确认 `SC2336_SCCB_GAP_US` 仍是 5000 |
| 写表 `errno=5` | 检查 `CONFIG_I2C_TRACE=y` 是否被误删（见边界第 1 条） |
| `malloc` 失败 / 拿不到帧缓冲 | 确认 `CONFIG_MM_REGIONS=2` 与 `CONFIG_ESPRESSIF_SPIRAM=y` |
| 解码报长度不符 | 某行 `THUMB:` 被其它输出打断，重新采集一次 |
| 解码输出与上次完全一样 | 确认读的是本次的 log（脚本默认 `latest.log`，每次采集都会覆盖） |

## 后续工作

- **以太网通道**：做图像识别的最大缺口。整帧 1.8 MB 走串口不现实，需要以太网。
  注意 `board_emac_init()` 目前全树 0 个调用方、`configs/eth/defconfig` 也不存在，得从零搭。
  串口 base64 传缩略图可以先作为演示过渡——已经这么做了，见
  [fall_alert.md](fall_alert.md)（定时采集 + 大模型跌倒判定 + 飞书告警）。
- **AE / AWB**：当前是静态值，接入自动曝光和自动白平衡后才能适应变化的光照。
- **`I2C_TRACE` 机制**：上面那个判别性实验成本很低（改 2 行、可 `git checkout` 退回），
  做掉能把这条边界从"机制未解释"收成明确结论。
