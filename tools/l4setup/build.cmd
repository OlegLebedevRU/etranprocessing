@echo off
setlocal
cd /d "%~dp0"
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building l4setup (C / MSVC /MT Static - Unified x86/x64, universal x86 installer)
echo =======================================================

set TARGET_CMD=%1

:: Find MSVC VsDevCmd directory
set "VS_DEV_CMD="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
) else if exist "d:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=d:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
)

if not defined VS_DEV_CMD (
    echo Error: MSVC VsDevCmd.bat not found in standard paths.
    exit /b 1
)

if not exist bin mkdir bin
if not exist obj mkdir obj
if /i "%TARGET_CMD%"=="test" goto :run_tests
if /i "%TARGET_CMD%"=="tests" goto :run_tests
if "%TARGET_CMD%"=="" set "TARGET_CMD=all"
set "RC_PAYLOAD_FLAGS="
if exist "%~dp0res\payload_x86.bin" set "RC_PAYLOAD_FLAGS=/d EMBED_PAYLOAD_X86"
if exist "%~dp0res\payload_x64.bin" set "RC_PAYLOAD_FLAGS=%RC_PAYLOAD_FLAGS% /d EMBED_PAYLOAD_X64"
set "SOURCES=src\main.c src\cli.c src\log.c src\uac.c src\preflight.c src\drainage.c src\unpack.c src\services.c src\cert_phase.c src\smoke.c src\proxy_probe.c ..\l4pin\src\http_client.c ..\leo4proxy\src\policy_json.c src\summary.c src\engine.c src\ui.c ..\l4pin\src\cert_discovery.c ..\l4superv\src\hardware_fingerprint.c ..\l4superv\src\miniz.c"
if /i "%TARGET_CMD%"=="x64" goto :only_x64
if /i not "%TARGET_CMD%"=="all" if /i not "%TARGET_CMD%"=="x86" exit /b 1
call :build x86
if errorlevel 1 exit /b 1
copy /y bin\x86\l4setup.exe bin\l4setup.exe >nul
if errorlevel 1 exit /b 1
if /i "%TARGET_CMD%"=="x86" exit /b 0
:only_x64
call :build x64
exit /b %errorlevel%
:build
if not exist bin\%1 mkdir bin\%1
if not exist obj\%1 mkdir obj\%1
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && rc.exe /nologo %RC_PAYLOAD_FLAGS% /i res /i src /fo obj\%1\l4setup.res res\l4setup.rc && cl.exe /nologo /O2 /MT /W4 /wd4100 /wd4127 /wd4244 /wd4702 /wd4706 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /I res /I ..\l4pin\src /I ..\l4superv\src /Foobj\%1\ %SOURCES% obj\%1\l4setup.res /link /SUBSYSTEM:WINDOWS,6.01 /OUT:bin\%1\l4setup.exe kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib crypt32.lib ncrypt.lib winhttp.lib ws2_32.lib iphlpapi.lib shlwapi.lib wtsapi32.lib ole32.lib comctl32.lib version.lib"
exit /b %errorlevel%
:run_tests
call run_tests.cmd
exit /b %errorlevel%
