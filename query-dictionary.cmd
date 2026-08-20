@echo off
setlocal
chcp 65001 >nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\query-dictionary.ps1"
if errorlevel 1 (
  echo.
  echo 词库查询失败。
)
echo.
pause
