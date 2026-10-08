@echo off
setlocal
cd /d "%~dp0"
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not exist "%VS_DEV_CMD%" exit /b 1
if "%1"=="" goto all
if /i "%1"=="all" goto all
if /i "%1"=="x86" goto x86
if /i "%1"=="x64" goto x64
exit /b 1
:all
call :build x86
if errorlevel 1 exit /b 1
copy /y bin\x86\l4rollback.exe bin\l4rollback.exe >nul
if errorlevel 1 exit /b 1
call :build x64
exit /b %errorlevel%
:x86
call :build x86
if errorlevel 1 exit /b 1
copy /y bin\x86\l4rollback.exe bin\l4rollback.exe >nul
exit /b %errorlevel%
:x64
call :build x64
exit /b %errorlevel%
:build
if not exist bin\%1 mkdir bin\%1
if not exist obj\%1 mkdir obj\%1
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && cl.exe /nologo /O2 /Gy /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /DL4_RECOVERY_READER_ONLY /Foobj\%1\ src\main.c src\execute.c src\files.c src\worker.c src\supervisor.c ..\l4common\recovery_plan.c ..\l4common\recovery_store.c ..\l4common\journal.c ..\l4common\update_state.c ..\l4common\layout.c ..\l4common\probe_ipc.c /link /OPT:REF,ICF /MAP:bin\%1\l4rollback.map /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\%1\l4rollback.exe advapi32.lib bcrypt.lib ole32.lib shlwapi.lib"
exit /b %errorlevel%
