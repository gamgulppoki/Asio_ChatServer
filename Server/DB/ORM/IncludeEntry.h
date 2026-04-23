#pragma once

#include "Meta.h"

// Include() 호출 시 DbSet 에 쌓이는 엔트리.
// 어떤 RelationMeta 와 매칭됐는지 가리키기만 함. 소유권 없음 — 원본은 EntityMeta.Relations 에 있음.
struct IncludeEntry
{
    const RelationMeta* Relation = nullptr;
};