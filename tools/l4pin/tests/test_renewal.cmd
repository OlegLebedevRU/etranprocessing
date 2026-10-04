@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "RENEW_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
call :run x86
if errorlevel 1 exit /b 1
call :run x64
exit /b %errorlevel%
:run
cmd /c ""%RENEW_VS%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /Foobj\%1\ tests\test_renewal.c src\http_client.c src\xml_utils.c src\cng_crypto.c src\cert_store.c src\cert_discovery.c /link /OUT:obj\%1\test_renewal.exe winhttp.lib crypt32.lib ncrypt.lib bcrypt.lib advapi32.lib ole32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_renewal.exe
exit /b %errorlevel%
