@echo off
cd /d "%~dp0"
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building l4con (C / MSVC /MT Static - Unified 32/64)
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
echo [Build x86] 32-bit static binary (Windows 7 SP1+ compatible)...
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && rc.exe /nologo /fo obj\x86\l4con.res res\l4con.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /I ..\l4pin\src /Foobj\x86\ src\main.c src\config.c ..\leo4proxy\src\policy_json.c ..\l4pin\src\http_client.c src\mqtt_protocol.c src\link_probe.c ..\l4common\probe_ipc.c ..\l4common\update_state.c ..\l4common\journal.c src\command_runner.c ..\l4common\layout.c ..\l4common\access.c src\mqtt_client.c src\rpc_contract.c src\update_event.c src\update_status.c src\update_reporting.c src\update_admission.c ..\l4common\remote_result.c ..\l4common\remote_outcome.c ..\l4common\remote_launch_failure.c ..\l4common\recovery_plan.c ..\l4common\communication_plan.c ..\l4common\communication_recovery.c ..\l4common\remote_status.c ..\l4common\remote_request.c ..\l4common\remote_host.c ..\l4common\active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_codec.c ..\l4common\journal_reader.c ..\l4common\route_plan.c ..\l4common\catalog.c ..\l4common\catalog_floor.c ..\l4common\metadata.c src\file_manager.c src\fm_process.c src\event_ipc.c src\tool_inventory.c src\service_mgr.c ..\l4pin\src\cert_discovery.c obj\x86\l4con.res /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\l4con.exe ws2_32.lib winhttp.lib advapi32.lib user32.lib shlwapi.lib ole32.lib shell32.lib crypt32.lib ncrypt.lib bcrypt.lib version.lib"
if errorlevel 1 (
    echo [ERROR] x86 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x86 build SUCCESS: bin\x86\l4con.exe
    copy /y bin\x86\l4con.exe bin\l4con.exe >nul
)
exit /b 0

:do_build_x64
echo.
echo [Build x64] 64-bit static binary...
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && rc.exe /nologo /fo obj\x64\l4con.res res\l4con.rc && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /I src /I res /I ..\l4pin\src /Foobj\x64\ src\main.c src\config.c ..\leo4proxy\src\policy_json.c ..\l4pin\src\http_client.c src\mqtt_protocol.c src\link_probe.c ..\l4common\probe_ipc.c ..\l4common\update_state.c ..\l4common\journal.c src\command_runner.c ..\l4common\layout.c ..\l4common\access.c src\mqtt_client.c src\rpc_contract.c src\update_event.c src\update_status.c src\update_reporting.c src\update_admission.c ..\l4common\remote_result.c ..\l4common\remote_outcome.c ..\l4common\remote_launch_failure.c ..\l4common\recovery_plan.c ..\l4common\communication_plan.c ..\l4common\communication_recovery.c ..\l4common\remote_status.c ..\l4common\remote_request.c ..\l4common\remote_host.c ..\l4common\active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_codec.c ..\l4common\journal_reader.c ..\l4common\route_plan.c ..\l4common\catalog.c ..\l4common\catalog_floor.c ..\l4common\metadata.c src\file_manager.c src\fm_process.c src\event_ipc.c src\tool_inventory.c src\service_mgr.c ..\l4pin\src\cert_discovery.c obj\x64\l4con.res /link /OUT:bin\x64\l4con.exe ws2_32.lib winhttp.lib advapi32.lib user32.lib shlwapi.lib ole32.lib shell32.lib crypt32.lib ncrypt.lib bcrypt.lib version.lib"
if errorlevel 1 (
    echo [ERROR] x64 build failed!
    set BUILD_FAILED=1
) else (
    echo [OK] x64 build SUCCESS: bin\x64\l4con.exe
)
exit /b 0

:summary
if %BUILD_FAILED% neq 0 goto :print_summary
if /i "%TARGET_ARCH%"=="x64" goto :only_test_x64
if /i "%TARGET_ARCH%"=="64" goto :only_test_x64
call :do_test_x86
if /i "%TARGET_ARCH%"=="all" call :do_test_x64
goto :print_summary

:only_test_x64
call :do_test_x64

