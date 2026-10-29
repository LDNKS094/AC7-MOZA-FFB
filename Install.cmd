@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Install.ps1" %*
set "AC7_INSTALL_RESULT=%ERRORLEVEL%"
pause
exit /b %AC7_INSTALL_RESULT%
