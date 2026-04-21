#include "DBConnection.h"

#include <iostream>
#include <stdexcept>
#include <cstring>

using std::cout;
using std::endl;

#pragma comment(lib, "odbc32.lib")

/*----------------
	DBConnection
-----------------*/

DBConnection::DBConnection()
{
	// ENV는 Pool이 주입. 실제 연결은 Connect() 에서.
}

DBConnection::~DBConnection()
{
	Clear();
	// ENV는 Pool 소유 — 여기서 free 하지 않음.
}

bool DBConnection::Connect(SQLHENV env, const WCHAR* connectionString)
{
	_env = env;

	if (::SQLAllocHandle(SQL_HANDLE_DBC, _env, &_connection) != SQL_SUCCESS)
		return false;

	SQLWCHAR    resultString[1024] = { 0 };
	SQLSMALLINT resultStringLen    = 0;

	SQLRETURN ret = ::SQLDriverConnectW(
		_connection,
		NULL,
		(SQLWCHAR*)connectionString, SQL_NTS,
		resultString, _countof(resultString), &resultStringLen,
		SQL_DRIVER_NOPROMPT
	);

	if (!SQL_SUCCEEDED(ret))
		return false;

	if (::SQLAllocHandle(SQL_HANDLE_STMT, _connection, &_statement) != SQL_SUCCESS)
		return false;

	return true;
}

void DBConnection::Clear()
{
	if (_statement != SQL_NULL_HANDLE)
	{
		::SQLFreeHandle(SQL_HANDLE_STMT, _statement);
		_statement = SQL_NULL_HANDLE;
	}

	if (_connection != SQL_NULL_HANDLE)
	{
		::SQLDisconnect(_connection);
		::SQLFreeHandle(SQL_HANDLE_DBC, _connection);
		_connection = SQL_NULL_HANDLE;
	}
}

bool DBConnection::Execute(const std::string& query)
{
	SQLRETURN ret = ::SQLExecDirectA(_statement, (SQLCHAR*)query.c_str(), SQL_NTS);
	// SQL_NO_DATA: searched UPDATE/DELETE 가 0행 매칭 — 에러 아님 (OCC 충돌 감지용)
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO || ret == SQL_NO_DATA)
		return true;

	HandleError(ret);
	return false;
}

bool DBConnection::Fetch()
{
	SQLRETURN ret = ::SQLFetch(_statement);

	switch (ret)
	{
	case SQL_SUCCESS:
	case SQL_SUCCESS_WITH_INFO:
		return true;
	case SQL_NO_DATA:
		return false;
	case SQL_ERROR:
		HandleError(ret);
		return false;
	default:
		return true;
	}
}

int32 DBConnection::GetRowCount()
{
	SQLLEN count = 0;
	SQLRETURN ret = ::SQLRowCount(_statement, &count);

	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		return static_cast<int32>(count);

	return -1;
}

void DBConnection::Unbind()
{
	::SQLFreeStmt(_statement, SQL_UNBIND);
	::SQLFreeStmt(_statement, SQL_RESET_PARAMS);
	::SQLFreeStmt(_statement, SQL_CLOSE);
}

bool DBConnection::Begin()
{
	SQLRETURN ret = ::SQLSetConnectAttr(
		_connection,
		SQL_ATTR_AUTOCOMMIT,
		(SQLPOINTER)SQL_AUTOCOMMIT_OFF,
		SQL_IS_UINTEGER
	);
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		return true;

	HandleError(ret);
	return false;
}

bool DBConnection::Commit()
{
	SQLRETURN ret = ::SQLEndTran(SQL_HANDLE_DBC, _connection, SQL_COMMIT);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return RestoreAutocommit();
}

bool DBConnection::Rollback()
{
	SQLRETURN ret = ::SQLEndTran(SQL_HANDLE_DBC, _connection, SQL_ROLLBACK);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return RestoreAutocommit();
}

// Pool 반납 직전 안전망. 이미 ON이면 no-op, OFF면 ON으로 복원.
bool DBConnection::RestoreAutocommit()
{
	if (_connection == SQL_NULL_HANDLE)
		return false;

	SQLRETURN ret = ::SQLSetConnectAttr(
		_connection,
		SQL_ATTR_AUTOCOMMIT,
		(SQLPOINTER)SQL_AUTOCOMMIT_ON,
		SQL_IS_UINTEGER
	);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}
	return true;
}

bool DBConnection::BindParam(int32 paramIndex, bool* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_TINYINT, SQL_TINYINT, size32(bool), value, index);
}

bool DBConnection::BindParam(int32 paramIndex, float* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_FLOAT, SQL_REAL, 0, value, index);
}

