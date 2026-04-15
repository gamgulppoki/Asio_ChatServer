#include "DBConnection.h"
#include <spdlog/spdlog.h>

DBConnection::DBConnection()
{
}

// 소멸자에서 연결 해제
DBConnection::~DBConnection()
{
	Disconnect();
}

// ODBC를 통해 DB에 연결한다.
// ConnectionString 예시: L"DRIVER={ODBC Driver 17 for SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=WebzenDB;Trusted_Connection=Yes;"
bool DBConnection::Connect(const WCHAR* ConnectionString)
{
	// 1. 환경 핸들 생성
	if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &Env) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnection] Failed to allocate environment handle");
		return false;
	}

	// 2. ODBC 버전 설정 (3.x)
	if (SQLSetEnvAttr(Env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnection] Failed to set ODBC version");
		return false;
	}

	// 3. 연결 핸들 생성
	if (SQLAllocHandle(SQL_HANDLE_DBC, Env, &Dbc) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnection] Failed to allocate connection handle");
		return false;
	}

	// 4. DB 접속
	WCHAR OutConnectionString[1024] = {};
	SQLSMALLINT OutConnectionStringLen = 0;

	SQLRETURN Ret = SQLDriverConnectW(
		Dbc,
		NULL,
		(SQLWCHAR*)ConnectionString,
		SQL_NTS,
		OutConnectionString,
		1024,
		&OutConnectionStringLen,
		SQL_DRIVER_NOPROMPT
	);

	if (Ret != SQL_SUCCESS && Ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(Dbc, SQL_HANDLE_DBC);
		return false;
	}

	// 5. 문장 핸들 생성
	if (SQLAllocHandle(SQL_HANDLE_STMT, Dbc, &Stmt) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnection] Failed to allocate statement handle");
		return false;
	}

	spdlog::info("[DBConnection] Connected to database");
	return true;
}

// 모든 핸들을 해제하고 연결을 종료한다.
void DBConnection::Disconnect()
{
	if (Stmt != SQL_NULL_HSTMT)
	{
		SQLFreeHandle(SQL_HANDLE_STMT, Stmt);
		Stmt = SQL_NULL_HSTMT;
	}

	if (Dbc != SQL_NULL_HDBC)
	{
		SQLDisconnect(Dbc);
		SQLFreeHandle(SQL_HANDLE_DBC, Dbc);
		Dbc = SQL_NULL_HDBC;
	}

	if (Env != SQL_NULL_HENV)
	{
		SQLFreeHandle(SQL_HANDLE_ENV, Env);
		Env = SQL_NULL_HENV;
	}
}

// SQL 쿼리를 실행한다. SELECT가 아닌 INSERT/UPDATE/DELETE에 적합하다.
bool DBConnection::Execute(const WCHAR* Query)
{
	// 이전 실행 결과 정리
	SQLCloseCursor(Stmt);
	SQLFreeStmt(Stmt, SQL_UNBIND);

	SQLRETURN Ret = SQLExecDirectW(Stmt, (SQLWCHAR*)Query, SQL_NTS);

	if (Ret != SQL_SUCCESS && Ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(Stmt, SQL_HANDLE_STMT);
		return false;
	}

	return true;
}

// 결과 집합에서 한 행을 가져온다. 더 이상 행이 없으면 false를 반환한다.
bool DBConnection::Fetch()
{
	SQLRETURN Ret = SQLFetch(Stmt);
	return (Ret == SQL_SUCCESS || Ret == SQL_SUCCESS_WITH_INFO);
}

// bool 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, bool* OutValue)
{
	SQLBindCol(Stmt, iColumn, SQL_C_BIT, OutValue, 0, &Indicators[iColumn]);
}

// int16 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, int16* OutValue)
{
	SQLBindCol(Stmt, iColumn, SQL_C_SSHORT, OutValue, 0, &Indicators[iColumn]);
}

// int32 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, int32* OutValue)
{
	SQLBindCol(Stmt, iColumn, SQL_C_LONG, OutValue, 0, &Indicators[iColumn]);
}

// int64 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, int64* OutValue)
{
	SQLBindCol(Stmt, iColumn, SQL_C_SBIGINT, OutValue, 0, &Indicators[iColumn]);
}

// float 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, float* OutValue)
{
	SQLBindCol(Stmt, iColumn, SQL_C_FLOAT, OutValue, 0, &Indicators[iColumn]);
}

// double 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, double* OutValue)
{
	SQLBindCol(Stmt, iColumn, SQL_C_DOUBLE, OutValue, 0, &Indicators[iColumn]);
}

// WCHAR 문자열 컬럼을 C++ 버퍼에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, WCHAR* OutValue, int32 iLen)
{
	SQLBindCol(Stmt, iColumn, SQL_C_WCHAR, OutValue, iLen, &Indicators[iColumn]);
}

// ODBC 에러 정보를 spdlog로 출력한다.
void DBConnection::HandleError(SQLHANDLE Handle, SQLSMALLINT Type)
{
	SQLWCHAR SqlState[6] = {};
	SQLINTEGER NativeError = 0;
	SQLWCHAR Message[512] = {};
	SQLSMALLINT MessageLen = 0;

	SQLSMALLINT iRecord = 1;
	while (SQLGetDiagRecW(Type, Handle, iRecord, SqlState, &NativeError, Message, 512, &MessageLen) == SQL_SUCCESS)
	{
		// WCHAR -> char 변환하여 로그 출력
		char Buf[1024] = {};
		size_t Converted = 0;
		wcstombs_s(&Converted, Buf, sizeof(Buf), Message, _TRUNCATE);
		spdlog::error("[DBConnection] ODBC Error: {}", Buf);
		iRecord++;
	}
}