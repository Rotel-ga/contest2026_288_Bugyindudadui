# ESP32-P4X board self-test

`p4x_selftest` is a device-side qualification demo for the
ESP32-P4X-Function-EV-Board.

## Usage

```text
p4x_selftest
p4x_selftest --json
p4x_selftest --help
```

The default mode prints a human-readable result table. `--json` emits one
JSON object for automated collection.

The test covers system information, monotonic timing, the `/dev/gpio0`
software control path, and a 100 kHz one-byte read from ES8311 address
`0x18` on `/dev/i2c1`. GPIO physical voltage is reported as `SKIP` because
no external LED or measurement fixture is available. A skipped physical test
is not presented as electrical validation.
