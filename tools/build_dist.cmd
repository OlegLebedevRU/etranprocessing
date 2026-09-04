@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo =======================================================
echo   Leo4 Suite Unified Multi-Arch Dist Builder
echo =======================================================
echo.

echo [1/2] Building all x86 and x64 installers and supervisor binaries...
cd /d "%~dp0l4superv"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo [2/2] Packaging unified tools.zip and populating tools/dist...
call pack_zip.cmd
if errorlevel 1 (
    echo [ERROR] Packaging failed!
    exit /b 1
)

echo.
echo =======================================================
echo  [OK] Full Multi-Architecture Distribution Ready in:
echo       %~dp0dist
echo =======================================================
exit /b 0
