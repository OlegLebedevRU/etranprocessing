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
set "TEST_ARCH=x86"
if /i "%1"=="x64" set "TEST_ARCH=x64"
echo [1/2] Compiling test_l4setup.exe (%TEST_ARCH%)...
call "%VS_DEV_CMD%" -arch=%TEST_ARCH% -no_logo
if /i "%2"=="scm" goto :scm_rights_only
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Gy /Foobj\ tests\test_acceptance_local.c ..\l4common\journal.c ..\l4common\layout.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_acceptance_receipt.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_acceptance_receipt.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_acceptance_export.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_acceptance_export.exe advapi32.lib
if errorlevel 1 exit /b 1
bin\test_acceptance_export.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_catalog_floor.c ..\l4common\catalog_floor.c ..\l4common\metadata.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_catalog_floor.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_catalog_floor.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_communication_monitor_clock.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_communication_monitor_clock.exe advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
bin\test_communication_monitor_clock.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ ..\l4common\tests\test_remote_status_apply.c ..\l4common\remote_launch_failure.c ..\l4common\recovery_plan.c ..\l4common\communication_plan.c ..\l4common\communication_recovery.c ..\l4common\remote_outcome.c ..\l4common\update_state.c ..\l4common\remote_result.c ..\l4common\remote_request.c ..\l4common\remote_host.c ..\l4common\journal.c ..\l4common\layout.c ..\l4common\active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_codec.c ..\l4common\metadata.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_apply.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_apply.exe
if errorlevel 1 exit /b 1

cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_remote_outcome_report.c ..\l4common\remote_launch_failure.c ..\l4common\recovery_plan.c ..\l4common\communication_plan.c ..\l4common\communication_recovery.c ..\l4common\remote_status.c ..\l4common\remote_outcome.c ..\l4common\update_state.c ..\l4common\remote_result.c ..\l4common\remote_request.c ..\l4common\remote_host.c ..\l4common\journal.c ..\l4common\layout.c ..\l4common\active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_codec.c ..\l4common\metadata.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_writer.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_writer.exe
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W4 /WX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /Foobj\ /Febin\restore.exe tests\test_remote_restore.c ..\l4common\journal.c ..\l4common\layout.c /link advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\restore.exe
if errorlevel 1 exit /b 1

cl /nologo /O2 /MT /W4 /WX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /Foobj\ /Febin\executor.exe tests\test_remote_executor.c src\remote_policy.c /link advapi32.lib bcrypt.lib
if errorlevel 1 exit /b 1
bin\executor.exe
if errorlevel 1 exit /b 1

cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_controller.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_controller.exe
if errorlevel 1 exit /b 1
bin\test_remote_controller.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_policy.c src\remote_policy.c ..\l4common\communication_recovery.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_policy.exe
if errorlevel 1 exit /b 1
bin\test_remote_policy.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_service_transaction.c ..\l4common\journal.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_service_transaction.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_remote_service_transaction.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_completion.c src\remote_commit.c ..\l4common\journal.c ..\l4common\journal_reader.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_completion.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_remote_completion.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_commit.c tests\remote_commit_admission_stubs.c src\remote_commit.c ..\l4common\journal_reader.c ..\l4common\journal.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_commit.exe ole32.lib advapi32.lib shell32.lib bcrypt.lib
if errorlevel 1 exit /b 1
bin\test_remote_commit.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_bootstrap_receipt.c src\bootstrap_receipt.c ..\l4common\metadata.c ..\l4common\journal.c ..\l4common\layout.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_bootstrap_receipt.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_bootstrap_receipt.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_config.c src\broker_config.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\launcher.c ..\l4common\config_transaction.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_config.exe ole32.lib advapi32.lib shell32.lib bcrypt.lib
if errorlevel 1 exit /b 1
bin\test_remote_config.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_service.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\l4common\journal.c ..\l4common\journal_codec.c ..\l4common\layout.c ..\l4common\update_state.c ..\l4common\update_state_store.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_service.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_service.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_launch_failure.c ..\l4common\remote_launch_failure.c ..\l4common\remote_result.c ..\l4common\journal.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_launch_failure.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_remote_launch_failure.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_remote_worker_start.c ..\l4common\communication_recovery.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_worker_start.exe
if errorlevel 1 exit /b 1
bin\test_remote_worker_start.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_worker_entry.c ..\l4common\journal.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_worker_entry.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\test_worker_entry.exe
if errorlevel 1 exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_supervisor_template.c ..\l4common\recovery_plan.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_supervisor_template.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_supervisor_template.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ ..\l4common\tests\test_remote_status.c ..\l4common\remote_launch_failure.c ..\l4common\recovery_plan.c ..\l4common\communication_plan.c ..\l4common\communication_recovery.c ..\l4common\remote_status.c ..\l4common\remote_result.c ..\l4common\remote_request.c ..\l4common\remote_host.c ..\l4common\journal.c ..\l4common\layout.c ..\l4common\active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_codec.c ..\l4common\metadata.c ..\leo4proxy\src\policy_json.c ..\l4common\remote_outcome.c ..\l4common\update_state.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_status.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_status.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_remote_worker_plan.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_worker_plan.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_worker_plan.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_remote_result.c src\remote_preparation_result.c ..\l4common\remote_result.c ..\l4common\remote_request.c ..\l4common\journal.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_result.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_result.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_remote_prepare.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_prepare.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_prepare.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ ..\l4common\tests\test_platform_profile.c ..\l4common\platform_profile.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_platform_profile.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_platform_profile.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ ..\l4common\tests\test_remote_request.c ..\l4common\remote_request.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_request.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_request.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ ..\l4common\tests\test_journal_reader.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_journal_reader.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_journal_reader.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_installed_source.c ..\l4common\journal_reader.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_installed_source.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_installed_source.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ ..\l4common\tests\test_remote_host.c ..\l4common\remote_request.c ..\l4common\journal_reader.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_host.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_host.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_remote_entry.c ..\l4common\remote_host.c ..\l4common\remote_request.c ..\l4common\journal_reader.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\active_updater.c ..\l4common\bootstrap_history.c ..\l4common\journal_codec.c ..\l4common\metadata.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_remote_entry.exe advapi32.lib bcrypt.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_remote_entry.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_proxy_certificate.c ..\l4common\proxy_certificate.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_proxy_certificate.exe shell32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_proxy_certificate.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_service_token.c ..\l4common\child_probe.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_service_token.exe userenv.lib advapi32.lib ws2_32.lib iphlpapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_service_token.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_child_probe.c ..\l4common\child_probe.c ..\l4common\service_token.c ..\l4common\proxy_certificate.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_child_probe.exe shell32.lib ws2_32.lib iphlpapi.lib userenv.lib advapi32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_child_probe.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /wd4100 /wd4127 /wd4244 /wd4702 /wd4706 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /I res /I ..\l4pin\src /I ..\l4superv\src /Foobj\ tests\test_l4setup.c src\cli.c src\log.c src\unpack.c src\summary.c src\preflight.c ..\l4pin\src\cert_discovery.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_l4setup.exe ws2_32.lib kernel32.lib user32.lib shell32.lib advapi32.lib crypt32.lib ncrypt.lib shlwapi.lib
if not "%errorlevel%"=="0" (
    echo [ERROR] Test compilation failed!
    exit /b 1
)

