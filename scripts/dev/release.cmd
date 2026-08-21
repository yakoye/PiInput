@echo off
setlocal
chcp 65001 >nul
set "PSEXE=pwsh.exe"
where pwsh.exe >nul 2>&1 || set "PSEXE=powershell.exe"
"%PSEXE%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows\release.ps1" %*
set "CODE=%ERRORLEVEL%"
echo.
if not "%CODE%"=="0" echo 发布未完成，退出码 %CODE%。
pause
exit /b %CODE%
