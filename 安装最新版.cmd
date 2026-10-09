@echo off
setlocal
title ESP-HI Install Latest - COM5
cd /d "%~dp0"
echo Close the device chat window before installing.
".toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe" -X utf8 tools\install_latest.py
pause
endlocal
