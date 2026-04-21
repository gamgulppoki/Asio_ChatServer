#pragma once

// <sql.h>/<sqltypes.h> 가 INT64/UINT64 등 Windows 타입을 필요로 하므로 먼저 include
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <sql.h>
#include <sqlext.h>
#include <string>
#include "Types.h"

/*----------------
	DBConnection
-----------------*/

class DBContext;

enum
{
	VARCHAR_MAX = 8000,
	BINARY_MAX  = 8000
};

class DBConnection
{
public:
	DBConnection();
	~DBConnection();

	DBConnection(const DBConnection&)            = delete;
	DBConnection& operator=(const DBConnection&) = delete;

	// ENV는 Pool이 소유하여 모든 DBConnection이 공유. 주입받아 DBC/STMT만 만든다.
	bool			Connect(SQLHENV env, const WCHAR* connectionString);

	bool			Execute(const std::string& query);
	bool			Fetch();
	int32			GetRowCount();
	void			Unbind();

	bool			Begin();
	bool			Commit();
	bool			Rollback();

	// Pool 반납 직전에 호출. Commit/Rollback 미호출로 autocommit이 OFF로 남은 경우를 안전하게 ON으로 되돌린다.
	bool			RestoreAutocommit();

public:
	bool			BindParam(int32 paramIndex, bool* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, float* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, double* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, int8* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, int16* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, int32* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, int64* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, TIMESTAMP_STRUCT* value, SQLLEN* index);
	bool			BindParam(int32 paramIndex, const char* str, SQLLEN* index);
	bool			BindParam(int32 paramIndex, const BYTE* bin, int32 size, SQLLEN* index);

	bool			BindCol(int32 columnIndex, bool* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, float* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, double* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, int8* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, int16* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, int32* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, int64* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, TIMESTAMP_STRUCT* value, SQLLEN* index);
	bool			BindCol(int32 columnIndex, char* str, int32 size, SQLLEN* index);
	bool			BindCol(int32 columnIndex, BYTE* bin, int32 size, SQLLEN* index);

private:
	void			Clear();

	bool			BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index);
	bool			BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index);
	void			HandleError(SQLRETURN ret);

private:
	SQLHENV			_env        = SQL_NULL_HANDLE;  // Pool 소유. 여기서 free 하지 않음.
	SQLHDBC			_connection = SQL_NULL_HANDLE;
	SQLHSTMT		_statement  = SQL_NULL_HANDLE;
	
};