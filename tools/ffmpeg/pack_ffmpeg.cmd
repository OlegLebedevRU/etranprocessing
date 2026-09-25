@echo off
setlocal enabledelayedexpansion

echo ===============================================================
echo  Packing FFmpeg Media Engine into unified ffmpeg.zip
echo ===============================================================

for %%I in ("%~dp0..") do set "REPO_TOOLS=%%~fI"
for %%I in ("%REPO_TOOLS%\..") do set "REPO_ROOT=%%~fI"

set "STAGING=%REPO_TOOLS%\obj\staging_ffmpeg"
set "DIST_DIR=%REPO_TOOLS%\dist"
set "OUT_ZIP=%DIST_DIR%\ffmpeg.zip"
set "STATIC_SRC=%REPO_ROOT%\ffmpeg-win32"
set "CANDIDATE_MODE=0"
if /i "%~1"=="--candidate" (
    set "CANDIDATE_MODE=1"
    set "STAGING=%REPO_TOOLS%\obj\staging_ffmpeg_candidate_1_8_1"
    set "DIST_DIR=%REPO_TOOLS%\dist\candidate-1.8.1"
    set "OUT_ZIP=%REPO_TOOLS%\dist\candidate-1.8.1\ffmpeg.zip"
    if exist "%REPO_TOOLS%\obj\staging_ffmpeg_candidate_1_8_1" (
        echo [ERROR] Candidate FFmpeg staging already exists.
        exit /b 1
    )
    if exist "%REPO_TOOLS%\dist\candidate-1.8.1\ffmpeg.zip" (
        echo [ERROR] Candidate FFmpeg archive already exists.
        exit /b 1
    )
)

:: 1. Locate x64 binary
set "FFMPEG_X64_EXE="
set "FFMPEG_X64_DIR="
if defined FFMPEG_SRC_X64 (
    if exist "%FFMPEG_SRC_X64%\ffmpeg.exe" (
        set "FFMPEG_X64_EXE=%FFMPEG_SRC_X64%\ffmpeg.exe"
        set "FFMPEG_X64_DIR=%FFMPEG_SRC_X64%"
    ) else if exist "%FFMPEG_SRC_X64%\bin\ffmpeg.exe" (
        set "FFMPEG_X64_EXE=%FFMPEG_SRC_X64%\bin\ffmpeg.exe"
        set "FFMPEG_X64_DIR=%FFMPEG_SRC_X64%\bin"
    ) else if exist "%FFMPEG_SRC_X64%\x64\ffmpeg.exe" (
        set "FFMPEG_X64_EXE=%FFMPEG_SRC_X64%\x64\ffmpeg.exe"
        set "FFMPEG_X64_DIR=%FFMPEG_SRC_X64%\x64"
    )
)
if not defined FFMPEG_X64_EXE (
    if exist "%STATIC_SRC%\x64\ffmpeg.exe" (
        set "FFMPEG_X64_EXE=%STATIC_SRC%\x64\ffmpeg.exe"
        set "FFMPEG_X64_DIR=%STATIC_SRC%\x64"
    ) else if exist "%~dp0bin\x64\ffmpeg.exe" (
        set "FFMPEG_X64_EXE=%~dp0bin\x64\ffmpeg.exe"
        set "FFMPEG_X64_DIR=%~dp0bin\x64"
    ) else if exist "D:\ffmpeg\bin\ffmpeg.exe" (
        set "FFMPEG_X64_EXE=D:\ffmpeg\bin\ffmpeg.exe"
        set "FFMPEG_X64_DIR=D:\ffmpeg\bin"
    ) else (
        echo [ERROR] FFmpeg x64 binary not found in ffmpeg-win32, bin, or D:\ffmpeg.
        exit /b 1
    )
)

