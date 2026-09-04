@echo off
call "%~dp0install_mosquitto.cmd" %*
exit /b %ERRORLEVEL%
