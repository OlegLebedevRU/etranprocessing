@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "RENEW_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call :run x86
if errorlevel 1 exit /b 1
call :run x64
exit /b %errorlevel%
:run
setlocal
call "%RENEW_VCVARS%" %1 >nul
if errorlevel 1 exit /b 1
if not exist obj\%1\renew-authority\l4con mkdir obj\%1\renew-authority\l4con
if not exist obj\%1\renew-authority\l4pin mkdir obj\%1\renew-authority\l4pin
cl /nologo /W4 /WX /MT /O2 /utf-8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /DEVENT_PIPE_NAME=L\"\\\\.\\pipe\\L4Con_Renewal_Authority_Test\" /Foobj\%1\ tests\test_renewal_authority.c src\event_ipc.c src\mqtt_protocol.c /link /OUT:obj\%1\renew-authority\l4con\l4con.exe advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /MT /O2 /utf-8 /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /DEVENT_PIPE_NAME=L\"\\\\.\\pipe\\L4Con_Renewal_Authority_Test\" /I ..\l4pin\src /Foobj\%1\ ..\l4pin\tests\test_renewal_client.c ..\l4pin\src\http_client.c ..\l4pin\src\xml_utils.c ..\l4pin\src\cng_crypto.c ..\l4pin\src\cert_store.c ..\l4pin\src\cert_discovery.c /link /OUT:obj\%1\renew-authority\l4pin\l4pin.exe winhttp.lib crypt32.lib ncrypt.lib bcrypt.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
copy /y obj\%1\renew-authority\l4pin\l4pin.exe obj\%1\renew-authority\l4pin\wrong.exe >nul
obj\%1\renew-authority\l4con\l4con.exe
exit /b %errorlevel%
