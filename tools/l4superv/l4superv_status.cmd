@echo off
setlocal enabledelayedexpansion

set "SUPERV_EXE="
if exist "%~dp0l4superv.exe" set "SUPERV_EXE=%~dp0l4superv.exe"
if not defined SUPERV_EXE if exist "%~dp0bin\l4superv.exe" set "SUPERV_EXE=%~dp0bin\l4superv.exe"

if not defined SUPERV_EXE (
    echo [ERROR] l4superv.exe not found.
    pause
    exit /b 1
)

"%SUPERV_EXE%" --status
pause
