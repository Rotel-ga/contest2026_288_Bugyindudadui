#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Turn the board's base64 THUMB lines into a PNG the vision model can read.

The board prints a 1/8 decimated RGB565 copy of the frame (160x90) because the
full 1280x720 frame is 1.8 MB and will not fit through a 115200 console.  See
docs/bringup/camera_csi.md for the data path.

The PNG is assembled with zlib and struct so Pillow is not required - the whole
monitor stays on the standard library, same as the rest of tools/.
"""

import base64
import re
import struct
import zlib
from dataclasses import dataclass

HEADER_RE = re.compile(r"thumb begin w=(\d+) h=(\d+) fmt=(\S+) bytes=(\d+)")
FOOTER_RE = re.compile(r"thumb end sum32=0x([0-9a-fA-F]+)")
PAYLOAD_RE = re.compile(r"THUMB:([A-Za-z0-9+/=]+)")

# A real 160x90 scene has hundreds of distinct colours.  Below this the frame is
# effectively a flat colour, which means the sensor is not producing an image
# (see the "ang gain" constraint in docs/bringup/camera_csi.md) - analysing it
# would only waste a model call and produce a confident, meaningless answer.
MIN_DISTINCT_COLOURS = 16


class ThumbError(RuntimeError):
    """The console text does not carry a usable thumbnail."""


@dataclass
class Thumb:
    width: int
    height: int
    r5: list
    g6: list
    b5: list
    distinct: int
    checksum: str        # "ok", "mismatch" or "missing"

    @property
    def pixel_count(self):
        return self.width * self.height


def parse(text):
    """Extract the LAST thumbnail found in ``text``.

    Taking the last one matters in the monitor loop: a console buffer can still
    hold the tail of a previous capture, and silently analysing a stale frame is
    exactly the failure mode that is hard to notice.
    """
    start = text.rfind("camera_capture: thumb begin")
    if start < 0:
        raise ThumbError("no 'thumb begin' header in the console output")
    text = text[start:]

    header = HEADER_RE.search(text)
    if not header:
        raise ThumbError("malformed 'thumb begin' header")

    width, height = int(header.group(1)), int(header.group(2))
    fmt, nbytes = header.group(3), int(header.group(4))
    if fmt != "rgb565le":
        raise ThumbError(f"unsupported pixel format {fmt!r}")
    if width <= 0 or height <= 0 or nbytes != width * height * 2:
        raise ThumbError(f"inconsistent header: {width}x{height} bytes={nbytes}")

    payload = "".join(PAYLOAD_RE.findall(text))
    if not payload:
        raise ThumbError("header present but no THUMB: payload lines")

    data = base64.b64decode(payload + "=" * (-len(payload) % 4))
    if len(data) != nbytes:
        # Usually one THUMB: line got cut by interleaved I2C trace output.
        raise ThumbError(f"payload truncated: {len(data)}/{nbytes} bytes")

    footer = FOOTER_RE.search(text)
    if footer:
        want = int(footer.group(1), 16)
        got = sum(data) & 0xFFFFFFFF
        checksum = "ok" if want == got else "mismatch"
        if checksum == "mismatch":
            raise ThumbError(f"checksum mismatch board=0x{want:08x} "
                             f"host=0x{got:08x}")
    else:
        checksum = "missing"

    pixels = [data[i * 2] | (data[i * 2 + 1] << 8)
              for i in range(width * height)]
    distinct = len(set(pixels))
    if distinct < MIN_DISTINCT_COLOURS:
        raise ThumbError(f"near-constant frame ({distinct} colours); the "
                         "sensor is not producing an image")

    return Thumb(
        width=width,
        height=height,
        r5=[(p >> 11) & 0x1F for p in pixels],
        g6=[(p >> 5) & 0x3F for p in pixels],
        b5=[p & 0x1F for p in pixels],
        distinct=distinct,
        checksum=checksum,
    )


def _stretch(values, full_scale):
    lo, hi = min(values), max(values)
    if hi == lo:
        return [128] * len(values)
    return [(v - lo) * 255 // (hi - lo) for v in values]


def _to_8bit(values, full_scale):
    return [(v * 255 + full_scale // 2) // full_scale for v in values]


def to_png(thumb, scale=1, stretch=False):
    """Render the thumbnail as PNG bytes.

    ``scale`` nearest-neighbour upscales the image.  160x90 is small enough that
    some vision models downrank or refuse it; upscaling adds no information but
    reliably gets the frame accepted.  ``stretch`` levels each channel to full
    range, which helps in dim scenes - the capture path has no auto-exposure.
    """
    if scale < 1:
        raise ValueError("scale must be >= 1")

    if stretch:
        r = _stretch(thumb.r5, 31)
        g = _stretch(thumb.g6, 63)
        b = _stretch(thumb.b5, 31)
    else:
        r = _to_8bit(thumb.r5, 31)
        g = _to_8bit(thumb.g6, 63)
        b = _to_8bit(thumb.b5, 31)

    width, height = thumb.width, thumb.height
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            i = y * width + x
            row += bytes((r[i], g[i], b[i])) * scale
        rows.extend([bytes(row)] * scale)

    return encode_png(width * scale, height * scale, rows)


def encode_png(width, height, rows):
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    scanlines = b"".join(b"\x00" + row for row in rows)
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(scanlines, 9)) +
            chunk(b"IEND", b""))
