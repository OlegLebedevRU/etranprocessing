@echo off
setlocal
cd /d "%~dp0"
if not exist res\network-profile.bin type nul >res\network-profile.bin
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
set "RC_PAYLOAD_FLAGS=/d EMBED_NETWORK_PROFILE"
if exist "%~dp0res\payload_x86.bin" set "RC_PAYLOAD_FLAGS=%RC_PAYLOAD_FLAGS% /d EMBED_PAYLOAD_X86"
if exist "%~dp0res\payload_x64.bin" set "RC_PAYLOAD_FLAGS=%RC_PAYLOAD_FLAGS% /d EMBED_PAYLOAD_X64"
set "SOURCES=src\main.c src\layout_plan.c ..\l4common\layout.c ..\l4common\access.c ..\l4common\release.c ..\l4common\service_switch.c ..\l4common\journal.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\recovery_task.c ..\l4common\recovery_task_win.c ..\l4common\worker_job.c ..\l4common\worker_start.c ..\l4common\worker_handoff.c ..\l4common\communication_recovery.c ..\l4common\communication_plan.c ..\l4common\communication_store.c ..\l4common\journal_codec.c ..\l4common\config_transaction.c ..\l4common\launcher.c ..\l4common\bootstrap.c ..\l4common\bootstrap_history.c ..\l4common\bootstrap_services.c src\cli.c src\log.c src\uac.c src\preflight.c src\drainage.c src\unpack.c src\services.c src\cert_phase.c src\smoke.c src\readiness.c src\admission.c src\manifest.c src\root_manifest.c src\fresh_install.c src\broker_config.c src\install_path.c src\install_bundle.c src\bootstrap_receipt.c src\recovery_receipt.c src\broker_environment.c src\install_entry.c src\remote_entry.c src\acceptance_local.c src\acceptance_entry.c src\acceptance_export.c src\remote_controller.c src\remote_policy.c src\worker_entry.c src\remote_prepare.c src\remote_worker_plan.c src\remote_config.c src\remote_service.c src\remote_service_transaction.c src\remote_executor.c src\remote_restore.c src\remote_completion.c src\remote_commit.c src\remote_outcome_report.c src\remote_worker_start.c src\supervisor_template.c src\remote_preparation_result.c src\remote_launch_failure_report.c ..\l4common\remote_launch_failure.c ..\l4common\remote_status.c ..\l4common\remote_outcome.c ..\l4common\remote_result.c src\installed_source.c ..\l4common\platform_profile.c ..\l4common\remote_request.c ..\l4common\remote_host.c ..\l4common\active_updater.c ..\l4common\journal_reader.c src\update_metadata.c ..\l4common\metadata.c ..\l4common\catalog.c ..\l4common\catalog_floor.c ..\l4common\route_plan.c ..\l4common\registry_http.c ..\l4common\package_cache.c ..\l4common\child_probe.c ..\l4common\service_token.c ..\l4common\proxy_certificate.c src\proxy_probe.c ..\l4common\probe_ipc.c ..\l4pin\src\http_client.c ..\leo4proxy\src\policy_json.c src\summary.c src\engine.c src\ui.c ..\l4pin\src\cert_discovery.c ..\l4superv\src\hardware_fingerprint.c ..\l4superv\src\miniz.c"
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
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && rc.exe /nologo %RC_PAYLOAD_FLAGS% /i res /i src /fo obj\%1\l4setup.res res\l4setup.rc && cl.exe /nologo /O2 /MT /W4 /wd4100 /wd4127 /wd4244 /wd4702 /wd4706 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /I res /I ..\l4pin\src /I ..\l4superv\src /Foobj\%1\ %SOURCES% obj\%1\l4setup.res /link /SUBSYSTEM:WINDOWS,6.01 /OUT:bin\%1\l4setup.exe oleaut32.lib userenv.lib kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib crypt32.lib ncrypt.lib winhttp.lib ws2_32.lib iphlpapi.lib shlwapi.lib wtsapi32.lib ole32.lib comctl32.lib version.lib wintrust.lib"
exit /b %errorlevel%
:run_tests
call run_tests.cmd
exit /b %errorlevel%
