"""
ChatServer 바이너리 프로토콜을 말하는 최소 Python 클라이언트.

의존성 없음: protobuf 런타임 대신 proto3 wire format 을 직접 인코딩/디코딩한다.
(이 프로젝트가 쓰는 필드는 string / int64 / bool / int32 뿐이라 varint + length-delimited 만 있으면 된다.)

패킷 프레임: [uint16 size][uint16 id][protobuf payload]   (little-endian, size 는 헤더 포함)
패킷 ID    : Server/Network/ClientPacketHandler.h 의 enum 을 파싱해서 가져온다 (proto 순서가 바뀌어도 안전).

용도: 통합 테스트 / 부하 테스트 (Tools/LoadTest/*.py).
"""
import re
import socket
import struct
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HANDLER_HEADER = REPO_ROOT / "Server" / "Network" / "ClientPacketHandler.h"


# ---------------------------------------------------------------------------
# 패킷 ID
# ---------------------------------------------------------------------------
def load_packet_ids(header_path: Path = HANDLER_HEADER) -> dict:
    """PKT_C_LOGIN = 1002, 형태의 enum 항목을 {이름: id} 로 읽는다."""
    text = header_path.read_text(encoding="utf-8", errors="ignore")
    ids = {}
    for m in re.finditer(r"PKT_(\w+)\s*=\s*(\d+)", text):
        ids[m.group(1)] = int(m.group(2))
    if not ids:
        raise RuntimeError(f"packet id enum not found in {header_path}")
    return ids


PKT = load_packet_ids()
PKT_NAME = {v: k for k, v in PKT.items()}


# ---------------------------------------------------------------------------
# proto3 wire format (필요한 부분만)
# ---------------------------------------------------------------------------
def _varint(value: int) -> bytes:
    if value < 0:
        value += 1 << 64            # int64 음수는 10바이트 varint
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        if value:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _read_varint(buf: bytes, pos: int):
    result, shift = 0, 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def encode(fields: dict) -> bytes:
    """
    {field_no: value} → protobuf bytes.
    value 가 str/bytes 면 length-delimited, int/bool 이면 varint.
    proto3 기본값(0, "", False) 은 생략한다 (직렬화 규칙과 동일).
    """
    out = bytearray()
    for field_no, value in fields.items():
        if isinstance(value, bool):
            if not value:
                continue
            out += _varint((field_no << 3) | 0) + _varint(1)
        elif isinstance(value, int):
            if value == 0:
                continue
            out += _varint((field_no << 3) | 0) + _varint(value)
        elif isinstance(value, (str, bytes)):
            data = value.encode("utf-8") if isinstance(value, str) else value
            if not data:
                continue
            out += _varint((field_no << 3) | 2) + _varint(len(data)) + data
        else:
            raise TypeError(f"unsupported field type: {type(value)}")
    return bytes(out)


def decode(buf: bytes) -> dict:
    """
    protobuf bytes → {field_no: value}. 같은 필드가 반복되면 list 로 모은다 (repeated).
    varint 는 int 로, length-delimited 는 bytes 로 돌려준다 (문자열은 호출자가 .decode()).
    """
    fields = {}
    pos = 0
    while pos < len(buf):
        key, pos = _read_varint(buf, pos)
        field_no, wire_type = key >> 3, key & 0x7
        if wire_type == 0:
            value, pos = _read_varint(buf, pos)
            if value >= 1 << 63:      # int64 음수 복원
                value -= 1 << 64
        elif wire_type == 1:
            value = struct.unpack_from("<q", buf, pos)[0]
            pos += 8
        elif wire_type == 2:
            length, pos = _read_varint(buf, pos)
            value = buf[pos:pos + length]
            pos += length
        elif wire_type == 5:
            value = struct.unpack_from("<i", buf, pos)[0]
            pos += 4
        else:
            raise ValueError(f"unsupported wire type {wire_type}")

        if field_no in fields:
            existing = fields[field_no]
            if isinstance(existing, list):
                existing.append(value)
            else:
                fields[field_no] = [existing, value]
        else:
            fields[field_no] = value
    return fields


def text(fields: dict, field_no: int, default: str = "") -> str:
    v = fields.get(field_no)
    return v.decode("utf-8", errors="replace") if isinstance(v, (bytes, bytearray)) else default


# ---------------------------------------------------------------------------
# 클라이언트
# ---------------------------------------------------------------------------
class ChatClient:
    """
    동기식 요청/응답 클라이언트. 서버가 중간에 push 하는 패킷(S_TRANSFER_RECEIVED, S_CHAT 등)은
    pushed 리스트에 쌓아 두고, 기다리던 응답 ID 가 오면 그것을 돌려준다.
    """

    HEADER = struct.Struct("<HH")

    def __init__(self, host="127.0.0.1", port=9000, timeout=10.0):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = bytearray()
        self.pushed = []          # (pkt_name, fields) - 기다리지 않은 패킷
        self.lock = threading.Lock()
        self.nickname = None

    # ---- 프레이밍 ----
    def send(self, pkt_name: str, fields: dict | None = None):
        payload = encode(fields or {})
        size = self.HEADER.size + len(payload)
        self.sock.sendall(self.HEADER.pack(size, PKT[pkt_name]) + payload)

    def _recv_frame(self):
        while True:
            if len(self.buf) >= self.HEADER.size:
                size, pkt_id = self.HEADER.unpack_from(self.buf, 0)
                if len(self.buf) >= size:
                    payload = bytes(self.buf[self.HEADER.size:size])
                    del self.buf[:size]
                    return PKT_NAME.get(pkt_id, f"UNKNOWN_{pkt_id}"), decode(payload)
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed connection")
            self.buf += chunk

    def wait_for(self, pkt_name: str):
        """pkt_name 응답이 올 때까지 읽는다. 그 사이의 다른 패킷은 pushed 에 보관."""
        while True:
            name, fields = self._recv_frame()
            if name == pkt_name:
                return fields
            self.pushed.append((name, fields))

    def request(self, send_name: str, fields: dict | None, recv_name: str):
        with self.lock:
            self.send(send_name, fields)
            return self.wait_for(recv_name)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    # ---- 도메인 API ----
    def register(self, name: str, email: str, password: str) -> tuple[bool, str]:
        r = self.request("C_REGISTER", {1: name, 2: email, 3: password}, "S_REGISTER")
        return bool(r.get(1, 0)), text(r, 2)

    def login(self, email: str, password: str) -> tuple[bool, str]:
        r = self.request("C_LOGIN", {1: email, 2: password}, "S_LOGIN")
        ok = bool(r.get(1, 0))
        if ok:
            self.nickname = text(r, 3)
        return ok, text(r, 2)

    def get_balance(self) -> int | None:
        r = self.request("C_GET_BALANCE", {}, "S_GET_BALANCE")
        return int(r.get(2, 0)) if r.get(1, 0) else None

    def transfer(self, target_nickname: str, amount: int) -> dict:
        r = self.request("C_TRANSFER", {1: target_nickname, 2: amount}, "S_TRANSFER")
        return {
            "success":    bool(r.get(1, 0)),
            "msg":        text(r, 2),
            "my_balance": int(r.get(3, 0)),
            "retries":    int(r.get(4, 0)),
        }


def wait_for_server(host="127.0.0.1", port=9000, timeout=15.0) -> bool:
    """서버 포트가 열릴 때까지 대기. 테스트 스크립트가 서버 기동 직후 붙을 때 사용."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1.0):
                return True
        except OSError:
            time.sleep(0.3)
    return False
