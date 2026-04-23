#pragma once

// Layer 7 Relationship 의 객체 캐싱 슬롯.
// Include 된 쿼리에서 관련 엔티티가 hydrate 되면, Identity Map 의 객체 포인터가
// Bind 를 통해 꽂힘. DB 매핑 없음 — FK 값은 별도 Property 가 보유.
// 소유권 없음 — 포인터가 가리키는 객체는 Identity Map 이 소유.
class DBContext;
template<typename> struct EntityBuilder;   // forward declare (navigation() 등록 시 Bind 람다 캡처)

template<typename T>
class Navigation
{
public:
    T*   Get()      const { return LoadedPtr; }
    bool IsLoaded() const { return LoadedPtr != nullptr; }

private:
    friend class DBContext;
    template<typename> friend struct EntityBuilder;

    void Bind(T* Ptr) { LoadedPtr = Ptr; }

    T* LoadedPtr = nullptr;
};