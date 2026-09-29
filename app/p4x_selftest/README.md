# ESP32-P4X board self-test

`p4x_selftest` is a device-side qualification demo for the
ESP32-P4X-Function-EV-Board. It also carries the SC2336 camera capture path
used by the fall monitor and the photo-identification preview.

## Usage

```text
p4x_selftest
p4x_selftest --json
p4x_selftest --camera
p4x_selftest --camera-capture [<dig_fine> <dig_coarse> <ang>]
p4x_selftest --jpeg-capture [--no-awb] [<dig_fine> <dig_coarse> <ang>]
p4x_selftest --jpeg-selftest
p4x_selftest --help
```

The default mode prints a human-readable result table. `--json` emits one
JSON object for automated collection. The test covers system information,
monotonic timing, the `/dev/gpio0` software control path, and a 100 kHz
one-byte read from ES8311 address `0x18` on `/dev/i2c1`. GPIO physical voltage
is reported as `SKIP` because no external LED or measurement fixture is
available. A skipped physical test is not presented as electrical validation.

`--camera` verifies the SC2336 control bus at 7-bit address `0x30` by reading
its 16-bit chip-ID registers (`0xcb3a` expected). A matching ID confirms
control-bus communication only.

`--camera-capture` programs the official 166-entry 1280x720 RAW8 register table,
reads back PLL, output size, HTS and VTS (`verify summary mismatches=0`), runs
the frame through the ISP (RAW8 BGGR → RGB565, static white balance) into a
PSRAM buffer, and prints a 640x360 box-averaged RGB565 thumbnail as base64
`THUMB:` lines with a `sum32`. The configuration has no writable file system,
so the full frame is not saved; decode the console log with
`tools/camera/decode_thumb.py`.

`--jpeg-capture` encodes the full 1280x720 frame on the board (fixed-point
colour conversion, Q13 integer DCT, grey-world white balance unless
`--no-awb`) and prints it as base64 `jpg:` lines between `jpeg_sw: begin` and
`jpeg_sw: end`; `tools/monitor/jpeg_frame.py` validates length, `sum32`,
SOI/EOI and the SOF size. `--jpeg-selftest` checks the encoder on a synthetic
frame. The three optional numbers override the sensor gain registers
`0x3e07`, `0x3e06` and `0x3e09` (default `0x80 0x00 0x10`).

A capture is successful only when the frame-complete callback fired
(`stage wait ret=0`, `PASS one frame`); an I2C acknowledge or chip-ID match
alone is not treated as a capture. See
[docs/bringup/camera_csi.md](../../docs/bringup/camera_csi.md) for tuning and
limitations.
