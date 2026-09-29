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
import fcntl
import re
import select
import subprocess
import termios
import time

# Espressif USB vendor ID; the built-in USB Serial/JTAG bridge shows up with it.
ESP_VID = "303a"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

# Console RX ring on the board (ESP_USBCDC_BUFFERSIZE 64 in
# board/contest_board/chip/espressif/esp_usbserial.c) holds 63 bytes; input
# beyond that while NSH is not reading is dropped without notice.
RX_QUEUE_MAX = 63


class BoardBusy(RuntimeError):
    """The board did not get back to the NSH prompt in time."""


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
        self.trace = None
        self.progress = None
        self.pace_handshake = False

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *_exc):
        self.close()
        return False

    def open(self):
        fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(fd)
            raise BoardBusy("串口正在被另一个监控/识物脚本占用，请先退出它")
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
                select.select([self._fd], [], [], max(0, min(0.1, end - time.time())))
                continue
            except OSError:
                break
            if chunk:
                if self.trace:
                    self.trace.write(chunk)
                    self.trace.flush()
                buf += chunk
            else:
                select.select([self._fd], [], [], max(0, min(0.1, end - time.time())))
        return buf

    def sync(self, seconds=1.0):
        """Push a newline and swallow the banner or a stale prompt."""
        os.write(self._fd, b"\n")
        return self.drain(seconds)

    def _read_until(self, buf, done, end):
        """Append to ``buf`` until ``done(buf)`` holds or time.time() > end."""
        next_progress = time.monotonic() + 5
        while not done(buf) and time.time() < end:
            if self.progress and time.monotonic() >= next_progress:
                self.progress(f"串口已接收 {len(buf)} 字节，仍在等待完成标记")
                next_progress = time.monotonic() + 5
            try:
                chunk = os.read(self._fd, 65536)
            except BlockingIOError:
                chunk = b""
            except OSError:
                break
            if chunk:
                if self.trace:
                    self.trace.write(chunk)
                    self.trace.flush()
                buf += chunk
            else:
                select.select([self._fd], [], [], max(0, min(0.1, end - time.time())))
        return buf

    def run_command(self, command, budget, done_markers, tail=2.0):
        """Send one NSH command, collect until a marker appears or time is up.

        Returns the cleaned text of this command only.  A marker hit still
        waits ``tail`` seconds so trailing lines arrive.  Raises BoardBusy
        when the board does not get back to the prompt within ``budget``.
        """
        line = command.encode() + b"\n"
        if len(line) > RX_QUEUE_MAX:
            raise ValueError(f"NSH command over {RX_QUEUE_MAX} bytes: {command}")
        end = time.time() + budget

        # A capture cut short on the host keeps running on the board; input
        # queued meanwhile past RX_QUEUE_MAX is dropped.  So send only a short
        # echo first (its leading newline ends any stale half line) and the
        # command once its output shows NSH is idle.  The anchor is the output
        # line: the echoed input reads "echo @@...".
        if self.progress:
            self.progress("等待 NSH 握手回应")
        anchor = b"\n@@" + os.urandom(4).hex().encode()
        handshake = b"\necho " + anchor[1:] + b"\n"
        if self.pace_handshake:
            # Opt-in for the independent object listener. Pace the handshake
            # too, and handle nonblocking short writes before waiting for it.
            for byte in handshake:
                while True:
                    try:
                        if os.write(self._fd, bytes([byte])) == 1:
                            break
                    except BlockingIOError:
                        pass
                    if time.time() >= end:
                        raise BoardBusy("发送 NSH 握手超时")
                    select.select([], [self._fd], [], 0.01)
                time.sleep(0.005)
        else:
            os.write(self._fd, handshake)
        buf = self._read_until(b"", lambda b: anchor in b, end)
        at = buf.find(anchor)
        if at < 0:
            raise BoardBusy(f"板子 {budget:.0f}s 内没有回到 nsh 提示符"
                            f"（上一条命令未结束或板子卡死，需复位）")

        # echo output can precede readline's next prompt.  Wait for that
        # prompt before feeding the next command into the small USB RX FIFO.
        buf = self._read_until(
            buf, lambda b: b"nsh> " in b[at + len(anchor):], end)
        if b"nsh> " not in buf[at + len(anchor):]:
            raise BoardBusy("收到 echo 回应，但没有收到下一条 NSH 提示符")
        buf += self.drain(0.05)
        if self.progress:
            self.progress(f"NSH 握手成功，发送：{command}")
        # Pace only the short command, never the image receive path.  Check
        # write progress rather than assuming a nonblocking write is complete.
        for byte in line:
            while True:
                try:
                    if os.write(self._fd, bytes([byte])) == 1:
                        break
                except BlockingIOError:
                    pass
                if time.time() >= end:
                    raise BoardBusy("发送采集命令超时")
                select.select([], [self._fd], [], 0.01)
            time.sleep(0.005)
        markers = tuple(m.encode() if isinstance(m, str) else m
                        for m in done_markers)
        buf = self._read_until(buf[at + len(anchor):],
                               lambda b: any(m in b for m in markers), end)
        if any(m in buf for m in markers):
            buf += self.drain(tail)
        else:
            # A host deadline does not cancel the foreground NSH command.
            # Keep consuming data for a bounded recovery period so the USB
            # writer can finish, or report an unresolved busy board clearly.
            if self.progress:
                self.progress("采集预算已到，继续接收最多 30 秒以等待命令结束")
            buf = self._read_until(buf,
                                   lambda b: any(m in b for m in markers),
                                   time.time() + 30)
            if any(m in buf for m in markers):
                buf += self.drain(tail)
            elif self.progress:
                self.progress("恢复等待仍未收到完成标记；保留串口日志，板端状态未确认")
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
