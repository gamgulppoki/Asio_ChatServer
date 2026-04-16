#pragma once

#include "DBConnection.h"
#include "Column.h"
#include <string>
#include <optional>

// 구조체 T를 DB 테이블에 매핑하는 ORM 클래스.
// AddColumn으로 컬럼-필드 관계를 등록하면 SELECT/INSERT/UPDATE를 자동 생성한다.
template<typename T>
class DBModel
{
	// 컬럼 하나의 메타데이터: 이름, 결과 바인드 함수, 파라미터 바인드 함수
	struct ColumnDesc
	{
		WString Name;
		bool bAutoIncrement = false;
		Function<void(DBConnection&, int32, T&)> BindFunc;
		Function<void(DBConnection&, int32&, const T&)> BindParamFunc;
	};

public:
	DBModel(DBConnection& Conn, const WCHAR* TableName)
		: Conn(Conn), TableName(TableName)
	{
	}

	// 일반 타입(int32, int64, float, double, bool, int16) 컬럼을 등록한다.
	// 멤버 포인터에서 타입을 자동 추론한다.
	template<typename MemberType>
	void AddColumn(const WCHAR* Name, MemberType T::* MemberPtr, bool bAutoIncrement = false)
	{
		ColumnDesc Desc;
		Desc.Name = Name;
		Desc.bAutoIncrement = bAutoIncrement;
		Desc.BindFunc = [MemberPtr](DBConnection& C, int32 iCol, T& Row)
		{
			C.BindCol(iCol, &(Row.*MemberPtr));
		};
		Desc.BindParamFunc = [MemberPtr](DBConnection& C, int32& Idx, const T& Row)
		{
			C.BindParam(Idx++, Row.*MemberPtr);
		};
		Columns.push_back(std::move(Desc));
	}

	// WCHAR 배열 컬럼을 등록한다. 배열 크기 N은 템플릿에서 자동 추론된다.
	template<int32 N>
	void AddColumn(const WCHAR* Name, WCHAR(T::* MemberPtr)[N], bool bAutoIncrement = false)
	{
		ColumnDesc Desc;
		Desc.Name = Name;
		Desc.bAutoIncrement = bAutoIncrement;
		Desc.BindFunc = [MemberPtr](DBConnection& C, int32 iCol, T& Row)
		{
			C.BindCol(iCol, Row.*MemberPtr, sizeof(WCHAR) * N);
		};
		Desc.BindParamFunc = [MemberPtr](DBConnection& C, int32& Idx, const T& Row)
		{
			C.BindParam(Idx++, Row.*MemberPtr, N);
		};
		Columns.push_back(std::move(Desc));
	}

	// 테이블의 모든 행을 조회한다.
	Vector<T> SelectAll()
	{
		WString Query = BuildSelectQuery();

		if (!Conn.Execute(Query.c_str()))
			return {};

		T Row = {};
		for (int32 i = 0; i < static_cast<int32>(Columns.size()); i++)
			Columns[i].BindFunc(Conn, i + 1, Row);

		Vector<T> Results;
		while (Conn.Fetch())
			Results.push_back(Row);

		return Results;
	}

	// 표현식 기반 조건 조회. WHERE 절의 값은 prepared 바인딩된다.
	Vector<T> SelectWhere(const Expression& Expr)
	{
		WString Query = BuildSelectQuery();
		Query += L" WHERE ";
		Query += Expr.Sql;

		int32 Idx = 1;
		Expr.Bind(Conn, Idx);

		if (!Conn.Execute(Query.c_str()))
			return {};

		T Row = {};
		for (int32 i = 0; i < static_cast<int32>(Columns.size()); i++)
			Columns[i].BindFunc(Conn, i + 1, Row);

		Vector<T> Results;
		while (Conn.Fetch())
			Results.push_back(Row);

		return Results;
	}

	// 표현식 기반 단일 행 조회. 결과가 없으면 nullopt.
	std::optional<T> SelectOne(const Expression& Expr)
	{
		WString Query = BuildSelectQuery(true);
		Query += L" WHERE ";
		Query += Expr.Sql;

		int32 Idx = 1;
		Expr.Bind(Conn, Idx);

		if (!Conn.Execute(Query.c_str()))
			return std::nullopt;

		T Row = {};
		for (int32 i = 0; i < static_cast<int32>(Columns.size()); i++)
			Columns[i].BindFunc(Conn, i + 1, Row);

		if (!Conn.Fetch())
			return std::nullopt;

		return Row;
	}

	// 행 하나를 삽입한다. bAutoIncrement 컬럼은 자동으로 제외된다.
	bool Insert(const T& Row)
	{
		WString Query = BuildInsertQuery();

		int32 Idx = 1;
		for (auto& Col : Columns)
		{
			if (Col.bAutoIncrement)
				continue;
			Col.BindParamFunc(Conn, Idx, Row);
		}

		return Conn.Execute(Query.c_str());
	}

	// 표현식 기반 조건 수정. SET 값과 WHERE 값 모두 prepared 바인딩된다.
	bool Update(const T& Row, const Expression& Expr)
	{
		WString Query = BuildUpdateQuery();
		Query += L" WHERE ";
		Query += Expr.Sql;

		int32 Idx = 1;
		for (auto& Col : Columns)
		{
			if (Col.bAutoIncrement)
				continue;
			Col.BindParamFunc(Conn, Idx, Row);
		}
		Expr.Bind(Conn, Idx);

		return Conn.Execute(Query.c_str());
	}

	// 조건에 맞는 행을 삭제한다. 실수 방지를 위해 조건은 필수.
	bool Delete(const Expression& Expr)
	{
		WString Query = L"DELETE FROM " + TableName + L" WHERE " + Expr.Sql;

		int32 Idx = 1;
		Expr.Bind(Conn, Idx);

		return Conn.Execute(Query.c_str());
	}

private:
	// SELECT col1, col2, ... FROM TableName
	// bTopOne이 true면 SELECT TOP 1 형태로 생성한다 (SelectOne용).
	WString BuildSelectQuery(bool bTopOne = false)
	{
		WString Query = bTopOne ? L"SELECT TOP 1 " : L"SELECT ";
		for (int32 i = 0; i < static_cast<int32>(Columns.size()); i++)
		{
			if (i > 0) Query += L", ";
			Query += Columns[i].Name;
		}
		Query += L" FROM ";
		Query += TableName;
		return Query;
	}

	// INSERT INTO TableName (col1, col2) VALUES (?, ?)
	WString BuildInsertQuery()
	{
		WString Cols;
		WString Vals;
		bool bFirst = true;

		for (auto& Col : Columns)
		{
			if (Col.bAutoIncrement)
				continue;

			if (!bFirst)
			{
				Cols += L", ";
				Vals += L", ";
			}
			Cols += Col.Name;
			Vals += L"?";
			bFirst = false;
		}

		return L"INSERT INTO " + TableName + L" (" + Cols + L") VALUES (" + Vals + L")";
	}

	// UPDATE TableName SET col1 = ?, col2 = ?
	WString BuildUpdateQuery()
	{
		WString SetClause;
		bool bFirst = true;

		for (auto& Col : Columns)
		{
			if (Col.bAutoIncrement)
				continue;

			if (!bFirst)
				SetClause += L", ";

			SetClause += Col.Name;
			SetClause += L" = ?";
			bFirst = false;
		}

		return L"UPDATE " + TableName + L" SET " + SetClause;
	}

	DBConnection& Conn;
	WString TableName;
	Vector<ColumnDesc> Columns;
};