bool DBConnection::BindParam(int32 paramIndex, double* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_DOUBLE, SQL_DOUBLE, 0, value, index);
}

bool DBConnection::BindParam(int32 paramIndex, int8* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_TINYINT, SQL_TINYINT, size32(int8), value, index);
}

bool DBConnection::BindParam(int32 paramIndex, int16* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_SHORT, SQL_SMALLINT, size32(int16), value, index);
}

bool DBConnection::BindParam(int32 paramIndex, int32* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_LONG, SQL_INTEGER, size32(int32), value, index);
}

bool DBConnection::BindParam(int32 paramIndex, int64* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_SBIGINT, SQL_BIGINT, size32(int64), value, index);
}

bool DBConnection::BindParam(int32 paramIndex, TIMESTAMP_STRUCT* value, SQLLEN* index)
{
	return BindParam(paramIndex, SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, size32(TIMESTAMP_STRUCT), value, index);
}

bool DBConnection::BindParam(int32 paramIndex, const char* str, SQLLEN* index)
{
	SQLULEN size = static_cast<SQLULEN>(::strlen(str) + 1);
	*index = SQL_NTS;

	if (size > VARCHAR_MAX)
		return BindParam(paramIndex, SQL_C_CHAR, SQL_LONGVARCHAR, size, (SQLPOINTER)str, index);
	else
		return BindParam(paramIndex, SQL_C_CHAR, SQL_VARCHAR, size, (SQLPOINTER)str, index);
}

bool DBConnection::BindParam(int32 paramIndex, const BYTE* bin, int32 size, SQLLEN* index)
{
	if (bin == nullptr)
	{
		*index = SQL_NULL_DATA;
		size = 1;
	}
	else
		*index = size;

	if (size > BINARY_MAX)
		return BindParam(paramIndex, SQL_C_BINARY, SQL_LONGVARBINARY, size, (BYTE*)bin, index);
	else
		return BindParam(paramIndex, SQL_C_BINARY, SQL_BINARY, size, (BYTE*)bin, index);
}

bool DBConnection::BindCol(int32 columnIndex, bool* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_TINYINT, size32(bool), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, float* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_FLOAT, size32(float), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, double* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_DOUBLE, size32(double), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, int8* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_TINYINT, size32(int8), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, int16* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_SHORT, size32(int16), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, int32* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_LONG, size32(int32), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, int64* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_SBIGINT, size32(int64), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, TIMESTAMP_STRUCT* value, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_TYPE_TIMESTAMP, size32(TIMESTAMP_STRUCT), value, index);
}

bool DBConnection::BindCol(int32 columnIndex, char* str, int32 size, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_C_CHAR, size, str, index);
}

bool DBConnection::BindCol(int32 columnIndex, BYTE* bin, int32 size, SQLLEN* index)
{
	return BindCol(columnIndex, SQL_BINARY, size, bin, index);
}

bool DBConnection::BindParam(SQLUSMALLINT paramIndex, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN len, SQLPOINTER ptr, SQLLEN* index)
{
	SQLRETURN ret = ::SQLBindParameter(_statement, paramIndex, SQL_PARAM_INPUT, cType, sqlType, len, 0, ptr, 0, index);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return true;
}

bool DBConnection::BindCol(SQLUSMALLINT columnIndex, SQLSMALLINT cType, SQLULEN len, SQLPOINTER value, SQLLEN* index)
{
	SQLRETURN ret = ::SQLBindCol(_statement, columnIndex, cType, value, len, index);
	if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(ret);
		return false;
	}

	return true;
}

void DBConnection::HandleError(SQLRETURN ret)
{
	if (ret == SQL_SUCCESS)
		return;

	SQLSMALLINT index = 1;
	SQLCHAR     sqlState[MAX_PATH] = { 0 };
	SQLINTEGER  nativeErr = 0;
	SQLCHAR     errMsg[MAX_PATH]   = { 0 };
	SQLSMALLINT msgLen = 0;
	SQLRETURN   errorRet = 0;

	while (true)
	{
		errorRet = ::SQLGetDiagRecA(
			SQL_HANDLE_STMT,
			_statement,
			index,
			sqlState,
			&nativeErr,
			errMsg,
			_countof(errMsg),
			&msgLen
		);

		if (errorRet == SQL_NO_DATA)
			break;

		if (errorRet != SQL_SUCCESS && errorRet != SQL_SUCCESS_WITH_INFO)
			break;

		cout << "ODBC Error: [" << sqlState << "] " << errMsg << " (native=" << nativeErr << ")" << endl;
		index++;
	}
}