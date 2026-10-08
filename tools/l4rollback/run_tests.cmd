@echo off
setlocal
cd /d "%~dp0"
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "ARCH=x86"
if /i "%1"=="x64" set "ARCH=x64"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -no_logo
if errorlevel 1 exit /b 1
if not exist obj\%ARCH% mkdir obj\%ARCH%
if not exist bin\%ARCH% mkdir bin\%ARCH%
set "FLAGS=/nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\%ARCH%\"
cl.exe %FLAGS% tests\test_files_worker.c src\files.c src\worker.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\recovery_plan.c ..\l4common\update_state.c ..\l4common\update_state_store.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\%ARCH%\test_files_worker.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\%ARCH%\test_files_worker.exe
if errorlevel 1 exit /b 1
cl.exe %FLAGS% tests\test_supervisor.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\%ARCH%\test_supervisor.exe advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
bin\%ARCH%\test_supervisor.exe
if errorlevel 1 exit /b 1
cl.exe %FLAGS% tests\test_execute.c src\files.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\update_state.c ..\l4common\update_state_store.c /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\%ARCH%\test_execute.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib
if errorlevel 1 exit /b 1
bin\%ARCH%\test_execute.exe
exit /b %errorlevel%
