@echo off
setlocal
set "CURRENT=%LOCALAPPDATA%\LiteIME\Dev\current.txt"
set "PREVIEW="
if exist "%CURRENT%" set /p VERSION_DIR=<"%CURRENT%"
if defined VERSION_DIR set "PREVIEW=%VERSION_DIR%\bin\liteime-preview.exe"
if not defined PREVIEW set "PREVIEW=%~dp0dist\windows-x64\bin\liteime-preview.exe"
if not exist "%PREVIEW%" (
  echo LiteIME Preview is not installed.
  echo Run setup-dev.cmd first.
  exit /b 1
)
start "LiteIME Preview" "%PREVIEW%"
endlocal
