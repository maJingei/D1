@echo off
pushd %~dp0

set SOLUTION=%CD%
set PROTOC=%SOLUTION%\Common\protoBuf\bin\protoc.exe
set PROTO_SRC=%SOLUTION%\Common\protoBuf\Protocol.proto
set GEN=%SOLUTION%\Tools\PacketGenerator
set GEN_EXE=%GEN%\GenPacket.exe

echo ============================================
echo  D1Server Packet Generator
echo ============================================

echo [1/3] protoc: Protocol.proto -^> Protocol.pb.h / .cc
%PROTOC% --proto_path=%SOLUTION%\Common\protoBuf --proto_path=%SOLUTION%\Common\protoBuf\include --cpp_out=%SOLUTION%\Common Protocol.proto
if %ERRORLEVEL% NEQ 0 ( echo [ERROR] protoc failed! & popd & exit /b 1 )
echo [OK]

echo [2/3] GenPacket: C_ -^> D1Server\ClientPacketHandler.h
pushd %GEN%
%GEN_EXE% --path %PROTO_SRC% --output %SOLUTION%\D1Server\ClientPacketHandler --recv C_ --send S_
if %ERRORLEVEL% NEQ 0 ( echo [ERROR] Server handler failed! & popd & popd & exit /b 1 )
popd
echo [OK]

echo [3/3] GenPacket: S_ -^> D1ConsoleClient\Network\ServerPacketHandler.h
pushd %GEN%
%GEN_EXE% --path %PROTO_SRC% --output %SOLUTION%\D1ConsoleClient\Network\ServerPacketHandler --recv S_ --send C_
if %ERRORLEVEL% NEQ 0 ( echo [ERROR] Client handler failed! & popd & popd & exit /b 1 )
popd
echo [OK]

echo.
echo Done! All packets generated.
popd