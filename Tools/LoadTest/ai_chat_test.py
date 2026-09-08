"""
AI 채팅 통합 테스트 (모의 Claude 서버 사용).

전제: 모의 서버가 8765 에서 돌고, ChatServer 가 아래 환경 변수로 떠 있어야 한다.
  ANTHROPIC_BASE_URL=http://127.0.0.1:8765  ANTHROPIC_API_KEY=test-key  CHATSERVER_AI_DAILY_CALLS=6

검증
  1. 스트리밍: S_AI_CHAT 조각이 여러 개 오고, 합치면 모의 서버의 echo 와 같다
  2. 이력: 두 번째 요청의 messages 길이가 3 (user, assistant, user) — 모의 서버가 echo 에 N 을 넣는다
  3. 429 / 500 재시도 후 성공
  4. refusal → 정중한 한 줄, error 이벤트 → done=false
  5. 세션당 1건: 응답 중 두 번째 요청은 거절
  6. 일일 한도: 6회 초과 시 거절
  7. DB: AiMessage 행 수, AiUsage 행의 Calls
"""
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chat_protocol import ChatClient, text, wait_for_server  # noqa: E402


def ask(c: ChatClient, message: str, timeout=30.0):
    """C_AI_CHAT 을 보내고 done 까지 조각을 모은다. (text, success, error, pieces)"""
    c.send("C_AI_CHAT", {1: message})
    pieces, err, ok = [], "", False
    deadline = time.time() + timeout
    while time.time() < deadline:
        name, f = c._recv_frame()
        if name != "S_AI_CHAT":
            continue
        t = text(f, 1)
        if t:
            pieces.append(t)
        if f.get(2, 0):
            ok = bool(f.get(3, 0))
            err = text(f, 4)
            break
    return "".join(pieces), ok, err, pieces


def sql(q: str) -> str:
    out = subprocess.run(["sqlcmd", "-S", r".\SQLEXPRESS", "-E", "-d", "ChatServerDB", "-h", "-1", "-W", "-Q", "SET NOCOUNT ON; " + q],
                         capture_output=True, text=True, timeout=30)
    return out.stdout.strip()


def main() -> int:
    if not wait_for_server():
        print("[FAIL] chat server not reachable")
        return 2

    run_id = int(time.time()) % 1_000_000
    nick, email = f"ai{run_id}", f"ai{run_id}@loadtest.local"
    c = ChatClient()
    ok, msg = c.register(nick, email, "password123")
    assert ok, msg
    ok, msg = c.login(email, "password123")
    assert ok, msg
    user_id = sql(f"SELECT Id FROM [User] WHERE Email = N'{email}';")
    results = []

    def check(label, cond, detail=""):
        results.append(cond)
        print(f"{'OK  ' if cond else 'FAIL'} {label}  {detail}")

    # 1. 스트리밍 + echo
    t, ok, err, pieces = ask(c, "안녕")
    check("streaming pieces > 1", ok and len(pieces) > 1, f"pieces={len(pieces)} text='{t}'")
    check("echo text", t == "echo: 안녕 (history 1 msgs)", f"'{t}' {err}")

    # 2. 이력 (user, assistant, user)
    t, ok, err, _ = ask(c, "두번째")
    check("history sent = 3 msgs", ok and t.endswith("(history 3 msgs)"), f"'{t}' {err}")

    # 3. 429 / 500 재시도
    t0 = time.time()
    t, ok, err, _ = ask(c, "@429 재시도")
    check("429 then retry ok", ok and t.startswith("echo: @429"), f"{time.time() - t0:.1f}s '{t}' {err}")
    t, ok, err, _ = ask(c, "@500 재시도")
    check("500 then retry ok", ok and t.startswith("echo: @500"), f"'{t}' {err}")

    # 4. refusal / error
    t, ok, err, _ = ask(c, "@refuse")
    check("refusal → polite line", ok and "답할 수 없습니다" in t, f"'{t}' {err}")
    t, ok, err, _ = ask(c, "@error")
    check("stream error → done=false", (not ok) and "overloaded" in err, f"'{t}' err='{err}'")

    # 5. 세션당 1건 (느린 응답 중 두 번째 요청)
    c.send("C_AI_CHAT", {1: "@slow 느리게"})
    time.sleep(0.2)
    c.send("C_AI_CHAT", {1: "겹침"})
    busy_rejected = False
    slow_ok = False
    deadline = time.time() + 30
    done_count = 0
    while time.time() < deadline and done_count < 2:
        name, f = c._recv_frame()
        if name != "S_AI_CHAT" or not f.get(2, 0):
            continue
        done_count += 1
        if not f.get(3, 0) and "in progress" in text(f, 4):
            busy_rejected = True
        elif f.get(3, 0):
            slow_ok = True
    check("second request while busy rejected", busy_rejected and slow_ok)

    # 6. 일일 한도 (서버 CHATSERVER_AI_DAILY_CALLS=6 가정: 지금까지 성공 6회면 다음은 거절)
    t, ok, err, _ = ask(c, "한도")
    check("daily call limit", (not ok) and "limit" in err.lower(), f"err='{err}' (서버 한도 6 가정)")

    # 7. DB
    n_msgs = sql(f"SELECT COUNT(*) FROM AiMessage WHERE UserId = {user_id};")
    calls = sql(f"SELECT Calls FROM AiUsage WHERE UserId = {user_id};")
    check("AiMessage rows saved", n_msgs.isdigit() and int(n_msgs) >= 10, f"rows={n_msgs}")
    check("AiUsage.Calls == 6", calls == "6", f"calls={calls}")

    c.close()
    print("RESULT:", "PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
