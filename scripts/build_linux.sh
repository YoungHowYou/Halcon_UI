#!/bin/bash
# ============================================================
#  Halcon_UI Linux 构建脚本
#  前置条件:
#    1. 已安装 CMake >= 3.20, make, gcc/g++
#    2. 已安装 vcpkg 并设置 VCPKG_ROOT 环境变量
#    3. 已设置 HALCONROOT / HALCONEXAMPLES 环境变量
#
#  用法:
#    ./build_linux.sh          → Debug 构建
#    ./build_linux.sh release  → Release 构建
# ============================================================

set -euo pipefail

if [ "${1:-}" = "release" ]; then
    PRESET="linux-x64-release"
    CONFIG="Release"
else
    PRESET="linux-x64-debug"
    CONFIG="Debug"
fi

echo "[Halcon_UI] 构建配置: ${CONFIG}"
echo "[Halcon_UI] CMake Preset: ${PRESET}"

# 检查 VCPKG_ROOT
if [ -z "${VCPKG_ROOT:-}" ]; then
    echo "[ERROR] 请先设置 VCPKG_ROOT 环境变量指向 vcpkg 安装目录"
    exit 1
fi

# 检查 HALCONROOT
if [ -z "${HALCONROOT:-}" ]; then
    echo "[ERROR] 请先设置 HALCONROOT 环境变量指向 Halcon 安装目录"
    exit 1
fi

# 配置
echo "[Halcon_UI] 正在配置 CMake..."
cmake --preset "${PRESET}" -S .
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake 配置失败"
    exit 1
fi

# 构建
echo "[Halcon_UI] 正在构建..."
cmake --build --preset "${PRESET}" --config "${CONFIG}"
if [ $? -ne 0 ]; then
    echo "[ERROR] 构建失败"
    exit 1
fi

# 输出路径
if [ "$(uname -s)" = "Linux" ]; then
    echo "[Halcon_UI] 构建完成！输出目录: lib/x64-linux/"
else
    echo "[Halcon_UI] 构建完成！输出目录: bin/"
fi
