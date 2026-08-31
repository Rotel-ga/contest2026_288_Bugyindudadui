# ES8311 I2C post-merge 回归

## 验证基线

- 上游分支：`upstream/dev-ai-contest-2026`
- 上游提交：`a0a7451b9a3e78e1b4f5a135288fa8fc5ea43558`
- 合入 PR：`#9 feat: add ESP32-P4X ES8311 I2C probe`
- 验证分支：`test/p1-i2c-post-merge`
- 开发板：ESP32-P4X-Function-EV-Board V1.6
- 芯片：ESP32-P4 revision v3.2

## 回归结果

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| 上游 head clean build | 通过 | `i2c_post_merge_build.log`，包含 `Generated: nuttx.bin` |
| I2C 展开配置 | 通过 | `i2c_post_merge_config.txt`，I2C1/SCL8/SDA7/bus 1/100 kHz |
| 镜像 | 通过 | 235388 bytes；SHA-256 见 `i2c_post_merge_image.sha256` |
| 芯片识别 | 通过 | `i2c_post_merge_chip_id.log`，ESP32-P4 revision v3.2 |
| J20 烧录 | 通过 | `i2c_post_merge_flash.log`，包含 `Hash of data verified.` |
| NSH | 通过 | `i2c_post_merge_scan.log`，出现 builtin `i2c` 和 `nsh>` |
| I2C 设备节点 | 通过 | `i2c_post_merge_scan.log`，存在 `/dev/i2c1` |
| ES8311 地址响应 | 通过 | 4 次完整有效扫描均只出现 7 位地址 `0x18` |

## 构建产物

```text
image_size=235388 bytes
sha256=b7277861c8b15783cc1d4f29031375bd3ebe30333a052643bdd6d653f16df38a
flash_offset=0x2000
```

镜像哈希与合入前构建不同，原因是镜像包含构建时间；镜像大小保持 235388 bytes。post-merge 验收以本次从 `a0a7451` clean build 得到的哈希为准。

## 扫描结果

设备配置：

```text
/dev/i2c1
bus=1
frequency=100000
```

执行命令：

```text
i2c dev 0x03 0x77
```

首次连续采集得到 2 次完整有效扫描，均出现：

```text
10: -- -- -- -- -- -- -- -- 18 -- -- -- -- -- -- --
```

该连续采集的第三条命令在采集进程关闭 stdin 时被截断为 `i2c dev 0x0`，因此未计入有效扫描，也未从原始日志删除。随后 `i2c_post_merge_scan_retry.log` 又得到 2 次完整有效扫描，均只出现 `0x18`。补充日志开头的 `missing required argument(s)` 来自前一条截断命令残留，后续两次完整扫描正常。

合计：4 次完整有效扫描，结果一致，均只发现 `0x18`。

## 结论边界

本次只完成上游合入提交上的 I2C 回归，证明 I2C1、GPIO7/GPIO8、`/dev/i2c1` 和 ES8311 7 位地址 `0x18` 的探测路径可重复工作。

暂不把项目 P1 标记为最终完成，下一步继续评估 SPI 最小回环实验的硬件引脚、板级初始化和独立配置可行性。该结果不代表 I2S、codec audio upper-half、录音、播放、功放或扬声器已经支持。
