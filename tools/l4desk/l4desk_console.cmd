@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo   l4desk Console Debug Launcher
echo ========================================================

set EXE=bin\l4desk.exe
if not exist "%EXE%" (
    if exist "..\..\..\l4desk.exe" (
        set EXE=..\..\..\l4desk.exe
    ) else if exist "l4desk.exe" (
        set EXE=l4desk.exe
    ) else (
        echo [ERROR] l4desk.exe not found! Please run build.cmd first.
        pause
        exit /b 1
    )
)

echo Starting %EXE% in console debug mode...
echo Arguments: %*
"%EXE%" --console --verbose %*
