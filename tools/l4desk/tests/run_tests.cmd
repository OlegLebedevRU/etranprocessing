@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"

echo =======================================================
echo Building and Running l4desk Unit Tests
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

call "%VS_DEV_CMD%" -arch=x64 -no_logo

echo.
echo [1/3] Building fake_ffmpeg.exe...
cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /Foobj\ tests\fake_ffmpeg.c /link /OUT:bin\fake_ffmpeg.exe
if errorlevel 1 (
    echo [ERROR] Failed to build fake_ffmpeg.exe
    exit /b 1
)

echo.
echo [2/4] Building and running test_ctl_protocol.exe...
cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /I src /I include /Foobj\ tests\test_ctl_protocol.c src\ctl_protocol.c src\json_min.c src\desktop_state.c src\display_inventory.c src\input_inject.c src\dedup_cache.c src\ffmpeg_cmdline.c src\ffmpeg_supervisor.c src\log.c src\media_backend.c src\l4capture_adapter.c src\input_gate.c src\kiosk_focus.c src\kiosk_lifecycle.c /link /OUT:bin\test_ctl_protocol.exe ws2_32.lib winhttp.lib advapi32.lib user32.lib wtsapi32.lib ole32.lib oleaut32.lib gdi32.lib
if errorlevel 1 (
    echo [ERROR] Failed to build test_ctl_protocol.exe
    exit /b 1
)

bin\test_ctl_protocol.exe
if errorlevel 1 (
    echo [ERROR] test_ctl_protocol failed!
    exit /b 1
)

echo.
echo [3/4] Building and running test_orchestrator.exe...
cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /I src /I include /Foobj\ tests\test_orchestrator.c src\ffmpeg_supervisor.c src\ffmpeg_cmdline.c src\display_inventory.c src\desktop_state.c src\input_inject.c src\json_min.c src\log.c src\media_backend.c src\l4capture_adapter.c src\input_gate.c src\kiosk_focus.c src\kiosk_lifecycle.c /link /OUT:bin\test_orchestrator.exe ws2_32.lib advapi32.lib user32.lib wtsapi32.lib ole32.lib oleaut32.lib gdi32.lib
if errorlevel 1 (
    echo [ERROR] Failed to build test_orchestrator.exe
    exit /b 1
)

bin\test_orchestrator.exe
if errorlevel 1 (
    echo [ERROR] test_orchestrator failed!
    exit /b 1
)

echo.
echo [4/4] Building and running test_l4capture_adapter.exe...
cl.exe /nologo /O2 /MT /W4 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /I src /I include /Foobj\ tests\test_l4capture_adapter.c src\l4capture_adapter.c src\media_backend.c src\input_gate.c src\kiosk_focus.c src\kiosk_lifecycle.c src\input_inject.c src\desktop_state.c src\log.c /link /OUT:bin\test_l4capture_adapter.exe ws2_32.lib advapi32.lib user32.lib wtsapi32.lib gdi32.lib
if errorlevel 1 (
    echo [ERROR] Failed to build test_l4capture_adapter.exe
    exit /b 1
)

bin\test_l4capture_adapter.exe
if errorlevel 1 (
    echo [ERROR] test_l4capture_adapter failed!
    exit /b 1
)

echo.
echo =======================================================
echo ALL UNIT TESTS PASSED SUCCESSFULLY!
echo =======================================================
exit /b 0
