@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "RPC_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
call :run x86
if errorlevel 1 exit /b 1
call :run x64
exit /b %errorlevel%
:run
cmd /c ""%RPC_VS%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /I src /I ..\l4pin\src /Foobj\%1\ tests\test_rpc_runtime.c src\rpc_contract.c src\config.c src\mqtt_protocol.c src\command_runner.c src\event_ipc.c src\tool_inventory.c ..\leo4proxy\src\policy_json.c ..\l4pin\src\http_client.c ..\l4pin\src\cert_discovery.c /link /OUT:obj\%1\test_rpc_runtime.exe ws2_32.lib winhttp.lib advapi32.lib shlwapi.lib ole32.lib shell32.lib crypt32.lib ncrypt.lib version.lib"
if errorlevel 1 exit /b 1
obj\%1\test_rpc_runtime.exe
exit /b %errorlevel%