echo.
echo [2/2] Running Unit Tests...
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ tests\test_broker_environment.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_broker_environment.exe advapi32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_broker_environment.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_layout.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_layout.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_layout.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_access.c ..\l4common\layout.c ..\l4common\access.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_access.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_access.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_deployment.c ..\l4common\layout.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_deployment.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_deployment.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_journal.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\journal_codec.c ..\l4common\config_transaction.c ..\l4common\bootstrap.c ..\l4common\bootstrap_history.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_journal.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_journal.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_bootstrap_launcher.c ..\l4common\journal_codec.c ..\l4common\bootstrap_history.c ..\l4common\layout.c ..\l4common\launcher.c ..\l4common\journal.c ..\l4common\config_transaction.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_bootstrap_launcher.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_bootstrap_launcher.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_bootstrap_services.c ..\l4common\bootstrap_history.c ..\l4common\update_state.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_bootstrap_services.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_bootstrap_services.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_install_profiles.c src\broker_config.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\journal_codec.c ..\l4common\bootstrap.c ..\l4common\bootstrap_history.c ..\l4common\config_transaction.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_install_profiles.exe advapi32.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_install_profiles.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_fresh_install.c ..\l4common\bootstrap_history.c src\broker_config.c ..\l4common\access.c ..\l4common\config_transaction.c ..\l4common\launcher.c ..\l4common\update_state.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_fresh_install.exe advapi32.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_fresh_install.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_fm_root.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_fm_root.exe advapi32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_fm_root.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_proxy_probe.c src\proxy_probe.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_proxy_probe.exe ws2_32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_proxy_probe.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_pipeline.c src\engine.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_pipeline.exe kernel32.lib advapi32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_pipeline.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_certificate_phase.c src\cert_phase.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_certificate_phase.exe kernel32.lib user32.lib advapi32.lib crypt32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_certificate_phase.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /I ..\l4superv\src /Foobj\ tests\test_service_start.c src\cli.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_service_start.exe kernel32.lib user32.lib advapi32.lib shell32.lib ws2_32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_service_start.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_desk_startup_wait.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_desk_startup_wait.exe kernel32.lib user32.lib ws2_32.lib wtsapi32.lib winhttp.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_desk_startup_wait.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_upstream_pipe.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_upstream_pipe.exe kernel32.lib user32.lib ws2_32.lib wtsapi32.lib winhttp.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_upstream_pipe.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DL4_PROBE_PREFIX=L\"L4HealthTest\" /Foobj\ ..\l4common\tests\test_probe_ipc.c ..\l4common\probe_ipc.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_probe_ipc.exe advapi32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_probe_ipc.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DL4_PROBE_PREFIX=L\"L4ReadyAdapterTest\" /I src /Foobj\ tests\test_readiness.c ..\l4common\layout.c ..\l4common\child_probe.c ..\l4common\service_token.c ..\l4common\proxy_certificate.c src\proxy_probe.c ..\l4common\probe_ipc.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_readiness.exe advapi32.lib ws2_32.lib iphlpapi.lib userenv.lib shell32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_readiness.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_admission.c ..\l4common\layout.c ..\l4common\release.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_admission.exe advapi32.lib crypt32.lib wintrust.lib bcrypt.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_admission.exe
if not "%errorlevel%"=="0" exit /b 1
pushd ..\..
uv run --locked python -m l4release.tests.manifest_fixture
set "FIXTURE_RESULT=%errorlevel%"
popd
if not "%FIXTURE_RESULT%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_manifest.c src\manifest.c src\admission.c ..\l4common\metadata.c ..\l4common\layout.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_manifest.exe advapi32.lib crypt32.lib wintrust.lib bcrypt.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_manifest.exe ..\dist\.release\manifest-fixture\l4tools-layout-%TEST_ARCH%.json %TEST_ARCH%
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_metadata.c src\manifest.c src\admission.c ..\l4common\metadata.c ..\l4common\layout.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_metadata.exe advapi32.lib crypt32.lib wintrust.lib bcrypt.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_metadata.exe ..\dist\.release\manifest-fixture %TEST_ARCH%
if not "%errorlevel%"=="0" exit /b 1
pushd ..\..
uv run --locked python -m l4release.tests.catalog_fixture
set "CATALOG_RESULT=%errorlevel%"
popd
if not "%CATALOG_RESULT%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_catalog.c ..\l4common\catalog.c ..\l4common\catalog_floor.c ..\l4common\route_plan.c ..\l4common\metadata.c ..\l4common\layout.c ..\l4common\journal.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_catalog.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_catalog.exe ..\dist\.release\catalog-fixture
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_package_cache.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_package_cache.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_package_cache.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_registry_http.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_registry_http.exe advapi32.lib winhttp.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_registry_http.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_registry_tls.c ..\l4common\registry_http.c ..\l4common\layout.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_registry_tls.exe advapi32.lib winhttp.lib shell32.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
pushd ..\..
uv run --locked python -m l4release.tests.registry_tls_fixture tools/l4setup/bin/test_registry_tls.exe
set "REGISTRY_RESULT=%errorlevel%"
popd
if not "%REGISTRY_RESULT%"=="0" exit /b 1
pushd ..\..
uv run --locked python -m l4release.tests.root_fixture
set "ROOT_RESULT=%errorlevel%"
popd
if not "%ROOT_RESULT%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_root_manifest.c src\root_manifest.c src\manifest.c src\admission.c ..\l4common\metadata.c ..\l4common\layout.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_root_manifest.exe advapi32.lib crypt32.lib wintrust.lib bcrypt.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_root_manifest.exe ..\dist\.release\root-fixture %TEST_ARCH%
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /Foobj\ tests\test_update_metadata.c ..\l4common\communication_recovery.c ..\l4common\communication_plan.c ..\l4common\journal_codec.c ..\l4common\config_transaction.c ..\l4common\service_switch.c ..\l4common\bootstrap.c ..\l4common\bootstrap_history.c ..\l4common\package_cache.c ..\l4common\child_probe.c ..\l4common\service_token.c ..\l4common\proxy_certificate.c src\proxy_probe.c src\root_manifest.c src\manifest.c src\admission.c ..\l4common\route_plan.c ..\l4common\catalog.c ..\l4common\catalog_floor.c ..\l4common\journal.c ..\l4common\journal_reader.c ..\l4common\metadata.c ..\l4common\layout.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\leo4proxy\src\policy_json.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_update_metadata.exe ws2_32.lib iphlpapi.lib userenv.lib advapi32.lib crypt32.lib wintrust.lib bcrypt.lib shlwapi.lib ole32.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_update_metadata.exe ..\dist\.release\root-fixture %TEST_ARCH%
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_recovery_plan.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_recovery_plan.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_recovery_plan.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_recovery_task.c ..\l4common\recovery_task.c ..\l4common\recovery_task_win.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_recovery_task.exe advapi32.lib bcrypt.lib ole32.lib oleaut32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_recovery_task.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_worker_job.c ..\l4common\communication_worker.c ..\l4common\worker_job.c ..\l4rollback\src\worker.c ..\l4common\recovery_task.c ..\l4common\recovery_task_win.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_worker_job.exe advapi32.lib bcrypt.lib ole32.lib oleaut32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_worker_job.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_worker_start.c ..\l4common\worker_start.c ..\l4common\worker_job.c ..\l4common\recovery_task.c ..\l4common\recovery_task_win.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_worker_start.exe advapi32.lib bcrypt.lib ole32.lib oleaut32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_worker_start.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_worker_handoff.c ..\l4common\worker_handoff.c ..\l4common\worker_job.c ..\l4common\recovery_task.c ..\l4common\recovery_task_win.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_worker_handoff.exe advapi32.lib bcrypt.lib ole32.lib oleaut32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_worker_handoff.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_communication_recovery.c ..\l4common\communication_recovery.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_communication_recovery.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_communication_recovery.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_communication_plan.c ..\l4common\communication_plan.c ..\l4common\communication_store.c ..\l4common\communication_recovery.c ..\l4common\journal_codec.c ..\l4common\config_transaction.c ..\l4common\bootstrap.c ..\l4common\bootstrap_history.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_communication_plan.exe advapi32.lib crypt32.lib wintrust.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_communication_plan.exe
if not "%errorlevel%"=="0" exit /b 1
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_communication_runtime.c ..\l4common\communication_boot.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\communication_plan.c ..\l4common\communication_store.c ..\l4common\communication_recovery.c ..\l4common\journal_codec.c ..\l4common\bootstrap.c ..\l4common\bootstrap_history.c ..\l4common\release.c ..\l4superv\src\miniz.c ..\l4common\config_transaction.c ..\l4common\update_state.c ..\l4common\update_state_store.c ..\l4common\layout.c ..\l4common\journal.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_communication_runtime.exe advapi32.lib crypt32.lib wintrust.lib bcrypt.lib ole32.lib shlwapi.lib
if not "%errorlevel%"=="0" exit /b 1
bin\test_communication_runtime.exe
if not "%errorlevel%"=="0" exit /b 1
bin\test_l4setup.exe %*
if errorlevel 1 (
    echo.
    echo [ERROR] Unit tests failed!
    exit /b 1
)

echo.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File tests\test_fresh_baseline.ps1
if errorlevel 1 exit /b 1
echo [SUCCESS] All unit tests PASSED successfully!
exit /b 0


:scm_rights_only
:: Explicit administrative isolated SCM regression. Never starts a service or
:: touches suite service names. Prerequisite exit2 is NOT recorded as passed.
cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\ ..\l4common\tests\test_supervisor_crash_rights_scm.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_supervisor_crash_rights_scm.exe advapi32.lib
if errorlevel 1 exit /b 1
bin\test_supervisor_crash_rights_scm.exe
exit /b %errorlevel%
