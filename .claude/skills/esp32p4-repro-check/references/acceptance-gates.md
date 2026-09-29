# ESP32-P4X acceptance gates

Use this reference when executing or reviewing a reproduction run. Paths are relative to `contest2026_288_Bugyindudadui` unless stated otherwise.

## 1. Baseline gate

Run `scripts/check_baseline.py` first and stop on any failure.

| Profile | Identity check | Scope |
| --- | --- | --- |
| `final` (default) | source digest `7698ec765f6f2f18fbd9a40014c4a57a5bf7cb36be3d4385a1600fdd90c613b3` over 254 committed files | complete work, seven configurations |
| `p0` | commit `35a953cc3673c0329b6a8de569604d492e1b64f0` is an ancestor of `HEAD`, tree `0598b69891e19663a0ba3f559f03293746bc7974` | 2026-09-17 bring-up, four configurations |

The `final` digest is SHA-256 over the sorted `mode blob path` lines that `git ls-tree -r HEAD` lists for `app/`, `board/contest_board/` and `tools/`, skipping `*.md` and `.built`. It equals the sources of team-fork commit `79b565e814a5d8850edfbaa1a423a35be8eb92d7` (tree `1c574b39baa21787d5ca2eb2baa535db462f6c14`). The digest does not depend on commit IDs, so it gives the same answer on the team fork, after the official "Rebase and merge" (new commit IDs, identical files) and after a squash; whether `79b565e` is an ancestor is only reported. Documentation, Skill and manifest commits leave it unchanged, a committed edit to any firmware or host-tool file changes it, and uncommitted or untracked files in those directories fail a separate check. Do not use a branch name or a commit title as a substitute for this check.

Required manifest mappings:

| Source | Destination |
| --- | --- |
| `board/contest_board` | `vendor/openvela/boards/contest2026_288_board` |
| `app/p4x_selftest` | `packages/demos/contest2026_288_p4x_selftest` |
| `app/desktop` | `packages/demos/contest2026_288_desktop` |

## 2. Clean-build gate

For every config, execute from the openvela workspace:

```text
build.sh vendor/openvela/boards/contest2026_288_board/configs/<config> distclean
contest repo: bash board/contest_board/tools/prepare_esp_hal.sh
contest repo: git -C board/contest_board/chip/esp-hal-3rdparty submodule update --init components/mbedtls/mbedtls
build.sh vendor/openvela/boards/contest2026_288_board/configs/<config>
```

`distclean` deletes the ignored HAL checkout; a HAL left over from an older compatibility patch must be removed and prepared again, otherwise `prepare_esp_hal.sh` reports that the tree does not match.

Require the real build exit status 0, `Generated: nuttx.bin`, a non-empty `nuttx/nuttx.bin`, and saved size and SHA-256. Do not trust a wrapper or `tee` exit status without `pipefail`/`PIPESTATUS[0]`. Images embed build time and path, so SHA-256 differs between builds; compare size and identify the running image with `uname -a`.

Reference sizes, built on 2026-09-29 from `79b565e` (the sources of the `final` digest):

| Config | Purpose | `nuttx.bin` |
| --- | --- | ---: |
| `nsh` | J20 NSH, Timer, GPIO | 227956 |
| `uart0` | physical UART0 console | 229492 |
| `i2c` | I2C1 and i2ctool | 235572 |
| `demo` | I2C1 + p4x_selftest camera capture and JPEG | 260508 |
| `lcd` | MIPI-DSI display + LVGL demo | 600136 |
| `desktop` | desktop, lock screen, settings, touch | 2819684 |
| `desktop_camera` | final product: desktop + fall monitor + photo identification + camera | 2839456 |

## 3. Host-test gate

From the contest repository:

```bash
export ASAN_OPTIONS=detect_leaks=0
for t in board/contest_board/tests/test_*.py tools/monitor/tests/test_*.py tools/photo_identify/tests/test_*.py; do
  python3 "$t" || echo "FAIL $t"
done
```

Require all 11 files to pass: shared IRQ ownership, USB frame transport, camera triple buffering, camera preview, identify bridge, panel mailbox, desktop font banks, plus 16 monitor and 6 photo-identify unit tests.

## 4. Flash gate

Use the J20 USB Serial/JTAG port selected from the current enumeration (`python3 -m serial.tools.list_ports -v`). Require `esptool --chip esp32p4 ... chip-id`, ESP32-P4 revision v3.2, write offset `0x2000`, and `Hash of data verified.` Close every serial client before flashing.

## 5. Console gates

