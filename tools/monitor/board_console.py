#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Raw serial console access for the ESP32-P4 USB Serial/JTAG bridge.

Only the standard library is used - no pyserial - because opening this device
through pyserial toggles DTR/RTS and makes the board re-enumerate.  The
termios setup below is the same one tools/camera/capture_camera.py has been
using for the single-shot capture; it is factored out here so the fall monitor
can drive the same console in a loop.
"""

import os
import re
import subprocess
import termios
import time

# Espressif USB vendor ID; the built-in USB Serial/JTAG bridge shows up with it.
ESP_VID = "303a"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


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


def clean(raw):
    """Drop NULs and ANSI escapes so the payload can be matched by regex."""
    text = raw.replace(b"\x00", b"").decode("utf-8", errors="replace")
    return ANSI_RE.sub("", text)


class BoardConsole:
    """A raw 8N1 console that never touches DTR/RTS.

    Keeping one instance open across many captures matters: every close/open
    cycle risks catching the device mid re-enumeration, which shows up as
    "read zero bytes from port".
    """

    def __init__(self, port, baud=termios.B115200):
        self.port = port
        self.baud = baud
        self._fd = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_exc):
        self.close()
        return False

    def open(self):
        fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attr = termios.tcgetattr(fd)
        attr[0] = 0                                    # iflag
        attr[1] = 0                                    # oflag
        attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attr[3] = 0                                    # lflag: raw
        attr[4] = self.baud
        attr[5] = self.baud
        attr[6] = list(attr[6])
        attr[6][termios.VMIN] = 0
        attr[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attr)
        termios.tcflush(fd, termios.TCIOFLUSH)
        self._fd = fd

    def close(self):
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    def drain(self, seconds):
        """Read and discard-into-buffer for a fixed time; returns the bytes."""
        buf = b""
        end = time.time() + seconds
        while time.time() < end:
            try:
                chunk = os.read(self._fd, 65536)
            except BlockingIOError:
                time.sleep(0.03)
                continue
            except OSError:
                break
            if chunk:
                buf += chunk
            else:
                time.sleep(0.03)
        return buf

    def sync(self, seconds=1.0):
        """Push a newline and swallow the banner or a stale prompt."""
        os.write(self._fd, b"\n")
        return self.drain(seconds)

    def run_command(self, command, budget, done_markers, tail=2.0):
        """Send one NSH command, collect until a marker appears or time is up.

        Returns the cleaned text.  A marker hit still waits ``tail`` seconds so
        trailing lines (the "thumb end" checksum in particular) arrive.
        """
        os.write(self._fd, (command + "\n").encode())
        buf = b""
        end = time.time() + budget
        markers = tuple(m.encode() if isinstance(m, str) else m
                        for m in done_markers)
        while time.time() < end:
            try:
                chunk = os.read(self._fd, 65536)
            except BlockingIOError:
                time.sleep(0.03)
                continue
            except OSError:
                break
            if not chunk:
                time.sleep(0.03)
                continue
            buf += chunk
            if any(m in buf for m in markers):
                buf += self.drain(tail)
                break
        return clean(buf)


def list_candidates():
    """Every USB-ish tty currently present, Espressif or not."""
    return ["/dev/" + name for name in sorted(os.listdir("/dev"))
            if name.startswith(("ttyACM", "ttyUSB"))]


def resolve_port(explicit):
    """Return an explicit port or autodetect one; raise if neither works.

    The two failure modes need different fixes, so they get different messages:
    no tty at all means the board did not enumerate (cable/power/J20), while a
    tty that is not Espressif means autodetect picked wrong and --port settles
    it.
    """
    if explicit:
        return explicit

    port = find_port()
    if port is not None:
        return port

    candidates = list_candidates()
    if not candidates:
        raise RuntimeError(
            "未发现任何 USB 串口设备（/dev/ttyACM*、/dev/ttyUSB* 都不存在），"
            "板子没有枚举出来。检查：USB 线接在 J20（USB Serial/JTAG）、板子已上电、"
            "插上后等 2~3 秒再试；然后用 `lsusb | grep 303a` 确认主机看得到设备。")

    raise RuntimeError(
        "存在串口设备 " + ", ".join(candidates) +
        "，但没有一个带 Espressif VID 303a。若确定其中某个是板子，"
        "用 --port 显式指定。")
