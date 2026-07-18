@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-ime-tests.ps1"
set "exit_code=%ERRORLEVEL%"
echo.
if not "%exit_code%"=="0" echo LiteIME input tests failed.
pause
exit /b %exit_code%
