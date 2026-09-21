@echo off
setlocal

rem ============================================================
rem l4capture test script - builds and runs test suite (x86)
rem Usage: test.cmd
rem ============================================================

set "ROOT=%~dp0."

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
    echo ERROR: Visual Studio installation not found.
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: vcvars32.bat failed
    exit /b 1
)

if not exist "%ROOT%\obj\test" mkdir "%ROOT%\obj\test"
if not exist "%ROOT%\bin" mkdir "%ROOT%\bin"

echo === Compiling tests ===

cl.exe /nologo /O2 /MT /W4 /utf-8 /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS ^
    /I "%ROOT%\include" /I "%ROOT%\vendor\openh264\codec\api" ^
    /Fo"%ROOT%\obj\test\\" /Fd"%ROOT%\obj\test\\" ^
    "%ROOT%\src\common\clock.c" ^
    "%ROOT%\src\common\limits.c" ^
    "%ROOT%\src\common\deadline.c" ^
    "%ROOT%\src\common\pipeline.c" ^
    "%ROOT%\src\ipc\ipc_protocol.c" ^
    "%ROOT%\src\ipc\ipc_pipe.c" ^
    "%ROOT%\src\safety\safety_gate.c" ^
    "%ROOT%\tests\test_runner.c" ^
    "%ROOT%\tests\test_ipc_framing.c" ^
    "%ROOT%\tests\test_limits.c" ^
    "%ROOT%\tests\test_deadline.c" ^
    "%ROOT%\tests\test_safety_gate.c" ^
    /c
if errorlevel 1 (
    echo ERROR: Test compilation failed
    exit /b 1
)

link.exe /nologo /SUBSYSTEM:CONSOLE,6.01 /OUT:"%ROOT%\bin\l4capture_tests.exe" ^
    "%ROOT%\obj\test\clock.obj" ^
    "%ROOT%\obj\test\limits.obj" ^
    "%ROOT%\obj\test\deadline.obj" ^
    "%ROOT%\obj\test\pipeline.obj" ^
    "%ROOT%\obj\test\ipc_protocol.obj" ^
    "%ROOT%\obj\test\ipc_pipe.obj" ^
    "%ROOT%\obj\test\safety_gate.obj" ^
    "%ROOT%\obj\test\test_runner.obj" ^
    "%ROOT%\obj\test\test_ipc_framing.obj" ^
    "%ROOT%\obj\test\test_limits.obj" ^
    "%ROOT%\obj\test\test_deadline.obj" ^
    "%ROOT%\obj\test\test_safety_gate.obj" ^
    kernel32.lib user32.lib advapi32.lib gdi32.lib ws2_32.lib ole32.lib
if errorlevel 1 (
    echo ERROR: Test linking failed
    exit /b 1
)

echo === Running tests ===
"%ROOT%\bin\l4capture_tests.exe"
set "TEST_RC=%errorlevel%"

if "%TEST_RC%"=="0" (
    echo.
    echo === ALL TESTS PASSED ===
) else (
    echo.
    echo === TESTS FAILED (exit code %TEST_RC%) ===
)

exit /b %TEST_RC%
