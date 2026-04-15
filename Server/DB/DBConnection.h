#pragma once

#include "Types.h"
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

// DB 연결을 관리하는 클래스
// ODBC API를 직접 사용하여 MSSQL에 접속하고 쿼리를 실행한다.
class DBConnection
{
public:
	DBConnection();
	~DBConnection();

	bool Connect(const WCHAR* ConnectionString);
	void Disconnect();

	bool Execute(const WCHAR* Query);
	bool Fetch();
	void BindCol(int32 iColumn, bool* OutValue);
	void BindCol(int32 iColumn, int16* OutValue);
	void BindCol(int32 iColumn, int32* OutValue);
	void BindCol(int32 iColumn, int64* OutValue);
	void BindCol(int32 iColumn, float* OutValue);
	void BindCol(int32 iColumn, double* OutValue);
	void BindCol(int32 iColumn, WCHAR* OutValue, int32 iLen);

private:
	void HandleError(SQLHANDLE Handle, SQLSMALLINT Type);

	static constexpr int32 MAX_COLUMNS = 32;

	SQLHENV		Env = SQL_NULL_HENV;
	SQLHDBC		Dbc = SQL_NULL_HDBC;
	SQLHSTMT	Stmt = SQL_NULL_HSTMT;
	Array<SQLLEN, MAX_COLUMNS>	Indicators = {};
};