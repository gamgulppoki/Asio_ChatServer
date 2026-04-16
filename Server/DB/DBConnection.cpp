#include "DBConnection.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

DBConnection::DBConnection()
{
}

// 소멸자에서 연결 해제
DBConnection::~DBConnection()
{
	Disconnect();
}

// ODBC 환경 핸들(Pool이 소유)을 받아 DB에 연결한다.
// ConnectionString 예시: L"DRIVER={ODBC Driver 17 for SQL Server};SERVER=.\\SQLEXPRESS;DATABASE=WebzenDB;Trusted_Connection=Yes;"
bool DBConnection::Connect(SQLHENV Env, const WCHAR* ConnectionString)
{
	// 1. 연결 핸들 생성
	if (SQLAllocHandle(SQL_HANDLE_DBC, Env, &Dbc) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnection] Failed to allocate connection handle");
		return false;
	}

	// 2. DB 접속
	WCHAR OutConnectionString[1024] = {};
	SQLSMALLINT OutConnectionStringLen = 0;

	SQLRETURN Ret = SQLDriverConnectW(
		Dbc,
		NULL,
		const_cast<SQLWCHAR*>(ConnectionString),
		SQL_NTS,
		OutConnectionString,
		1024,
		&OutConnectionStringLen,
		SQL_DRIVER_NOPROMPT
	);

	if (Ret != SQL_SUCCESS && Ret != SQL_SUCCESS_WITH_INFO)
	{
		HandleError(Dbc, SQL_HANDLE_DBC);
		Disconnect();
		return false;
	}

	// 3. 문장 핸들 생성
	if (SQLAllocHandle(SQL_HANDLE_STMT, Dbc, &Stmt) != SQL_SUCCESS)
	{
		spdlog::error("[DBConnection] Failed to allocate statement handle");
		Disconnect();
		return false;
	}

	return true;
}

// SchemaDir 안의 모든 .sql 파일을 읽어 실행한다.
// 현재 .sql은 ASCII만 가정(컬럼명/키워드). 한글 등 들어가면 UTF-8 디코딩 필요.
bool DBConnection::ApplySchema(const WCHAR* SchemaDir)
{
	namespace fs = std::filesystem;

	if (!fs::exists(SchemaDir))
		return true;

	for (auto& Entry : fs::directory_iterator(SchemaDir))
	{
		if (Entry.path().extension() != ".sql")
			continue;

		std::ifstream File(Entry.path());
		std::stringstream Ss;
		Ss << File.rdbuf();
		std::string Utf8 = Ss.str();
		std::wstring Wide(Utf8.begin(), Utf8.end());

		if (!Execute(Wide.c_str()))
		{
			spdlog::error("[DBConnection] Schema apply failed: {}",
				Entry.path().filename().string());
			return false;
		}

		spdlog::info("[DBConnection] Applied schema: {}",
			Entry.path().filename().string());
	}
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
}

// SQL 쿼리를 실행한다. SELECT가 아닌 INSERT/UPDATE/DELETE에 적합하다.
bool DBConnection::Execute(const WCHAR* Query)
{
	// 이전 실행 결과 정리 (커서/컬럼 바인딩만 해제, 파라미터 바인딩은 유지)
	SQLCloseCursor(Stmt);
	SQLFreeStmt(Stmt, SQL_UNBIND);

	SQLRETURN Ret = SQLExecDirectW(Stmt, const_cast<SQLWCHAR*>(Query), SQL_NTS);

	// 실행 완료 후 파라미터 바인딩 해제
	SQLFreeStmt(Stmt, SQL_RESET_PARAMS);

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
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_BIT, OutValue, 0, &Indicators[iColumn]);
}

// int16 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, int16* OutValue)
{
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_SSHORT, OutValue, 0, &Indicators[iColumn]);
}

// int32 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, int32* OutValue)
{
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_LONG, OutValue, 0, &Indicators[iColumn]);
}

// int64 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, int64* OutValue)
{
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_SBIGINT, OutValue, 0, &Indicators[iColumn]);
}

// float 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, float* OutValue)
{
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_FLOAT, OutValue, 0, &Indicators[iColumn]);
}

// double 컬럼을 C++ 변수에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, double* OutValue)
{
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_DOUBLE, OutValue, 0, &Indicators[iColumn]);
}

// WCHAR 문자열 컬럼을 C++ 버퍼에 바인딩한다.
void DBConnection::BindCol(int32 iColumn, WCHAR* OutValue, int32 iLen)
{
	ASSERT_CRASH(iColumn >= 1 && iColumn < MAX_COLUMNS);
	SQLBindCol(Stmt, static_cast<SQLUSMALLINT>(iColumn), SQL_C_WCHAR, OutValue, iLen, &Indicators[iColumn]);
}

// bool 값을 파라미터에 바인딩한다.
void DBConnection::BindParam(int32 iIndex, const bool& Value)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_BIT, SQL_BIT, 0, 0,
		const_cast<bool*>(&Value), 0, nullptr);
}

// int16 값을 파라미터에 바인딩한다.
void DBConnection::BindParam(int32 iIndex, const int16& Value)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_SSHORT, SQL_SMALLINT, 0, 0,
		const_cast<int16*>(&Value), 0, nullptr);
}

// int32 값을 파라미터에 바인딩한다.
void DBConnection::BindParam(int32 iIndex, const int32& Value)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_SLONG, SQL_INTEGER, 0, 0,
		const_cast<int32*>(&Value), 0, nullptr);
}

// int64 값을 파라미터에 바인딩한다.
void DBConnection::BindParam(int32 iIndex, const int64& Value)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_SBIGINT, SQL_BIGINT, 0, 0,
		const_cast<int64*>(&Value), 0, nullptr);
}

// float 값을 파라미터에 바인딩한다.
void DBConnection::BindParam(int32 iIndex, const float& Value)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_FLOAT, SQL_REAL, 0, 0,
		const_cast<float*>(&Value), 0, nullptr);
}

// double 값을 파라미터에 바인딩한다.
void DBConnection::BindParam(int32 iIndex, const double& Value)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_DOUBLE, SQL_DOUBLE, 0, 0,
		const_cast<double*>(&Value), 0, nullptr);
}

// WCHAR 문자열을 파라미터에 바인딩한다. iSize는 컬럼의 문자 수(NVARCHAR 크기).
void DBConnection::BindParam(int32 iIndex, const WCHAR* Value, int32 iSize)
{
	ASSERT_CRASH(iIndex >= 1 && iIndex < MAX_COLUMNS);
	Indicators[iIndex] = static_cast<SQLLEN>(::wcslen(Value)) * static_cast<SQLLEN>(sizeof(WCHAR));
	SQLBindParameter(Stmt, static_cast<SQLUSMALLINT>(iIndex), SQL_PARAM_INPUT,
		SQL_C_WCHAR, SQL_WVARCHAR, iSize, 0,
		const_cast<WCHAR*>(Value), static_cast<SQLLEN>(iSize) * static_cast<SQLLEN>(sizeof(WCHAR)), &Indicators[iIndex]);
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
		(void)wcstombs_s(&Converted, Buf, sizeof(Buf), Message, _TRUNCATE);
		spdlog::error("[DBConnection] ODBC Error: {}", Buf);
		iRecord++;
	}
}