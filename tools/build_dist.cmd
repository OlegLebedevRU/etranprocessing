@echo off
setlocal EnableExtensions enabledelayedexpansion
cd /d "%~dp0"

echo ===============================================================
echo   Leo4 Terminal Tools Release Builder (l4setup Zero-Touch)
echo ===============================================================
echo.

:: Check legacy mode
if /i "%1"=="legacy" goto :build_legacy

:: Step 1: Read version
if not "%1"=="" (
    set "L4TOOLS_VERSION=%1"
    echo %1>"%~dp0version.txt"
    goto :version_set
)
if exist "%~dp0version.txt" (
    set /p L4TOOLS_VERSION=<"%~dp0version.txt"
    goto :version_set
)
set "L4TOOLS_VERSION=1.7.7"
echo 1.7.7>"%~dp0version.txt"

:version_set
set "L4TOOLS_VERSION=%L4TOOLS_VERSION: =%"

echo Target Version: %L4TOOLS_VERSION%

:: Parse SemVer for RC resource
for /f "tokens=1,2,3 delims=." %%a in ("%L4TOOLS_VERSION%") do (
    set "VER_MAJ=%%a"
    set "VER_MIN=%%b"
    set "VER_PATCH=%%c"
)
if "%VER_MAJ%"=="" set "VER_MAJ=1"
if "%VER_MIN%"=="" set "VER_MIN=7"
if "%VER_PATCH%"=="" set "VER_PATCH=7"

:: Generate tools/l4setup/res/version.h
if not exist "%~dp0l4setup\res" md "%~dp0l4setup\res"
(
    echo #pragma once
    echo.
    echo #define L4TOOLS_VERSION "%L4TOOLS_VERSION%"
    echo #define L4TOOLS_VERSION_RC %VER_MAJ%,%VER_MIN%,%VER_PATCH%,0
    echo.
    echo #define L4SETUP_VERSION_MAJOR %VER_MAJ%
    echo #define L4SETUP_VERSION_MINOR %VER_MIN%
    echo #define L4SETUP_VERSION_PATCH %VER_PATCH%
    echo #define L4SETUP_VERSION_BUILD 0
    echo #define L4SETUP_VERSION_STRING "%L4TOOLS_VERSION%"
    echo #define L4SETUP_VERSION_WSTRING L"%L4TOOLS_VERSION%"
    echo #define VER_FILEVERSION %VER_MAJ%,%VER_MIN%,%VER_PATCH%,0
    echo #define VER_PRODUCTVERSION %VER_MAJ%,%VER_MIN%,%VER_PATCH%,0
    echo #define VER_FILEVERSION_STR "%L4TOOLS_VERSION%.0\0"
    echo #define VER_PRODUCTVERSION_STR "%L4TOOLS_VERSION%\0"
) > "%~dp0l4setup\res\version.h"

:: Also ensure tools/l4setup/src/version.h is in sync
if not exist "%~dp0l4setup\src" md "%~dp0l4setup\src"
(
    echo #pragma once
    echo.
    echo #define L4SETUP_VERSION_MAJOR %VER_MAJ%
    echo #define L4SETUP_VERSION_MINOR %VER_MIN%
    echo #define L4SETUP_VERSION_PATCH %VER_PATCH%
    echo #define L4SETUP_VERSION_BUILD 0
    echo #define L4SETUP_VERSION_STRING "%L4TOOLS_VERSION%"
    echo #define L4SETUP_VERSION_WSTRING L"%L4TOOLS_VERSION%"
) > "%~dp0l4setup\src\version.h"

:: Step 2: Build all 7 components in order
echo.
echo [1/7] Building l4pin...
cd /d "%~dp0l4pin"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: l4pin
    exit /b 1
)

echo.
echo [2/7] Building l4superv...
cd /d "%~dp0l4superv"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: l4superv
    exit /b 1
)

echo.
echo [3/7] Building l4desk...
cd /d "%~dp0l4desk"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: l4desk
    exit /b 1
)

echo.
echo [4/7] Building l4con...
cd /d "%~dp0l4con"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: l4con
    exit /b 1
)

echo.
echo [5/7] Building l4sql...
cd /d "%~dp0l4sql"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: l4sql
    exit /b 1
)

echo.
echo [6/7] Building l4capture...
cd /d "%~dp0l4capture"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: l4capture
    exit /b 1
)

echo.
echo [7/7] Building leo4proxy...
cd /d "%~dp0leo4proxy"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Component build failed: leo4proxy
    exit /b 1
)

cd /d "%~dp0"

:: Step 3 & 4: Stage files and package x86/x64 payloads
echo.
echo [*] Staging components and creating payload_x86.bin and payload_x64.bin...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0release\Build-StagingPayloads.ps1" -ToolsRoot "%~dp0."
if errorlevel 1 (
    echo [ERROR] Staging and payload packaging failed!
    exit /b 1
)

:: Step 5: Build l4setup.exe with embedded resources
echo.
echo [*] Compiling unified l4setup.exe with embedded payloads...
cd /d "%~dp0l4setup"
call build.cmd
if errorlevel 1 (
    echo [ERROR] l4setup compilation failed!
    exit /b 1
)

if not exist "%~dp0dist" md "%~dp0dist"
copy /y "%~dp0l4setup\bin\l4setup.exe" "%~dp0dist\l4setup.exe" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy l4setup.exe to tools\dist\
    exit /b 1
)

cd /d "%~dp0"

:: Step 6 & 7: Generate Release Manifest, SHA256SUMS and perform verification
echo.
echo [*] Generating release manifest and running verifications...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0release\New-ReleaseManifest.ps1" -Version "%L4TOOLS_VERSION%" -ToolsRoot "%~dp0." -DistDir "%~dp0dist"
if errorlevel 1 (
    echo [ERROR] Release manifest generation or verification failed!
    exit /b 1
)

echo.
echo ===============================================================
echo  [OK] Release build v%L4TOOLS_VERSION% completed successfully!
echo       Output directory: %~dp0dist\
echo       - l4setup.exe
echo       - l4tools-release.json
echo       - SHA256SUMS
echo ===============================================================
exit /b 0

:build_legacy
echo.
echo ===============================================================
echo  [LEGACY] Building legacy tools.zip and l4install_*.exe
echo ===============================================================
cd /d "%~dp0l4superv"
call build.cmd all
if errorlevel 1 (
    echo [ERROR] Legacy l4superv/l4install build failed!
    exit /b 1
)
call pack_zip.cmd
if errorlevel 1 (
    echo [ERROR] Legacy packaging failed!
    exit /b 1
)
cd /d "%~dp0"
echo.
echo [OK] Legacy distribution ready in %~dp0dist
exit /b 0
