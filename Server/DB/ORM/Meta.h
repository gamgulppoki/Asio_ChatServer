#pragma once

#include "Types.h"
#include "Property.h"
#include "PrimaryProperty.h"
#include "Navigation.h"

#include <functional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

// 여기는 개념적인 곳을 만드는 곳. 메타데이터!
struct FieldMeta
{
    TypeTag DataType;
    std::string DataName;

    std::function<DbValue(const void*)> Read;
    std::function<DbValue(const void*)> ReadOrigin;
    std::function<void(void*, const DbValue&)> Write;
    std::function<bool(const void*)> IsDirty;
    std::function<void(void*)> ClearDirty;

    void set(void* obj, DbValue value)
    {
        Write(obj, value);
    }

    DbValue get(const void* obj)
    {
        return Read(obj);
    }

    DbValue get_origin(const void* obj)
    {
        return ReadOrigin(obj);
    }

    bool is_dirty(const void* obj) const
    {
        return IsDirty(obj);
    }

    void clear_dirty(void* obj)
    {
        ClearDirty(obj);
    }
};

struct RelationMeta
{
    std::string Name;               // C++에서의 이름 (변수 이름)
    std::string FKColumnName;       // 이 외래키를 가진 column의 이름
    
    std::string TargetTableName;    // 가리키는 테이블 이름 (상대 테이블의 pk를 무조건 가리킨다고 가정)
    std::type_index TargetType;
    
    // Include 들어온 멤버 포인터 비교용 
    std::function<bool(const void*)> Matches;
    std::function<void(void*, void*)> BindObj;
};

struct EntityMeta
{
    std::string TableName; // 테이블 이름
    std::vector<FieldMeta> Fields; // 엔티티 인스턴스
    std::unordered_map<std::string, size_t> FieldIndex; // 이름 -> 인덱스
    std::string PrimaryKeyName; // PK 필드 이름
    std::vector<RelationMeta> Relations; // 관계 메타 (Navigation 매칭용)

    std::function<void*()> Factory; // 인스턴스 생성
    std::function<void(void*)> Destroyer; // 인스턴스 소멸 (타입별 delete)

    FieldMeta& field(std::string name)
    {
        auto index = FieldIndex.find(name);
        if (index == FieldIndex.end())
        {
            throw std::out_of_range("field not found: " + name);
        }

        return Fields[index->second];
    }

    void* create_instance()
    {
        return Factory();
    }
};



template <typename T>
struct EntityBuilder
{
    EntityBuilder()
    {
        Meta.Factory = []()
        {
            return (void*)new T();
        };
        Meta.Destroyer = [](void* p)
        {
            delete static_cast<T*>(p);
        };
    }

    template <typename M>
    void field(std::string name, Property<M> T::* member)
    {
        FieldMeta tmpMeta;
        tmpMeta.DataName = name;

        if constexpr (std::is_same_v<M, int64>)
            tmpMeta.DataType = TypeTag::INT;
        else if constexpr (std::is_same_v<M, float64>)
            tmpMeta.DataType = TypeTag::DOUBLE;
        else if constexpr (std::is_same_v<M, bool>)
            tmpMeta.DataType = TypeTag::BOOL;
        else if constexpr (std::is_same_v<M, std::string>)
            tmpMeta.DataType = TypeTag::STRING;
        else
            tmpMeta.DataType = TypeTag::NONE;

        tmpMeta.Read = [member](const void* obj)
        {
            const T* m = static_cast<const T*>(obj);
            return DbValue{(m->*member).value()};
        };

        tmpMeta.ReadOrigin = [member](const void* obj)
        {
            const T* m = static_cast<const T*>(obj);
            return DbValue{(m->*member).origin()};
        };

        tmpMeta.Write = [member](void* obj, const DbValue& value)
        {
            T* m = static_cast<T*>(obj);
            (m->*member).init(std::get<M>(value));
        };

        tmpMeta.IsDirty = [member](const void* obj)
        {
            const T* m = static_cast<const T*>(obj);
            return (m->*member).is_dirty();
        };

        tmpMeta.ClearDirty = [member](void* obj)
        {
            T* m = static_cast<T*>(obj);
            (m->*member).clear_dirty();
        };

        Meta.FieldIndex[name] = Meta.Fields.size();
        Meta.Fields.push_back(tmpMeta);
    }

