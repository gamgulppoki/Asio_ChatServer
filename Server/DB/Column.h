#pragma once

#include <string>

// 표현식 빌드용 컬럼 디스크립터.
// DBModel의 ColumnDesc와 별개로, Where 절 표현식을 만들기 위한 메타 정보만 가진다.

// 값을 SQL 리터럴 문자열로 변환한다.
// 숫자: 그대로, 문자열: NVARCHAR 리터럴(N'...') 형태.
inline WString ToSqlLiteral(int32 Value)          { return std::to_wstring(Value); }
inline WString ToSqlLiteral(int64 Value)          { return std::to_wstring(Value); }
inline WString ToSqlLiteral(int16 Value)          { return std::to_wstring(Value); }
inline WString ToSqlLiteral(float Value)          { return std::to_wstring(Value); }
inline WString ToSqlLiteral(double Value)         { return std::to_wstring(Value); }
inline WString ToSqlLiteral(bool Value)           { return Value ? L"1" : L"0"; }
inline WString ToSqlLiteral(const WCHAR* Value)   { return WString(L"N'") + Value + L"'"; }
inline WString ToSqlLiteral(const WString& Value) { return WString(L"N'") + Value + L"'"; }

// Where 절 표현식. 내부에 SQL 문자열 조각만 들고 있는다.
struct Expression
{
    WString Sql;
};

// 두 표현식을 AND로 합성한다.
inline Expression operator&&(const Expression& A, const Expression& B)
{
    return Expression{ L"(" + A.Sql + L" AND " + B.Sql + L")" };
}

// 두 표현식을 OR로 합성한다.
inline Expression operator||(const Expression& A, const Expression& B)
{
    return Expression{ L"(" + A.Sql + L" OR " + B.Sql + L")" };
}

// 일반 값 타입(int32, int64, float, ...) 컬럼.
template<typename T, typename Owner>
struct Column
{
    const WCHAR* Name;
    T Owner::* MemberPtr;

    Expression operator==(const T& Value) const { return Make(L"=",  Value); }
    Expression operator!=(const T& Value) const { return Make(L"<>", Value); }
    Expression operator< (const T& Value) const { return Make(L"<",  Value); }
    Expression operator<=(const T& Value) const { return Make(L"<=", Value); }
    Expression operator> (const T& Value) const { return Make(L">",  Value); }
    Expression operator>=(const T& Value) const { return Make(L">=", Value); }

private:
    Expression Make(const WCHAR* Op, const T& Value) const
    {
        return Expression{ WString(Name) + L" " + Op + L" " + ToSqlLiteral(Value) };
    }
};

// WCHAR 배열 컬럼 (NVARCHAR 매핑).
template<int32 N, typename Owner>
struct StringColumn
{
    const WCHAR* Name;
    WCHAR (Owner::* MemberPtr)[N];

    Expression operator==(const WCHAR* Value) const { return Make(L"=",  Value); }
    Expression operator!=(const WCHAR* Value) const { return Make(L"<>", Value); }

private:
    Expression Make(const WCHAR* Op, const WCHAR* Value) const
    {
        return Expression{ WString(Name) + L" " + Op + L" " + ToSqlLiteral(Value) };
    }
};