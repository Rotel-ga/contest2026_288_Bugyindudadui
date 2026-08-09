# ESP32-P4X contest board

该目录通过 manifest 映射到：

```text
vendor/openvela/boards/contest2026_288_board
```

它包含 ESP32-P4 out-of-tree 芯片层、ESP32-P4X Function EV Board 板级实现、链接脚本和最小 NSH 配置。当前真机基线为 ESP32-P4 revision v3.2，控制台使用 J20 USB-Serial/JTAG。

## 第三方依赖

运行以下脚本准备固定版本的 ESP HAL：

```bash
./tools/prepare_esp_hal.sh
```

脚本克隆 `espressif/esp-hal-3rdparty` 的固定 commit，并应用 `patches/esp-hal-openvela-compat.patch`。`chip/esp-hal-3rdparty/` 被忽略，不应提交到专属仓。

## 配置

```text
configs/nsh/defconfig
```

当前配置启用 USB-Serial/JTAG、NSH、procfs 和板级 late initialization。`esp_bringup()` 在 late initialization 阶段挂载 procfs，使 `free` 和 `ps` 可用。

## 构建

从 openvela 工作区根目录执行：

```bash
PATH="$HOME/.local/bin:$PATH" \
  ./build.sh vendor/openvela/boards/contest2026_288_board/configs/nsh
```

详细识别、烧录和验收步骤见仓库根 README 与 `docs/bringup/`。
