// DBContext
// 변경된 data들의 값을 담고있는 것
#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "DBConnection.h"
#include "Meta.h"
#include "Sql.h"
#include "Column.h"

enum class DBOp
{
    ADDED,
    MODIFIED,
    DELETED,
    UNCHANGED,
};

struct ChangeEntry
{
    DBOp op;
    std::type_index type;
    void* obj;
    DbValue newPk;   // ADDED 에서 Execute 직후 Fetch 로 받은 생성 PK. 나머지 op 는 미사용.
};

class DBContext
{
    template<typename T>
    class DbSet
    {
    public:
        
        // 소유권을 DBContext 로 이전. 반환된 raw 포인터는 읽기/수정용 (delete 금지).
        T* Add(std::unique_ptr<T> obj)
        {
            T* raw = obj.release();
            context->AddChanges(ChangeEntry{ DBOp::ADDED, typeid(T), raw });
            return raw;
        }
        
        void Delete(T* obj)
        {
            // ADDED 엔트리가 있으면 상쇄 (객체도 파괴). SaveChanges 전 취소.
            if (context->TryCancelAdded(obj, typeid(T)))
                return;

            context->RemoveChanges(obj);
            context->AddChanges(ChangeEntry{ DBOp::DELETED, typeid(T), obj });
        }
        
        void Update(T* obj)
        {
            if (context->HasChangeFor(obj))
                return;
            context->AddChanges(ChangeEntry{ DBOp::MODIFIED, typeid(T), obj });
        }
        
        DbSet<T> Where(Condition condition) const
        {
            DbSet<T> copy = *this;
            
            copy.Conditions.push_back(std::move(condition));
            return copy;
        }

        std::vector<T*> ToList()
        {
            std::vector<T*> results;

            auto* conn = context->GetConnection();
            if (!conn) return results;

            const auto& meta = MetaRegistry::Instance().Entities.at(typeid(T));

            // 1. SELECT SQL 생성
            std::string sql = select_sql(meta, Conditions);

            // 2. WHERE 파라미터 바인딩 (값/lenInd는 Execute까지 살아있어야 함)
            std::vector<DbValue> paramVals;
            std::vector<SQLLEN>  paramLens(Conditions.size(), 0);
            paramVals.reserve(Conditions.size());
            for (const auto& c : Conditions)
                paramVals.push_back(c.value);

            for (size_t i = 0; i < paramVals.size(); ++i)
            {
                std::visit([&](auto& v)
                {
                    using V = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<V, int64>)
                        conn->BindParam(int32(i + 1), &v, &paramLens[i]);
                    else if constexpr (std::is_same_v<V, float64>)
                        conn->BindParam(int32(i + 1), &v, &paramLens[i]);
                    else if constexpr (std::is_same_v<V, bool>)
                        conn->BindParam(int32(i + 1), &v, &paramLens[i]);
                    else if constexpr (std::is_same_v<V, std::string>)
                        conn->BindParam(int32(i + 1), v.c_str(), &paramLens[i]);
                    // monostate(NULL)는 미구현
                }, paramVals[i]);
            }

            // 3. 출력 컬럼용 슬롯 준비 + BindCol
            struct ColumnSlot
            {
                TypeTag tag = TypeTag::NONE;
                int64   i64 = 0;
                float64 f64 = 0.0;
                bool    b   = false;
                char    str[4096] = {};
                SQLLEN  lenInd = 0;
            };
            std::vector<ColumnSlot> slots(meta.Fields.size());

            for (size_t i = 0; i < meta.Fields.size(); ++i)
            {
                auto& s = slots[i];
                s.tag = meta.Fields[i].DataType;
                switch (s.tag)
                {
                case TypeTag::INT:
                    conn->BindCol(int32(i + 1), &s.i64, &s.lenInd);
                    break;
                case TypeTag::DOUBLE:
                    conn->BindCol(int32(i + 1), &s.f64, &s.lenInd);
                    break;
                case TypeTag::BOOL:
                    conn->BindCol(int32(i + 1), &s.b, &s.lenInd);
                    break;
                case TypeTag::STRING:
                    conn->BindCol(int32(i + 1), s.str, int32(sizeof(s.str)), &s.lenInd);
                    break;
                default:
                    break;
                }
            }

