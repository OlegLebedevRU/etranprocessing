@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "ADMISSION_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not "%~1"=="" goto one
for %%A in (x86 x64) do for %%G in (0 1) do (
    call :run %%A %%G
    if errorlevel 1 exit /b 1
)
exit /b 0
:one
call :run %1 0
if errorlevel 1 exit /b 1
call :run %1 1
exit /b %errorlevel%
:run
if not exist obj\admission\%1-%2 mkdir obj\admission\%1-%2
cmd /c ""%ADMISSION_VS%" -arch=%1 -no_logo && cl.exe /nologo /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /DL4CON_REMOTE_ADMISSION_ENABLED=%2 /I src /Foobj\admission\%1-%2\ tests\test_update_admission.c src\rpc_contract.c src\mqtt_protocol.c ..\leo4proxy\src\policy_json.c /link /OUT:obj\admission\%1-%2\test_update_admission.exe"
if errorlevel 1 exit /b 1
obj\admission\%1-%2\test_update_admission.exe
exit /b %errorlevel%
