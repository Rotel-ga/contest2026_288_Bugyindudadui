---
name: esp32p4-repro-check
description: Reproduce and audit the VelaP4X ESP32-P4X openvela contest work. Use when asked to verify the contest baseline, run the seven clean-build configurations (nsh, uart0, i2c, demo, lcd, desktop, desktop_camera), flash the ESP32-P4 Simple Boot image, validate J20 or physical UART0, exercise GPIO/Timer/I2C/p4x_selftest/JTAG, the MIPI-DSI display, GT911 touch, the desktop PIN lock screen, the SC2336 camera, the fall monitor or photo identification, or collect submission evidence without overstating unsupported capabilities.
---

# ESP32-P4X reproduction check

Reproduce the board from a verified source state and preserve evidence for every result. Treat build, flash, software-path, physical-path and cloud-AI checks as separate gates.

## Start with the read-only preflight

Run from the contest repository:

```bash
python3 .claude/skills/esp32p4-repro-check/scripts/check_baseline.py --repo .
```

The default `final` profile verifies the complete work:

- source baseline by content: the digest of the 254 committed files under `app/`, `board/contest_board/` and `tools/` (`*.md` and `.built` excluded) equals `7698ec765f6f2f18fbd9a40014c4a57a5bf7cb36be3d4385a1600fdd90c613b3`, the sources of team-fork commit `79b565e`. The official repository merges with "Rebase and merge", so the same sources carry different commit IDs there; whether `79b565e` is an ancestor of `HEAD` is reported for information only;
- no uncommitted or untracked build inputs in those three directories;
- the three manifest mappings exist, including `app/desktop → packages/demos/contest2026_288_desktop`;
- configuration contracts: `desktop_camera = desktop + selftest`, `demo = i2c + camera`, and `desktop_camera` does not enable `I2C_TRACE`;
- 24 device and entry-point tokens (`/dev/gpio0`, `/dev/i2c1`, framebuffer, touch, `/data`, `fgctl`, `pictl`, PIN storage, IRQ and USB frame paths);
- no API key or webhook token in `app/`, `board/contest_board/` or `tools/`;
- official `esp_lcd` provenance: reversing `openvela.patch` on a temporary copy restores all 19 upstream hashes;
- stored `p4x_selftest` evidence hashes.

Expected result on a clean checkout of the team fork or the official repository: `Summary: PASS=10 FAIL=0 RESULT=PASS`. Documentation, Skill and manifest commits leave the digest unchanged; any committed change to a firmware or host-tool file changes it.

Use `--profile p0` only on a checkout of the 2026-09-17 bring-up period (four configurations, `demo = i2c + selftest`); it verifies commit `35a953c` and its tree. On the final tree the `p0` profile reports the post-P0 BSP changes by design. Add `--json` for machine-readable output; `--expected-digest` audits a different `final` source state.

Stop if any check fails. Do not repair a mismatch by resetting, cleaning, or changing branches without explicit user approval.

## Follow the gated workflow

1. **Verify the source baseline** with the preflight above.
2. **Record the environment.** Save `repo manifest -r`, repository status, tool versions, board revision, actual serial devices and command lines.
3. **Build one configuration at a time.** For each of `nsh`, `uart0`, `i2c`, `demo`, `lcd`, `desktop`, `desktop_camera`, run `distclean → prepare_esp_hal.sh → mbedTLS submodule update → build`. Require exit status 0 and `Generated: nuttx.bin`; archive image size and SHA-256. `desktop` and `desktop_camera` need the `app/desktop` manifest link; without it the link step fails on `desktop_boot_main`.
4. **Run the host tests** (`board/contest_board/tests/*.py`, `tools/monitor/tests/*.py`, `tools/photo_identify/tests/*.py`) with `ASAN_OPTIONS=detect_leaks=0`; require all 11 files to pass.
5. **Flash through J20.** Identify the current `/dev/ttyACMx`; never assume a historical device number. Require ESP32-P4 revision identification, offset `0x2000`, and `Hash of data verified.` The desktop settings partition at `0xF80000` is outside the image; never erase it without the owner's approval.
6. **Validate the intended console.** J20 for everything except `uart0`; crossed 3.3 V GPIO37/GPIO38 with common ground for `uart0`, never USB-UART VCC.
7. **Run functional gates** from [references/acceptance-gates.md](references/acceptance-gates.md): P0 (NSH/Timer/GPIO/I2C/selftest/JTAG), display and touch, desktop and PIN lock, camera, fall monitor, photo identification.
8. **Separate evidence classes.** GPIO software readback is not pin voltage; a mock model response is not a recognition result; user visual confirmation is recorded as such, with the count only when it was counted.
9. **Close with an evidence matrix.** Record command, status, raw log, image hash, photo/video reference, limitation and baseline for each claim.

Read [references/acceptance-gates.md](references/acceptance-gates.md) before build, flash or AI work, and when deciding PASS, FAIL and SKIP.

## Preserve safety and truthfulness

- Never write eFuse, Secure Boot keys, Flash Encryption keys, or real secrets.
- Keep `MIMO_API_KEY` and `FEISHU_WEBHOOK_URL` in the environment only; never write them to files, logs, commits or chat.
- Never claim on-device AI inference: the board captures and encodes, the PC scripts call the MiMo model in the cloud.
- Never claim fall-detection accuracy, false-positive or false-negative rates without a labelled dataset; staged runs are functional evidence only.
- Never describe the PIN lock as encryption, Secure Boot or serial-console protection; it is a UI access limit.
- Run one serial client at a time: `fall_watch.py`, `identify_watch.py` and serial terminals share the J20 port, and opening the port may reset the board.
- Keep `I2C_TRACE` disabled in `desktop_camera`; its records corrupt image frames on the USB console.
- Never infer board wiring from generic Kconfig defaults; never call an I2C acknowledge complete audio support; never claim `/dev/timer0`.
- Never convert a historical log into a result for a later baseline. Keep failed and truncated logs; exclude them from success counts without deleting them.

## Report the outcome

Return a compact matrix with these columns:

```text
Gate | Baseline | Command or action | Result | Evidence | Limitation
```

End with three explicit lists:

- `PASSED`: directly verified on the stated baseline.
- `SKIPPED/PENDING`: not executed, lacking equipment, or only user-confirmed without a log.
- `FAILED`: executed and failed, including the first decisive error.