- J20: 115200 8N1 NuttX banner and `nsh>` for all configurations except `uart0`. Desktop configurations print desktop logs on the same console; NSH remains usable.
- Physical UART0 (`uart0`): GPIO37/U0TXD → USB-UART RX, GPIO38/U0RXD → USB-UART TX, common ground, no VCC, banner, NSH and reboot recovery.

## 6. P0 functional gates

- NSH and Timer: `help`, `uname -a`, `free`, `ps`, `uptime`, timed `sleep`/`usleep`. The timer path uses system time; do not claim `/dev/timer0`.
- GPIO4: `/dev/gpio0` low→high→low software readback and final low. External voltage is a separate gate; without instruments mark it `SKIP`.
- I2C1/ES8311: `/dev/i2c1`, 100 kHz, three complete scans whose only address is `0x18` on the `i2c` config (the camera module answers at `0x30` and GT911 at `0x5D` when attached).
- Selftest (`demo`): three human and three JSON runs; without a GPIO fixture require `PASS=4 FAIL=0 SKIP=1 RESULT=PASS`, JSON schema version 1.
- JTAG: Espressif `openocd-esp32`, J20 built-in USB-JTAG, `board/esp32p4-builtin.cfg`; require target examination, `reset halt`, `halted`, readable PC.

## 7. Display, touch and desktop gates

- `lcd`: `/dev/fb0` is 1024×600 RGB565; `lvgldemo widgets` shows changing LVGL widgets.
- Touch: GT911 at `0x5D`; drags follow the finger (X and Y are mirrored in the driver).
- `desktop` / `desktop_camera`: power-on shows the lock screen without an open serial monitor; swipe unlock, home, app center, settings and return work.
- PIN: set a 6-digit PIN twice; a wrong PIN is rejected; five wrong entries lock input for 30 s; the correct PIN unlocks; the mode survives a reset; changing or disabling the PIN asks for the old PIN. Settings live in `/data/desktop/settings.bin` (LittleFS at `0xF80000`, 512 KiB).
- Record visual results as user confirmation unless a log line proves them (`DESKTOP PAGE ...`, `DESKTOP swipe unlock`).

## 8. Camera gates

- `demo`: `p4x_selftest --camera-capture` and `p4x_selftest --jpeg-capture` report `verify summary mismatches=0`, `stage wait ret=0` and `PASS one frame`.
- Decode on the host with `tools/camera/decode_thumb.py` or `tools/monitor/jpeg_frame.py`; a frame with only a few distinct colours is not a real image.
- `desktop_camera`: the same capture must not disturb display refresh or touch.

## 9. Fall-monitor gates (`desktop_camera`)

1. Link test without the model or Feishu: `python3 tools/monitor/fall_watch.py --jpeg --once --dry-run --backend mock`.
2. Panel control with the real model, no Feishu: `python3 tools/monitor/fall_watch.py --port /dev/ttyACM0 --panel-control --jpeg --backend direct --dry-run --interval 10 --capture-timeout 120`, then press “开始监控”.
3. Alerts: export `FEISHU_WEBHOOK_URL` and drop `--dry-run`.

Require validated JPEG frames in `out/monitor/frames/`, one `events.jsonl` line per round with `ok`, `fall_detected`, `confidence`, `reason`, `enc_ms`, and `alert` when a card was sent. Report per-round timing and verdicts; never report accuracy from staged rounds.

## 10. Photo-identification gates (`desktop_camera`)

1. Stop the fall monitor and exit its script first.
2. Open 应用中心 → 拍照识物 and wait for the live preview.
3. `python3 tools/photo_identify/identify_watch.py --port /dev/ttyACM0 --backend direct --command-timeout 30 --capture-timeout 120 --max-completion-tokens 4096` (use `--backend mock` for a link test).
4. Press “拍照识别”; require the PNG in `out/photo_identify/frames/`, a result JSON in `out/photo_identify/logs/`, and the Chinese text in the result box.

If the script reports an NSH handshake timeout, close other serial clients, keep the matching `*.session.serial.log`, and retry; do not reflash or rewrite drivers on the strength of a single session.

## 11. Evidence gate

For each claim, record the exact commit and multi-repository manifest, the command and its true exit status, the raw log including failures and truncations, image size and SHA-256, the physical setup or photo/video reference, PASS/FAIL/SKIP and the limitation. Historical evidence proves a capability was observed on its own baseline only.

## 12. Claim boundary

Do not claim without direct evidence: on-device AI inference, fall-detection accuracy, continuous-video fall detection, AE/AWB on the sensor, audio/I2S/recording/playback, SPI2, Wi-Fi/BLE or Ethernet, Secure Boot, Flash Encryption, eFuse programming, PIN-based storage encryption, GPIO physical voltage, CMake builds of the desktop configurations, or long-run stability statistics.
