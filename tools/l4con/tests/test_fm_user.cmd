@echo off
setlocal
set "VSCMD_SKIP_SENDTELEMETRY=1"
set "FM_USER_VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if not "%~1"=="" goto :single
call :run x86
if errorlevel 1 exit /b 1
call :run x64
exit /b %errorlevel%
:single
call :run %1
exit /b %errorlevel%
:run
cmd /c ""%FM_USER_VS%" -arch=%1 -no_logo && cl.exe /nologo /O2 /W4 /WX /MT /utf-8 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /D_WIN32_WINNT=0x0601 /DUNICODE /D_UNICODE /Foobj\%1\ tests\test_fm_user.c /link /OUT:obj\%1\test_fm_user.exe advapi32.lib wtsapi32.lib"
if errorlevel 1 exit /b 1
obj\%1\test_fm_user.exe
exit /b %errorlevel%
