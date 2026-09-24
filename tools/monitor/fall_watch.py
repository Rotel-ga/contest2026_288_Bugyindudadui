#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Periodic camera capture -> vision model -> Feishu alert.

Every ``--interval`` seconds the board is asked for one frame over the NSH
console (``p4x_selftest --camera-capture``), the base64 thumbnail is rebuilt
into a PNG on the PC, sent to a vision model for fall detection, and a Feishu
group alert is pushed when the model says someone is on the floor.

    ESP32-P4 + SC2336 --base64 thumb/USB serial--> PC --PNG--> MiMo
                                                    |
                                                    +--> Feishu webhook

Quick start (two terminals):

    export MIMO_API_KEY=...            # never commit this
    tools/monitor/mimo_proxy.py

    export FEISHU_WEBHOOK_URL=https://open.feishu.cn/open-apis/bot/v2/hook/...
    tools/monitor/fall_watch.py --interval 10

Single terminal, no proxy:

    tools/monitor/fall_watch.py --backend direct --interval 10

Dry run, nothing is sent to Feishu:

    tools/monitor/fall_watch.py --once --dry-run

Known limits, do not paper over them: the frame is a 160x90 1/8 decimation with
static white balance and no auto exposure, so this detects "a person is lying on
the floor" far better than it detects the moment of falling.  See
docs/bringup/camera_csi.md and docs/bringup/fall_alert.md.
"""

import argparse
import base64
import contextlib
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ai_client                                    # noqa: E402
import board_console                                # noqa: E402
import thumb_image                                  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
OUTDIR = REPO / "out" / "monitor"

CAPTURE_COMMAND = "p4x_selftest --camera-capture"
CAPTURE_DONE = ("PASS one frame", "CSI capture failed")


class CaptureError(RuntimeError):
    """The board did not produce a frame."""


def capture_frame(console, gain, timeout):
    """Run one capture on the board and return the console text."""
    command = CAPTURE_COMMAND
    if gain:
        command += " " + " ".join(gain)

    text = console.run_command(command, timeout, CAPTURE_DONE)
    if "CSI capture failed" in text:
        line = next((l.strip() for l in text.splitlines()
                     if "CSI capture failed" in l), "CSI capture failed")
        raise CaptureError(line)
    if "PASS one frame" not in text:
        raise CaptureError(f"capture timed out after {timeout:.0f}s "
                           f"({len(text.splitlines())} console lines)")
    return text


def prune(directory, pattern, keep):
    if keep <= 0:
        return
    files = sorted(directory.glob(pattern))
    for stale in files[:max(0, len(files) - keep)]:
        stale.unlink(missing_ok=True)


def stamp():
    return time.strftime("%Y%m%d-%H%M%S")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    board = ap.add_argument_group("board")
    board.add_argument("--port", help="serial device (default: autodetect)")
    board.add_argument("--gain", nargs=3, metavar=("FINE", "COARSE", "ANG"),
                       help="SC2336 gain override, e.g. --gain 0x80 0x00 0x10")
    board.add_argument("--capture-timeout", type=float, default=120.0,
                       help="seconds to wait for one frame (default: 120)")

    loop = ap.add_argument_group("loop")
    loop.add_argument("--interval", type=float, default=15.0,
                      help="seconds between captures, measured from the start "
                           "of one capture to the next (default: 15)")
    loop.add_argument("--once", action="store_true",
                      help="capture and analyse a single frame, then exit")
    loop.add_argument("--keep", type=int, default=500,
                      help="how many frame PNGs/logs to retain (0: all)")
    loop.add_argument("--outdir", type=Path, default=OUTDIR,
                      help=f"artefact directory (default: {OUTDIR})")
    loop.add_argument("--from-log", type=Path, metavar="LOG",
                      help="replay a capture log (e.g. out/camera/latest.log) "
                           "instead of talking to the board; implies --once")

    model = ap.add_argument_group("model")
    model.add_argument("--backend", choices=ai_client.MimoClient.BACKENDS,
                       default="proxy",
                       help="proxy: local mimo_proxy.py; direct: call MiMo "
                            "from this process using MIMO_API_KEY; "
                            "mock: no model call, for offline testing")
    model.add_argument("--proxy-url", default="http://127.0.0.1:8000/analyze")
    model.add_argument("--scale", type=int, default=4,
                       help="nearest-neighbour upscale before sending "
                            "(default: 4, i.e. 640x360)")
    model.add_argument("--stretch", action="store_true",
                       help="level each channel to full range; helps in dim "
                            "scenes because the capture has no auto exposure")

    alert = ap.add_argument_group("alert")
    alert.add_argument("--webhook",
                       default=os.environ.get("FEISHU_WEBHOOK_URL", ""),
                       help="Feishu custom-robot webhook "
                            "(default: $FEISHU_WEBHOOK_URL)")
    alert.add_argument("--cooldown", type=float, default=60.0,
                       help="minimum seconds between two alerts (default: 60)")
    alert.add_argument("--min-confidence", type=float, default=0.0,
                       help="ignore positives below this confidence (0..1)")
    alert.add_argument("--confirm", type=int, default=1,
                       help="require N consecutive positive frames before "
                            "alerting (default: 1)")
    alert.add_argument("--notify-failures", type=int, default=5,
                       help="send a Feishu text notice after N consecutive "
                            "capture failures (0: never)")
    alert.add_argument("--notify-start", action="store_true",
                       help="send a Feishu text notice when monitoring starts")
    alert.add_argument("--dry-run", action="store_true",
                       help="never contact Feishu; print the verdict only")

    args = ap.parse_args()

    if args.interval <= 0 or args.scale < 1 or args.confirm < 1:
        print("--interval 必须 > 0，--scale 和 --confirm 必须 >= 1",
              file=sys.stderr)
        return 2

    if args.from_log:
        args.once = True

    try:
        if args.from_log:
            if not args.from_log.is_file():
                raise RuntimeError(f"日志文件不存在：{args.from_log}")
            port = f"<replay {args.from_log}>"
        else:
            port = board_console.resolve_port(args.port)
        client = ai_client.MimoClient(backend=args.backend,
                                      proxy_url=args.proxy_url)
        webhook = "" if args.dry_run else ai_client.validate_webhook(
            args.webhook)
    except (RuntimeError, ai_client.AiError) as error:
        print(f"配置错误：{error}", file=sys.stderr)
        return 2

    frames_dir = args.outdir / "frames"
    logs_dir = args.outdir / "logs"
    frames_dir.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    events_path = args.outdir / "events.jsonl"
    latest_png = args.outdir / "latest.png"

    print(f"port     : {port}")
    print(f"backend  : {args.backend}"
          + (f" ({args.proxy_url})" if args.backend == "proxy" else ""))
    print(f"interval : {args.interval:g}s   cooldown: {args.cooldown:g}s   "
          f"confirm: {args.confirm}")
    print(f"alerts   : {'DRY RUN (not sending)' if args.dry_run else 'Feishu webhook'}")
    print(f"outdir   : {args.outdir}")

    frame_id = 0
    last_alert = 0.0
    positive_streak = 0
    failure_streak = 0
    failure_notified = False

    with contextlib.ExitStack() as stack:
        if args.from_log:
            console = None
        else:
            console = stack.enter_context(board_console.BoardConsole(port))
            console.sync(1.0)

        if args.notify_start and not args.dry_run:
            try:
                ai_client.send_text(
                    webhook,
                    f"✅ AI 跌倒监控已启动：每 {args.interval:g} 秒分析一帧"
                    f"（设备 {port}）。")
            except ai_client.AiError as error:
                print(f"启动通知发送失败：{error}", file=sys.stderr)

        try:
            while True:
                started = time.monotonic()
                frame_id += 1
                now = time.strftime("%H:%M:%S")
                tag = f"{stamp()}-{frame_id:05d}"

                try:
                    if console is None:
                        text = board_console.clean(
                            args.from_log.read_bytes())
                    else:
                        text = capture_frame(console, args.gain,
                                            args.capture_timeout)
                        (logs_dir / f"{tag}.log").write_text(
                            text, encoding="utf-8")
                    thumb = thumb_image.parse(text)
                    png = thumb_image.to_png(thumb, scale=args.scale,
                                             stretch=args.stretch)
                    png_path = frames_dir / f"{tag}.png"
                    png_path.write_bytes(png)
                    latest_png.write_bytes(png)
                    failure_streak = 0
                    failure_notified = False
                except (CaptureError, thumb_image.ThumbError) as error:
                    failure_streak += 1
                    print(f"[{now}] #{frame_id} 采集失败（连续 {failure_streak} "
                          f"次）：{error}", file=sys.stderr)
                    if (args.notify_failures and not failure_notified
                            and failure_streak >= args.notify_failures
                            and not args.dry_run):
                        try:
                            ai_client.send_text(
                                webhook,
                                f"⚠️ AI 跌倒监控异常：连续 {failure_streak} 次"
                                f"采集失败（{error}），监控画面可能已中断。")
                            failure_notified = True
                        except ai_client.AiError as notify_error:
                            print(f"异常通知发送失败：{notify_error}",
                                  file=sys.stderr)
                    if args.once:
                        return 1
                    time.sleep(max(0.0, args.interval -
                                   (time.monotonic() - started)))
                    continue

                event = {
                    "time": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "frame_id": frame_id,
                    "frame": str(png_path),
                    "distinct": thumb.distinct,
                }

                try:
                    raw = client.analyze(
                        base64.b64encode(png).decode("ascii"),
                        frame_id, "png")
                    fall, confidence, reason = ai_client.parse_verdict(raw)
                    event.update(ok=True, fall_detected=fall,
                                 confidence=confidence, reason=reason,
                                 raw=raw)
                except ai_client.AiError as error:
                    event.update(ok=False, error=str(error))
                    print(f"[{now}] #{frame_id} 分析失败：{error}",
                          file=sys.stderr)
                    fall = False
                    confidence = 0.0
                    reason = ""

                if event.get("ok"):
                    verdict = "跌倒" if fall else "正常"
                    print(f"[{now}] #{frame_id} {verdict} "
                          f"conf={confidence:.2f} 色数={thumb.distinct} "
                          f"原因={reason}")

                accepted = fall and confidence >= args.min_confidence
                positive_streak = positive_streak + 1 if accepted else 0
                event["positive_streak"] = positive_streak

                if accepted and positive_streak >= args.confirm:
                    if time.monotonic() - last_alert < args.cooldown:
                        print("  检测到跌倒，处于告警冷却期，不重复发送。")
                        event["alert"] = "cooldown"
                    elif args.dry_run:
                        print("  [dry-run] 本应发送飞书告警。")
                        event["alert"] = "dry-run"
                    else:
                        try:
                            ai_client.send_fall_alert(
                                webhook, reason, confidence, str(png_path),
                                frame_id, thumb.distinct)
                            last_alert = time.monotonic()
                            event["alert"] = "sent"
                            print("  已发送飞书跌倒告警。")
                        except ai_client.AiError as error:
                            event["alert"] = f"failed: {error}"
                            print(f"  飞书告警发送失败：{error}",
                                  file=sys.stderr)
                elif accepted:
                    print(f"  疑似跌倒，等待连续确认 "
                          f"({positive_streak}/{args.confirm})。")
                    event["alert"] = "pending-confirm"

                with events_path.open("a", encoding="utf-8") as fp:
                    fp.write(json.dumps(event, ensure_ascii=False) + "\n")

                prune(frames_dir, "*.png", args.keep)
                prune(logs_dir, "*.log", args.keep)

                if args.once:
                    return 0

                elapsed = time.monotonic() - started
                if elapsed > args.interval:
                    # Capture alone takes ~4 s of base64 at 115200 plus sensor
                    # init; say so instead of silently drifting.
                    print(f"  注意：本轮耗时 {elapsed:.1f}s，已超过 "
                          f"--interval {args.interval:g}s，立即开始下一轮。")
                else:
                    time.sleep(args.interval - elapsed)
        except KeyboardInterrupt:
            print("\n监控已停止。")
            return 0


if __name__ == "__main__":
    sys.exit(main())
