@echo off
pushd %~dp0

REM ============================================
REM Paths
REM ============================================
SET MODEL_DIR=Tools\ModelGenerator\Models
SET GEN_DIR=Tools\ModelGenerator
SET DB_DIR=Server\DB

REM ============================================
REM ModelGenerator: .json -> *Model.h / *Cols.h / *.sql
REM ============================================
pushd %GEN_DIR%
for %%f in (Models\*.json) do (
    python ModelGenerator.py --input=%%f --db-dir=../../%DB_DIR%
    IF ERRORLEVEL 1 (
        echo [ERROR] ModelGenerator failed on %%f
        popd
        EXIT /B 1
    )
)
popd

echo [OK] Model generation complete
popd