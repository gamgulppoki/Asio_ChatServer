@echo off
pushd %~dp0

REM ============================================
REM EntityGenerator: Server\DB\Entities\*.h -> Server\DB\Generated\EntitiesGenerated.h
REM   DB_ENTITY 마커가 붙은 struct 를 스캔해 describe_entity / Col<T> 특수화를 생성한다.
REM   엔티티 필드를 추가/삭제했으면 반드시 실행 (빌드에 자동 연결되어 있지 않음).
REM ============================================
python Tools\EntityGenerator\EntityGenerator.py Server\DB\Entities Server\DB\Generated
IF ERRORLEVEL 1 (
    echo [ERROR] EntityGenerator failed
    popd
    EXIT /B 1
)

echo [OK] Entity generation complete
popd
