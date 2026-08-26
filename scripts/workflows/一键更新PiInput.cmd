@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0PiInput-OneClick-Update.ps1"
set "PIINPUT_UPDATE_EXIT=%ERRORLEVEL%"
if not "%PIINPUT_UPDATE_EXIT%"=="0" (
  echo.
  echo PiInput update failed. Exit code: %PIINPUT_UPDATE_EXIT%
) else (
  echo.
  echo PiInput update completed successfully.
)
echo.
pause
exit /b %PIINPUT_UPDATE_EXIT%
