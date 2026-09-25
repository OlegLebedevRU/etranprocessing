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
cmd /c ""%VS_DEV_CMD%" -arch=x86 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /Foobj\x86\ tests\test_mosquitto_prepare.c src\services.c src\log.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\x86\test_mosquitto_prepare.exe advapi32.lib user32.lib"
if errorlevel 1 exit /b 1
bin\x86\test_mosquitto_prepare.exe
if errorlevel 1 exit /b 1
cmd /c ""%VS_DEV_CMD%" -arch=x64 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /Foobj\x64\ tests\test_mosquitto_prepare.c src\services.c src\log.c /link /OUT:bin\x64\test_mosquitto_prepare.exe advapi32.lib user32.lib"
if errorlevel 1 exit /b 1
bin\x64\test_mosquitto_prepare.exe
exit /b %errorlevel%
