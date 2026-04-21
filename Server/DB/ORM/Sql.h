#pragma once

#include "Meta.h"
#include "Condition.h"
#include <string>
#include <vector>

inline std::string to_sql_type(TypeTag tag)
{
    switch (tag)
    {
        case TypeTag::INT:    return "BIGINT";
        case TypeTag::DOUBLE: return "FLOAT";
        case TypeTag::BOOL:   return "BIT";
        case TypeTag::STRING: return "NVARCHAR(4000)";
        default:              return "SQL_VARIANT";
    }
}

inline std::string create_table_sql(const EntityMeta& meta)
{
    std::string sql;
    sql += "IF OBJECT_ID(N'[dbo].[" + meta.TableName + "]', N'U') IS NULL\n";
    sql += "CREATE TABLE [" + meta.TableName + "] (\n";

    for (size_t i = 0; i < meta.Fields.size(); ++i)
    {
        const auto& f = meta.Fields[i];
        sql += "    [" + f.DataName + "] " + to_sql_type(f.DataType);

        if (f.DataName == meta.PrimaryKeyName)
        {
            sql += " NOT NULL PRIMARY KEY";
            if (f.DataType == TypeTag::INT)
                sql += " IDENTITY(1,1)";
        }

        if (i + 1 < meta.Fields.size())
            sql += ",";
        sql += "\n";
    }

    sql += ");";
    return sql;
}

inline std::string insert_sql(const EntityMeta& meta)
{
    std::string sql;
    sql += "INSERT INTO [" + meta.TableName + "] (";

    bool first = true;
    for (const auto& f : meta.Fields)
    {
        if (f.DataName == meta.PrimaryKeyName && f.DataType == TypeTag::INT)
            continue;   // IDENTITY 컬럼은 DB가 생성
        if (!first) sql += ", ";
        sql += "[" + f.DataName + "]";
        first = false;
    }

    sql += ") OUTPUT INSERTED.[" + meta.PrimaryKeyName + "] VALUES (";

    first = true;
    for (const auto& f : meta.Fields)
    {
        if (f.DataName == meta.PrimaryKeyName && f.DataType == TypeTag::INT)
            continue;
        if (!first) sql += ", ";
        sql += "?";
        first = false;
    }

    sql += ");";
    return sql;
}

inline std::string delete_sql(const EntityMeta& meta)
{
    return "DELETE FROM [" + meta.TableName + "] WHERE ["
           + meta.PrimaryKeyName + "] = ?;";
}

// dirty_indices: SET 절에 포함할 필드 인덱스. 호출자가 dirty 인 컬럼만 골라서 넘김.
// WHERE 절: OCC. 모든 컬럼의 originValue 와 매칭. 호출자가 같은 순서로 originValue 바인딩 필요.
inline std::string update_sql(const EntityMeta& meta, const std::vector<size_t>& dirty_indices)
{
    std::string sql = "UPDATE [" + meta.TableName + "] SET ";

    bool first = true;
    for (size_t idx : dirty_indices)
    {
        const auto& f = meta.Fields[idx];
        if (f.DataName == meta.PrimaryKeyName && f.DataType == TypeTag::INT)
            continue;   // IDENTITY PK — SET 금지 (안전 장치)
        if (!first) sql += ", ";
        sql += "[" + f.DataName + "] = ?";
        first = false;
    }

    sql += " WHERE ";
    for (size_t i = 0; i < meta.Fields.size(); ++i)
    {
        if (i > 0) sql += " AND ";
        sql += "[" + meta.Fields[i].DataName + "] = ?";
    }
    sql += ";";
    return sql;
}

inline std::string select_sql(const EntityMeta& meta, const std::vector<Condition>& conditions)
{
    std::string sql = "SELECT ";

    for (size_t i = 0; i < meta.Fields.size(); ++i)
    {
        sql += "[" + meta.Fields[i].DataName + "]";
        if (i + 1 < meta.Fields.size())
            sql += ", ";
    }

    sql += " FROM [" + meta.TableName + "]";

    if (!conditions.empty())
    {
        sql += " WHERE ";
        for (size_t i = 0; i < conditions.size(); ++i)
        {
            const auto& c = conditions[i];
            // T-SQL은 "==" 대신 "=" 사용. C++ 스타일 "=="로 들어오면 치환.
            std::string op = c.op;
            if (op == "==" || op.empty())
                op = "=";
            sql += "[" + c.column + "] " + op + " ?";
            if (i + 1 < conditions.size())
                sql += " AND ";
        }
    }

    sql += ";";
    return sql;
}


