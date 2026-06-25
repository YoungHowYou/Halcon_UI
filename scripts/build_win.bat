@echo off
REM ============================================================
REM  Halcon_UI Windows 构建脚本
REM  前置条件:
REM    1. 已安装 Visual Studio 2022 (含 C++ 桌面开发)
REM    2. 已安装 CMake >= 3.20
REM    3. 已安装 vcpkg 并设置 VCPKG_ROOT 环境变量
REM    4. 已设置 HALCONROOT / HALCONEXAMPLES 环境变量
REM
REM  用法:
REM    build_win.bat          → Debug 构建
REM    build_win.bat release  → Release 构建
REM ============================================================

setlocal enabledelayedexpansion

if "%1"=="release" (
    set PRESET=win-x64-release
    set CONFIG=Release
) else (
    set PRESET=win-x64-debug
    set CONFIG=Debug
)

echo [Halcon_UI] 构建配置: %CONFIG%
echo [Halcon_UI] CMake Preset: %PRESET%

REM 检查 VCPKG_ROOT
if "%VCPKG_ROOT%"=="" (
    echo [ERROR] 请先设置 VCPKG_ROOT 环境变量指向 vcpkg 安装目录
    exit /b 1
)

REM 检查 HALCONROOT
if "%HALCONROOT%"=="" (
    echo [ERROR] 请先设置 HALCONROOT 环境变量指向 Halcon 安装目录
    exit /b 1
)

REM 配置
echo [Halcon_UI] 正在配置 CMake...
cmake --preset %PRESET% -S .
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake 配置失败
    exit /b 1
)

REM 构建
echo [Halcon_UI] 正在构建...
cmake --build --preset %PRESET% --config %CONFIG%
if %ERRORLEVEL% neq 0 (
    echo [ERROR] 构建失败
    exit /b 1
)

echo [Halcon_UI] 构建完成！输出目录: bin\
endlocal