:: 2. Locate x86 binary
set "FFMPEG_X86_EXE="
set "FFMPEG_X86_DIR="
if defined FFMPEG_SRC_X86 (
    if exist "%FFMPEG_SRC_X86%\ffmpeg.exe" (
        set "FFMPEG_X86_EXE=%FFMPEG_SRC_X86%\ffmpeg.exe"
        set "FFMPEG_X86_DIR=%FFMPEG_SRC_X86%"
    ) else if exist "%FFMPEG_SRC_X86%\bin\ffmpeg.exe" (
        set "FFMPEG_X86_EXE=%FFMPEG_SRC_X86%\bin\ffmpeg.exe"
        set "FFMPEG_X86_DIR=%FFMPEG_SRC_X86%\bin"
    ) else if exist "%FFMPEG_SRC_X86%\x86\ffmpeg.exe" (
        set "FFMPEG_X86_EXE=%FFMPEG_SRC_X86%\x86\ffmpeg.exe"
        set "FFMPEG_X86_DIR=%FFMPEG_SRC_X86%\x86"
    )
)
if not defined FFMPEG_X86_EXE (
    if exist "%STATIC_SRC%\x86\ffmpeg.exe" (
        set "FFMPEG_X86_EXE=%STATIC_SRC%\x86\ffmpeg.exe"
        set "FFMPEG_X86_DIR=%STATIC_SRC%\x86"
    ) else if exist "%~dp0bin\x86\ffmpeg.exe" (
        set "FFMPEG_X86_EXE=%~dp0bin\x86\ffmpeg.exe"
        set "FFMPEG_X86_DIR=%~dp0bin\x86"
    )
)

:: 3. Locate License file
set "FFMPEG_LICENSE="
if exist "%~dp0LICENSE" (
    set "FFMPEG_LICENSE=%~dp0LICENSE"
) else if exist "%STATIC_SRC%\LICENSE" (
    set "FFMPEG_LICENSE=%STATIC_SRC%\LICENSE"
) else if exist "D:\ffmpeg\LICENSE" (
    set "FFMPEG_LICENSE=D:\ffmpeg\LICENSE"
)

echo [1/5] Preparing staging area in %STAGING%...
if exist "%STAGING%" rd /s /q "%STAGING%"
md "%STAGING%"
md "%STAGING%\ffmpeg"
md "%STAGING%\ffmpeg\x64"
md "%STAGING%\ffmpeg\x86"

echo [2/5] Staging x64 FFmpeg binaries...
echo       Source: %FFMPEG_X64_EXE%
copy /y "%FFMPEG_X64_EXE%" "%STAGING%\ffmpeg\x64\ffmpeg.exe" >nul

if exist "%FFMPEG_X64_DIR%\avcodec-63.dll" (
    echo       [INFO] Shared libraries detected, copying DLL dependencies...
    copy /y "%FFMPEG_X64_DIR%\avcodec-63.dll" "%STAGING%\ffmpeg\x64\avcodec-63.dll" >nul
    copy /y "%FFMPEG_X64_DIR%\avdevice-63.dll" "%STAGING%\ffmpeg\x64\avdevice-63.dll" >nul
    copy /y "%FFMPEG_X64_DIR%\avfilter-12.dll" "%STAGING%\ffmpeg\x64\avfilter-12.dll" >nul
    copy /y "%FFMPEG_X64_DIR%\avformat-63.dll" "%STAGING%\ffmpeg\x64\avformat-63.dll" >nul
    copy /y "%FFMPEG_X64_DIR%\avutil-61.dll" "%STAGING%\ffmpeg\x64\avutil-61.dll" >nul
    copy /y "%FFMPEG_X64_DIR%\swresample-7.dll" "%STAGING%\ffmpeg\x64\swresample-7.dll" >nul
    copy /y "%FFMPEG_X64_DIR%\swscale-10.dll" "%STAGING%\ffmpeg\x64\swscale-10.dll" >nul
) else (
    echo       [INFO] Static zero-dependency x64 binary without external DLLs.
)

if defined FFMPEG_LICENSE (
    copy /y "%FFMPEG_LICENSE%" "%STAGING%\ffmpeg\x64\LICENSE" >nul
)

