"""
포인트 이체 동시성 테스트.

시나리오
  1. 유저 N 명 회원가입 + 로그인 (각 초기 잔고 10,000 P)
  2. 모든 유저가 동시에 서로에게 이체 (허브 계좌 집중 + 양방향 교차)
     - 허브 집중: 1..N-1 → 0 번 계좌. 같은 행을 여러 트랜잭션이 동시에 갱신 → OCC 충돌 유도
     - 양방향 교차: 0 → 1..N-1 도 동시에. A→B / B→A 가 겹쳐 데드락 여부 확인
  3. 검증
     - 보존 법칙: 전체 잔고 합 == N * 10,000 (실패한 이체는 양쪽 다 반영되지 않아야 함)
     - 개별 정합: 각 유저 잔고 == 초기 - 보낸 성공 금액 + 받은 성공 금액
  4. 리포트: 성공/실패 건수, OCC 재시도 총합·분포, 소요 시간, 초당 처리 건수

사용법
  python Tools/LoadTest/transfer_test.py [--users 10] [--rounds 5] [--amount 100] [--host 127.0.0.1] [--port 9000]
서버가 떠 있어야 한다. 종료 코드 0 = 검증 통과.
"""
import argparse
import collections
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chat_protocol import ChatClient, wait_for_server  # noqa: E402

INITIAL_BALANCE = 10_000   # Handle_C_REGISTER 의 kInitialBalance 와 맞춘다


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--users", type=int, default=10)
    ap.add_argument("--rounds", type=int, default=5, help="유저당 이체 시도 횟수")
    ap.add_argument("--amount", type=int, default=100)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    args = ap.parse_args()

    if not wait_for_server(args.host, args.port):
        print(f"[FAIL] server not reachable at {args.host}:{args.port}")
        return 2

    run_id = int(time.time()) % 1_000_000
    users = []
    for i in range(args.users):
        nick = f"lt{run_id}u{i}"
        email = f"{nick}@loadtest.local"
        c = ChatClient(args.host, args.port)
        ok, msg = c.register(nick, email, "password123")
        if not ok:
            print(f"[FAIL] register {nick}: {msg}")
            return 2
        ok, msg = c.login(email, "password123")
        if not ok:
            print(f"[FAIL] login {nick}: {msg}")
            return 2
        users.append(c)
    print(f"[OK] {args.users} users registered & logged in (run {run_id})")

    for c in users:
        b = c.get_balance()
        if b != INITIAL_BALANCE:
            print(f"[FAIL] initial balance {c.nickname} = {b}")
            return 2

    # ---- 동시 이체 ----
    results = []                      # (from, to, result dict)
    results_lock = threading.Lock()
    hub = users[0].nickname

    def sender(client: ChatClient, target: str):
        for _ in range(args.rounds):
            r = client.transfer(target, args.amount)
            with results_lock:
                results.append((client.nickname, target, r))

    threads = []
    for c in users[1:]:
        threads.append(threading.Thread(target=sender, args=(c, hub)))               # 허브 집중
    for c in users[1:]:
        threads.append(threading.Thread(target=sender, args=(users[0], c.nickname)))  # 양방향 교차 (0 → i)

    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.perf_counter() - t0

    # ---- 검증 ----
    sent = collections.Counter()
    received = collections.Counter()
    success = fail = 0
    retry_hist = collections.Counter()
    fail_msgs = collections.Counter()
    for frm, to, r in results:
        if r["success"]:
            success += 1
            sent[frm] += args.amount
            received[to] += args.amount
            retry_hist[r["retries"]] += 1
        else:
            fail += 1
            fail_msgs[r["msg"]] += 1

    ok = True
    total = 0
    for c in users:
        b = c.get_balance()
        expected = INITIAL_BALANCE - sent[c.nickname] + received[c.nickname]
        total += b
        if b != expected:
            ok = False
            print(f"[FAIL] {c.nickname}: balance {b} != expected {expected}")
    if total != INITIAL_BALANCE * args.users:
        ok = False
        print(f"[FAIL] conservation: total {total} != {INITIAL_BALANCE * args.users}")

    pushes = sum(len(c.pushed) for c in users)
    total_attempts = success + fail
    total_retries = sum(k * v for k, v in retry_hist.items())

    print()
    print(f"attempts        : {total_attempts}  (success {success}, fail {fail})")
    print(f"elapsed         : {elapsed:.3f}s  →  {total_attempts / elapsed:.1f} transfers/s")
    print(f"OCC retries     : total {total_retries}, histogram {dict(sorted(retry_hist.items()))}")
    if fail_msgs:
        print(f"fail reasons    : {dict(fail_msgs)}")
    print(f"push received   : {pushes} S_TRANSFER_RECEIVED")
    print(f"conservation    : total {total} == {INITIAL_BALANCE * args.users}  {'OK' if ok else 'BROKEN'}")

    for c in users:
        c.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
