@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\update-dictionaries.ps1"
set "exit_code=%ERRORLEVEL%"
echo.
if not "%exit_code%"=="0" (
  echo PiInput dictionary update failed. Existing dictionaries were preserved.
) else (
  echo PiInput dictionaries are up to date.
)
pause
exit /b %exit_code%
