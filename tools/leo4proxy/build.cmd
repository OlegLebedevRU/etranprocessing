@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building leo4proxy (C / MSVC /MT Static)
echo =======================================================

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not exist %VCVARS% (
    set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist %VCVARS% (
    set VCVARS="d:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)

if not exist %VCVARS% (
    echo Error: vcvars64.bat not found in standard paths.
    exit /b 1
)

call %VCVARS%

if not exist bin mkdir bin
if not exist obj mkdir obj

cl.exe /nologo /O2 /MT /W4 /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ src\main.c src\cert_store.c src\schannel_tls.c src\mqtt_proxy.c src\http_proxy.c src\service_mgr.c /link /OUT:bin\leo4proxy.exe ws2_32.lib crypt32.lib ncrypt.lib secur32.lib advapi32.lib

if %ERRORLEVEL% equ 0 (
    echo =======================================================
    echo Build SUCCESS: bin\leo4proxy.exe
    echo =======================================================
) else (
    echo =======================================================
    echo Build FAILED with error code %ERRORLEVEL%
    echo =======================================================
    exit /b %ERRORLEVEL%
)
