@echo off
setlocal
title ESP-HI Workspace Cleanup
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\cleanup_workspace.ps1" -Apply
if errorlevel 1 echo Cleanup did not complete. See the message above.
pause
endlocal
