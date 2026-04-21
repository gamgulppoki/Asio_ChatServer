#pragma once

#include "../ORM/Types.h"
#include "../ORM/Attributes.h"
#include "../ORM/Property.h"
#include "../ORM/PrimaryProperty.h"
#include <string>


// 예시용 엔티티 — ORMTest에서 가져온 샘플.
// 실제 사용할 엔티티는 Entities.h 에 선언한다.
// codegen 에서 제외되도록 클래스 전체 주석 처리.
/*
DB_ENTITY
class Person
{
public:
    PrimaryProperty<int64> id;
    Property<std::string> name;
    Property<int64> age;
};
*/