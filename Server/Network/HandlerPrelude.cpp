#include "HandlerPrelude.h"
#include "../ServerGlobal.h"
#include "DB/Generated/EntitiesGenerated.h"

HandlerPrelude::HandlerPrelude(const SharedPtr<GameSession>& Session)
	: MyUserId(static_cast<int64>(Session->GetPlayerId()))
{
	if (MyUserId == 0)
	{
		Error = "Not logged in";
		return;
	}

	// 로그인 통과 후에만 풀에서 연결을 꺼낸다.
	Scope = std::make_unique<DBConnectionScope>(GDBPool);
	Db.SetDBConnection(Scope->Get());
}

User* HandlerPrelude::FindUserByEmail(const std::string& Email)
{
	auto Users = Db.Set<User>().Where(Col<User>::Email == Email).ToList();
	if (Users.empty())
	{
		Error = "Email not found";
		return nullptr;
	}
	return Users.front();
}

User* HandlerPrelude::FindMe()
{
	auto Users = Db.Set<User>().Where(Col<User>::Id == MyUserId).ToList();
	if (Users.empty())
	{
		Error = "User not found";
		return nullptr;
	}
	return Users.front();
}
