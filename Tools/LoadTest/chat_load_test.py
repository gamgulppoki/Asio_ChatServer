"""
채팅 브로드캐스트 부하 테스트.

시나리오
  1. N 명 가입·로그인 (bcrypt 때문에 가입은 병렬로)
  2. 방 하나 생성, N 명 전원 입장
  3. 각 클라가 초당 R 건씩 M 건 채팅 전송. 메시지 본문에 송신 시각(ns) 을 넣는다
  4. 모든 클라가 받는 S_CHAT 마다 (수신 시각 − 본문의 송신 시각) = 브로드캐스트 지연
     같은 머신이라 시계가 같다. 송신자 자신도 에코를 받으므로 기대 수신 수 = N × (N × M)

측정
  - 송신 TPS (클라 기준), 서버가 뿌린 전달 건수/s (deliveries/s)
  - 지연 p50 / p95 / p99 / max
  - 유실 (기대 수신 수 − 실제 수신 수, grace 시간 뒤)

사용법
  python Tools/LoadTest/chat_load_test.py [--users 50] [--msgs 20] [--rate 10] [--host ...] [--port ...]
"""
import argparse
import collections
import statistics
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chat_protocol import ChatClient, text, wait_for_server  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--users", type=int, default=50)
    ap.add_argument("--msgs", type=int, default=20, help="유저당 전송 메시지 수")
    ap.add_argument("--rate", type=float, default=10.0, help="유저당 초당 전송 건수")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--grace", type=float, default=3.0, help="전송 종료 후 수신 대기 초")
    args = ap.parse_args()

    if not wait_for_server(args.host, args.port):
        print("[FAIL] server not reachable")
        return 2

    run_id = int(time.time()) % 1_000_000

    # ---- 1. 가입 + 로그인 (병렬: bcrypt 해시가 요청당 수백 ms) ----
    def make_client(i: int) -> ChatClient:
        nick = f"cl{run_id}u{i}"
        c = ChatClient(args.host, args.port, timeout=30.0)
        ok, msg = c.register(nick, f"{nick}@loadtest.local", "password123")
        if not ok:
            raise RuntimeError(f"register {nick}: {msg}")
        ok, msg = c.login(f"{nick}@loadtest.local", "password123")
        if not ok:
            raise RuntimeError(f"login {nick}: {msg}")
        return c

    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=16) as pool:
        clients = list(pool.map(make_client, range(args.users)))
    setup_s = time.perf_counter() - t0
    print(f"[OK] {args.users} users registered+logged in in {setup_s:.1f}s "
          f"({setup_s / args.users * 1000:.0f} ms/user incl. bcrypt hash+verify, 16 parallel)")

    # ---- 2. 방 생성 + 입장 ----
    room_id = clients[0].create_room(f"load{run_id}")
    if room_id is None:
        print("[FAIL] create room")
        return 2
    for c in clients:
        if not c.enter_room(room_id):
            print(f"[FAIL] enter room {c.nickname}")
            return 2
    print(f"[OK] room {room_id}: {args.users} users entered")

    # ---- 3. 수신 집계 ----
    latencies_ns = []
    received = 0
    got_keys = set()            # (recipient, sender, seq) — 유실 패턴 분석용
    lock = threading.Lock()

    def make_on_frame(recipient: int):
        def on_frame(name, fields):
            nonlocal received
            if name != "S_CHAT":
                return
            body = text(fields, 1)
            try:
                sender_idx, seq, sent_ns = body.split("|")
                sent_ns = int(sent_ns)
            except ValueError:
                return
            lat = time.perf_counter_ns() - sent_ns
            with lock:
                received += 1
                latencies_ns.append(lat)
                got_keys.add((recipient, int(sender_idx), int(seq)))
        return on_frame

    for i, c in enumerate(clients):
        c.start_streaming(make_on_frame(i))

    # ---- 4. 송신 ----
    interval = 1.0 / args.rate if args.rate > 0 else 0.0

    def sender(c: ChatClient, idx: int):
        for seq in range(args.msgs):
            body = f"{idx}|{seq}|{time.perf_counter_ns()}"
            c.send_async("C_CHAT", {1: body})
            if interval:
                time.sleep(interval)

    threads = [threading.Thread(target=sender, args=(c, i)) for i, c in enumerate(clients)]
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    send_s = time.perf_counter() - t0

    # ---- 5. 수신 대기 ----
    expected = args.users * args.users * args.msgs
    deadline = time.time() + args.grace
    while time.time() < deadline:
        with lock:
            if received >= expected:
                break
        time.sleep(0.05)
    recv_s = time.perf_counter() - t0

    with lock:
        lats = sorted(latencies_ns)
        got = received

    for c in clients:
        c.close()

    sent = args.users * args.msgs
    if not lats:
        print("[FAIL] nothing received")
        return 1

    def pct(p):
        return lats[min(len(lats) - 1, int(len(lats) * p))] / 1e6

    print()
    print(f"sent            : {sent} msgs in {send_s:.2f}s  →  {sent / send_s:.0f} msgs/s (clients, {args.rate}/s each)")
    print(f"deliveries      : {got} / {expected} expected  ({got / expected * 100:.1f}%)  in {recv_s:.2f}s  →  {got / recv_s:.0f} deliveries/s")
    print(f"latency ms      : p50 {pct(0.50):.2f}  p95 {pct(0.95):.2f}  p99 {pct(0.99):.2f}  max {lats[-1] / 1e6:.2f}  (send → other client receive, same host)")
    print(f"lost            : {expected - got}")
    if got != expected:
        missing = [(r, s_, q) for r in range(args.users) for s_ in range(args.users) for q in range(args.msgs)
                   if (r, s_, q) not in got_keys]
        by_recipient = collections.Counter(r for r, _, _ in missing)
        by_sender = collections.Counter(s_ for _, s_, _ in missing)
        by_seq = collections.Counter(q for _, _, q in missing)
        print(f"  missing (recipient, sender, seq) sample: {missing[:10]}")
        print(f"  by recipient: {dict(by_recipient)}")
        print(f"  by sender   : {dict(by_sender)}")
        print(f"  by seq      : {dict(by_seq)}")
    return 0 if got == expected else 1


if __name__ == "__main__":
    sys.exit(main())
