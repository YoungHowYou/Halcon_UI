#!/usr/bin/env bash
# ============================================================
#  YouEyE Electron — Linux 启动脚本
#  用法:
#    ./start.sh            普通启动
#    ./start.sh --dev      启动并打开 DevTools
#    ./start.sh ws://192.168.1.100:8080/ws   指定 WS 地址
# ============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ---------- 检查 Node.js ----------
if ! command -v node &>/dev/null; then
    echo "[ERROR] 未找到 Node.js，请先安装 Node.js >= 18"
    exit 1
fi
echo "[INFO] Node.js $(node --version)"

# ---------- 安装依赖（如果缺失） ----------
if [ ! -d "node_modules" ]; then
    echo "[INFO] 首次运行，正在安装依赖..."
    npm install
    echo "[INFO] 依赖安装完成"
fi

# ---------- 解析参数 ----------
ELECTRON_ARGS=("$SCRIPT_DIR")
for arg in "$@"; do
    case "$arg" in
        --dev)
            ELECTRON_ARGS+=("--dev")
            ;;
        ws://*|wss://*)
            # 通过环境变量传入 WS URL（preload.js 会优先读取 config.json）
            echo "[INFO] 使用 WS 地址: $arg"
            # 临时覆写 config.json
            echo "{\"wsUrl\": \"$arg\"}" > "$SCRIPT_DIR/config.json"
            ;;
        *)
            ELECTRON_ARGS+=("$arg")
            ;;
    esac
done

# ---------- 定位 electron ----------
ELECTRON_BIN="$SCRIPT_DIR/node_modules/electron/dist/electron"
if [ ! -f "$ELECTRON_BIN" ]; then
    echo "[ERROR] 未找到 Electron 二进制文件: $ELECTRON_BIN"
    echo "[ERROR] 请运行 npm install 安装依赖"
    exit 1
fi

# ---------- 启动 ----------
echo "[INFO] 启动 YouEyE Electron..."
"$ELECTRON_BIN" "${ELECTRON_ARGS[@]}"
