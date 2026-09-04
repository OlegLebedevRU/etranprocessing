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

net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process '%SUPERV_EXE%' -ArgumentList '--restart' -Verb RunAs -Wait"
    exit /b %errorlevel%
)

"%SUPERV_EXE%" --restart
pause
