@echo off
cd /d "%~dp0"
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building leo4proxy (C / MSVC /MT Static - Unified 32/64)
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
set "DO_X64_NEXT=1"
goto :do_build_x86

:build_x86
set "DO_X64_NEXT=0"
goto :do_build_x86

:build_x64
goto :do_build_x64

:do_build_x86
echo.
echo [Build x86] 32-bit static binary (Windows 7 SP1+ compatible)...
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && rc.exe /nologo /fo obj\x86\leo4proxy.res res\leo4proxy.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /I src /Foobj\x86\ src\main.c src\policy.c src\policy_json.c src\cert_store.c src\endpoints.c src\schannel_tls.c src\upstream_probe.c src\credential_lifetime.c src\mqtt_proxy.c src\stream_proxy.c src\rtp_tunnel.c src\http_proxy.c src\reverse_proxy.c src\discovery.c src\firewall.c src\service_mgr.c src\tray_icon.c obj\x86\leo4proxy.res /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\leo4proxy.exe dnsapi.lib ws2_32.lib crypt32.lib ncrypt.lib secur32.lib advapi32.lib shell32.lib user32.lib gdi32.lib iphlpapi.lib winhttp.lib"
if errorlevel 1 (
    echo [ERROR] x86 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x86 build SUCCESS: bin\x86\leo4proxy.exe
    copy /y bin\x86\leo4proxy.exe bin\leo4proxy.exe >nul
)
if "%DO_X64_NEXT%"=="1" goto :do_build_x64
goto :summary

:do_build_x64
echo.
echo [Build x64] 64-bit static binary...
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && rc.exe /nologo /fo obj\x64\leo4proxy.res res\leo4proxy.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /I src /Foobj\x64\ src\main.c src\policy.c src\policy_json.c src\cert_store.c src\endpoints.c src\schannel_tls.c src\upstream_probe.c src\credential_lifetime.c src\mqtt_proxy.c src\stream_proxy.c src\rtp_tunnel.c src\http_proxy.c src\reverse_proxy.c src\discovery.c src\firewall.c src\service_mgr.c src\tray_icon.c obj\x64\leo4proxy.res /link /OUT:bin\x64\leo4proxy.exe dnsapi.lib ws2_32.lib crypt32.lib ncrypt.lib secur32.lib advapi32.lib shell32.lib user32.lib gdi32.lib iphlpapi.lib winhttp.lib"
if errorlevel 1 (
    echo [ERROR] x64 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x64 build SUCCESS: bin\x64\leo4proxy.exe
)
goto :summary

:summary
if %BUILD_FAILED% equ 0 if /i "%TARGET_ARCH%"=="all" (
    call tests\test_stabilization.cmd
    if errorlevel 1 set BUILD_FAILED=1
    call tests\test_endpoints.cmd
    if errorlevel 1 set BUILD_FAILED=1
)
echo.
echo =======================================================
if %BUILD_FAILED% equ 0 (
    echo Unified Build COMPLETE:
    if exist bin\x86\leo4proxy.exe echo   - x86 [32-bit]: bin\x86\leo4proxy.exe
    if exist bin\x64\leo4proxy.exe echo   - x64 [64-bit]: bin\x64\leo4proxy.exe
    if exist bin\leo4proxy.exe     echo   - Default:      bin\leo4proxy.exe

    :: Copy companion scripts and documentation into bin directories
    for %%f in (leo4proxy_*.cmd) do (
        copy /y "%%f" bin\"%%f" >nul
        copy /y "%%f" bin\x86\"%%f" >nul
        copy /y "%%f" bin\x64\"%%f" >nul
    )
    if exist USER_GUIDE.md (
        powershell.exe -NoProfile -Command "$ErrorActionPreference='Stop'; $guide=[IO.File]::ReadAllText('USER_GUIDE.md'); $encoding=New-Object Text.UTF8Encoding($false); [IO.File]::WriteAllText('bin\USER_GUIDE.md',$guide.Replace('../../docs/','../../../docs/').Replace('(README.md)','(../README.md)'),$encoding); foreach($guideArch in @('x86','x64')) { [IO.File]::WriteAllText(('bin\'+$guideArch+'\USER_GUIDE.md'),$guide.Replace('../../docs/','../../../../docs/').Replace('(README.md)','(../../README.md)'),$encoding) }"
        if errorlevel 1 set BUILD_FAILED=1
    )
) else (
    echo Unified Build FAILED with errors.
)
echo =======================================================
exit /b %BUILD_FAILED%
