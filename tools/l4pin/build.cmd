@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building l4pin (C / MSVC /MT Static - Unified x86/x64)
echo =======================================================

set TARGET_ARCH=%1
if "%TARGET_ARCH%"=="" set TARGET_ARCH=all

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
echo [Build x86] 32-bit universal static binary (compatible with x86 and x64 via WOW64)...
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && rc.exe /nologo /fo obj\x86\app.res res\app.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\x86\ src\main.c src\url_finder.c src\http_client.c src\xml_utils.c src\cng_crypto.c src\cert_store.c obj\x86\app.res /link /OUT:bin\x86\l4pin.exe ncrypt.lib crypt32.lib winhttp.lib advapi32.lib shell32.lib user32.lib"
if errorlevel 1 (
    echo [ERROR] x86 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x86 build SUCCESS: bin\x86\l4pin.exe
    copy /y bin\x86\l4pin.exe bin\l4pin.exe >nul
    echo [OK] Copied universal x86 binary to: bin\l4pin.exe
)
exit /b 0

:do_build_x64
echo.
echo [Build x64] 64-bit static binary...
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && rc.exe /nologo /fo obj\x64\app.res res\app.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\x64\ src\main.c src\url_finder.c src\http_client.c src\xml_utils.c src\cng_crypto.c src\cert_store.c obj\x64\app.res /link /OUT:bin\x64\l4pin.exe ncrypt.lib crypt32.lib winhttp.lib advapi32.lib shell32.lib user32.lib"
if errorlevel 1 (
    echo [ERROR] x64 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x64 build SUCCESS: bin\x64\l4pin.exe
)
exit /b 0

:summary
echo.
echo =======================================================
if %BUILD_FAILED% equ 0 (
    echo Unified Build COMPLETE:
    if exist bin\x86\l4pin.exe echo   - x86 [32-bit universal]: bin\x86\l4pin.exe
    if exist bin\x64\l4pin.exe echo   - x64 [64-bit]:           bin\x64\l4pin.exe
    if exist bin\l4pin.exe     echo   - Universal default:      bin\l4pin.exe

    :: Copy companion scripts and documentation into bin directories
    if exist install_cert.cmd (
        copy /y install_cert.cmd bin\install_cert.cmd >nul
        copy /y install_cert.cmd bin\x86\install_cert.cmd >nul
        copy /y install_cert.cmd bin\x64\install_cert.cmd >nul
    )
    if exist USER_GUIDE.md (
        copy /y USER_GUIDE.md bin\USER_GUIDE.md >nul
        copy /y USER_GUIDE.md bin\x86\USER_GUIDE.md >nul
        copy /y USER_GUIDE.md bin\x64\USER_GUIDE.md >nul
    )
    if exist CHANGELOG.md (
        copy /y CHANGELOG.md bin\CHANGELOG.md >nul
        copy /y CHANGELOG.md bin\x86\CHANGELOG.md >nul
        copy /y CHANGELOG.md bin\x64\CHANGELOG.md >nul
    )
) else (
    echo Unified Build FAILED with errors.
)
echo =======================================================
exit /b %BUILD_FAILED%
