@echo off
setlocal
rem This launcher stays pure ASCII on purpose. A .cmd file carries no encoding
rem mark, so Chinese text in it is decoded with whatever code page the machine
rem happens to use and turns to mojibake on a non-Chinese Windows. The Chinese
rem output all comes from the PowerShell script, which is UTF-8 with a BOM and
rem is therefore read correctly by both Windows PowerShell and PowerShell 7.
rem The code page switch below is what lets that Chinese reach the console.
chcp 65001 >nul
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0PiInput-OneClick-Update.ps1" %*
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
