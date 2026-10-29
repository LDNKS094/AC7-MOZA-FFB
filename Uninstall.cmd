@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Uninstall.ps1" %*
set "AC7_UNINSTALL_RESULT=%ERRORLEVEL%"
pause
exit /b %AC7_UNINSTALL_RESULT%
