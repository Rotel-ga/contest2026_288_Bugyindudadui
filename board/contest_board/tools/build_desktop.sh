#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build the desktop configuration with lock screen and settings.
set -euo pipefail
BOARD_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OPENVELA_ROOT=${OPENVELA_ROOT:-/home/mi/Developer/openvela}
WORKSPACE=$(cd "$OPENVELA_ROOT" && pwd)
if [[ ! -f "$WORKSPACE/nuttx/tools/configure.sh" ||
      ! -f "$WORKSPACE/build/envsetup.sh" ]]; then
  echo "OPENVELA_ROOT is not a complete openvela workspace: $WORKSPACE" >&2
  exit 1
fi
APP_LINK="$WORKSPACE/packages/demos/contest2026_288_desktop"
APP_SOURCE=$(cd "$BOARD_DIR/../../app/desktop" && pwd)
VENDOR_BOARD_LINK="$WORKSPACE/vendor/openvela/boards/contest2026_288_board"
VENDOR_BOARD_SOURCE=$(cd "$BOARD_DIR" && pwd)
export PATH="$HOME/.local/bin:$PATH"
ORIGINAL_APP_LINK=""
RESTORE_APP_LINK=0
ORIGINAL_VENDOR_BOARD_LINK=""
RESTORE_VENDOR_BOARD_LINK=0
if [[ -L "$APP_LINK" ]]; then
  ORIGINAL_APP_LINK=$(readlink "$APP_LINK")
  if [[ $(readlink -f "$APP_LINK") != "$APP_SOURCE" ]]; then
    rm "$APP_LINK"
    ln -s "$APP_SOURCE" "$APP_LINK"
    RESTORE_APP_LINK=1
  fi
elif [[ -e "$APP_LINK" ]]; then
  echo "Application link path is occupied: $APP_LINK" >&2
  exit 1
else
  ln -s "$APP_SOURCE" "$APP_LINK"
  RESTORE_APP_LINK=2
fi

# The full openvela tree has one shared vendor-board symlink. Temporarily point
# it at this stable worktree so build.sh compiles this branch's board code.
if [[ -L "$VENDOR_BOARD_LINK" ]]; then
  ORIGINAL_VENDOR_BOARD_LINK=$(readlink "$VENDOR_BOARD_LINK")
  if [[ $(readlink -f "$VENDOR_BOARD_LINK") != "$VENDOR_BOARD_SOURCE" ]]; then
    rm "$VENDOR_BOARD_LINK"
    ln -s "$VENDOR_BOARD_SOURCE" "$VENDOR_BOARD_LINK"
    RESTORE_VENDOR_BOARD_LINK=1
  fi
elif [[ -e "$VENDOR_BOARD_LINK" ]]; then
  echo "Vendor board path is occupied: $VENDOR_BOARD_LINK" >&2
  exit 1
else
  ln -s "$VENDOR_BOARD_SOURCE" "$VENDOR_BOARD_LINK"
  RESTORE_VENDOR_BOARD_LINK=2
fi
restore_app_link() {
  if [[ "$RESTORE_APP_LINK" == 1 ]]; then
    rm -f "$APP_LINK"
    ln -s "$ORIGINAL_APP_LINK" "$APP_LINK"
  elif [[ "$RESTORE_APP_LINK" == 2 ]]; then
    rm -f "$APP_LINK"
  fi
  if [[ "$RESTORE_VENDOR_BOARD_LINK" == 1 ]]; then
    rm -f "$VENDOR_BOARD_LINK"
    ln -s "$ORIGINAL_VENDOR_BOARD_LINK" "$VENDOR_BOARD_LINK"
  elif [[ "$RESTORE_VENDOR_BOARD_LINK" == 2 ]]; then
    rm -f "$VENDOR_BOARD_LINK"
  fi
}
trap restore_app_link EXIT
cd "$WORKSPACE"
bash nuttx/tools/configure.sh -e "$BOARD_DIR/configs/desktop"
# The stable worktree contains board sources; the complete openvela checkout
# owns the pinned HAL used by this repository. Keep the stable worktree clean.
HAL_SOURCE="$OPENVELA_ROOT/contest2026_288_Bugyindudadui/board/contest_board/chip/esp-hal-3rdparty"
HAL_LINK="$BOARD_DIR/chip/esp-hal-3rdparty"
# The standard checkout keeps the pinned HAL beside this script.  A copied
# worktree may already contain it as a directory or as a link, so only create
# the link when the path is genuinely absent (and do not try to link a path to
# itself after this branch is moved into the standard repository directory).
if [[ ! -e "$HAL_LINK" && ! -L "$HAL_LINK" ]]; then
  ln -s "$HAL_SOURCE" "$HAL_LINK"
fi
if [[ ! -f "$HAL_LINK/components/esp_common/include/esp_err.h" ]]; then
  echo "Pinned HAL is missing: $HAL_LINK" >&2
  exit 1
fi
if [[ ! -f "$HAL_LINK/components/mbedtls/mbedtls/include/mbedtls/version.h" ]]; then
  echo "Pinned mbedTLS submodule is missing: $HAL_LINK/components/mbedtls/mbedtls" >&2
  exit 1
fi
if [[ $# -eq 0 ]]; then set -- -j8; fi
# Re-run application registration when a Makefile adds an NSH command.
# NuttX otherwise keeps the old apps context marker across incremental builds.
./build.sh vendor/openvela/boards/contest2026_288_board/configs/desktop context
(set +eu
source "$WORKSPACE/build/envsetup.sh"
set -e
make -C "$APP_LINK" TOPDIR="$WORKSPACE/nuttx" APPDIR="$WORKSPACE/apps" register)
./build.sh vendor/openvela/boards/contest2026_288_board/configs/desktop "$@"
test -s nuttx/nuttx.bin
python3 - <<'CHECK'
from pathlib import Path
size = Path('nuttx/nuttx.bin').stat().st_size
if size + 0x2000 > 0xf80000:
    raise SystemExit('Firmware overlaps reserved desktop data partition')
print('Desktop image fits before 0xf80000 settings partition:', size)
CHECK
sha256sum nuttx/nuttx.bin
