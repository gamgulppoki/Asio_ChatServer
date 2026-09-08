#pragma once

#include "Meta.h"
#include "Condition.h"
#include <string>
#include <vector>

#include "IncludeEntry.h"

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

// 필드 단위 타입. 문자열은 LEN(n) 표식으로 지정한 길이를 쓴다 (없으면 4000).
inline std::string to_sql_type(const FieldMeta& f)
{
    if (f.DataType == TypeTag::STRING)
        return "NVARCHAR(" + std::to_string(f.MaxLen > 0 ? f.MaxLen : 4000) + ")";
    return to_sql_type(f.DataType);
}

// 테이블 힌트 상수. DbSet::WithHint() 에 넘겨 FROM [T] t0 WITH (...) 로 나간다.
// 이 프로젝트가 다루는 힌트는 두 가지뿐이다:
//   NoLock  — 락을 걸지도 기다리지도 않는다. 커밋 안 된 행을 읽을 수 있다(dirty read).
//             방 리스트·친구 목록처럼 잠깐 틀려도 되는 조회에만. 잔고·정산에는 금지.
//   UpdLock — 읽는 시점에 갱신 락을 잡아 트랜잭션 끝까지 남이 못 고치게 한다(비관적).
//             잔고처럼 충돌이 잦고 실패 비용이 큰 갱신용. 반드시 BeginTransaction() 뒤에 써야
//             락이 Commit 까지 유지된다 (autocommit 이면 문장 끝에서 풀린다).
namespace Hint
{
    inline constexpr const char* NoLock  = "NOLOCK";
    inline constexpr const char* UpdLock = "UPDLOCK, ROWLOCK";
}

