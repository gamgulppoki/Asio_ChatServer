#include "AuthUtils.h"
#include <windows.h>
#include <bcrypt.h>
#include <argon2.h>
#include <spdlog/spdlog.h>

// 비밀번호를 argon2id로 해싱한다.
// 16바이트 랜덤 salt를 생성하고, Encoded 문자열("$argon2id$..." 형식)을 반환한다.
bool AuthUtils::HashPassword(const std::string& Password, std::string& OutEncoded)
{
	uint8_t Salt[16] = {};
	BCryptGenRandom(nullptr, Salt, sizeof(Salt), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

	char Encoded[128] = {};
	int Ret = argon2id_hash_encoded(
		2,       // iterations (시간 비용)
		65536,   // memory 64MB
		1,       // parallelism
		Password.c_str(), Password.size(),
		Salt, sizeof(Salt),
		32,      // 해시 길이
		Encoded, sizeof(Encoded)
	);

	if (Ret != ARGON2_OK)
	{
		spdlog::error("[AuthUtils] argon2id_hash_encoded failed: {}", argon2_error_message(Ret));
		return false;
	}

	OutEncoded = Encoded;
	return true;
}

// 저장된 Encoded 해시와 입력 비밀번호를 비교한다.
bool AuthUtils::VerifyPassword(const std::string& Encoded, const std::string& Password)
{
	int Ret = argon2id_verify(Encoded.c_str(), Password.c_str(), Password.size());
	return Ret == ARGON2_OK;
}