@echo off
setlocal
cd /d "%~dp0"
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "TARGET_CMD=%~1"
:: Find MSVC VsDevCmd directory
set "VS_DEV_CMD="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
) else if exist "d:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=d:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
)

if not defined VS_DEV_CMD (
    echo Error: MSVC VsDevCmd.bat not found in standard paths.
    exit /b 1
)


if not exist bin mkdir bin
if not exist obj mkdir obj
if "%TARGET_CMD%"=="" set "TARGET_CMD=all"
if /i "%TARGET_CMD%"=="x64" goto :only_x64
if /i not "%TARGET_CMD%"=="all" if /i not "%TARGET_CMD%"=="x86" exit /b 1
call :build x86
if errorlevel 1 exit /b 1
copy /y bin\x86\l4launch.exe bin\l4launch.exe >nul
if errorlevel 1 exit /b 1
if /i "%TARGET_CMD%"=="x86" exit /b 0
:only_x64
call :build x64
exit /b %errorlevel%
:build
if not exist bin\%1 mkdir bin\%1
if not exist obj\%1 mkdir obj\%1
cmd /c ""%VS_DEV_CMD%" -arch=%1 -no_logo && rc.exe /nologo /fo obj\%1\l4launch.res res\l4launch.rc && cl.exe /nologo /O2 /MT /W4 /WX /utf-8 /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Foobj\%1\ src\main.c ..\l4common\launcher.c ..\l4common\layout.c ..\l4common\journal.c ..\l4common\config_transaction.c ..\l4common\release.c ..\l4superv\src\miniz.c obj\%1\l4launch.res /link /SUBSYSTEM:CONSOLE,6.01 /OUT:bin\%1\l4launch.exe"
exit /b %errorlevel%
