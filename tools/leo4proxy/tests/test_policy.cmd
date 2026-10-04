@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
if not exist obj\x86 mkdir obj\x86
if not exist obj\x64 mkdir obj\x64
set "POLICY_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%POLICY_VS%" exit /b 1
call :test x86
if errorlevel 1 exit /b 1
call :test x64
if errorlevel 1 exit /b 1
exit /b 0
:test
cmd /c ""%POLICY_VS%" -arch=%1 -no_logo && cl /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_policy.c src\policy_json.c src\rtp_tunnel.c src\endpoints.c src\schannel_tls.c src\credential_lifetime.c /Feobj\%1\test_policy.exe /link dnsapi.lib ws2_32.lib crypt32.lib advapi32.lib shell32.lib winhttp.lib secur32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_policy.exe
exit /b %errorlevel%
