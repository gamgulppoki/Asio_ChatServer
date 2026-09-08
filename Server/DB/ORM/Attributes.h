#pragma once

// ORM codegen markers.
// 컴파일러에게는 전부 빈 매크로다. Tools/EntityGenerator/EntityGenerator.py 만 이 표식을 읽어
// EntitiesGenerated.h (describe_entity / Col<T>) 와 CREATE TABLE / CREATE INDEX SQL 을 만든다.
// 덕분에 컴파일러는 평범한 C++ 을 보고, 생성기는 스키마 정보를 본다.

// 엔티티 표식. struct/class 선언 앞에 붙인다.
#define DB_ENTITY

// 외래키. Navigation<T> 멤버 앞에 붙여 FK 컬럼을 지정한다.
#define FK(col)

// 문자열 컬럼 길이 → NVARCHAR(n). 생략 시 NVARCHAR(4000).
// 인덱스를 걸 컬럼은 반드시 지정한다. MSSQL 비클러스터 인덱스 키 상한은 1,700 바이트이고
// NVARCHAR 는 글자당 2바이트라, 4000 그대로 두면 인덱스는 생성돼도 경고가 나고 긴 값에서 INSERT 가 실패한다.
#define LEN(n)

// 단일 컬럼 인덱스 / 유니크 인덱스. Property 멤버 앞에 붙인다.
//   INDEX  LEN(60)  Property<std::string> Nickname;
//   UNIQUE LEN(100) Property<std::string> Email;
#define INDEX
#define UNIQUE

// 복합 인덱스. struct 본문에 문장처럼 쓴다 (빈 매크로라 세미콜론만 남고, 클래스 본문의 빈 선언은 합법).
//   COMPOSITE_UNIQUE(FromUserId, ToUserId);
//   COMPOSITE_INDEX(ToUserId, Status);
#define COMPOSITE_INDEX(...)
#define COMPOSITE_UNIQUE(...)
