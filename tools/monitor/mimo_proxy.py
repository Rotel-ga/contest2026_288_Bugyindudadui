#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Local MiMo vision proxy for the fall monitor.

POST /analyze with {"image_base64", "image_format", "frame_id", "task",
"prompt"} and the request is forwarded to MiMo's OpenAI-compatible Chat
Completions API.  Running the model call in its own process keeps the API key
out of the monitor and lets several tools share one credential.

    export MIMO_API_KEY=...
    tools/monitor/mimo_proxy.py

The key is read from the environment and is deliberately not stored in this
file.  Environment overrides: MIMO_API_URL, MIMO_MODEL,
MIMO_MAX_COMPLETION_TOKENS, MIMO_PROXY_HOST, MIMO_PROXY_PORT.

fall_watch.py --backend direct skips this proxy entirely; the proxy exists for
the two-process layout and for reuse by other PC-side tools.
"""

import json
import os
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ai_client import FALL_SYSTEM_PROMPT             # noqa: E402

MIMO_API_URL = os.environ.get(
    "MIMO_API_URL", "https://api.xiaomimimo.com/v1/chat/completions")
MIMO_MODEL = os.environ.get("MIMO_MODEL", "mimo-v2.6-pro")
MIMO_API_KEY = os.environ.get("MIMO_API_KEY", "")
MIMO_MAX_COMPLETION_TOKENS = int(
    os.environ.get("MIMO_MAX_COMPLETION_TOKENS", "512"))
LISTEN_HOST = os.environ.get("MIMO_PROXY_HOST", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("MIMO_PROXY_PORT", "8000"))

SCENE_SYSTEM_PROMPT = (
    "你是环境分析助手。根据图片用一句简短中文说明画面中需要注意的内容，"
    "重点关注人员状态、障碍物和异常情况。无法确认时请回答：无法确认。"
)


def extract_text(payload):
    choices = payload.get("choices")
    if not isinstance(choices, list) or not choices:
        raise ValueError("MiMo response has no choices")

    message = choices[0].get("message", {})
    content = message.get("content")
    if isinstance(content, str) and content.strip():
        return content.strip()
    if isinstance(content, list):
        parts = [b["text"] for b in content
                 if isinstance(b, dict) and isinstance(b.get("text"), str)]
        if "".join(parts).strip():
            return "".join(parts).strip()

    finish_reason = choices[0].get("finish_reason", "unknown")
    raise ValueError(
        f"MiMo returned empty text content (finish_reason={finish_reason}); "
        "try increasing MIMO_MAX_COMPLETION_TOKENS")


def call_mimo(image_base64, prompt, image_format, system_prompt):
    if not MIMO_API_KEY:
        raise RuntimeError("MIMO_API_KEY is not set in the proxy environment")

    mime_type = "image/png" if image_format == "png" else "image/jpeg"
    request = urllib.request.Request(
        MIMO_API_URL,
        data=json.dumps({
            "model": MIMO_MODEL,
            "messages": [
                {"role": "system", "content": system_prompt},
                {
                    "role": "user",
                    "content": [
                        {"type": "image_url",
                         "image_url": {
                             "url": f"data:{mime_type};base64,{image_base64}"}},
                        {"type": "text", "text": prompt},
                    ],
                },
            ],
            "max_completion_tokens": MIMO_MAX_COMPLETION_TOKENS,
            "temperature": 0.2,
            "stream": False,
        }, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json",
                 "api-key": MIMO_API_KEY},
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"MiMo HTTP {exc.code}: {detail[:500]}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"MiMo connection failed: {exc.reason}") from exc

    return extract_text(payload)


class MimoProxyHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/analyze":
            self.send_error(404, "use POST /analyze")
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            image_base64 = request["image_base64"]

            if request.get("task") == "fall_detection":
                system_prompt = FALL_SYSTEM_PROMPT
                default_prompt = "请检测图片中是否有人摔倒，并只输出约定 JSON。"
            else:
                system_prompt = SCENE_SYSTEM_PROMPT
                default_prompt = "请分析图片中需要注意的环境信息。"

            image_format = request.get("image_format", "png").lower()
            if image_format not in ("png", "jpeg", "jpg"):
                raise ValueError(f"unsupported image format: {image_format}")

            text = call_mimo(image_base64,
                             request.get("prompt", default_prompt),
                             image_format, system_prompt)
            payload = {"ok": True, "frame_id": request.get("frame_id"),
                       "text": text}
            status = 200
        except Exception as exc:              # keep the HTTP contract alive
            payload = {"ok": False, "error": str(exc)}
            status = 500

        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        print("MIMO_PROXY:", format % args)


def main():
    if not MIMO_API_KEY:
        print("MIMO_API_KEY is not set; export it before starting the proxy",
              file=sys.stderr)
        return 2

    server = HTTPServer((LISTEN_HOST, LISTEN_PORT), MimoProxyHandler)
    print(f"MiMo model   : {MIMO_MODEL}")
    print(f"MiMo endpoint: {MIMO_API_URL}")
    print(f"listening    : http://{LISTEN_HOST}:{LISTEN_PORT}/analyze")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nMiMo proxy stopped")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
