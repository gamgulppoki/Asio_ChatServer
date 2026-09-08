"""
Claude Messages API 모의 서버 (스트리밍).

API 키 없이 서버의 AI 경로(HTTP 클라이언트 → SSE 파서 → 조각 push → 이력 저장 → 한도)를 끝까지 검증하기 위한 것.
실제 API 와 같은 형태의 SSE 이벤트를 chunked 인코딩으로 흘린다.

서버 쪽 환경 변수:
  ANTHROPIC_BASE_URL=http://127.0.0.1:8765   ANTHROPIC_API_KEY=test-key

마지막 user 메시지 내용으로 시나리오를 고른다:
  "@429"     → 첫 요청은 429 + Retry-After: 1, 재시도는 성공
  "@500"     → 첫 요청은 500, 재시도는 성공
  "@refuse"  → 텍스트 없이 stop_reason=refusal
  "@error"   → 텍스트 일부 뒤 SSE error 이벤트
  "@slow"    → 조각 사이 300 ms (스트리밍이 눈에 보이게)
  그 외      → "echo: <메시지> (history N msgs)" 를 조각으로 스트리밍. N = 요청의 messages 길이 (이력 검증용)

사용법: python Tools/LoadTest/mock_claude_server.py [--port 8765]
"""
import argparse
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_lock = threading.Lock()
_fail_once = {}   # 시나리오별 "한 번은 실패" 상태


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # 조용히
        pass

    # ---- 응답 헬퍼 ----
    def _json(self, status, obj, extra_headers=None):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for k, v in (extra_headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _chunk(self, data: bytes):
        self.wfile.write(f"{len(data):x}\r\n".encode() + data + b"\r\n")
        self.wfile.flush()

    def _event(self, name, obj):
        self._chunk(f"event: {name}\ndata: {json.dumps(obj, ensure_ascii=False)}\n\n".encode("utf-8"))

    # ---- POST /v1/messages ----
    def do_POST(self):
        if self.path != "/v1/messages":
            return self._json(404, {"type": "error", "error": {"type": "not_found_error", "message": "not found"}})
        if not self.headers.get("x-api-key"):
            return self._json(401, {"type": "error", "error": {"type": "authentication_error", "message": "missing x-api-key"}})
        if self.headers.get("anthropic-version") != "2023-06-01":
            return self._json(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "bad anthropic-version"}})

        length = int(self.headers.get("Content-Length", "0"))
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception:
            return self._json(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "bad json"}})

        messages = req.get("messages", [])
        if not messages or messages[0].get("role") != "user":
            return self._json(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "messages must start with user"}})
        for a, b in zip(messages, messages[1:]):
            if a["role"] == b["role"]:
                return self._json(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "roles must alternate"}})
        if not req.get("stream"):
            return self._json(400, {"type": "error", "error": {"type": "invalid_request_error", "message": "stream required by mock"}})

        # 서버는 연속된 user 메시지(실패한 호출 뒤)를 빈 줄로 합쳐 보낸다. 시나리오는 마지막 발화로 고른다.
        last = messages[-1]["content"].split(chr(10) * 2)[-1]
        tag = None
        for t in ("@429", "@500", "@refuse", "@error", "@slow"):
            if t in last:
                tag = t
                break

        # 한 번 실패 시나리오
        if tag in ("@429", "@500"):
            with _lock:
                first = not _fail_once.get(last, False)
                _fail_once[last] = True
            if first:
                if tag == "@429":
                    return self._json(429, {"type": "error", "error": {"type": "rate_limit_error", "message": "rate limited (mock)"}},
                                      {"Retry-After": "1"})
                return self._json(500, {"type": "error", "error": {"type": "api_error", "message": "internal (mock)"}})

        # ---- 스트리밍 응답 ----
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Transfer-Encoding", "chunked")
        self.send_header("Connection", "close")
        self.end_headers()

        input_tokens = sum(len(m["content"]) for m in messages) // 4 + 10
        self._event("message_start", {"type": "message_start", "message": {
            "id": "msg_mock", "type": "message", "role": "assistant", "model": req.get("model", "mock"),
            "content": [], "stop_reason": None, "usage": {"input_tokens": input_tokens, "output_tokens": 1}}})

        if tag == "@refuse":
            self._event("message_delta", {"type": "message_delta", "delta": {"stop_reason": "refusal", "stop_sequence": None},
                                          "usage": {"output_tokens": 0}})
            self._event("message_stop", {"type": "message_stop"})
            self._chunk(b"")
            return

        text = f"echo: {last} (history {len(messages)} msgs)"
        self._event("content_block_start", {"type": "content_block_start", "index": 0, "content_block": {"type": "text", "text": ""}})

        delay = 0.3 if tag == "@slow" else 0.02
        pieces = [text[i:i + 4] for i in range(0, len(text), 4)]
        for i, piece in enumerate(pieces):
            if tag == "@error" and i == 3:
                self._event("error", {"type": "error", "error": {"type": "overloaded_error", "message": "overloaded (mock)"}})
                self._chunk(b"")
                return
            self._event("content_block_delta", {"type": "content_block_delta", "index": 0,
                                                "delta": {"type": "text_delta", "text": piece}})
            time.sleep(delay)
        self._event("ping", {"type": "ping"})
        self._event("content_block_stop", {"type": "content_block_stop", "index": 0})
        self._event("message_delta", {"type": "message_delta", "delta": {"stop_reason": "end_turn", "stop_sequence": None},
                                      "usage": {"output_tokens": len(text) // 4 + 1}})
        self._event("message_stop", {"type": "message_stop"})
        self._chunk(b"")


def serve(port: int):
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"mock claude server on http://127.0.0.1:{port}  (Ctrl+C to stop)")
    httpd.serve_forever()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()
    try:
        serve(args.port)
    except KeyboardInterrupt:
        sys.exit(0)
