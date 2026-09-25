@echo off
setlocal
cd /d "%~dp0.."
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_DEV_CMD%" exit /b 2
if not exist obj mkdir obj
if not exist bin mkdir bin
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /I src /I ..\l4superv\src /Foobj\ tests\test_payload_smoke.c src\unpack.c src\log.c ..\l4superv\src\miniz.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\test_payload_smoke.exe shlwapi.lib"
exit /b %errorlevel%
