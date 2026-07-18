@echo off
setlocal
set "PREVIEW=%LOCALAPPDATA%\LiteIME\Dev\bin\liteime-preview.exe"
if not exist "%PREVIEW%" (
  echo LiteIME Preview is not installed.
  echo Run setup-dev.cmd first.
  exit /b 1
)
start "LiteIME Preview" "%PREVIEW%"
endlocal
