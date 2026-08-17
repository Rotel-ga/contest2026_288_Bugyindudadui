#!/usr/bin/env bash
# JTAG reset/halt 一键脚本（避免终端粘贴断行问题）
# 用法：在工程根目录 contest2026_288_Bugyindudadui 下执行：
#   sudo bash docs/bringup/run_openocd_jtag.sh
# 日志会写入 docs/bringup/openocd.log

set -uo pipefail

OCD_ROOT="${OCD_ROOT:-$HOME/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260703}"

# sudo 环境下 $HOME 可能变成 /root，做个兜底：若默认路径不存在，尝试原始用户目录
if [[ ! -x "$OCD_ROOT/bin/openocd" && -n "${SUDO_USER:-}" ]]; then
  OCD_ROOT="/home/${SUDO_USER}/.espressif/tools/openocd-esp32/v0.12.0-esp32-20260703"
fi

OPENOCD="$OCD_ROOT/bin/openocd"
SCRIPTS="$OCD_ROOT/share/openocd/scripts"

echo "OpenOCD:  $OPENOCD"
echo "SCRIPTS:  $SCRIPTS"
"$OPENOCD" --version 2>&1 | head -1

# 切到脚本所在仓库根目录（docs/bringup 的上两级）
cd "$(dirname "$0")/../.." || exit 1
echo "PWD:      $(pwd)"

LOG="docs/bringup/openocd.log"

"$OPENOCD" \
  -s "$SCRIPTS" \
  -c 'set ESP_RTOS hwthread; set ESP_ONLYCPU 1' \
  -f board/esp32p4-builtin.cfg \
  -c 'init' \
  -c 'reset halt' \
  -c 'echo "=== TARGET STATE AFTER reset halt ==="' \
  -c 'targets' \
  -c 'echo "=== PC (readable only when halted) ==="' \
  -c 'reg pc force' \
  -c 'esp appimage_offset 0x2000' \
  -c 'echo "=== JTAG reset/halt verification done ==="' \
  -c 'shutdown' \
  2>&1 | tee "$LOG"

echo "=== openocd 退出，日志已写入 $LOG ==="
