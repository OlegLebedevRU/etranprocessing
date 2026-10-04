@echo off
setlocal
cd /d "%~dp0.."
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "VS_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
call :test x86
if errorlevel 1 exit /b 1
call :test x64
exit /b %errorlevel%
:test
if not exist obj\%1 mkdir obj\%1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_endpoints.c src\policy_json.c /Feobj\%1\test_endpoints.exe /link ws2_32.lib dnsapi.lib crypt32.lib advapi32.lib shell32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_endpoints.exe
exit /b %errorlevel%
