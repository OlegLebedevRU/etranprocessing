@echo off
setlocal
cd /d "%~dp0.."
set "VS_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
call :test x86
if errorlevel 1 exit /b 1
call :test x64
if errorlevel 1 exit /b 1
cmd /c tests\test_policy.cmd
exit /b %errorlevel%
:test
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_registry_connect.c /Feobj\%1\test_registry_connect.exe /link ws2_32.lib crypt32.lib secur32.lib"
if not "%errorlevel%"=="0" exit /b 1
obj\%1\test_registry_connect.exe
if not "%errorlevel%"=="0" exit /b 1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_fm_connect.c /Feobj\%1\test_fm_connect.exe /link ws2_32.lib crypt32.lib secur32.lib"
if not "%errorlevel%"=="0" exit /b 1
obj\%1\test_fm_connect.exe
if not "%errorlevel%"=="0" exit /b 1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_deadlines.c src\credential_lifetime.c /Feobj\%1\test_deadlines.exe /link ws2_32.lib crypt32.lib secur32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_deadlines.exe
if errorlevel 1 exit /b 1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_upstream_probe.c src\upstream_probe.c /Feobj\%1\test_upstream_probe.exe"
if errorlevel 1 exit /b 1
obj\%1\test_upstream_probe.exe
if errorlevel 1 exit /b 1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_credential_lifetime.c /Feobj\%1\test_credential_lifetime.exe /link crypt32.lib secur32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_credential_lifetime.exe
if errorlevel 1 exit /b 1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_certificate_selection.c src\cert_store.c /Feobj\%1\test_certificate_selection.exe /link crypt32.lib ncrypt.lib advapi32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_certificate_selection.exe
if errorlevel 1 exit /b 1
cmd /c ""%VS_CMD%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_tls_rotation.c src\cert_store.c src\endpoints.c src\policy_json.c src\schannel_tls.c src\credential_lifetime.c /Feobj\%1\test_tls_rotation.exe /link dnsapi.lib ws2_32.lib crypt32.lib ncrypt.lib advapi32.lib shell32.lib secur32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_tls_rotation.exe
exit /b %errorlevel%
