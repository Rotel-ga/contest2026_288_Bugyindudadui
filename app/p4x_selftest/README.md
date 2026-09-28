# ESP32-P4X board self-test

`p4x_selftest` is a device-side qualification demo for the
ESP32-P4X-Function-EV-Board.

## Usage

```text
p4x_selftest
p4x_selftest --json
p4x_selftest --camera
p4x_selftest --camera-capture
p4x_selftest --help
```

The default mode prints a human-readable result table. `--json` emits one
JSON object for automated collection. `--camera` verifies the SC2336 control
bus at 7-bit address `0x30` by reading its 16-bit chip-ID registers using an
SCCB repeated-start transaction at 100 kHz. A matching ID (`0xcb3a`) confirms
control-bus communication only; it does not yet confirm MIPI CSI frame capture.
`--camera-capture` first verifies the SC2336 ID, then requests exactly one 640x480 8-bit Bayer frame
at 30 fps (`V4L2_PIX_FMT_SBGGR8`) through the direct CSI controller. The completed buffer is written to `/tmp/sc2336-640x480.raw`. The direct
CSI path requires a completed frame callback; an I2C ACK or chip-ID match
alone is not treated as a successful capture.

The test covers system information, monotonic timing, the `/dev/gpio0`
software control path, and a 100 kHz one-byte read from ES8311 address
`0x18` on `/dev/i2c1`. GPIO physical voltage is reported as `SKIP` because
no external LED or measurement fixture is available. A skipped physical test
is not presented as electrical validation.