inline std::string create_table_sql(const EntityMeta& meta)
{
    std::string sql;
    sql += "IF OBJECT_ID(N'[dbo].[" + meta.TableName + "]', N'U') IS NULL\n";
    sql += "CREATE TABLE [" + meta.TableName + "] (\n";

    for (size_t i = 0; i < meta.Fields.size(); ++i)
    {
        const auto& f = meta.Fields[i];
        sql += "    [" + f.DataName + "] " + to_sql_type(f);

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

// 컬럼별 기본값. 기존 행이 있는 테이블에 NOT NULL 컬럼을 추가할 때 필요하다.
inline std::string default_literal(TypeTag tag)
{
    switch (tag)
    {
        case TypeTag::INT:    return "0";
        case TypeTag::DOUBLE: return "0";
        case TypeTag::BOOL:   return "0";
        case TypeTag::STRING: return "N''";
        default:              return "NULL";
    }
}

// 스키마 마이그레이션 (추가 전용).
// 엔티티에 필드가 추가됐는데 테이블에는 컬럼이 없으면 ALTER TABLE ADD 로 채운다.
// 데이터를 보존해야 하는 원장 테이블에서 "테이블 드롭 후 재생성" 은 선택지가 아니기 때문.
// 컬럼 삭제/타입 변경은 다루지 않는다 (데이터 손실 위험이 있어 자동화 대상에서 제외).
inline std::vector<std::string> add_missing_columns_sql(const EntityMeta& meta)
{
    std::vector<std::string> statements;
    for (const auto& f : meta.Fields)
    {
        if (f.DataName == meta.PrimaryKeyName)
            continue;

        std::string sql;
        sql += "IF COL_LENGTH(N'[dbo].[" + meta.TableName + "]', N'" + f.DataName + "') IS NULL\n";
        sql += "    ALTER TABLE [" + meta.TableName + "] ADD [" + f.DataName + "] "
             + to_sql_type(f) + " NOT NULL DEFAULT " + default_literal(f.DataType) + ";";
        statements.push_back(std::move(sql));
    }
    return statements;
}

// 인덱스 생성 (없을 때만). 엔티티의 INDEX / UNIQUE / COMPOSITE_* 표식이 여기로 온다.
// 인덱스 정의 변경(컬럼 추가 등)은 이름이 같으면 감지하지 못한다 — 이름을 바꾸거나 수동으로 DROP 한다.
inline std::vector<std::string> create_indexes_sql(const EntityMeta& meta)
{
    std::vector<std::string> statements;
    for (const auto& ix : meta.Indexes)
    {
        std::string cols;
        for (size_t i = 0; i < ix.Columns.size(); ++i)
        {
            if (i > 0) cols += ", ";
            cols += "[" + ix.Columns[i] + "]";
        }

        std::string sql;
        sql += "IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = N'" + ix.Name
             + "' AND object_id = OBJECT_ID(N'[dbo].[" + meta.TableName + "]'))\n";
        sql += "    CREATE " + std::string(ix.Unique ? "UNIQUE " : "") + "NONCLUSTERED INDEX [" + ix.Name
             + "] ON [" + meta.TableName + "] (" + cols + ");";
        statements.push_back(std::move(sql));
    }
    return statements;
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

// includes 비어있으면 단일 테이블 SELECT, 있으면 JOIN 자동 포함.
// alias 규칙: 메인은 t0, Include 는 순번대로 t1, t2, ... (같은 타겟 테이블 중복 조인 시 구분용)
// tableHint: 메인 테이블(t0) 에 붙는 WITH (...) 힌트. 비어 있으면 힌트 없음. Hint::NoLock / Hint::UpdLock 참고.
inline std::string select_sql(const EntityMeta& meta,
                              const std::vector<Condition>& conditions,
                              const std::vector<IncludeEntry>& includes = {},
                              const std::string& tableHint = "")
{
    std::string sql = "SELECT ";

    // [1] SELECT 절 — 메인 테이블 컬럼 (t0.*)
    for (size_t i = 0; i < meta.Fields.size(); ++i)
    {
        if (i > 0) sql += ", ";
        sql += "t0.[" + meta.Fields[i].DataName + "]";
    }

    // [2] SELECT 절 — 각 Include 의 타겟 테이블 컬럼 (t1.*, t2.*, ...)
    for (size_t idx = 0; idx < includes.size(); ++idx)
    {
        const auto& rel = *includes[idx].Relation;
        const auto& targetMeta = MetaRegistry::Instance().Entities.at(rel.TargetType);
        std::string alias = "t" + std::to_string(idx + 1);
        for (const auto& f : targetMeta.Fields)
            sql += ", " + alias + ".[" + f.DataName + "]";
    }

    // [3] FROM 절 (+ 테이블 힌트)
    sql += " FROM [" + meta.TableName + "] t0";
    if (!tableHint.empty())
        sql += " WITH (" + tableHint + ")";

    // [4] JOIN 절 — Include 마다 한 줄씩. ON 은 FK = 타겟 PK
    for (size_t idx = 0; idx < includes.size(); ++idx)
    {
        const auto& rel = *includes[idx].Relation;
        const auto& targetMeta = MetaRegistry::Instance().Entities.at(rel.TargetType);
        std::string alias = "t" + std::to_string(idx + 1);
        sql += " JOIN [" + rel.TargetTableName + "] " + alias
             + " ON t0.[" + rel.FKColumnName + "]"
             + " = " + alias + ".[" + targetMeta.PrimaryKeyName + "]";
    }

    // [5] WHERE 절 — conditions 의 컬럼은 메인 테이블 기준이므로 t0. 접두
    if (!conditions.empty())
    {
        sql += " WHERE ";
        for (size_t i = 0; i < conditions.size(); ++i)
        {
            if (i > 0) sql += " AND ";
            const auto& c = conditions[i];
            // T-SQL은 "==" 대신 "=" 사용. C++ 스타일 "=="로 들어오면 치환.
            std::string op = c.op;
            if (op == "==" || op.empty())
                op = "=";
            sql += "t0.[" + c.column + "] " + op + " ?";
        }
    }

    sql += ";";
    return sql;
}