@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0disable-startup.ps1"
if errorlevel 1 (
  echo Failed to disable startup.
  pause
  exit /b 1
)
echo Quick Translate startup disabled.
pause
