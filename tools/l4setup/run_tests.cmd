@echo off
setlocal
cd /d "%~dp0"
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building and Running tools/l4setup Unit Tests
echo =======================================================

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

echo.
echo [1/2] Compiling test_l4setup.exe (x86)...
call "%VS_DEV_CMD%" -arch=x86 -no_logo
cl.exe /nologo /O2 /MT /W4 /wd4100 /wd4127 /wd4244 /wd4702 /wd4706 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /I res /I ..\l4pin\src /I ..\l4superv\src /Foobj\ tests\test_l4setup.c src\cli.c src\log.c src\unpack.c src\summary.c ..\l4pin\src\cert_discovery.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_l4setup.exe kernel32.lib user32.lib shell32.lib advapi32.lib crypt32.lib ncrypt.lib shlwapi.lib
if errorlevel 1 (
    echo [ERROR] Test compilation failed!
    exit /b 1
)

echo.
echo [2/2] Running Unit Tests...
bin\test_l4setup.exe
if errorlevel 1 (
    echo.
    echo [ERROR] Unit tests failed!
    exit /b 1
)

echo.
echo [SUCCESS] All unit tests PASSED successfully!
exit /b 0
