@echo off
setlocal
cd /d "%~dp0.."
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_DEV_CMD%" exit /b 2
if not exist bin\x86 mkdir bin\x86
if not exist bin\x64 mkdir bin\x64
if not exist obj\x86 mkdir obj\x86
if not exist obj\x64 mkdir obj\x64
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && cl.exe /nologo /W4 /WX /MT /D_WIN32_WINNT=0x0601 /Foobj\x86\test_service_start_state.obj tests\test_service_start_state.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\test_service_start_state.exe"
if errorlevel 1 exit /b 1
bin\x86\test_service_start_state.exe
if errorlevel 1 exit /b 1
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && cl.exe /nologo /W4 /WX /MT /D_WIN32_WINNT=0x0601 /Foobj\x64\test_service_start_state.obj tests\test_service_start_state.c /link /OUT:bin\x64\test_service_start_state.exe"
if errorlevel 1 exit /b 1
bin\x64\test_service_start_state.exe
exit /b %errorlevel%
