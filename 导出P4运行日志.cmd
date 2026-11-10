@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo 正在从 P4 导出设备日志。完整导出可能需要数分钟，请保持 USB 连接。
".toolchains\tools\python_env\idf6.1_py3.11_env\Scripts\python.exe" -X utf8 "tools\p4_logs.py" export
if errorlevel 1 (
  echo 导出未完全成功，请保留窗口中的提示。
) else (
  echo 导出完成。文件位于项目的 .local\p4-logs 文件夹。
)
pause
