@echo off
setlocal
chcp 65001 >nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-portable-tests.ps1" -PackageRoot "%~dp0"
set "exit_code=%ERRORLEVEL%"
if not "%exit_code%"=="0" echo PiInput portable tests failed with exit code %exit_code%.
pause
exit /b %exit_code%

