#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Vision-model and Feishu clients used by the fall monitor.

Two backends are supported for the model call:

  proxy  - POST to a local mimo_proxy.py /analyze endpoint (default).  Keeps the
           API key in one process and matches the existing PC-side layout.
  direct - call MiMo's OpenAI-compatible endpoint from this process, so a demo
           only needs one terminal.

No credential is stored in this file.  MIMO_API_KEY and FEISHU_WEBHOOK_URL come
from the environment or the command line.
"""

import json
import os
import re
import time
import urllib.error
import urllib.request

MIMO_API_URL = os.environ.get(
    "MIMO_API_URL", "https://api.xiaomimimo.com/v1/chat/completions")
MIMO_MODEL = os.environ.get("MIMO_MODEL", "mimo-v2.6-pro")
MIMO_MAX_COMPLETION_TOKENS = int(
    os.environ.get("MIMO_MAX_COMPLETION_TOKENS", "512"))

FEISHU_WEBHOOK_PREFIX = "https://open.feishu.cn/open-apis/bot/v2/hook/"

# The board sends a 160x90 decimated frame with static white balance and no auto
# exposure (docs/bringup/camera_csi.md).  Telling the model that up front is what
# keeps it from reading compression mush as a person on the floor.
FALL_SYSTEM_PROMPT = (
    "你是视频监控跌倒检测助手。输入是一张分辨率很低（约 160x90 放大后）的室内监控画面，"
    "可能偏暗、偏色、有噪点。只根据画面判断是否有人处于跌倒、倒地、失去平衡即将摔倒的状态。"
    "不要把坐下、弯腰、蹲下、躺在床上或正常走动误判为跌倒。画面模糊无法辨认人体时，"
    "fall_detected 必须为 false。只输出一行 JSON，不得包含 Markdown 或其他文字："
    "{\"fall_detected\":true或false,\"confidence\":0到1的小数,"
    "\"reason\":\"不超过30字的中文原因\"}"
)

FALL_PROMPT = (
    "请检测这张监控图片中是否有人摔倒或倒地，并只输出约定的 JSON。"
)


class AiError(RuntimeError):
    """The model call failed or returned something unusable."""


def _http_json(url, payload, headers, timeout=60):
    request = urllib.request.Request(
        url,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise AiError(f"HTTP {error.code}: {detail[:500]}") from error
    except urllib.error.URLError as error:
        raise AiError(f"连接失败：{error.reason}") from error

    try:
        return json.loads(body)
    except json.JSONDecodeError as error:
        raise AiError(f"返回非 JSON 内容：{body[:500]}") from error


def _extract_text(payload):
    choices = payload.get("choices")
    if not isinstance(choices, list) or not choices:
        raise AiError("MiMo 响应缺少 choices")

    message = choices[0].get("message", {})
    content = message.get("content")
    if isinstance(content, str) and content.strip():
        return content.strip()
    if isinstance(content, list):
        parts = [b["text"] for b in content
                 if isinstance(b, dict) and isinstance(b.get("text"), str)]
        if "".join(parts).strip():
            return "".join(parts).strip()

    reason = choices[0].get("finish_reason", "unknown")
    raise AiError(f"MiMo 返回空内容（finish_reason={reason}），"
                  "可尝试提高 MIMO_MAX_COMPLETION_TOKENS")


class MimoClient:
    """Calls the vision model either through the local proxy or directly."""

    BACKENDS = ("proxy", "direct", "mock")

    def __init__(self, backend="proxy",
                 proxy_url="http://127.0.0.1:8000/analyze",
                 api_key=None, timeout=60):
        if backend not in self.BACKENDS:
            raise ValueError(f"unknown backend: {backend}")
        self.backend = backend
        self.proxy_url = proxy_url
        self.api_key = api_key or os.environ.get("MIMO_API_KEY", "")
        self.timeout = timeout
        if backend == "direct" and not self.api_key:
            raise AiError("--backend direct 需要环境变量 MIMO_API_KEY")

    def analyze(self, image_base64, frame_id, image_format="png"):
        """Return the model's raw text answer."""
        if self.backend == "mock":
            # No model call: exercises the capture -> PNG -> alert path offline.
            # Set MONITOR_MOCK_FALL=1 to make the mock report a fall so the
            # Feishu side can be tested end to end.
            fall = os.environ.get("MONITOR_MOCK_FALL", "") == "1"
            return json.dumps({
                "fall_detected": fall,
                "confidence": 0.9 if fall else 0.05,
                "reason": f"mock 后端固定结论（帧 {frame_id}，"
                          f"{len(image_base64)} 字符 base64）",
            }, ensure_ascii=False)

        if self.backend == "proxy":
            result = _http_json(
                self.proxy_url,
                {
                    "image_base64": image_base64,
                    "image_format": image_format,
                    "frame_id": frame_id,
                    "task": "fall_detection",
                    "prompt": FALL_PROMPT,
                },
                {"Content-Type": "application/json; charset=utf-8"},
                self.timeout,
            )
            if not result.get("ok"):
                raise AiError(f"代理分析失败：{result.get('error', '未知错误')}")
            text = result.get("text")
            if not isinstance(text, str):
                raise AiError(f"代理返回缺少 text：{result}")
            return text

        mime = "image/png" if image_format == "png" else "image/jpeg"
        payload = _http_json(
            MIMO_API_URL,
            {
                "model": MIMO_MODEL,
                "messages": [
                    {"role": "system", "content": FALL_SYSTEM_PROMPT},
                    {
                        "role": "user",
                        "content": [
                            {"type": "image_url",
                             "image_url": {
                                 "url": f"data:{mime};base64,{image_base64}"}},
                            {"type": "text", "text": FALL_PROMPT},
                        ],
                    },
                ],
                "max_completion_tokens": MIMO_MAX_COMPLETION_TOKENS,
                "temperature": 0.2,
                "stream": False,
            },
            {"Content-Type": "application/json", "api-key": self.api_key},
            self.timeout,
        )
        return _extract_text(payload)


