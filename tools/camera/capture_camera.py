#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Run p4x_selftest --camera-capture over the board's serial console.

The whole console session is written to out/camera/ so that decode_thumb.py can
pull the base64 thumbnail out of it afterwards.  Only the Python standard
library is used - no pyserial - because opening the ESP USB Serial/JTAG device
through pyserial toggles DTR/RTS and makes the device re-enumerate.

Usage:
    tools/camera/capture_camera.py
    tools/camera/capture_camera.py --gain 0x80 0x00 0x10
    tools/camera/capture_camera.py --port /dev/ttyACM0 --timeout 240
"""

import argparse
import os
import re
import subprocess
import sys
import termios
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OUTDIR = REPO / "out" / "camera"

# Espressif USB vendor ID; the built-in USB Serial/JTAG bridge shows up with it.
ESP_VID = "303a"

REPORT_PATTERNS = (
    r"SC2336 ID[^\n]*",
    r"SC2336 register [^\n]*",
    r"verify summary[^\n]*",
    r"gain 0x[0-9a-f]{4} = [^\n]*",
    r"stage wbg[^\n]*",
    r"stage wait[^\n]*",
    r"probe [^\n]*",
    r"frame px=[^\n]*",
    r"first px[^\n]*",
    r"thumb begin[^\n]*",
    r"thumb end[^\n]*",
    r"PASS one frame[^\n]*",
    r"CSI capture failed[^\n]*",
    r"WARNING[^\n]*",
)


def find_port():
    """Return the first tty whose udev properties carry the Espressif VID."""
    for name in sorted(os.listdir("/dev")):
        if not name.startswith(("ttyACM", "ttyUSB")):
            continue
        dev = "/dev/" + name
        try:
            info = subprocess.run(["udevadm", "info", "-q", "property", dev],
                                  capture_output=True, text=True,
                                  timeout=5).stdout
        except (OSError, subprocess.SubprocessError):
            continue
        if f"ID_VENDOR_ID={ESP_VID}" in info:
            return dev
    return None


def open_port(dev, baud=termios.B115200):
    """Raw 8N1, non-blocking, no DTR/RTS poking."""
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attr = termios.tcgetattr(fd)
    attr[0] = 0                                        # iflag
    attr[1] = 0                                        # oflag
    attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attr[3] = 0                                        # lflag: raw
    attr[4] = baud
    attr[5] = baud
    attr[6] = list(attr[6])
    attr[6][termios.VMIN] = 0
    attr[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attr)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def drain(fd, seconds):
    buf = b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
            if chunk:
                buf += chunk
            else:
                time.sleep(0.03)
        except BlockingIOError:
            time.sleep(0.03)
        except OSError:
            break
    return buf


def run_command(fd, command, budget, done_markers):
    """Send one NSH command and collect output until a marker or the budget."""
    os.write(fd, (command + "\n").encode())
    buf = b""
    end = time.time() + budget
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
        except BlockingIOError:
            time.sleep(0.03)
            continue
        except OSError:
            break
        if not chunk:
            time.sleep(0.03)
            continue
        buf += chunk
        if any(m in buf for m in done_markers):
            buf += drain(fd, 2.0)      # let the tail arrive
            break
    return buf


def clean(raw):
    text = raw.replace(b"\x00", b"").decode("utf-8", errors="replace")
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device (default: autodetect)")
    ap.add_argument("--gain", nargs=3, metavar=("FINE", "COARSE", "ANG"),
                    help="SC2336 gain override, e.g. --gain 0x80 0x00 0x10; "
                         "omit to use the built-in default")
    ap.add_argument("--timeout", type=float, default=180.0,
                    help="seconds to wait for the capture (default: 180)")
    ap.add_argument("--outdir", type=Path, default=OUTDIR,
                    help=f"where to write the log (default: {OUTDIR})")
    args = ap.parse_args()

    port = args.port or find_port()
    if port is None:
        print("error: no Espressif serial device found; pass --port",
              file=sys.stderr)
        return 1

    args.outdir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    log_path = args.outdir / f"capture-{stamp}.log"
    latest = args.outdir / "latest.log"

    command = "p4x_selftest --camera-capture"
    if args.gain:
        command += " " + " ".join(args.gain)

    print(f"port    : {port}")
    print(f"command : {command}")

    fd = open_port(port)
    try:
        os.write(fd, b"\n")
        drain(fd, 1.0)                 # flush the banner / stale prompt
        raw = run_command(fd, command, args.timeout,
                          (b"PASS one frame", b"CSI capture failed"))
    finally:
        os.close(fd)

    text = clean(raw)
    log_path.write_text(text, encoding="utf-8")
    latest.write_text(text, encoding="utf-8")

    thumb_lines = text.count("THUMB:")
    print(f"\nlog     : {log_path}")
    print(f"latest  : {latest}")
    print(f"lines   : {len(text.splitlines())}  (THUMB: {thumb_lines})")

    print("\n--- application output ---")
    for pattern in REPORT_PATTERNS:
        for line in re.findall(pattern, text):
            print("  " + line.strip())

    if "PASS one frame" in text:
        print(f"\nnext: tools/camera/decode_thumb.py {latest}")
        return 0

    print("\ncapture did not report success", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
