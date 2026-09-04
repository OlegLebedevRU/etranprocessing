@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building l4superv and l4install (Leo4 Supervisor Suite)
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
if /i "%TARGET_ARCH%"=="win7" goto :build_x86
if /i "%TARGET_ARCH%"=="win7_x86" goto :build_x86
if /i "%TARGET_ARCH%"=="x64" goto :build_x64
if /i "%TARGET_ARCH%"=="64" goto :build_x64

echo Unknown architecture "%TARGET_ARCH%". Valid options: all, x86, win7, x64
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
echo [Build x86] 32-bit static binaries (Windows 7 SP1+ compatible)...
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && rc.exe /nologo /fo obj\x86\l4superv.res res\l4superv.rc && rc.exe /nologo /fo obj\x86\l4install.res res\l4install.rc && cl.exe /nologo /W4 /O2 /utf-8 /MT /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /Foobj\x86\ src\supervisor_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\proxy_client.c src\orchestrator.c obj\x86\l4superv.res /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\l4superv.exe advapi32.lib crypt32.lib winhttp.lib ws2_32.lib user32.lib shlwapi.lib && cl.exe /nologo /W4 /O2 /utf-8 /MT /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /Foobj\x86\ src\installer_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\miniz.c src\zip_extractor.c obj\x86\l4install.res /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\l4install_x86.exe advapi32.lib crypt32.lib winhttp.lib ws2_32.lib user32.lib shlwapi.lib shell32.lib"
if errorlevel 1 (
    echo [ERROR] x86 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x86 build SUCCESS: bin\x86\l4superv.exe, bin\x86\l4install_x86.exe
    copy /y bin\x86\l4superv.exe bin\l4superv.exe >nul
    copy /y bin\x86\l4install_x86.exe bin\x86\l4install.exe >nul
    copy /y bin\x86\l4install_x86.exe bin\l4install_x86.exe >nul
    copy /y bin\x86\l4install_x86.exe bin\l4install.exe >nul
)
exit /b 0

:do_build_x64
echo.
echo [Build x64] 64-bit static binaries...
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && rc.exe /nologo /fo obj\x64\l4superv.res res\l4superv.rc && rc.exe /nologo /fo obj\x64\l4install.res res\l4install.rc && cl.exe /nologo /W4 /O2 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /Foobj\x64\ src\supervisor_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\proxy_client.c src\orchestrator.c obj\x64\l4superv.res /link /OUT:bin\x64\l4superv.exe advapi32.lib crypt32.lib winhttp.lib ws2_32.lib user32.lib shlwapi.lib && cl.exe /nologo /W4 /O2 /utf-8 /MT /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /Foobj\x64\ src\installer_main.c src\config.c src\hardware_fingerprint.c src\state_mgr.c src\service_mgr.c src\mosquitto_conf.c src\miniz.c src\zip_extractor.c obj\x64\l4install.res /link /OUT:bin\x64\l4install_x64.exe advapi32.lib crypt32.lib winhttp.lib ws2_32.lib user32.lib shlwapi.lib shell32.lib"
if errorlevel 1 (
    echo [ERROR] x64 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x64 build SUCCESS: bin\x64\l4superv.exe, bin\x64\l4install_x64.exe
    copy /y bin\x64\l4install_x64.exe bin\x64\l4install.exe >nul
    copy /y bin\x64\l4install_x64.exe bin\l4install_x64.exe >nul
)
exit /b 0

:summary
echo.
echo =======================================================
if %BUILD_FAILED% equ 0 (
    echo Unified Build COMPLETE:
    if exist bin\x86\l4superv.exe echo   - x86 [32-bit]: bin\x86\l4superv.exe
    if exist bin\x64\l4superv.exe echo   - x64 [64-bit]: bin\x64\l4superv.exe
    if exist bin\l4superv.exe     echo   - Default:      bin\l4superv.exe
    if exist bin\x86\l4install_x86.exe echo   - x86 [32-bit]: bin\x86\l4install_x86.exe
    if exist bin\x64\l4install_x64.exe echo   - x64 [64-bit]: bin\x64\l4install_x64.exe
    if exist bin\l4install_x86.exe     echo   - Root x86:     bin\l4install_x86.exe
    if exist bin\l4install_x64.exe     echo   - Root x64:     bin\l4install_x64.exe

    :: Copy companion scripts and documentation into bin directories
    for %%f in (l4superv_*.cmd l4install_*.cmd pack_*.cmd) do (
        copy /y "%%f" bin\"%%f" >nul
        copy /y "%%f" bin\x86\"%%f" >nul
        copy /y "%%f" bin\x64\"%%f" >nul
    )
    if exist README.md (
        copy /y README.md bin\README.md >nul
        copy /y README.md bin\x86\README.md >nul
        copy /y README.md bin\x64\README.md >nul
    )
    if exist CHANGELOG.md (
        copy /y CHANGELOG.md bin\CHANGELOG.md >nul
        copy /y CHANGELOG.md bin\x86\CHANGELOG.md >nul
        copy /y CHANGELOG.md bin\x64\CHANGELOG.md >nul
    )
    if exist "%~dp0..\..\docs\terminal-tools-user-guide.md" (
        copy /y "%~dp0..\..\docs\terminal-tools-user-guide.md" bin\terminal-tools-user-guide.md >nul
        copy /y "%~dp0..\..\docs\terminal-tools-user-guide.md" bin\x86\terminal-tools-user-guide.md >nul
        copy /y "%~dp0..\..\docs\terminal-tools-user-guide.md" bin\x64\terminal-tools-user-guide.md >nul
    )
) else (
    echo Unified Build FAILED with errors.
)
echo =======================================================
exit /b %BUILD_FAILED%
