# ESP32-P4X acceptance gates

Use this reference when executing or reviewing a reproduction run. Paths are relative to `contest2026_288_Bugyindudadui` unless stated otherwise.

## 1. Baseline gate

Require all of the following before build or hardware work:

- Commit: `35a953cc3673c0329b6a8de569604d492e1b64f0`
- Tree: `0598b69891e19663a0ba3f559f03293746bc7974`
- Four configs: `board/contest_board/configs/{nsh,uart0,i2c,demo}/defconfig`
- App source: `app/p4x_selftest/p4x_selftest_main.c`
- Board mapping: `board/contest_board` → `vendor/openvela/boards/contest2026_288_board`
- App mapping: `app/p4x_selftest` → `packages/demos/contest2026_288_p4x_selftest`

The frozen commit may be an ancestor of a later documentation/Skill-only submission commit, but the BSP, selftest, manifest, and stored selftest evidence paths must be byte-for-byte unchanged from it.

Stop on any mismatch. Do not use a branch name as a substitute for commit verification.

## 2. Clean-build gate

For every config, execute from the openvela workspace:

```text
build.sh <config> distclean
contest repo: prepare_esp_hal.sh
contest repo: initialize components/mbedtls/mbedtls
build.sh <config>
```

Require:

- Actual build process exit status 0
- `Generated: nuttx.bin`
- Non-empty `nuttx/nuttx.bin`
- Saved image size and SHA-256

Do not trust a wrapper or `tee` exit status without `pipefail`/`PIPESTATUS[0]`.

## 3. Flash gate

Use the J20 USB Serial/JTAG port selected from the current device enumeration.

Require:

- `esptool --chip esp32p4 ... chip-id`
- ESP32-P4 revision v3.2
- Simple Boot write offset `0x2000`
- `Hash of data verified.`

A historical `/dev/ttyACM0` or `/dev/ttyACM2` is not a fixed port assignment.

## 4. Console gates

### J20

For `nsh`, `i2c`, and `demo`, require a 115200 8N1 NuttX banner and `nsh>` on J20.

### Physical UART0

For `uart0`, require:

- GPIO37/U0TXD → 3.3 V USB-UART RX
- GPIO38/U0RXD → USB-UART TX
- Common ground
- No VCC connection
- 115200 8N1 banner, NSH, and reboot recovery

## 5. Functional gates

### NSH and Timer

Run `help`, `uname -a`, `free`, `ps`, `uptime`, and timed sleep/usleep commands. The verified path uses system time; do not claim `/dev/timer0`.

### GPIO4

Require `/dev/gpio0` low→high→low software readback and final low restore. Report external GPIO4 voltage separately; without a meter, LED, logic analyzer, or oscilloscope, mark the physical gate `SKIP`.

### I2C1/ES8311

Require `/dev/i2c1`, 100 kHz, and at least three complete scans in which the only detected 7-bit address is `0x18`. Do not write codec registers merely to prove bus access.

### Selftest

Run three human and three JSON invocations. With no physical GPIO fixture, require:

```text
PASS=4 FAIL=0 SKIP=1 RESULT=PASS
```

Require JSON schema version 1, five typed test results, summary `4/0/1`, and result `PASS`.

### JTAG

Use Espressif `openocd-esp32`, J20 built-in USB-JTAG, and `board/esp32p4-builtin.cfg`. Require P4 target examination, revision v3.2, `reset halt`, state `halted`, and readable PC.

## 6. Evidence gate

For each claim, record:

- Exact commit and multi-repository manifest
- Command and true exit status
- Raw log, including failures and truncations
- Image size and SHA-256
- Physical setup/photo/video reference when applicable
- PASS/FAIL/SKIP and limitation

Historical evidence proves a capability was previously observed; it is not a post-merge run. Keep `35a953c` post-merge results separate.

## 7. Claim boundary

Do not claim unsupported audio, I2S, recording, playback, amplifier, SPI2, display, camera, Wi-Fi/BLE, Secure Boot, Flash Encryption, eFuse programming, GPIO physical voltage, or completed 10+10 stability statistics without direct evidence.
