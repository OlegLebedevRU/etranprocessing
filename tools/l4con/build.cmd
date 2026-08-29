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

:: 1. Check MSVC
set MSVC_FOUND=0
where cl.exe >nul 2>nul
if !errorlevel! equ 0 (
    set MSVC_FOUND=1
) else (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
        set MSVC_FOUND=1
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
        set MSVC_FOUND=1
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul
        set MSVC_FOUND=1
    )
)

if !MSVC_FOUND! equ 1 (
    echo [Toolchain] Using Microsoft Visual C++ (MSVC)
    rc.exe /fo obj\l4con.res res\l4con.rc
    if !errorlevel! neq 0 (
        echo [ERROR] rc.exe failed!
        exit /b 1
    )

    cl.exe /nologo /W4 /O2 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /Isrc /Ires /Fe:bin\l4con.exe src\main.c src\config.c src\mqtt_protocol.c src\command_runner.c src\mqtt_client.c src\service_mgr.c obj\l4con.res /link ws2_32.lib winhttp.lib advapi32.lib user32.lib
    if !errorlevel! neq 0 (
        echo [ERROR] MSVC compilation failed!
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

:: 2. Check GCC compiler
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

echo [ERROR] No supported compiler found (MSVC or MinGW GCC required).
exit /b 1
