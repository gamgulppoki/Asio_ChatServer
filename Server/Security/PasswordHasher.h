#pragma once

#include <string>

// bcrypt 비밀번호 해시.
//
// 내부는 Openwall crypt_blowfish (ServerCore/ThirdParty/bcrypt, 공개 도메인). OpenBSD bcrypt 와 호환되는
// "$2b$" 형식이다. 직접 구현한 암호 코드는 없다 — 검증된 구현을 감싸기만 한다.
//
// 결과 문자열 (60자):  $2b$12$  +  22자 salt  +  31자 hash
//                      ^버전 ^cost   ^무작위 16바이트를 bcrypt-base64 로   ^키 확장 결과
// salt 가 해시 문자열 안에 같이 들어 있어서, 검증할 때 저장된 문자열에서 salt 와 cost 를 꺼내
// 같은 계산을 다시 하고 결과를 비교한다. 별도 salt 컬럼이 필요 없다.
//
// cost: 키 확장을 2^cost 번 반복한다. 값이 1 오르면 시간이 2배. 로그인 1회에 수백 ms 가 목표
// (공격자가 유출된 해시로 초당 수십억 번 시도하는 것을 막는 게 목적이라 "일부러 느리게" 만든다).
//
// 한계: bcrypt 는 입력 72바이트까지만 본다. InputValidator 가 비밀번호를 64자로 제한하지만
// UTF-8 다바이트 문자라면 72바이트를 넘을 수 있고, 그 뒤는 무시된다. 이 프로젝트 범위에서는 문서화로 갈음.
namespace PasswordHasher
{
	// 2^12 = 4,096 회 키 확장. 측정값은 README 성능 표 참고.
	constexpr int kDefaultCost = 12;

	// 평문 → "$2b$..." 해시 문자열. 실패(난수 생성 실패 등) 시 빈 문자열.
	std::string Hash(const std::string& plain, int cost = kDefaultCost);

	// 평문이 저장된 해시와 맞는지. encoded 가 bcrypt 형식이 아니면 false.
	bool Verify(const std::string& plain, const std::string& encoded);

	// 저장된 값이 bcrypt 해시 형식인지 ("$2" 로 시작). 해싱 도입 전 평문 행을 구분하는 용도.
	bool IsHash(const std::string& stored);
}
