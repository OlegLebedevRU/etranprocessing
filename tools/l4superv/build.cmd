@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo Building l4superv and l4install (Leo4 Supervisor Suite)
echo =======================================================

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
    
    :: Compile Resources
    rc.exe /fo obj\l4superv.res res\l4superv.rc
    if !errorlevel! neq 0 (
        echo [ERROR] rc.exe failed for l4superv.rc!
        exit /b 1
    )

    rc.exe /fo obj\l4install.res res\l4install.rc
    if !errorlevel! neq 0 (
        echo [ERROR] rc.exe failed for l4install.rc!
        exit /b 1
    )

    :: 1. Build l4superv.exe
    echo [Compile] Building l4superv.exe...
    cl.exe /nologo /W4 /O2 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /Isrc /Ires /Fe:bin\l4superv.exe src\supervisor_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\proxy_client.c src\orchestrator.c obj\l4superv.res /link advapi32.lib crypt32.lib winhttp.lib ws2_32.lib user32.lib shlwapi.lib
    if !errorlevel! neq 0 (
        echo [ERROR] MSVC compilation failed for l4superv!
        exit /b 1
    )

    :: 2. Build l4install.exe
    echo [Compile] Building l4install.exe...
    cl.exe /nologo /W4 /O2 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /Isrc /Ires /Fe:bin\l4install.exe src\installer_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\miniz.c src\zip_extractor.c obj\l4install.res /link advapi32.lib crypt32.lib winhttp.lib ws2_32.lib user32.lib shlwapi.lib shell32.lib
    if !errorlevel! neq 0 (
        echo [ERROR] MSVC compilation failed for l4install!
        exit /b 1
    )

    copy /y bin\l4superv.exe bin\x64\l4superv.exe >nul 2>nul
    copy /y bin\l4superv.exe bin\x86\l4superv.exe >nul 2>nul

    copy /y bin\l4install.exe bin\x64\l4install.exe >nul 2>nul
    copy /y bin\l4install.exe bin\x86\l4install.exe >nul 2>nul

    for %%f in (l4superv_*.cmd l4install_*.cmd pack_*.cmd) do (
        copy /y "%%f" bin\"%%f" >nul 2>nul
        copy /y "%%f" bin\x86\"%%f" >nul 2>nul
        copy /y "%%f" bin\x64\"%%f" >nul 2>nul
    )
    if exist README.md (
        copy /y README.md bin\README.md >nul 2>nul
        copy /y README.md bin\x86\README.md >nul 2>nul
        copy /y README.md bin\x64\README.md >nul 2>nul
    )

    echo.
    echo =======================================================
    echo [OK] Build SUCCESS:
    echo   - bin\l4superv.exe
    echo   - bin\l4install.exe
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
    windres.exe -I res res\l4superv.rc -o obj\l4superv.res.o
    windres.exe -I res res\l4install.rc -o obj\l4install.res.o

    gcc.exe -O2 -static -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -I src -I res -o bin\l4superv.exe src\supervisor_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\proxy_client.c src\orchestrator.c obj\l4superv.res.o -ladvapi32 -lcrypt32 -lwinhttp -lws2_32 -luser32 -lshlwapi
    gcc.exe -O2 -static -Wall -Wextra -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -I src -I res -o bin\l4install.exe src\installer_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\miniz.c src\zip_extractor.c obj\l4install.res.o -ladvapi32 -lcrypt32 -lwinhttp -lws2_32 -luser32 -lshlwapi -lshell32

    copy /y bin\l4superv.exe bin\x64\l4superv.exe >nul 2>nul
    copy /y bin\l4superv.exe bin\x86\l4superv.exe >nul 2>nul
    copy /y bin\l4install.exe bin\x64\l4install.exe >nul 2>nul
    copy /y bin\l4install.exe bin\x86\l4install.exe >nul 2>nul

    for %%f in (l4superv_*.cmd l4install_*.cmd pack_*.cmd) do (
        copy /y "%%f" bin\"%%f" >nul 2>nul
        copy /y "%%f" bin\x86\"%%f" >nul 2>nul
        copy /y "%%f" bin\x64\"%%f" >nul 2>nul
    )
    if exist README.md (
        copy /y README.md bin\README.md >nul 2>nul
        copy /y README.md bin\x86\README.md >nul 2>nul
        copy /y README.md bin\x64\README.md >nul 2>nul
    )

    echo.
    echo =======================================================
    echo [OK] Build SUCCESS (MinGW):
    echo   - bin\l4superv.exe
    echo   - bin\l4install.exe
    echo =======================================================
    exit /b 0
)

echo [ERROR] No supported compiler found (MSVC or MinGW GCC required).
exit /b 1