            // 4. 실행
            if (!conn->Execute(sql))
            {
                conn->Unbind();
                return results;
            }

            // 5. Fetch + Hydration
            while (conn->Fetch())
            {
                // 5-1. PK 슬롯에서 DbValue 추출
                DbValue pk;
                {
                    size_t pkIdx = meta.FieldIndex.at(meta.PrimaryKeyName);
                    const auto& s = slots[pkIdx];
                    switch (s.tag)
                    {
                    case TypeTag::INT:    pk = DbValue{s.i64}; break;
                    case TypeTag::DOUBLE: pk = DbValue{s.f64}; break;
                    case TypeTag::BOOL:   pk = DbValue{s.b};   break;
                    case TypeTag::STRING: pk = DbValue{std::string(s.str)}; break;
                    default: break;
                    }
                }

                // 5-2. Identity Map 조회. hit 이면 재사용, hydrate 스킵
                if (void* cached = context->FindInIdentityMap(typeid(T), pk))
                {
                    results.push_back(static_cast<T*>(cached));
                    continue;
                }

                // 5-3. miss: 새 객체 만들고 hydrate
                T* obj = static_cast<T*>(meta.Factory());
                for (size_t i = 0; i < meta.Fields.size(); ++i)
                {
                    const auto& f = meta.Fields[i];
                    const auto& s = slots[i];

                    if (s.lenInd == SQL_NULL_DATA)
                        continue;   // NULL 컬럼 스킵

                    switch (s.tag)
                    {
                    case TypeTag::INT:
                        f.Write(obj, DbValue{s.i64});
                        break;
                    case TypeTag::DOUBLE:
                        f.Write(obj, DbValue{s.f64});
                        break;
                    case TypeTag::BOOL:
                        f.Write(obj, DbValue{s.b});
                        break;
                    case TypeTag::STRING:
                        f.Write(obj, DbValue{std::string(s.str)});
                        break;
                    default:
                        break;
                    }
                }

                // 5-4. Map 에 등록 + results 에 push
                // 소유권은 IdentityMap → DBContext 가 잡음. 여기선 delete 금물.
                context->RegisterInIdentityMap(typeid(T), pk, obj);
                results.push_back(obj);
            }

            // 6. 정리
            conn->Unbind();
            return results;
        }
        
    public:
        DBContext* context = nullptr;
        std::vector<Condition> Conditions;
    };
    
