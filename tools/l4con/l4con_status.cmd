@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "EXE_PATH="
if exist "%~dp0l4con.exe" (
    set "EXE_PATH=%~dp0l4con.exe"
) else if exist "%~dp0bin\l4con.exe" (
    set "EXE_PATH=%~dp0bin\l4con.exe"
)

if defined EXE_PATH (
    "%EXE_PATH%" --status
) else (
    sc query L4Con
)
echo.
pause
