#!/usr/bin/env bash

set -euo pipefail

ESP_HAL_URL="https://github.com/espressif/esp-hal-3rdparty.git"
ESP_HAL_COMMIT="b90b1837cb5ad24747deb4c895246037cc206ce5"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ESP_HAL_DIR="${BOARD_DIR}/chip/esp-hal-3rdparty"
PATCH_FILE="${BOARD_DIR}/patches/esp-hal-openvela-compat.patch"

if [[ ! -d "${ESP_HAL_DIR}/.git" ]]; then
  git clone "${ESP_HAL_URL}" "${ESP_HAL_DIR}"
fi

git -C "${ESP_HAL_DIR}" fetch --quiet origin "${ESP_HAL_COMMIT}"
git -C "${ESP_HAL_DIR}" checkout --detach "${ESP_HAL_COMMIT}"

if git -C "${ESP_HAL_DIR}" apply --reverse --check "${PATCH_FILE}"; then
  echo "esp-hal compatibility patch is already applied"
elif git -C "${ESP_HAL_DIR}" apply --check "${PATCH_FILE}"; then
  git -C "${ESP_HAL_DIR}" apply "${PATCH_FILE}"
  echo "applied esp-hal compatibility patch"
else
  echo "error: esp-hal tree does not match the pinned commit or patch state" >&2
  exit 1
fi

echo "esp-hal-3rdparty ready at ${ESP_HAL_COMMIT}"