public:
    ~DBContext()
    {
        auto& reg = MetaRegistry::Instance();

        // Changes 의 ADDED 는 소유권이 DBContext 로 넘어왔지만 아직 Map 에 없음.
        // Rollback 후 방치 / SaveChanges 미호출 시 누수 방지용.
        for (auto& c : Changes)
        {
            if (c.op == DBOp::ADDED)
                reg.Entities.at(c.type).Destroyer(c.obj);
        }
        Changes.clear();

        for (auto& [key, obj] : IdentityMap)
        {
            const auto& type = std::get<0>(key);
            reg.Entities.at(type).Destroyer(obj);
        }
        IdentityMap.clear();
    }
    
    template<typename T>
    DbSet<T> Set()
    {
        DbSet<T> dbSet;
        dbSet.context = this;
        
        return dbSet;
    }
    
    bool SaveChanges()
    {
        // IdentityMap 스캔: dirty Property 가 하나라도 있으면 MODIFIED 자동 등록.
        // 이미 ADDED/MODIFIED/DELETED 로 들어있는 obj 는 skip.
        for (auto& [key, obj] : IdentityMap)
        {
            if (HasChangeFor(obj)) continue;

            const auto& type = std::get<0>(key);
            auto& meta = MetaRegistry::Instance().Entities.at(type);

            bool anyDirty = false;
            for (const auto& f : meta.Fields)
            {
                if (f.is_dirty(obj)) { anyDirty = true; break; }
            }

            if (anyDirty)
                Changes.push_back(ChangeEntry{ DBOp::MODIFIED, type, obj });
        }

        // 모든 Changes 를 flush → 전부 성공하면 Commit + ApplyChange, 하나라도 실패하면 Rollback
        dbConnection->Begin();

        bool allOk = true;

        for (auto it = Changes.begin(); it != Changes.end(); )
        {
            auto& c = *it;
            std::string sql;
            auto& meta = MetaRegistry::Instance().Entities.at(c.type);
            std::vector<DbValue> values;

            switch(c.op)
            {
            case DBOp::ADDED:
                sql = insert_sql(meta);
                for (auto& f : meta.Fields)
                {
                    if (f.DataName == meta.PrimaryKeyName && f.DataType == TypeTag::INT)
                        continue;   // IDENTITY — DB가 채움
                    values.push_back(f.Read(c.obj));
                }
                break;

            case DBOp::DELETED:
                sql = delete_sql(meta);
                values.push_back(meta.field(meta.PrimaryKeyName).Read(c.obj));
                break;

            case DBOp::MODIFIED:
            {
                std::vector<size_t> modifiedIdx;
                for (size_t i = 0; i < meta.Fields.size(); ++i)
                {
                    const auto& f = meta.Fields[i];
                    if (f.DataName == meta.PrimaryKeyName && f.DataType == TypeTag::INT)
                        continue;   // PK 제외 (IDENTITY)
                    if (f.IsDirty(c.obj)) modifiedIdx.push_back(i);
                }

                // dirty 컬럼이 하나도 없으면 UPDATE 자체가 의미 없음 → 빈 SET 으로 SQL 깨지는 것 방지.
                // 사용자가 명시 ctx.Update(obj) 호출했지만 정작 dirty 필드가 없는 경우 등.
                if (modifiedIdx.empty())
                {
                    ++it;
                    continue;   // for 의 다음 iteration 으로 (switch 안 continue 는 enclosing for 대상)
                }

                sql = update_sql(meta, modifiedIdx);

                // SET 자리표시자: dirty 컬럼의 currentValue
                for (auto idx : modifiedIdx)
                    values.push_back(meta.Fields[idx].Read(c.obj));

                // WHERE 자리표시자: OCC. 모든 컬럼의 originValue (update_sql 의 WHERE 순서와 일치)
                for (auto& f : meta.Fields)
                    values.push_back(f.ReadOrigin(c.obj));

                break;
            }

            default:
                ++it;       // UNCHANGED 등 안전망. ++it 없으면 무한 루프
                continue;
            }

            std::vector<SQLLEN> lenInds(values.size(), 0);

            for (size_t i=0;i<values.size();i++)
            {
                std::visit([&] (auto& args)
                {
                    using T = std::remove_reference_t<decltype(args)>;
                    if constexpr (std::is_same_v<T, std::string>)
                    {
                        dbConnection->BindParam(int32(i+1), args.c_str(), &lenInds[i]);
                    }
                    else if constexpr (std::is_same_v<T, int64>)
                    {
                        dbConnection->BindParam(int32(i+1), &args, &lenInds[i]);
                    }
                    else if constexpr (std::is_same_v<T, float64>)
                    {
                        dbConnection->BindParam(int32(i+1), &args, &lenInds[i]);
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                        dbConnection->BindParam(int32(i+1), &args, &lenInds[i]);

                }, values[i]);
            }

            bool b = dbConnection->Execute(sql);

            if (!b)
            {
                dbConnection->Unbind();
                allOk = false;
                break;
            }
            // occ 오류 잡아주기. update 인 경우에만. 즉시 Changes 에서 빼서 OCCChanges 로 이동.
            else if (c.op == DBOp::MODIFIED && dbConnection->GetRowCount() == 0)
            {
                dbConnection->Unbind();
                OCCChanges.push_back(std::move(*it));
                it = Changes.erase(it);
                allOk = false;
                break;
            }

            // ADDED 면 OUTPUT INSERTED.<pk> 결과를 c.newPk 에 저장만. C++ 반영은 Commit 이후.
            // Fetch 실패 시 newPk = monostate 인 채로 ApplyChange 진입하면 std::get 실패 / IdentityMap 키 충돌.
            // 비정상 상황이므로 트랜잭션 전체 Rollback.
            if (c.op == DBOp::ADDED && meta.field(meta.PrimaryKeyName).DataType == TypeTag::INT)
            {
                int64 newId = 0;
                SQLLEN lenInd = 0;
                dbConnection->BindCol(1, &newId, &lenInd);
                if (dbConnection->Fetch())
                {
                    c.newPk = DbValue{newId};
                }
                else
                {
                    dbConnection->Unbind();
                    allOk = false;
                    break;
                }
            }

            dbConnection->Unbind();
            ++it;
        }

        if (allOk)
        {
            dbConnection->Commit();
            ApplyChange();
        }
        else
        {
            dbConnection->Rollback();
            for (auto& occ : OCCChanges)
            {
                auto& meta = MetaRegistry::Instance().Entities.at(occ.type);

                // 1. occ.obj 의 PK 추출
                DbValue pk = meta.field(meta.PrimaryKeyName).Read(occ.obj);

                // 2. SELECT SQL 생성 (PK 매칭 단건)
                Condition pkCondition;
                pkCondition.column = meta.PrimaryKeyName;
                pkCondition.op     = "=";
                pkCondition.value  = pk;
                std::string sql = select_sql(meta, { pkCondition });

                // 3. WHERE 파라미터 바인딩 (PK 1개)
                SQLLEN pkLen = 0;
                std::visit([&](auto& v)
                {
                    using V = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<V, int64>)
                        dbConnection->BindParam(1, &v, &pkLen);
                    else if constexpr (std::is_same_v<V, float64>)
                        dbConnection->BindParam(1, &v, &pkLen);
                    else if constexpr (std::is_same_v<V, bool>)
                        dbConnection->BindParam(1, &v, &pkLen);
                    else if constexpr (std::is_same_v<V, std::string>)
                        dbConnection->BindParam(1, v.c_str(), &pkLen);
                }, pk);

                // 4. 출력 슬롯 + BindCol
                struct ColumnSlot
                {
                    TypeTag tag = TypeTag::NONE;
                    int64   i64 = 0;
                    float64 f64 = 0.0;
                    bool    b   = false;
                    char    str[4096] = {};
                    SQLLEN  lenInd = 0;
                };
                std::vector<ColumnSlot> slots(meta.Fields.size());

                for (size_t i = 0; i < meta.Fields.size(); ++i)
                {
                    auto& s = slots[i];
                    s.tag = meta.Fields[i].DataType;
                    switch (s.tag)
                    {
                    case TypeTag::INT:
                        dbConnection->BindCol(int32(i + 1), &s.i64, &s.lenInd);
                        break;
                    case TypeTag::DOUBLE:
                        dbConnection->BindCol(int32(i + 1), &s.f64, &s.lenInd);
                        break;
                    case TypeTag::BOOL:
                        dbConnection->BindCol(int32(i + 1), &s.b, &s.lenInd);
                        break;
                    case TypeTag::STRING:
                        dbConnection->BindCol(int32(i + 1), s.str, int32(sizeof(s.str)), &s.lenInd);
                        break;
                    default:
                        break;
                    }
                }

                // 5. Execute + Fetch (단건)
                if (!dbConnection->Execute(sql))
                {
                    dbConnection->Unbind();
                    continue;
                }

                if (dbConnection->Fetch())
                {
                    // 6. 슬롯 → occ.obj 에 Write (FieldMeta::Write 가 init 경유 → currentValue/originValue 둘 다 + dirty=false)
                    for (size_t i = 0; i < meta.Fields.size(); ++i)
                    {
                        const auto& f = meta.Fields[i];
                        const auto& s = slots[i];

                        if (s.lenInd == SQL_NULL_DATA)
                            continue;

                        switch (s.tag)
                        {
                        case TypeTag::INT:
                            f.Write(occ.obj, DbValue{s.i64});
                            break;
                        case TypeTag::DOUBLE:
                            f.Write(occ.obj, DbValue{s.f64});
                            break;
                        case TypeTag::BOOL:
                            f.Write(occ.obj, DbValue{s.b});
                            break;
                        case TypeTag::STRING:
                            f.Write(occ.obj, DbValue{std::string(s.str)});
                            break;
                        default:
                            break;
                        }
                    }
                }

                dbConnection->Unbind();
            }

            OCCChanges.clear();
        }

        return allOk;
    }
    
    void SetDBConnection(DBConnection* conn)
    {
        dbConnection = conn;
    }

    DBConnection* GetConnection() { return dbConnection; }
    
    void AddChanges(ChangeEntry&& changes)
    {
        Changes.push_back(std::move(changes));
    }
    
    void RemoveChanges(void* obj)
    {
        for (auto it = Changes.begin(); it != Changes.end(); )
        {
            if (it->obj == obj)
                it = Changes.erase(it);
            else
                ++it;
        }
    }

    bool HasChangeFor(void* obj) const
    {
        for (const auto& c : Changes)
            if (c.obj == obj) return true;
        return false;
    }

    // ADDED 엔트리가 있으면 전부 제거하고 객체도 파괴. Delete 시 상쇄용.
    bool TryCancelAdded(void* obj, std::type_index type)
    {
        bool found = false;
        for (auto it = Changes.begin(); it != Changes.end(); )
        {
            if (it->obj == obj && it->op == DBOp::ADDED)
            {
                it = Changes.erase(it);
                found = true;
            }
            else ++it;
        }
        if (found)
        {
            auto& meta = MetaRegistry::Instance().Entities.at(type);
            meta.Destroyer(obj);
        }
        return found;
    }

    void* FindInIdentityMap(std::type_index type, DbValue pk)
    {
        auto it = IdentityMap.find({type, pk});
        if (it == IdentityMap.end()) return nullptr;
        return it->second;
    }

    void RegisterInIdentityMap(std::type_index type, DbValue pk, void* obj)
    {
        IdentityMap[{type, pk}] = obj;
    }

