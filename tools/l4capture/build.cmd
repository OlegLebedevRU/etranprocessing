@echo off
setlocal

rem ============================================================
rem l4capture build script - MSVC x86/x64 static (/MT)
rem Usage: build.cmd [all|x86|x64]
rem ============================================================

set "ROOT=%~dp0"
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=all"

rem --- Locate MSVC environment ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)
if not defined VS_PATH (
    echo ERROR: Visual Studio not found.
    exit /b 1
)

rem --- Prepare output directories ---
if not exist "%ROOT%obj\x86" mkdir "%ROOT%obj\x86"
if not exist "%ROOT%obj\x64" mkdir "%ROOT%obj\x64"
if not exist "%ROOT%bin\x86" mkdir "%ROOT%bin\x86"
if not exist "%ROOT%bin\x64" mkdir "%ROOT%bin\x64"

rem ======================== x86 ========================
if "%ARCH%"=="x64" goto skip_x86

echo.
echo === Building x86 (32-bit, Win7 SP1 target) ===
call "%VS_PATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 ( echo ERROR: vcvars32.bat failed & exit /b 1 )

rc.exe /nologo /d"NDEBUG" /fo "%ROOT%obj\x86\l4capture.res" "%ROOT%res\l4capture.rc"
if errorlevel 1 ( echo ERROR: Resource compilation failed (x86) & exit /b 1 )

cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I "%ROOT%include" /I "%ROOT%vendor\openh264\codec\api" /Fo"%ROOT%obj\x86\\" /Fd"%ROOT%obj\x86\\" "%ROOT%src\common\clock.c" "%ROOT%src\common\limits.c" "%ROOT%src\common\deadline.c" "%ROOT%src\common\pipeline.c" "%ROOT%src\ipc\ipc_protocol.c" "%ROOT%src\ipc\ipc_pipe.c" "%ROOT%src\safety\safety_gate.c" "%ROOT%src\capture\cursor.c" "%ROOT%src\capture\gdi_capture.c" "%ROOT%src\pipeline\scale.c" "%ROOT%src\pipeline\color_convert.c" "%ROOT%src\network\rtp_packetizer.c" "%ROOT%src\network\rtp_sender.c" "%ROOT%src\network\rtcp_sender.c" "%ROOT%src\main.c" /c
if errorlevel 1 ( echo ERROR: Compilation failed (x86) & exit /b 1 )

link.exe /nologo /SUBSYSTEM:CONSOLE,6.01 /OUT:"%ROOT%bin\x86\l4capture.exe" "%ROOT%obj\x86\clock.obj" "%ROOT%obj\x86\limits.obj" "%ROOT%obj\x86\deadline.obj" "%ROOT%obj\x86\pipeline.obj" "%ROOT%obj\x86\ipc_protocol.obj" "%ROOT%obj\x86\ipc_pipe.obj" "%ROOT%obj\x86\safety_gate.obj" "%ROOT%obj\x86\cursor.obj" "%ROOT%obj\x86\gdi_capture.obj" "%ROOT%obj\x86\scale.obj" "%ROOT%obj\x86\color_convert.obj" "%ROOT%obj\x86\rtp_packetizer.obj" "%ROOT%obj\x86\rtp_sender.obj" "%ROOT%obj\x86\rtcp_sender.obj" "%ROOT%obj\x86\main.obj" "%ROOT%obj\x86\l4capture.res" kernel32.lib user32.lib advapi32.lib gdi32.lib ws2_32.lib ole32.lib
if errorlevel 1 ( echo ERROR: Linking failed (x86) & exit /b 1 )

echo === x86 build OK ===
copy /y "%ROOT%bin\x86\l4capture.exe" "%ROOT%bin\l4capture.exe" >nul

:skip_x86

rem ======================== x64 ========================
if "%ARCH%"=="x86" goto skip_x64

echo.
echo === Building x64 (64-bit) ===
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo ERROR: vcvars64.bat failed & exit /b 1 )

rc.exe /nologo /d"NDEBUG" /fo "%ROOT%obj\x64\l4capture.res" "%ROOT%res\l4capture.rc"
if errorlevel 1 ( echo ERROR: Resource compilation failed (x64) & exit /b 1 )

cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I "%ROOT%include" /I "%ROOT%vendor\openh264\codec\api" /Fo"%ROOT%obj\x64\\" /Fd"%ROOT%obj\x64\\" "%ROOT%src\common\clock.c" "%ROOT%src\common\limits.c" "%ROOT%src\common\deadline.c" "%ROOT%src\common\pipeline.c" "%ROOT%src\ipc\ipc_protocol.c" "%ROOT%src\ipc\ipc_pipe.c" "%ROOT%src\safety\safety_gate.c" "%ROOT%src\capture\cursor.c" "%ROOT%src\capture\gdi_capture.c" "%ROOT%src\pipeline\scale.c" "%ROOT%src\pipeline\color_convert.c" "%ROOT%src\network\rtp_packetizer.c" "%ROOT%src\network\rtp_sender.c" "%ROOT%src\network\rtcp_sender.c" "%ROOT%src\main.c" /c
if errorlevel 1 ( echo ERROR: Compilation failed (x64) & exit /b 1 )

link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:"%ROOT%bin\x64\l4capture.exe" "%ROOT%obj\x64\clock.obj" "%ROOT%obj\x64\limits.obj" "%ROOT%obj\x64\deadline.obj" "%ROOT%obj\x64\pipeline.obj" "%ROOT%obj\x64\ipc_protocol.obj" "%ROOT%obj\x64\ipc_pipe.obj" "%ROOT%obj\x64\safety_gate.obj" "%ROOT%obj\x64\cursor.obj" "%ROOT%obj\x64\gdi_capture.obj" "%ROOT%obj\x64\scale.obj" "%ROOT%obj\x64\color_convert.obj" "%ROOT%obj\x64\rtp_packetizer.obj" "%ROOT%obj\x64\rtp_sender.obj" "%ROOT%obj\x64\rtcp_sender.obj" "%ROOT%obj\x64\main.obj" "%ROOT%obj\x64\l4capture.res" kernel32.lib user32.lib advapi32.lib gdi32.lib ws2_32.lib ole32.lib
if errorlevel 1 ( echo ERROR: Linking failed (x64) & exit /b 1 )

echo === x64 build OK ===

:skip_x64

echo.
echo === BUILD COMPLETE ===
exit /b 0
