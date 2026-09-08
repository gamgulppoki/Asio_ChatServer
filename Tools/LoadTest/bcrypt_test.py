"""
bcrypt 비밀번호 해싱 검증.

  1. 새 유저 가입 → DB 에 $2b$ 해시로 저장됐는지 (sqlcmd 로 직접 확인)
  2. 올바른 비밀번호 로그인 성공 / 틀린 비밀번호 실패
  3. 해싱 도입 전 평문 행(bulk 더미 유저) 로그인 → 성공 + 해시로 자동 승격 → 재로그인 성공
  4. 가입(해시 1회) 왕복 시간 = cost 12 의 체감 비용

사용법: python Tools/LoadTest/bcrypt_test.py   (서버가 떠 있어야 함)
"""
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chat_protocol import ChatClient, wait_for_server  # noqa: E402


def db_password_of(email: str) -> str:
    out = subprocess.run(
        ["sqlcmd", "-S", r".\SQLEXPRESS", "-E", "-d", "ChatServerDB", "-h", "-1", "-W",
         "-Q", f"SET NOCOUNT ON; SELECT Password FROM [User] WHERE Email = N'{email}';"],
        capture_output=True, text=True, timeout=30)
    return out.stdout.strip()


def main() -> int:
    if not wait_for_server():
        print("[FAIL] server not reachable")
        return 2

    run_id = int(time.time()) % 1_000_000
    email = f"bc{run_id}@loadtest.local"
    nick = f"bc{run_id}"
    ok_all = True

    # 1. 가입 (해시 1회) 시간
    c = ChatClient()
    t0 = time.perf_counter()
    ok, msg = c.register(nick, email, "correct-horse")
    reg_ms = (time.perf_counter() - t0) * 1000
    print(f"register        : {'OK' if ok else 'FAIL ' + msg}  ({reg_ms:.0f} ms round-trip, hash 1회 포함)")
    ok_all &= ok

    stored = db_password_of(email)
    is_hash = stored.startswith("$2b$12$") and len(stored) == 60
    print(f"stored password : {stored[:29]}...  ({len(stored)} chars)  {'bcrypt OK' if is_hash else 'NOT A HASH'}")
    ok_all &= is_hash

    # 2. 로그인 성공/실패
    t0 = time.perf_counter()
    ok, msg = c.login(email, "correct-horse")
    login_ms = (time.perf_counter() - t0) * 1000
    print(f"login (correct) : {'OK' if ok else 'FAIL ' + msg}  ({login_ms:.0f} ms, verify 1회 포함)")
    ok_all &= ok

    c2 = ChatClient()
    ok, msg = c2.login(email, "wrong-password")
    print(f"login (wrong)   : {'rejected OK' if not ok else 'ACCEPTED - BUG'}  ({msg})")
    ok_all &= (not ok)
    c2.close()

    # 3. 평문 레거시 행 → 로그인 시 승격
    legacy_email = "bulk1@loadtest.local"
    before = db_password_of(legacy_email)
    if before and not before.startswith("$2"):
        c3 = ChatClient()
        ok, msg = c3.login(legacy_email, "password123")
        after = db_password_of(legacy_email)
        upgraded = after.startswith("$2b$")
        print(f"legacy login    : {'OK' if ok else 'FAIL ' + msg}, stored before='{before}' after='{after[:29]}...'  {'upgraded OK' if upgraded else 'NOT UPGRADED'}")
        ok_all &= ok and upgraded
        c3.close()

        c4 = ChatClient()
        ok, msg = c4.login(legacy_email, "password123")
        print(f"legacy re-login : {'OK (bcrypt verify)' if ok else 'FAIL ' + msg}")
        ok_all &= ok
        c4.close()
    else:
        print(f"legacy login    : skipped (no plaintext legacy row for {legacy_email})")

    c.close()
    print("RESULT          :", "PASS" if ok_all else "FAIL")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
