@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0enable-startup.ps1"
if errorlevel 1 (
  echo Failed to enable startup.
  pause
  exit /b 1
)
echo Quick Translate startup enabled.
pause
