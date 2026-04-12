@echo off
pushd %~dp0

REM ============================================
REM protoc 경로 (vcpkg 설치 후 실제 경로로 수정 필요)
REM ============================================
SET PROTOC=vcpkg_installed\x64-windows-static\x64-windows-static\tools\protobuf\protoc.exe
SET PROTO_DIR=Proto
SET GEN_DIR=Tools\PacketGenerator

REM ============================================
REM 1. protoc: .proto -> .pb.h / .pb.cc
REM ============================================
%PROTOC% -I=%PROTO_DIR% --cpp_out=%PROTO_DIR% %PROTO_DIR%\Protocol.proto
IF ERRORLEVEL 1 (
    echo [ERROR] protoc failed
    EXIT /B 1
)

REM ============================================
REM 2. PacketGenerator: .proto -> PacketHandler.h
REM ============================================
pushd %GEN_DIR%
python PacketGenerator.py --path=../../%PROTO_DIR%/Protocol.proto --output=ClientPacketHandler --recv=C_ --send=S_
python PacketGenerator.py --path=../../%PROTO_DIR%/Protocol.proto --output=ServerPacketHandler --recv=S_ --send=C_
IF ERRORLEVEL 1 (
    echo [ERROR] PacketGenerator failed
    EXIT /B 1
)
popd

REM ============================================
REM 3. 생성 파일 복사
REM ============================================
XCOPY /Y %PROTO_DIR%\Protocol.pb.h Server\Network\ >NUL
XCOPY /Y %PROTO_DIR%\Protocol.pb.cc Server\Network\ >NUL
XCOPY /Y %GEN_DIR%\ClientPacketHandler.h Server\Network\ >NUL

XCOPY /Y %PROTO_DIR%\Protocol.pb.h Client\ >NUL
XCOPY /Y %PROTO_DIR%\Protocol.pb.cc Client\ >NUL
XCOPY /Y %GEN_DIR%\ServerPacketHandler.h Client\ >NUL

REM ============================================
REM 4. 중간 생성물 정리
REM ============================================
DEL /Q /F %PROTO_DIR%\*.pb.h >NUL 2>&1
DEL /Q /F %PROTO_DIR%\*.pb.cc >NUL 2>&1
DEL /Q /F %GEN_DIR%\ClientPacketHandler.h >NUL 2>&1
DEL /Q /F %GEN_DIR%\ServerPacketHandler.h >NUL 2>&1

echo [OK] Packet generation complete