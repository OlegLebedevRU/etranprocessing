@echo off
setlocal

echo =======================================================
echo Building leo4-simple-svc-mqtt (C / MSVC /MT Static - Unified 32/64)
echo =======================================================

set TARGET_ARCH=%1
if "%TARGET_ARCH%"=="" set TARGET_ARCH=all

:: Find MSVC VC Auxiliary Build directory
set "VC_DIR="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build" (
    set "VC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build" (
    set "VC_DIR=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build" (
    set "VC_DIR=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build"
) else if exist "d:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build" (
    set "VC_DIR=d:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build" (
    set "VC_DIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build"
)

if not defined VC_DIR (
    echo Error: MSVC VC Auxiliary Build directory not found in standard paths.
    exit /b 1
)

if not exist bin mkdir bin
if not exist bin\x86 mkdir bin\x86
if not exist bin\x64 mkdir bin\x64
if not exist obj\x86 mkdir obj\x86
if not exist obj\x64 mkdir obj\x64

set BUILD_FAILED=0

if /i "%TARGET_ARCH%"=="all" goto :build_all
if /i "%TARGET_ARCH%"=="x86" goto :build_x86
if /i "%TARGET_ARCH%"=="32" goto :build_x86
if /i "%TARGET_ARCH%"=="win32" goto :build_x86
if /i "%TARGET_ARCH%"=="x64" goto :build_x64
if /i "%TARGET_ARCH%"=="64" goto :build_x64

echo Unknown architecture "%TARGET_ARCH%". Valid options: all, x86, x64
exit /b 1

:build_all
call :do_build_x86
call :do_build_x64
goto :summary

:build_x86
call :do_build_x86
goto :summary

:build_x64
call :do_build_x64
goto :summary

:do_build_x86
echo.
echo [Build x86] 32-bit static binary...
cmd /c ""%VC_DIR%\vcvars32.bat" && rc.exe /nologo /fo obj\x86\leo4_svc_mqtt.res res\leo4_svc_mqtt.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /Foobj\x86\ src\main.c src\config.c src\mqtt_client.c src\service_mgr.c obj\x86\leo4_svc_mqtt.res /link /OUT:bin\x86\leo4-simple-svc-mqtt.exe ws2_32.lib winhttp.lib advapi32.lib user32.lib"
if errorlevel 1 (
    echo [ERROR] x86 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x86 build SUCCESS: bin\x86\leo4-simple-svc-mqtt.exe
    copy /y bin\x86\leo4-simple-svc-mqtt.exe bin\leo4-simple-svc-mqtt.exe >nul
)
exit /b 0

:do_build_x64
echo.
echo [Build x64] 64-bit static binary...
cmd /c ""%VC_DIR%\vcvars64.bat" && rc.exe /nologo /fo obj\x64\leo4_svc_mqtt.res res\leo4_svc_mqtt.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /Foobj\x64\ src\main.c src\config.c src\mqtt_client.c src\service_mgr.c obj\x64\leo4_svc_mqtt.res /link /OUT:bin\x64\leo4-simple-svc-mqtt.exe ws2_32.lib winhttp.lib advapi32.lib user32.lib"
if errorlevel 1 (
    echo [ERROR] x64 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x64 build SUCCESS: bin\x64\leo4-simple-svc-mqtt.exe
)
exit /b 0

:summary
echo.
echo =======================================================
if %BUILD_FAILED% equ 0 (
    echo Unified Build COMPLETE:
    if exist bin\x86\leo4-simple-svc-mqtt.exe echo   - x86 [32-bit]: bin\x86\leo4-simple-svc-mqtt.exe
    if exist bin\x64\leo4-simple-svc-mqtt.exe echo   - x64 [64-bit]: bin\x64\leo4-simple-svc-mqtt.exe
    if exist bin\leo4-simple-svc-mqtt.exe     echo   - Default:      bin\leo4-simple-svc-mqtt.exe

    :: Copy companion scripts and documentation into bin directories
    for %%f in (leo4_svc_mqtt_*.cmd) do (
        copy /y "%%f" bin\"%%f" >nul
        copy /y "%%f" bin\x86\"%%f" >nul
        copy /y "%%f" bin\x64\"%%f" >nul
    )
    if exist README.md (
        copy /y README.md bin\README.md >nul
        copy /y README.md bin\x86\README.md >nul
        copy /y README.md bin\x64\README.md >nul
    )
) else (
    echo Unified Build FAILED with errors.
)
echo =======================================================
exit /b %BUILD_FAILED%
