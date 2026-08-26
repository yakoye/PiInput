@echo off
setlocal
chcp 65001 >nul
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0PiInput-OneClick-Update.ps1" %*
set "PIINPUT_UPDATE_EXIT=%ERRORLEVEL%"
if not "%PIINPUT_UPDATE_EXIT%"=="0" (
  echo.
  echo PiInput 更新失败，退出码：%PIINPUT_UPDATE_EXIT%
) else (
  echo.
  echo PiInput 更新完成。
)
echo.
pause
exit /b %PIINPUT_UPDATE_EXIT%
