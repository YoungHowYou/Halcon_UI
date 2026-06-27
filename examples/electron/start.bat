@echo off
REM ============================================================
REM  YouEyE Electron 客户端 — 后台启动（不阻塞 Halcon）
REM  用法: Halcon 中 system_call('start.bat')
REM ============================================================
cd /d "%~dp0"
start "" "%~dp0node_modules\electron\dist\electron.exe" "%~dp0"