:print_summary
echo.
echo =======================================================
if %BUILD_FAILED% equ 0 (
    echo Unified Build COMPLETE:
    if exist bin\x86\l4con.exe echo   - x86 [32-bit]: bin\x86\l4con.exe
    if exist bin\x64\l4con.exe echo   - x64 [64-bit]: bin\x64\l4con.exe
    if exist bin\l4con.exe     echo   - Default:      bin\l4con.exe

    :: Copy companion scripts and documentation into bin directories
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

:do_test_x86
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && cl.exe /nologo /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /Foobj\x86\ tests\test_mqtt5_protocol.c src\mqtt_protocol.c /link /OUT:obj\x86\test_mqtt5_protocol.exe && obj\x86\test_mqtt5_protocol.exe"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
call :test_discovery x86
call :test_rpc x86
call :test_update_event x86
call :test_update_reporting x86
call :test_update_admission x86
call :test_active_updater x86
call :test_fm_user x86
call :test_link_evidence x86
call :test_fm x86
call :test_fm_process x86
call :test_update_guard x86
call :test_update_state x86
exit /b 0

:do_test_x64
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && cl.exe /nologo /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /Foobj\x64\ tests\test_mqtt5_protocol.c src\mqtt_protocol.c /link /OUT:obj\x64\test_mqtt5_protocol.exe && obj\x64\test_mqtt5_protocol.exe"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
call :test_discovery x64
call :test_rpc x64
call :test_update_event x64
call :test_update_reporting x64
call :test_update_admission x64
call :test_active_updater x64
call :test_fm_user x64
call :test_link_evidence x64
call :test_fm x64
call :test_fm_process x64
call :test_update_guard x64
call :test_update_state x64
exit /b 0

:test_discovery
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_proxy_discovery.c src\config.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_proxy_discovery.exe winhttp.lib"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_proxy_discovery.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_rpc
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_rpc_contract.c src\rpc_contract.c src\mqtt_protocol.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_rpc_contract.exe"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_rpc_contract.exe "..\..\docs\contracts\rpc7xxx-gate1.json"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_fm
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /I src /Foobj\%1\ tests\test_file_manager.c ..\l4common\update_state_store.c ..\l4common\update_state.c ..\l4common\journal.c ..\l4common\layout.c ..\l4common\access.c src\rpc_contract.c src\mqtt_protocol.c ..\leo4proxy\src\policy_json.c ..\l4pin\src\cert_discovery.c /link /OUT:obj\%1\test_file_manager.exe winhttp.lib crypt32.lib bcrypt.lib advapi32.lib ole32.lib shlwapi.lib shell32.lib"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_file_manager.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_fm_process
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /I src /Foobj\%1\ tests\test_fm_process.c ..\l4common\update_state.c ..\l4common\journal.c ..\l4common\layout.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_fm_process.exe shell32.lib advapi32.lib bcrypt.lib ole32.lib"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_fm_process.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_update_guard
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Foobj\%1\ ..\l4common\tests\test_update_guard.c ..\l4common\update_guard.c /link /OUT:obj\%1\test_update_guard.exe"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_update_guard.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_update_state
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /Foobj\%1\ ..\l4common\tests\test_update_state.c ..\l4common\update_state.c ..\l4common\journal.c ..\l4common\layout.c /link /OUT:obj\%1\test_update_state.exe advapi32.lib bcrypt.lib ole32.lib shell32.lib"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_update_state.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_update_event
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_update_event.c src\update_event.c src\update_status.c ..\l4common\remote_result.c ..\l4common\remote_outcome.c ..\l4common\remote_launch_failure.c src\rpc_contract.c src\mqtt_protocol.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_update_event.exe"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_update_event.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0

:test_update_reporting
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /I src /Foobj\%1\ tests\test_update_reporting.c src\update_event.c src\update_status.c ..\l4common\remote_result.c ..\l4common\remote_outcome.c ..\l4common\remote_launch_failure.c src\rpc_contract.c src\mqtt_protocol.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_update_reporting.exe ole32.lib"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_update_reporting.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0
:test_update_admission
call tests\test_update_admission.cmd %1
if errorlevel 1 set BUILD_FAILED=1
exit /b 0

:test_active_updater
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /Foobj\%1\ ..\l4common\tests\test_active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_reader.c ..\l4common\journal_codec.c ..\l4common\journal.c ..\l4common\layout.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_active_updater.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib"
if errorlevel 1 set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_active_updater.exe
if errorlevel 1 set BUILD_FAILED=1
exit /b 0

:test_fm_user
call tests\test_fm_user.cmd %1
if errorlevel 1 set BUILD_FAILED=1
exit /b 0

:test_link_evidence
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /Foobj\%1\ tests\test_link_evidence.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\%1\test_link_evidence.exe ole32.lib"
if not "%errorlevel%"=="0" set BUILD_FAILED=1
if %BUILD_FAILED% equ 0 obj\%1\test_link_evidence.exe
if not "%errorlevel%"=="0" set BUILD_FAILED=1
exit /b 0