private:
    // Commit 이후 호출. Changes 전체를 순회하며 C++ 메모리 상태 반영 + Changes 비움.
    // - ADDED:    c.newPk 를 obj 에 역주입 + IdentityMap 등록 + 모든 Property clear_dirty
    // - MODIFIED: 모든 Property clear_dirty (다음 SaveChanges 에서 다시 dirty 로 안 잡히게)
    // - DELETED:  IdentityMap 에서 빼고 Destroyer 로 객체 해제
    void ApplyChange()
    {
        for (auto& c : Changes)
        {
            auto& meta = MetaRegistry::Instance().Entities.at(c.type);

            if (c.op == DBOp::ADDED)
            {
                meta.field(meta.PrimaryKeyName).Write(c.obj, c.newPk);
                IdentityMap[{ c.type, c.newPk }] = c.obj;

                for (auto& f : meta.Fields)
                    f.clear_dirty(c.obj);
            }
            else if (c.op == DBOp::MODIFIED)
            {
                for (auto& f : meta.Fields)
                    f.clear_dirty(c.obj);
            }
            else if (c.op == DBOp::DELETED)
            {
                DbValue pk = meta.field(meta.PrimaryKeyName).Read(c.obj);
                auto it = IdentityMap.find({ c.type, pk });
                if (it != IdentityMap.end())
                {
                    void* p = it->second;
                    IdentityMap.erase(it);
                    meta.Destroyer(p);
                }
            }
        }
        Changes.clear();
    }

    // 값들을 저장해놓는 변수
    // 복사해서 저장 (안정성)
    std::vector<ChangeEntry> Changes;
    std::vector<ChangeEntry> OCCChanges;
    std::map<std::tuple<std::type_index, DbValue>, void*> IdentityMap;

    DBConnection* dbConnection = nullptr;
};
