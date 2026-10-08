@echo off
setlocal
cd /d "%~dp0.."
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "MODE=%~1"
set "VS_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
call :test x86
if errorlevel 1 exit /b 1
call :test x64
exit /b %errorlevel%
:test
if not exist obj\%1 mkdir obj\%1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /Foobj\%1\ tests\test_candidate_runtime.c ..\l4common\child_probe.c ..\l4common\service_token.c ..\l4common\proxy_certificate.c ..\l4common\layout.c src\proxy_probe.c ..\leo4proxy\src\policy_json.c /Feobj\%1\test_candidate_runtime.exe /link ws2_32.lib iphlpapi.lib userenv.lib advapi32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_candidate_runtime.exe "%~dp0..\..\leo4proxy\bin\%1\leo4proxy.exe" %MODE%
exit /b %errorlevel%
