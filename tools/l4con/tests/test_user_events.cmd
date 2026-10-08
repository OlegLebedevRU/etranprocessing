@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "EVENT_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call :run x86
if errorlevel 1 exit /b 1
call :run x64
exit /b %errorlevel%
:run
setlocal
call "%EVENT_VCVARS%" %1 >nul
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /MT /utf-8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DEVENT_PIPE_NAME=L\"\\\\.\\pipe\\L4Con_UserEvents_Test_v1\" /Foobj\%1\ tests\test_user_events.c src\event_ipc.c src\command_runner.c ..\l4common\layout.c src\mqtt_protocol.c /link /OUT:obj\%1\test_user_events.exe advapi32.lib shell32.lib shlwapi.lib crypt32.lib
if errorlevel 1 exit /b 1
obj\%1\test_user_events.exe
exit /b %errorlevel%
