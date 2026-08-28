@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building l4con (Leo4 Diagnostic Console Agent)
echo =======================================================

set TARGET_ARCH=%1
if "%TARGET_ARCH%"=="" set TARGET_ARCH=all

if not exist bin md bin
if not exist bin\x86 md bin\x86
if not exist bin\x64 md bin\x64
if not exist obj md obj

:: Check GCC compiler
set GCC_FOUND=0
where gcc.exe >nul 2>nul
if !errorlevel! equ 0 (
    set GCC_FOUND=1
) else (
    if exist "C:\Program Files\JetBrains\CLion 2025.2.4\bin\mingw\bin\gcc.exe" (
        set "PATH=C:\Program Files\JetBrains\CLion 2025.2.4\bin\mingw\bin;!PATH!"
        set GCC_FOUND=1
    )
)

if !GCC_FOUND! equ 1 (
    echo [Toolchain] Using MinGW GCC
    windres.exe -I res res\l4con.rc -o obj\l4con.res.o
    if !errorlevel! neq 0 (
        echo [ERROR] windres failed!
        exit /b 1
    )

    gcc.exe -O2 -static -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -I src -I res -o bin\l4con.exe src\main.c src\config.c src\mqtt_protocol.c src\command_runner.c src\mqtt_client.c src\service_mgr.c obj\l4con.res.o -lws2_32 -lwinhttp -ladvapi32 -luser32
    if !errorlevel! neq 0 (
        echo [ERROR] GCC compilation failed!
        exit /b 1
    )

    copy /y bin\l4con.exe bin\x64\l4con.exe >nul 2>nul
    copy /y bin\l4con.exe bin\x86\l4con.exe >nul 2>nul

    for %%f in (l4con_*.cmd) do (
        copy /y "%%f" bin\"%%f" >nul
        copy /y "%%f" bin\x86\"%%f" >nul
        copy /y "%%f" bin\x64\"%%f" >nul
    )
    if exist README.md (
        copy /y README.md bin\README.md >nul
        copy /y README.md bin\x86\README.md >nul
        copy /y README.md bin\x64\README.md >nul
    )

    echo.
    echo =======================================================
    echo [OK] Build SUCCESS:
    echo   - bin\l4con.exe
    echo   - bin\x64\l4con.exe
    echo   - bin\x86\l4con.exe
    echo =======================================================
    exit /b 0
)

echo [ERROR] No supported compiler found (GCC/MinGW required).
exit /b 1
