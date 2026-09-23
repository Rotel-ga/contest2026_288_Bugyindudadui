---
name: esp32p4-repro-check
description: Reproduce and audit the ESP32-P4X openvela contest board on the frozen official baseline. Use when asked to verify the contest commit, run the four clean-build configurations, flash the ESP32-P4 Simple Boot image, validate J20 or physical UART0, exercise GPIO/Timer/I2C/p4x_selftest/JTAG, or collect final submission evidence without overstating unsupported capabilities.
---

# ESP32-P4X reproduction check

Reproduce the board from a verified source state and preserve evidence for every result. Treat build, flash, software-path, and physical-path checks as separate gates.

## Start with the read-only preflight

Run from the contest repository:

```bash
python3 .claude/skills/esp32p4-repro-check/scripts/check_baseline.py \
  --repo .
```

Stop if any check fails. Do not repair a mismatch by resetting, cleaning, or changing branches without explicit user approval.

For a machine-readable result, add `--json`. For an archived human-readable result:

```bash
python3 .claude/skills/esp32p4-repro-check/scripts/check_baseline.py \
  --repo . | tee docs/bringup/esp32p4_repro_check_preflight.log
```

## Follow the gated workflow

1. **Verify identity.** Require the frozen baseline commit to be an ancestor of `HEAD`, its tree to match, protected BSP/selftest paths to remain unchanged from that commit, required source/config files, manifest link mappings, the `demo = i2c + selftest` invariant, and stored evidence hashes.
2. **Record the environment.** Save `repo manifest -r`, repository status, tool versions, board revision, actual serial devices, and command lines.
3. **Build one configuration at a time.** For each of `nsh`, `uart0`, `i2c`, and `demo`, run the project sequence exactly: `distclean → prepare_esp_hal.sh → mbedTLS submodule update → build`. Require both exit status 0 and `Generated: nuttx.bin`; archive image size and SHA-256.
4. **Flash through J20.** Identify the current `/dev/ttyACMx`; never assume a historical device number. Require ESP32-P4 revision identification, offset `0x2000`, and `Hash of data verified.`
5. **Validate the intended console.** Use J20 for `nsh`, `i2c`, and `demo`. Use crossed 3.3 V GPIO37/GPIO38 with common ground for `uart0`; do not connect USB-UART VCC.
6. **Run functional gates.** Exercise NSH/Timer/GPIO, three complete I2C scans for 7-bit address `0x18`, three human and three JSON selftest runs, and Espressif OpenOCD reset/halt.
7. **Separate evidence classes.** Report GPIO software readback independently from physical pin voltage. Mark unavailable physical instrumentation as `SKIP`, not `PASS`.
8. **Close with an evidence matrix.** Record command, status, raw log, image hash, photo/video reference, limitation, and baseline for each claim.

Read [references/acceptance-gates.md](references/acceptance-gates.md) before build/flash work or when deciding PASS, FAIL, and SKIP.

## Preserve safety and truthfulness

- Never write eFuse, Secure Boot keys, Flash Encryption keys, or real secrets.
- Never infer board wiring from generic Kconfig defaults.
- Never call an I2C acknowledge or one-byte read complete audio support.
- Never call software GPIO readback external voltage evidence.
- Never claim `/dev/timer0`; the verified timer path uses system time.
- Never convert a historical log into a post-merge result.
- Keep failed and truncated logs; exclude them from successful-run counts without deleting them.
- Stop feature expansion during final submission work. Limit changes to evidence, documentation, and submission blockers.

## Report the outcome

Return a compact matrix with these columns:

```text
Gate | Baseline | Command or action | Result | Evidence | Limitation
```

End with three explicit lists:

- `PASSED`: directly verified on the stated baseline.
- `SKIPPED/PENDING`: not executed or lacking physical equipment.
- `FAILED`: executed and failed, including the first decisive error.