echo [3/5] Staging x86 FFmpeg binaries...
if defined FFMPEG_X86_EXE (
    echo       Source: %FFMPEG_X86_EXE%
    copy /y "%FFMPEG_X86_EXE%" "%STAGING%\ffmpeg\x86\ffmpeg.exe" >nul
    if exist "%FFMPEG_X86_DIR%\avcodec-63.dll" (
        echo       [INFO] Shared libraries detected for x86, copying DLL dependencies...
        copy /y "%FFMPEG_X86_DIR%\avcodec-63.dll" "%STAGING%\ffmpeg\x86\avcodec-63.dll" >nul
        copy /y "%FFMPEG_X86_DIR%\avdevice-63.dll" "%STAGING%\ffmpeg\x86\avdevice-63.dll" >nul
        copy /y "%FFMPEG_X86_DIR%\avfilter-12.dll" "%STAGING%\ffmpeg\x86\avfilter-12.dll" >nul
        copy /y "%FFMPEG_X86_DIR%\avformat-63.dll" "%STAGING%\ffmpeg\x86\avformat-63.dll" >nul
        copy /y "%FFMPEG_X86_DIR%\avutil-61.dll" "%STAGING%\ffmpeg\x86\avutil-61.dll" >nul
        copy /y "%FFMPEG_X86_DIR%\swresample-7.dll" "%STAGING%\ffmpeg\x86\swresample-7.dll" >nul
        copy /y "%FFMPEG_X86_DIR%\swscale-10.dll" "%STAGING%\ffmpeg\x86\swscale-10.dll" >nul
    ) else (
        echo       [INFO] Static zero-dependency x86 binary without external DLLs.
    )
    if defined FFMPEG_LICENSE (
        copy /y "%FFMPEG_LICENSE%" "%STAGING%\ffmpeg\x86\LICENSE" >nul
    )
) else (
    echo       [INFO] x86 binary not present on build machine. Creating manifest placeholder...
    (
        echo FFmpeg x86 ^(32-bit^) binary is not bundled in this build.
        echo For Windows 7 Embedded / POSReady 7 32-bit terminals, obtain a Win32 static build
        echo as documented in SOURCES.md and place ffmpeg.exe into C:\l4tools\ffmpeg\ffmpeg.exe.
    ) > "%STAGING%\ffmpeg\x86\README_X86.txt"
)

copy /y "%~dp0VERSION.txt" "%STAGING%\ffmpeg\VERSION.txt" >nul
copy /y "%~dp0README.md" "%STAGING%\ffmpeg\README.md" >nul
copy /y "%~dp0SOURCES.md" "%STAGING%\ffmpeg\SOURCES.md" >nul

echo [4/5] Generating SHA-256 manifest (ffmpeg.sha256)...
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Write-Sha256.ps1" -StageRoot "%STAGING%\ffmpeg"
if errorlevel 1 exit /b 1

echo [5/5] Creating zip archive %OUT_ZIP%...
if not exist "%DIST_DIR%" md "%DIST_DIR%"
if exist "%OUT_ZIP%" del /f /q "%OUT_ZIP%"

powershell -NoProfile -NonInteractive -InputFormat None -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGING%\ffmpeg\*' -DestinationPath '%OUT_ZIP%' -CompressionLevel Fastest -Force"
if errorlevel 1 exit /b 1

if not exist "%OUT_ZIP%" (
    echo [ERROR] Failed to create %OUT_ZIP%
    exit /b 1
)

echo Generating distribution checksum %OUT_ZIP%.sha256...
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Write-Sha256.ps1" -ArchivePath "%OUT_ZIP%"
if errorlevel 1 exit /b 1

if "%CANDIDATE_MODE%"=="0" (
    copy /y "%OUT_ZIP%" "%~dp0bin\ffmpeg.zip" >nul 2>nul
    copy /y "%OUT_ZIP%" "%REPO_TOOLS%\l4superv\bin\ffmpeg.zip" >nul 2>nul
)

echo ===============================================================
echo  [OK] Successfully packed FFmpeg package:
echo    - %OUT_ZIP%
echo    - %OUT_ZIP%.sha256
echo ===============================================================
exit /b 0