    // PrimaryProperty 오버로드: 같은 람다 등록 + 자동 PK 등록.
    // 사용자가 PrimaryProperty<T> 멤버를 선언하면 codegen 의 b.field 호출이 이쪽으로 매칭됨.
    template <typename M>
    void field(std::string name, PrimaryProperty<M> T::* member)
    {
        FieldMeta tmpMeta;
        tmpMeta.DataName = name;

        if constexpr (std::is_same_v<M, int64>)
            tmpMeta.DataType = TypeTag::INT;
        else if constexpr (std::is_same_v<M, float64>)
            tmpMeta.DataType = TypeTag::DOUBLE;
        else if constexpr (std::is_same_v<M, bool>)
            tmpMeta.DataType = TypeTag::BOOL;
        else if constexpr (std::is_same_v<M, std::string>)
            tmpMeta.DataType = TypeTag::STRING;
        else
            tmpMeta.DataType = TypeTag::NONE;

        tmpMeta.Read = [member](const void* obj)
        {
            const T* m = static_cast<const T*>(obj);
            return DbValue{(m->*member).value()};
        };

        tmpMeta.ReadOrigin = [member](const void* obj)
        {
            const T* m = static_cast<const T*>(obj);
            return DbValue{(m->*member).origin()};
        };

        tmpMeta.Write = [member](void* obj, const DbValue& value)
        {
            T* m = static_cast<T*>(obj);
            (m->*member).init(std::get<M>(value));
        };

        tmpMeta.IsDirty = [member](const void* obj)
        {
            const T* m = static_cast<const T*>(obj);
            return (m->*member).is_dirty();
        };

        tmpMeta.ClearDirty = [member](void* obj)
        {
            T* m = static_cast<T*>(obj);
            (m->*member).clear_dirty();
        };

        Meta.FieldIndex[name] = Meta.Fields.size();
        Meta.Fields.push_back(tmpMeta);

        primary_key(name);   // 자동 PK 등록
    }
    
    template <typename M>
    void navigation(std::string name, std::string fkColumnName, std::string targetTableName, Navigation<M> T::* member)
    {
        RelationMeta relationMeta{name, fkColumnName, targetTableName, typeid(M)};
        relationMeta.Matches = [member](const void* ptr) -> bool
        {
            auto other = static_cast<const Navigation<M> T::* const*>(ptr);
            return *other == member;
        };
        
        // target 타입을 알 수 있기 때문에, 해당 타입으로 avigation 객체에 넣어주면 됨
        relationMeta.BindObj = [member](void* FromObj, void* ToObj)
        {
            T* from = static_cast<T*>(FromObj);
            M* to = static_cast<M*>(ToObj);
            (from->*member).Bind(to);
        };
        
        Meta.Relations.push_back(relationMeta);
    }

    void table(std::string name)
    {
        Meta.TableName = std::move(name);
    }

    void primary_key(std::string name)
    {
        Meta.PrimaryKeyName = std::move(name);
    }

    void Build(EntityMeta& meta)
    {
        meta = std::move(Meta);
    }

public:
    EntityMeta Meta;
};

class MetaRegistry
{
public:
    static MetaRegistry& Instance()
    {
        static MetaRegistry s;
        return s;
    }

    MetaRegistry(const MetaRegistry&) = delete;
    MetaRegistry& operator=(const MetaRegistry&) = delete;
    MetaRegistry(MetaRegistry&&) = delete;
    MetaRegistry& operator=(MetaRegistry&&) = delete;

    std::unordered_map<std::type_index, EntityMeta> Entities;

private:
    MetaRegistry() = default;
};

// 엔티티별 메타 기술 훅. codegen이 특수화를 만들어 넣음.
template <typename T>
void describe_entity(EntityBuilder<T>& builder);

// REGISTRY에 넣기 위한 함수
template <typename T>
EntityMeta& get_entity_meta()
{
    auto& reg = MetaRegistry::Instance();
    std::type_index index = typeid(T);

    if (reg.Entities.find(index) == reg.Entities.end())
    {
        EntityBuilder<T> builder;
        describe_entity<T>(builder);

        reg.Entities[index] = std::move(builder.Meta);
    }

    return reg.Entities[index];
}