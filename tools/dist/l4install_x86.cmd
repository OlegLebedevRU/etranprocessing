@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo =======================================================
echo   Leo4 Tools Suite - x86 Installer (Administrator)
echo =======================================================
echo.

:: Check for Administrator privileges and request UAC elevation
net session >nul 2>&1
if errorlevel 1 (
    echo [INFO] Administrator privileges required. Requesting UAC elevation...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c \"\"%~f0\" %*\"' -Verb RunAs"
    exit /b %ERRORLEVEL%
)

if not exist "%~dp0l4install_x86.exe" (
    echo [ERROR] l4install_x86.exe not found in %~dp0
    pause
    exit /b 1
)

"%~dp0l4install_x86.exe" %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
if %EXIT_CODE% equ 0 (
    echo [OK] 32-bit installation completed successfully.
) else (
    echo [ERROR] Installation exited with code %EXIT_CODE%.
)
echo.
pause
exit /b %EXIT_CODE%
