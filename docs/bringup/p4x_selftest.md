# ESP32-P4X 板卡自检应用验证报告

## 验证范围与基线

本报告记录 `p4x_selftest` 方案 2 的构建、烧录、真机运行和稳定配置回归结果。该应用面向 BSP 开发者、ODM 和产线验收，用一个 NSH 命令汇总系统、时基、GPIO 软件链路和 ES8311 I2C 最小读路径；它不替代外部电气测量，也不代表完整音频能力。

- 开发板：ESP32-P4X-Function-EV-Board V1.6
- 芯片：ESP32-P4 revision v3.2
- 上游基线：`upstream/dev-ai-contest-2026@bd62fe3f3cfd2594420381b390daf081b6c11467`
- 验证分支：`feat/p4x-selftest`
- 验证对象：基于上述上游基线完成提交前验证；最终交付状态以承载本文件的 commit/PR 为准
- 独立配置：`board/contest_board/configs/demo/defconfig`
- NSH 命令：`p4x_selftest`、`p4x_selftest --json`

实现使用独立 `app/p4x_selftest`，manifest 映射目标为 `packages/demos/contest2026_288_p4x_selftest`。`demo` 以已验证的 `i2c` 配置为基线，只增加自检应用开关；`nsh`、`uart0`、`i2c` 三套稳定 defconfig 未修改。

## Clean build 与烧录

### 固定构建顺序

`distclean` 会删除被忽略的 ESP HAL，因此验证严格按以下顺序执行：

```bash
PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/<config> \
  distclean

bash board/contest_board/tools/prepare_esp_hal.sh

git -C board/contest_board/chip/esp-hal-3rdparty \
  submodule update --init components/mbedtls/mbedtls

PATH="$HOME/.local/bin:$PATH" \
  ./build.sh \
  vendor/openvela/boards/contest2026_288_board/configs/<config>
```

在 linked worktree 验证期间，测试脚本临时将根工程的 board/app link 映射到目标 worktree，并用 trap 在每轮结束时恢复。最终确认 overlay 已恢复，没有把 `p4x_selftest` 泄漏到稳定配置。

### Demo 结果

| 项目 | 结果 |
| --- | --- |
| `demo` clean build | PASSED；出现 `Generated: nuttx.bin` |
| builtin 注册 | PASSED；`builtin_list.h` 可见 `p4x_selftest` |
| ELF 符号 | PASSED；`nm` 可见 `p4x_selftest_main` |
| `nuttx.bin` | 237592 bytes；SHA-256 `69f997b1113a15afbc1fce9832d23293fb82af86c2477bf729801a64eafaeaea` |
| `nuttx` ELF | SHA-256 `e2624826f25852ba546468a74d75590fc3454cc956dac644a65af3c807b11b18` |
| `nxstyle` / whitespace | PASSED |

本轮没有保留 `demo` 的全量原始 build log，因此本文不把任何仓库文件宣称为该次原始构建日志；上表只记录当场已核验的成功判据和产物摘要。

烧录使用 `/dev/ttyACM0`，`esptool --chip esp32p4` 识别 ESP32-P4 revision v3.2；Simple Boot 镜像写入偏移为 `0x2000`，结束标志为 `Hash of data verified.`。

## 真机测试方法

J20 USB Serial/JTAG 在 pyserial 打开端口时会因 DTR/RTS ioctl 触发重枚举，因此最终采集器使用 POSIX `open()` + `termios`，不操作 DTR/RTS。NSH 提示符可能包含 ANSI `ESC[K`，发送 CRLF 又会形成双提示符；最终采集只发送 LF，并保留原始 ANSI 字节。

应用返回值通过以下短 wrapper 分类：

```text
if p4x_selftest;then echo Z0;else echo ZN;fi
if p4x_selftest --json;then echo Z0;else echo ZN;fi
```

当前配置未启用 `CONFIG_NSH_VARS`，NSH 只能区分 0 与非 0，不能展示任意非零应用退出码。因此非法参数的动态结果证明“非零”，源码分支确认其精确返回值为 2。

## 真机结果

人类可读模式连续 3 次、JSON 模式连续 3 次，共 6 轮；六轮均为：

```text
PASS=4 FAIL=0 SKIP=1 RESULT=PASS
```

| 检查项 | 6 轮结果 | 说明 |
| --- | --- | --- |
| `system` | PASS | `NuttX 0.0.0 risc-v` |
| `timer` | PASS | 请求 500 ms，六轮均测得 510 ms |
| `gpio_sw` | PASS | `/dev/gpio0` 低→高→低回读成功，退出前恢复低 |
| `gpio_physical` | **SKIP** | 无 LED、万用表、逻辑分析仪或示波器等外部夹具，不能宣称 GPIO4 物理电平已验证 |
| `i2c_es8311` | PASS | `/dev/i2c1`、100 kHz、7 位地址 `0x18`，执行 1-byte read，数据为 `0x00` |

