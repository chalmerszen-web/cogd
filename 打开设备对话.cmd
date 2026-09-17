@echo off
setlocal
title ESP-HI Chat - COM5
cd /d "%~dp0"
".toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe" -X utf8 -m serial.tools.miniterm COM5 115200 --encoding UTF-8 --eol LF --echo --dtr 0 --rts 0
if errorlevel 1 pause
endlocal
