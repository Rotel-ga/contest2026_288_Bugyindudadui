#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rebuild the camera thumbnail from a captured console log.

p4x_selftest prints a decimated RGB565 frame as base64, one chunk per line
prefixed with "THUMB:".  The prefix matters: when CONFIG_I2C_TRACE is enabled
(and it has to be - see docs/bringup/camera_csi.md) the console is full of I2C
trace records, so the payload has to be picked out rather than read as a block.

Decoding and rendering happen in a single run on purpose.  An earlier two-step
version made it easy to re-render without re-decoding and silently look at the
previous capture; the tell was output identical to the run before.  Stale files
are removed up front for the same reason.

Only the standard library is used: the PNG is assembled with zlib and struct so
that Pillow is not required.

Usage:
    tools/camera/decode_thumb.py                      # out/camera/latest.log
    tools/camera/decode_thumb.py path/to/capture.log
"""

import argparse
import base64
import re
import struct
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUTDIR = REPO / "out" / "camera"

RAMP = " .:-=+*#%@"


def write_png(path, width, height, rgb_rows):
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    scanlines = b"".join(b"\x00" + row for row in rgb_rows)
    with open(path, "wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        fp.write(chunk(b"IHDR", ihdr))
        fp.write(chunk(b"IDAT", zlib.compress(scanlines, 9)))
        fp.write(chunk(b"IEND", b""))


def ascii_preview(width, height, lum, rows_wanted=30, cols_wanted=78):
    step_y = max(1, height // rows_wanted)
    step_x = max(1, width // cols_wanted)
    for y in range(0, height, step_y):
        line = []
        for x in range(0, width, step_x):
            value = lum[y * width + x]
            line.append(RAMP[min(len(RAMP) - 1, value * len(RAMP) // 256)])
        print("  " + "".join(line))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", type=Path,
                    default=OUTDIR / "latest.log",
                    help="captured console log (default: out/camera/latest.log)")
    ap.add_argument("--outdir", type=Path, default=OUTDIR,
                    help=f"where to write the artefacts (default: {OUTDIR})")
    args = ap.parse_args()

    if not args.log.is_file():
        print(f"error: no such log: {args.log}", file=sys.stderr)
        return 1

    args.outdir.mkdir(parents=True, exist_ok=True)
    raw_out = args.outdir / "thumb.rgb565"
    png_out = args.outdir / "thumb.png"
    stretch_out = args.outdir / "thumb-stretched.png"

    # Remove stale artefacts so a failed run cannot leave the previous image in
    # place and look like a success.
    for stale in (raw_out, png_out, stretch_out):
        stale.unlink(missing_ok=True)

    text = args.log.read_bytes().replace(b"\x00", b"").decode("utf-8", "replace")
    text = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)

    header = re.search(r"thumb begin w=(\d+) h=(\d+) fmt=(\S+) bytes=(\d+)",
                       text)
    if not header:
        print("error: no 'thumb begin' header in the log - the capture did not "
              "emit a thumbnail", file=sys.stderr)
        return 1

    width, height = int(header.group(1)), int(header.group(2))
    fmt, nbytes = header.group(3), int(header.group(4))
    footer = re.search(r"thumb end sum32=0x([0-9a-fA-F]+)", text)

    print(f"header  : {width}x{height} fmt={fmt} bytes={nbytes}")
    print("footer  : " + (f"sum32=0x{footer.group(1)}" if footer
                          else "<missing>"))

    if fmt != "rgb565le":
        print(f"error: unsupported pixel format {fmt!r}", file=sys.stderr)
        return 1

    payload = "".join(re.findall(r"THUMB:([A-Za-z0-9+/=]+)", text))
    expected_chars = nbytes // 3 * 4
    print(f"base64  : {len(payload)} chars (expected {expected_chars})")

    data = base64.b64decode(payload + "=" * (-len(payload) % 4))
    print(f"decoded : {len(data)} bytes (expected {nbytes})")

    ok = True
    if len(data) != nbytes:
        print("WARNING length mismatch - a line was probably broken up by "
              "console output; rendering what arrived", file=sys.stderr)
        ok = False
        data = (data + b"\x00" * nbytes)[:nbytes]

    if footer:
        want = int(footer.group(1), 16)
        got = sum(data) & 0xFFFFFFFF
        if want == got:
            print(f"checksum: ok (0x{got:08x})")
        else:
            print(f"checksum: MISMATCH board=0x{want:08x} host=0x{got:08x}",
                  file=sys.stderr)
            ok = False

    raw_out.write_bytes(data)

    # RGB565 little endian -> RGB888
    pixels = [data[i * 2] | (data[i * 2 + 1] << 8)
              for i in range(width * height)]
    r5 = [(p >> 11) & 0x1F for p in pixels]
    g6 = [(p >> 5) & 0x3F for p in pixels]
    b5 = [p & 0x1F for p in pixels]

    distinct = len(set(pixels))
    print(f"\npixels  : {len(pixels)}, distinct colours {distinct}")
    print(f"range   : r {min(r5)}..{max(r5)}/31  g {min(g6)}..{max(g6)}/63  "
          f"b {min(b5)}..{max(b5)}/31")

    # The threshold is set against what real data looks like, not against
    # "not degenerate": a real 160x90 scene has hundreds of distinct values.
    if distinct < 16:
        print(f"verdict : NEAR-CONSTANT ({distinct} colours) - this is not a "
              "real image", file=sys.stderr)
        ok = False
    elif distinct < 200:
        print(f"verdict : thin ({distinct} colours) - structure but little "
              "tonal range; under-exposed or gain too low?")
    else:
        print(f"verdict : looks like a real image ({distinct} colours)")

    def to_rows(red, green, blue):
        rows = []
        for y in range(height):
            row = bytearray()
            for x in range(width):
                i = y * width + x
                row += bytes((red[i], green[i], blue[i]))
            rows.append(bytes(row))
        return rows

    r8 = [(v * 255 + 15) // 31 for v in r5]
    g8 = [(v * 255 + 31) // 63 for v in g6]
    b8 = [(v * 255 + 15) // 31 for v in b5]
    write_png(png_out, width, height, to_rows(r8, g8, b8))

    def stretch(values):
        lo, hi = min(values), max(values)
        if hi == lo:
            return [128] * len(values)
        return [(v - lo) * 255 // (hi - lo) for v in values]

    rs, gs, bs = stretch(r5), stretch(g6), stretch(b5)
    write_png(stretch_out, width, height, to_rows(rs, gs, bs))

    print(f"\nraw     : {raw_out}")
    print(f"png     : {png_out}")
    print(f"stretch : {stretch_out}")

    lum = [(rs[i] * 30 + gs[i] * 59 + bs[i] * 11) // 100
           for i in range(len(pixels))]
    print("\nauto-levelled preview:")
    ascii_preview(width, height, lum)

    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
