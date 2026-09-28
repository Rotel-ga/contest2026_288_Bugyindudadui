#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pull the board's base64 JPEG (``p4x_selftest --jpeg-capture``) out of the
console text and check it before anyone looks at it.

Board format::

    camera_capture: frame px=921600 distinct=664 ...
    jpeg_sw: begin bytes=N w=W h=H q=80 sum32=0x........ awb=on enc_ms=M
    jpg:<base64, 72 chars per line>
    jpeg_sw: end

Standard library only, like the rest of tools/monitor, so the pixels are not
decoded here: the checks are length, sum32, SOI/EOI and the SOF size.  The
model and any viewer take the .jpg as is.
"""

import base64
import re
from dataclasses import dataclass

HEADER_RE = re.compile(
    r"jpeg_sw: begin bytes=(\d+) w=(\d+) h=(\d+) q=(\d+) "
    r"sum32=0x([0-9a-fA-F]+)(?: awb=(\w+))?(?: enc_ms=(\d+))?")
PAYLOAD_RE = re.compile(r"^jpg:([A-Za-z0-9+/=]+)\s*$", re.MULTILINE)
STATS_RE = re.compile(r"camera_capture: frame px=(\d+) distinct=(\d+)")
FAIL_RE = re.compile(r"jpeg_sw: (?:encode failed|output alloc failed)[^\n]*")

BEGIN = "jpeg_sw: begin"
END = "jpeg_sw: end"

# Same bar the board uses for its own "near-constant frame" warning
# (csi_report_stats), applied to the full-frame distinct count it prints.
MIN_DISTINCT_COLOURS = 64

# SOF markers (baseline / extended / progressive) carry the frame size.
SOF_MARKERS = (0xC0, 0xC1, 0xC2)


class JpegError(RuntimeError):
    """The console text does not carry a usable JPEG."""


@dataclass
class JpegFrame:
    width: int
    height: int
    quality: int
    data: bytes
    distinct: int        # full-frame distinct RGB565 values, -1 if not printed
    awb: str             # "on", "off", or "" for firmware that does not say
    enc_ms: int          # device encode time, -1 if not printed

    @property
    def size(self):
        return len(self.data)


def sof_size(data):
    """Return (width, height) from the first SOF segment, or None."""
    pos = 2
    while pos + 4 <= len(data):
        if data[pos] != 0xFF:
            return None
        marker = data[pos + 1]
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            pos += 2
            continue
        length = (data[pos + 2] << 8) | data[pos + 3]
        if marker in SOF_MARKERS:
            if pos + 9 > len(data):
                return None
            height = (data[pos + 5] << 8) | data[pos + 6]
            width = (data[pos + 7] << 8) | data[pos + 8]
            return width, height
        if marker == 0xDA:            # scan data follows, no SOF seen
            return None
        pos += 2 + length
    return None


def parse(text):
    """Extract and validate the LAST JPEG in ``text``.

    The last one, not the first: a console buffer can still hold the tail of
    an earlier capture, and a stale frame is the failure that goes unnoticed.
    """
    start = text.rfind(BEGIN)
    if start < 0:
        failed = FAIL_RE.search(text)
        if failed:
            raise JpegError(f"board-side encoder failed: {failed.group(0)}")
        if "thumb begin" in text:
            raise JpegError("console carries an RGB565 thumbnail but no JPEG; "
                            "run without --jpeg or flash the JPEG firmware")
        raise JpegError("no 'jpeg_sw: begin' header in the console output")

    header = HEADER_RE.match(text, start)
    if not header:
        raise JpegError("malformed 'jpeg_sw: begin' header")
    nbytes, width, height, quality = (int(header.group(i)) for i in range(1, 5))
    want_sum = int(header.group(5), 16)
    awb = header.group(6) or ""
    enc_ms = int(header.group(7)) if header.group(7) else -1

    end = text.find(END, header.end())
    if end < 0:
        raise JpegError("'jpeg_sw: end' missing; capture cut short")
    payload = "".join(PAYLOAD_RE.findall(text, header.end(), end))
    if not payload:
        raise JpegError("header present but no jpg: payload lines")

    try:
        data = base64.b64decode(payload, validate=True)
    except ValueError as error:
        raise JpegError(f"payload is not valid base64: {error}") from error
    if len(data) != nbytes:
        # Usually one jpg: line got cut by interleaved console output.
        raise JpegError(f"payload truncated: {len(data)}/{nbytes} bytes")

    got_sum = sum(data) & 0xFFFFFFFF
    if got_sum != want_sum:
        raise JpegError(f"checksum mismatch board=0x{want_sum:08x} "
                        f"host=0x{got_sum:08x}")

    if data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
        raise JpegError("payload is not a complete JPEG (SOI/EOI missing)")
    if sof_size(data) != (width, height):
        raise JpegError(f"JPEG frame size {sof_size(data)} does not match "
                        f"header {width}x{height}")

    # Frame stats of this capture only: after the previous JPEG, if any.
    prev_end = text.rfind(END, 0, start)
    stats = list(STATS_RE.finditer(text, prev_end if prev_end >= 0 else 0,
                                   start))
    distinct = int(stats[-1].group(2)) if stats else -1
    if 0 <= distinct < MIN_DISTINCT_COLOURS:
        raise JpegError(f"near-constant frame ({distinct} distinct values); "
                        "the sensor is not producing an image")

    return JpegFrame(width=width, height=height, quality=quality, data=data,
                     distinct=distinct, awb=awb, enc_ms=enc_ms)