JSON 三轮均通过 `json.loads` 解析，并检查 `schema_version`、应用/板卡字段、测试数组、状态、错误码、耗时、汇总计数和最终结果的类型及值。应用在 `--json` 有效路径上的自身 stdout 只有一个 JSON 对象；仓库中的 raw 串口日志还保留了 NSH 命令回显、wrapper 标记和提示符。

辅助参数结果：

- `p4x_selftest --help`：成功并输出 usage；
- `p4x_selftest --invalid`：动态证明返回非零，源码确认返回 2；
- 任何实际测试项失败时应用返回 1；本轮未出现实际失败。

I2C 结果只证明 ES8311 地址上的最小 acknowledge/read 路径，不表示 I2S、codec audio upper-half、录音、播放、功放或扬声器已经支持。

## 持久化证据

| 文件 | 内容 | SHA-256 |
| --- | --- | --- |
| `p4x_selftest_serial_raw.log` | 3 轮 human、3 轮 JSON、help、非法参数的脱敏原始串口记录 | `dd13caebc28022cbf8d8de60d42d06254bf57fbf9bafa851ff611e3d41a968b7` |
| `p4x_selftest_serial_report.json` | 串口采集器解析后的结构化结果 | `f7da8240d2aff8ccf2a89ad531d3609b18b211585b03619cd4cbd1cb26174b9e` |

两份仓库证据与 `/tmp/p4x_selftest_serial_raw.log`、`/tmp/p4x_selftest_serial_report.json` 原件逐字节比对。日志未保留真实 MAC 地址等设备唯一标识。

## 稳定配置无回归

`nsh`、`uart0`、`i2c` 均重新执行完整 `distclean → prepare HAL → mbedTLS submodule → build`。每轮同时验证：构建退出状态为 0、日志包含 `Generated: nuttx.bin`、展开配置未启用自检开关、`builtin_list.h` 未注册 `p4x_selftest`。

| 配置 | 结果 | `nuttx.bin` 大小 | SHA-256 |
| --- | --- | ---: | --- |
| `nsh` | PASSED | 227700 bytes | `58e25cc331a911a8436f8b80f54070573d8b3e81053af0c91de02dad470ce19d` |
| `uart0` | PASSED | 229492 bytes | `7df507950661481096d254c8e61f8d2d5c5294ecfe6753793c9e2a502c884338` |
| `i2c` | PASSED | 235388 bytes | `ba2d2fa50e0933fe52f984b2d0415c4ef33f65d17a6476aa7556a496ccdcc55c` |

本次三份完整执行日志的采集来源分别为 `/tmp/p4x_regression_nsh_build.log`、`/tmp/p4x_regression_uart0_build.log`、`/tmp/p4x_regression_i2c_build.log`；未将大体积全量日志复制进仓库，持久化结果以上表及成功判据为准。

## 已知限制与待验证项

1. **GPIO4 物理电平：SKIP。** 软件 ioctl 写入/回读不能替代引脚外部测量；获得合适夹具前必须保持 `SKIP`。
2. **应用栈峰值：待验证。** 当前 `Makefile` 的 `STACKSIZE = 2048`；本次配置未启用 `CONFIG_STACK_COLORATION`，构建目录也没有 `compile_commands.json`，无法量化运行时峰值，不能宣称已有栈余量数据。
3. **GPIO 故障注入矩阵：未执行。** 已静态确认 `open()` 成功后的失败路径会尝试恢复低并关闭，但没有对各 ioctl 失败点做动态故障注入；`restore_low=ok` 仅表示恢复写调用成功，不等同于再次 readback。
4. **方案 3：未进入。** 宿主报告套件与 Skill 联动只有在方案 2 稳定且材料时间充足后才单独评审，本轮未自动扩展。
5. **交付追踪：** 本报告记录基于 `bd62fe3` 的提交前验证；最终交付 commit 和 PR 以 Git 历史与远端页面为准。

## 结论

方案 2 的可验证范围已闭环：`demo` clean build、烧录、builtin 注册、人类/JSON 各 3 次真机运行、JSON 契约和三套稳定配置无回归均通过；GPIO4 外部电平因缺少夹具如实保留为 `SKIP`。本报告不替代提交前 diff 审查；后续交付与回归仍应遵守“不用软件回读冒充物理证据”的边界。
