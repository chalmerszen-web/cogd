@echo off
setlocal
cd /d "%~dp0"
".toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe" -X utf8 tools/kws/capture_human_review.py
pause
endlocal
