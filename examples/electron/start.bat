@echo off
REM ============================================================
REM  Halcon_UI Electron 客户端 — 一键启动
REM  用法: 双击此文件，或在终端运行 start.bat
REM ============================================================
cd /d "%~dp0"
call node_modules\.bin\electron.cmd .
