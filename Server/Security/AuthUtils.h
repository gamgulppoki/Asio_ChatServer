#pragma once

#include <string>

// 비밀번호 해싱/검증 유틸리티.
// argon2id 알고리즘을 사용하며, salt는 내부에서 자동 생성된다.
class AuthUtils
{
public:
	// 비밀번호를 argon2id로 해싱한다. 성공 시 Encoded 문자열이 OutEncoded에 저장된다.
	static bool HashPassword(const std::string& Password, std::string& OutEncoded);

	// 저장된 Encoded 해시와 입력 비밀번호를 비교한다.
	static bool VerifyPassword(const std::string& Encoded, const std::string& Password);
};