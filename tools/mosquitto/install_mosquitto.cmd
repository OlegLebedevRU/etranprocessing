@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo  Mosquitto Broker Installer ^& Configurator
echo =======================================================

:: 1. Check and request Administrator privileges (UAC elevation)
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b %ERRORLEVEL%
)

:: 2. Switch to script directory
set SCRIPT_DIR=%~dp0
if "%SCRIPT_DIR:~-1%"=="\" set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
cd /d "%SCRIPT_DIR%"

powershell.exe -NoLogo -NoProfile -NonInteractive -InputFormat None -ExecutionPolicy Bypass -File "%SCRIPT_DIR%\install_mosquitto.ps1" -TargetDir "%SCRIPT_DIR%" %*

if %ERRORLEVEL% equ 0 (
    echo.
    echo Installation and configuration completed successfully.
) else (
    echo.
    echo Installation failed with exit code %ERRORLEVEL%.
)

exit /b %ERRORLEVEL%
