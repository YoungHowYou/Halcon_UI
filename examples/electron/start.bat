@echo off
REM ============================================================
REM  YouEyE Electron — 后台启动（不阻塞 Halcon）
REM  用法: Halcon 中 system_call('start.bat')
REM ============================================================
powershell -Command "Start-Process -FilePath '%~dp0node_modules\electron\dist\electron.exe' -ArgumentList '%~dp0.'"