def parse_verdict(text):
    """Pull ``(fall_detected, confidence, reason)`` out of the model answer.

    Models wrap JSON in Markdown fences often enough that a strict json.loads on
    the whole answer is not worth the false failures.
    """
    matched = re.search(r"\{.*\}", text, flags=re.DOTALL)
    if not matched:
        raise AiError(f"模型未按约定返回 JSON：{text[:300]}")
    try:
        verdict = json.loads(matched.group(0))
    except json.JSONDecodeError as error:
        raise AiError(f"模型返回的 JSON 无法解析：{text[:300]}") from error

    fall = verdict.get("fall_detected") is True
    try:
        confidence = float(verdict.get("confidence", 0.0))
    except (TypeError, ValueError):
        confidence = 0.0
    reason = str(verdict.get("reason", "")).strip() or "未提供原因"
    return fall, max(0.0, min(1.0, confidence)), reason


def validate_webhook(url):
    if not url:
        raise AiError("未配置飞书 Webhook：设置 FEISHU_WEBHOOK_URL 或传 --webhook")
    if not url.startswith(FEISHU_WEBHOOK_PREFIX):
        raise AiError("飞书 Webhook 地址格式不正确，应以 "
                      f"{FEISHU_WEBHOOK_PREFIX} 开头")
    return url


def _post_feishu(webhook, payload):
    result = _http_json(
        webhook, payload,
        {"Content-Type": "application/json; charset=utf-8"}, 15)
    # Webhook responses use StatusCode; some also carry code.
    status = result.get("StatusCode", result.get("code"))
    if status != 0:
        message = result.get("StatusMessage", result.get("msg", "未知错误"))
        raise AiError(f"飞书拒绝消息：code={status}，msg={message}")


def send_text(webhook, text):
    _post_feishu(webhook, {"msg_type": "text", "content": {"text": text}})


def send_fall_alert(webhook, reason, confidence, frame_path, frame_id,
                    distinct=None):
    """Send the fall alert card.

    The frame itself is not attached: custom-robot webhooks cannot upload
    images (that needs an app credential and im/v1/images), so the card carries
    the local path of the PNG that triggered it instead.
    """
    detected_at = time.strftime("%Y-%m-%d %H:%M:%S")
    fields = [
        {"is_short": True,
         "text": {"tag": "lark_md", "content": "**检测状态**\n🔴 疑似跌倒"}},
        {"is_short": True,
         "text": {"tag": "lark_md", "content": f"**检测时间**\n{detected_at}"}},
        {"is_short": True,
         "text": {"tag": "lark_md",
                  "content": f"**模型置信度**\n{confidence:.2f}"}},
        {"is_short": True,
         "text": {"tag": "lark_md", "content": f"**帧序号**\n{frame_id}"}},
        {"is_short": False,
         "text": {"tag": "lark_md", "content": f"**AI 判断原因**\n{reason}"}},
        {"is_short": False,
         "text": {"tag": "lark_md", "content": f"**画面文件**\n{frame_path}"}},
    ]
    if distinct is not None:
        fields.append({
            "is_short": False,
            "text": {"tag": "lark_md",
                     "content": f"**画质自检**\n缩略图色数 {distinct}"},
        })

    _post_feishu(webhook, {
        "msg_type": "interactive",
        "card": {
            "config": {"wide_screen_mode": True},
            "header": {
                "title": {"tag": "plain_text", "content": "⚠️ AI 跌倒告警"},
                "template": "red",
            },
            "elements": [
                {"tag": "div",
                 "text": {"tag": "lark_md",
                          "content": "摄像头画面中发现疑似人员摔倒/倒地，"
                                     "请尽快确认现场安全情况。"}},
                {"tag": "div", "fields": fields},
                {"tag": "hr"},
                {"tag": "note",
                 "elements": [{"tag": "plain_text",
                               "content": "来源：ESP32-P4X SC2336 监控 + MiMo "
                                          "视觉模型。画面为 160x90 缩略图，"
                                          "请以现场确认为准。"}]},
            ],
        },
    })
