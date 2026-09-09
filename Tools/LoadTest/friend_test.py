"""
친구 · 마이페이지 · 잔고 핸들러 기능 테스트 (HandlerPrelude 회귀 확인용).

시나리오
  1. 유저 A, B 회원가입 + 로그인
  2. 미로그인 소켓으로 잔고 조회 / 친구 요청 → 전부 거절되어야 함 ("Not logged in")
  3. A → B 친구 요청: 성공 / 중복 / 자기 자신 / 없는 이메일 / 형식 오류 응답 확인
  4. B 의 받은 요청 목록에 A 가 있고, 수락하면 양쪽 친구 목록에 서로가 보임 (온라인 표시 포함)
  5. A 잔고 조회 == 초기 잔고, A 닉네임 변경이 B 의 친구 목록에 반영
  6. A 가 B 를 친구 삭제 → 양쪽 목록에서 사라짐. 다시 삭제하면 "Not a friend"
  7. B → A 재요청, A 가 거절 → A 의 받은 요청 목록 비어 있음
  8. A 탈퇴 → B 가 A 이메일로 요청하면 "Email not found"

사용법
  python Tools/LoadTest/friend_test.py [--host 127.0.0.1] [--port 9000]
서버가 떠 있어야 한다. 종료 코드 0 = 전부 통과.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from chat_protocol import ChatClient, text, wait_for_server  # noqa: E402

INITIAL_BALANCE = 10_000

failures = 0


def check(cond: bool, label: str, detail: str = ""):
    global failures
    if cond:
        print(f"  [OK]   {label}")
    else:
        failures += 1
        print(f"  [FAIL] {label}  {detail}")


def ok_msg(r: dict) -> tuple[bool, str]:
    return bool(r.get(1, 0)), text(r, 2)


def friend_list(c: ChatClient) -> dict[str, dict]:
    """S_GET_FRIEND_LIST → {email: {nickname, is_online}}"""
    r = c.request("C_GET_FRIEND_LIST", {}, "S_GET_FRIEND_LIST")
    return _friend_infos(r) if r.get(1, 0) else None


def pending_list(c: ChatClient) -> dict[str, dict]:
    r = c.request("C_GET_PENDING_FRIENDS", {}, "S_GET_PENDING_FRIENDS")
    return _friend_infos(r) if r.get(1, 0) else None


def _friend_infos(r: dict) -> dict[str, dict]:
    from chat_protocol import decode
    raw = r.get(2, [])
    if not isinstance(raw, list):
        raw = [raw]
    out = {}
    for blob in raw:
        f = decode(blob)
        out[text(f, 1)] = {"nickname": text(f, 2), "is_online": bool(f.get(3, 0))}
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    args = ap.parse_args()

    if not wait_for_server(args.host, args.port):
        print(f"[FAIL] server not reachable at {args.host}:{args.port}")
        return 2

    run_id = int(time.time()) % 1_000_000
    nick_a, nick_b = f"ft{run_id}a", f"ft{run_id}b"
    email_a, email_b = f"{nick_a}@friendtest.local", f"{nick_b}@friendtest.local"
    pw = "password123"

    # 1. 가입 + 로그인
    print("1. register / login")
    a, b = ChatClient(args.host, args.port), ChatClient(args.host, args.port)
    for c, nick, email in ((a, nick_a, email_a), (b, nick_b, email_b)):
        ok, msg = c.register(nick, email, pw)
        check(ok, f"register {nick}", msg)
        ok, msg = c.login(email, pw)
        check(ok, f"login {nick}", msg)

    # 2. 미로그인 → 전부 거절
    print("2. not logged in")
    anon = ChatClient(args.host, args.port)
    check(anon.get_balance() is None, "anon get_balance rejected")
    ok, msg = ok_msg(anon.request("C_REQUEST_FRIEND", {1: email_b}, "S_REQUEST_FRIEND"))
    check(not ok and msg == "Not logged in", "anon request_friend rejected", msg)
    ok, msg = ok_msg(anon.request("C_UPDATE_NICKNAME", {1: "anonNick"}, "S_UPDATE_NICKNAME"))
    check(not ok and msg == "Not logged in", "anon update_nickname rejected", msg)
    check(friend_list(anon) is None, "anon friend_list rejected")
    check(pending_list(anon) is None, "anon pending_list rejected")
    anon.close()

    # 3. A → B 친구 요청
    print("3. request friend")
    ok, msg = ok_msg(a.request("C_REQUEST_FRIEND", {1: email_b}, "S_REQUEST_FRIEND"))
    check(ok, "A -> B request", msg)
    ok, msg = ok_msg(a.request("C_REQUEST_FRIEND", {1: email_b}, "S_REQUEST_FRIEND"))
    check(not ok and msg.startswith("Already requested"), "duplicate request rejected", msg)
    ok, msg = ok_msg(a.request("C_REQUEST_FRIEND", {1: email_a}, "S_REQUEST_FRIEND"))
    check(not ok and msg == "Cannot add yourself", "self request rejected", msg)
    ok, msg = ok_msg(a.request("C_REQUEST_FRIEND", {1: f"nobody{run_id}@friendtest.local"}, "S_REQUEST_FRIEND"))
    check(not ok and msg == "Email not found", "unknown email rejected", msg)
    ok, msg = ok_msg(a.request("C_REQUEST_FRIEND", {1: "not-an-email"}, "S_REQUEST_FRIEND"))
    check(not ok and msg == "Invalid email format", "invalid email rejected", msg)
    ok, msg = ok_msg(b.request("C_REQUEST_FRIEND", {1: email_a}, "S_REQUEST_FRIEND"))
    check(not ok and msg.startswith("The other user already sent"), "reverse request rejected", msg)

    # 4. B 받은 요청 → 수락 → 양쪽 친구 목록
    print("4. pending / accept / friend list")
    pend = pending_list(b)
    check(pend is not None and email_a in pend, "B pending contains A", str(pend))
    ok, msg = ok_msg(b.request("C_ACCEPT_FRIEND", {1: email_a}, "S_ACCEPT_FRIEND"))
    check(ok, "B accepts A", msg)
    ok, msg = ok_msg(b.request("C_ACCEPT_FRIEND", {1: email_a}, "S_ACCEPT_FRIEND"))
    check(not ok and msg == "No pending request from this user", "accept again rejected", msg)
    fl_a, fl_b = friend_list(a), friend_list(b)
    check(fl_a is not None and email_b in fl_a and fl_a[email_b]["is_online"], "A list has B (online)", str(fl_a))
    check(fl_b is not None and email_a in fl_b and fl_b[email_a]["is_online"], "B list has A (online)", str(fl_b))

    # 5. 잔고 / 닉네임 변경
    print("5. balance / nickname")
    check(a.get_balance() == INITIAL_BALANCE, "A balance == initial", str(a.get_balance()))
    new_nick = f"{nick_a}x"
    ok, msg = ok_msg(a.request("C_UPDATE_NICKNAME", {1: new_nick}, "S_UPDATE_NICKNAME"))
    check(ok, "A update nickname", msg)
    ok, msg = ok_msg(a.request("C_UPDATE_NICKNAME", {1: "x"}, "S_UPDATE_NICKNAME"))
    check(not ok and msg.startswith("Invalid nickname"), "too-short nickname rejected", msg)
    fl_b = friend_list(b)
    check(fl_b is not None and fl_b.get(email_a, {}).get("nickname") == new_nick, "B sees A's new nickname", str(fl_b))

    # 6. 친구 삭제
    print("6. remove friend")
    ok, msg = ok_msg(a.request("C_REMOVE_FRIEND", {1: email_b}, "S_REMOVE_FRIEND"))
    check(ok, "A removes B", msg)
    check(friend_list(a) == {}, "A list empty", str(friend_list(a)))
    check(friend_list(b) == {}, "B list empty", str(friend_list(b)))
    ok, msg = ok_msg(a.request("C_REMOVE_FRIEND", {1: email_b}, "S_REMOVE_FRIEND"))
    check(not ok and msg == "Not a friend", "remove again rejected", msg)

    # 7. 재요청 → 거절
    print("7. reject")
    ok, msg = ok_msg(b.request("C_REQUEST_FRIEND", {1: email_a}, "S_REQUEST_FRIEND"))
    check(ok, "B -> A request", msg)
    ok, msg = ok_msg(a.request("C_REJECT_FRIEND", {1: email_b}, "S_REJECT_FRIEND"))
    check(ok, "A rejects B", msg)
    check(pending_list(a) == {}, "A pending empty", str(pending_list(a)))
    ok, msg = ok_msg(a.request("C_REJECT_FRIEND", {1: email_b}, "S_REJECT_FRIEND"))
    check(not ok and msg == "No pending request from this user", "reject again rejected", msg)

    # 8. 탈퇴
    print("8. delete account")
    ok, msg = ok_msg(a.request("C_DELETE_ACCOUNT", {}, "S_DELETE_ACCOUNT"))
    check(ok, "A deletes account", msg)
    a.close()
    ok, msg = ok_msg(b.request("C_REQUEST_FRIEND", {1: email_a}, "S_REQUEST_FRIEND"))
    check(not ok and msg == "Email not found", "deleted user not found", msg)
    ok, msg = ok_msg(b.request("C_DELETE_ACCOUNT", {}, "S_DELETE_ACCOUNT"))
    check(ok, "B deletes account (cleanup)", msg)
    b.close()

    print()
    if failures:
        print(f"[FAIL] {failures} check(s) failed")
        return 1
    print("[PASS] all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
