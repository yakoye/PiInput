@echo off
setlocal
if "%~1"=="" (
  echo Usage: set-schema.cmd full^|flypy^|natural^|mspy^|abc
  exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0set-schema.ps1" -Schema "%~1"
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
