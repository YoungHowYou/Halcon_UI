# Halcon_UI

Halcon 扩展包 —— 内置 HTTP/WebSocket 服务器，将 Halcon 处理的图像和数据实时推送到浏览器 / Electron 前端。

## 特性

- **纯 HTTP/WebSocket 协议**，浏览器 / Electron 原生支持
- **Chunked Streaming 推送图像**，服务器主动推帧，零轮询延迟
- **JPEG 自动压缩**（libjpeg-turbo），4000×3000 RGB 图从 36MB 压缩到 ~200KB
- **多窗口支持**，通过 `id` 字段区分不同图像显示窗口
- **前端热更新**，修改 `web/` 目录下的 HTML/CSS/JS 保存即生效，不用重新编译
- **双向通信**，前端可通过 POST/WebSocket 发送命令给 Halcon
- **CORS 全开**，支持前端框架（Vue/React/Electron）独立开发
- **Halcon 接口简洁**，4 个操作符，和原有扩展包风格一致
- **跨平台**：Windows / Linux 统一代码路径

## 项目结构

```
Halcon_UI/
├── CMakeLists.txt              # CMake 构建配置（跨平台）
├── CMakePresets.json           # CMake Presets（vcpkg 集成）
├── vcpkg.json                  # vcpkg 依赖清单
├── README.md                   # 本文件
├── PROTOCOL.md                 # 前后端通信协议
├── OPERATORS.md                # Halcon 操作符文档
├── .gitignore
├── def/
│   └── Halcon_UI.def           # Halcon 操作符定义
├── include/
│   ├── Halcon_UI.h             # DLL/SO 导出声明
│   ├── websocket.h             # HTTP/WebSocket 服务器 C 接口
│   ├── ws_queue.h              # 线程安全环形队列
│   └── ws_config.h             # 配置常量
├── src/                        # 源代码
│   ├── websocket.cpp           # HTTP/WebSocket 服务器实现
│   ├── Halcon_UI.cpp           # Halcon 扩展包装层（JPEG 编码）
│   └── Halcon_UI.c             # C 包装层（DEF 入口）
├── web/                        # 前端文件（服务器自动 serve）
│   ├── index.html
│   ├── style.css
│   ├── app.js
│   └── README_FRONTEND.md
├── examples/                   # HDevelop 例程
│   ├── demo.hdev
│   └── test_server.hdev
├── scripts/                    # 构建脚本
│   ├── build_win.bat           # Windows 一键构建
│   └── build_linux.sh          # Linux 一键构建
├── bin/                        # Windows 编译输出 (Halcon_UI.dll)
└── lib/x64-linux/              # Linux 编译输出 (libHalcon_UI.so)
```

## 环境要求

| 依赖 | Windows | Linux |
|------|---------|-------|
| **Halcon** | 24.11+ | 24.11+ |
| **CMake** | 3.20+ | 3.20+ |
| **编译器** | Visual Studio 2022 | GCC 9+ / Clang 10+ |
| **vcpkg** | ✅ 必需 | ✅ 必需 |
| **libjpeg-turbo** | 通过 vcpkg | 通过 vcpkg |

## 快速开始

### 1. 安装 vcpkg

```bash
# 克隆 vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh   # Linux
# 或
bootstrap-vcpkg.bat    # Windows

# 设置环境变量
export VCPKG_ROOT=/path/to/vcpkg   # Linux
set VCPKG_ROOT=C:\path\to\vcpkg    # Windows
```

### 2. 设置 Halcon 环境

```bash
export HALCONROOT=/opt/halcon          # Linux
export HALCONEXAMPLES=$HALCONROOT/examples

set HALCONROOT=C:\Program Files\MVTec\HALCON-24.11   # Windows
set HALCONEXAMPLES=%HALCONROOT%\examples
```

### 3. 构建

**Windows:**
```bash
scripts\build_win.bat          # Debug
scripts\build_win.bat release  # Release
```

**Linux:**
```bash
chmod +x scripts/build_linux.sh
./scripts/build_linux.sh          # Debug
./scripts/build_linux.sh release  # Release
```

或使用 CMake Presets 手动构建：

```bash
# 配置（自动通过 vcpkg 安装 libjpeg-turbo）
cmake --preset win-x64-debug      # Windows Debug
cmake --preset linux-x64-release  # Linux Release

# 构建
cmake --build --preset win-x64-debug
```

编译产物：
- **Windows** → `bin/Halcon_UI.dll`
- **Linux** → `lib/x64-linux/libHalcon_UI.so`

### 4. 安装扩展包

将项目路径添加到系统环境变量 `HALCONEXTENSIONS`：

```
HALCONEXTENSIONS=...;D:\path\to\Halcon_UI   # Windows
HALCONEXTENSIONS=...:/path/to/Halcon_UI     # Linux
```

添加后**重启 HDevelop** 使扩展包生效。

## 三方库管理

本项目使用 **vcpkg manifest 模式** 管理所有第三方依赖：

```json
// vcpkg.json
{
  "dependencies": [
    "libjpeg-turbo"   // JPEG 编解码（跨平台，替代 GDI+）
  ]
}
```

- **Windows**: 不再依赖 GDI+，统一使用 libjpeg-turbo
- **Linux**: 不再需要系统 `libjpeg-dev` 或 bundled .so
- 所有平台均由 vcpkg 自动下载、编译、链接

添加新依赖：
```bash
# 编辑 vcpkg.json，添加依赖条目后重新配置 CMake 即可
cmake --preset win-x64-debug
```

## Halcon 操作符

### WCreateWebServer — 创建 HTTP 服务器

```
WCreateWebServer (Port, WebRoot, ServerID)
```

| 参数 | 方向 | 类型 | 说明 |
|------|------|------|------|
| Port | 输入 | integer | 监听端口（如 9090） |
| WebRoot | 输入 | string | 静态文件根目录（空串=自动） |
| ServerID | 输出 | handle | 服务器句柄 |

### WSendWebData — 发送数据到前端

```
WSendWebData (ServerID, DictHandle)
```

| 参数 | 方向 | 类型 | 说明 |
|------|------|------|------|
| ServerID | 输入 | handle | 服务器句柄 |
| DictHandle | 输入 | handle | 包含数据的字典 |

### WRecvWebData — 从前端接收数据

```
WRecvWebData (ServerID, Timeout, DictHandle)
```

### WCloseWebServer — 关闭服务器

```
WCloseWebServer (ServerID)
```

详细协议见 [PROTOCOL.md](./PROTOCOL.md)。

```
DictHandle
├── "命令" → CmdDict
│   ├── "CMD"  → 0（图像）或其他整数（命令）
│   └── "Data" → DataDict
│       ├── "宽"   → 图像宽度
│       ├── "高"   → 图像高度
│       ├── "图号"  → 图像控件编号（整数，可选，默认 0）
│       └── "通道"  → 1（灰度）或 3（RGB）
└── "图" → HObject 图像（CMD=0 时必填）
```

图像会自动编码为 JPEG 传输，Halcon 端无需手动压缩。

### WRecvWebData — 接收前端命令

```
WRecvWebData (ServerID, Timeout, DictHandle)
```

| 参数 | 方向 | 类型 | 说明 |
|------|------|------|------|
| ServerID | 输入 | integer | 服务器 ID |
| Timeout | 输入 | integer | 超时毫秒（-1=永久，0=立即，>0=等待） |
| DictHandle | 输入 | handle | 预创建的空字典，接收后填充 |

接收后字典包含 `"命令"` 键，值为前端发来的 JSON dict。

### WCloseWebServer — 关闭服务器

```
WCloseWebServer (ServerID)
```

| 参数 | 方向 | 类型 | 说明 |
|------|------|------|------|
| ServerID | 输入 | integer | 服务器 ID |

## 快速开始

### 1. 编译并安装

按上述步骤编译，将路径加入 `HALCONEXTENSIONS`，重启 HDevelop。

### 2. 运行例程

在 HDevelop 中打开 `examples/test_minimal.hdev`：

```
WCreateWebServer (9090, ServerID)
stop ()
```

### 3. 浏览器访问

打开浏览器访问 `http://127.0.0.1:9090`，点击 **Start Stream**。

### 4. 推送图像

回到 HDevelop 按 F5 继续执行，浏览器即可看到图像。

## HTTP API

服务器提供以下 HTTP 接口：

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/` | 返回前端页面（web/index.html） |
| `GET` | `/api/stream` | Chunked Streaming，持续推送图像和命令 |
| `GET` | `/api/poll?timeout=N` | 单次轮询（备用），获取一帧数据 |
| `POST` | `/api/command` | 前端发送 JSON 命令到 Halcon |
| `GET` | `/<path>` | 静态文件（web/ 目录下的 CSS/JS/图片等） |

### Stream 帧格式

每帧为二进制数据：

```
┌──────────────┬──────────────┬───────────────┬──────────────┐
│ json_len     │ data_len     │ JSON 字符串   │ 二进制数据   │
│ (4字节 LE)   │ (4字节 LE)   │ (含 \0 结尾)  │ (JPEG 图像)  │
└──────────────┴──────────────┴───────────────┴──────────────┘
```

### 前端命令格式

```json
{
  "CMD": 100,
  "Data": { "threshold": 128 }
}
```

详细 API 文档见 [web/README_FRONTEND.md](web/README_FRONTEND.md)。

## 多窗口

Halcon 端通过 `Data.图号` 字段指定目标图像控件：

```
set_dict_tuple (DataDict, '图号', 0)   * 主预览区
set_dict_tuple (DataDict, '图号', 1)   * 控件 1
set_dict_tuple (DataDict, '图号', 2)   * 控件 2
```

前端根据 `图号` 路由到对应控件：`图号=0` 显示在主预览区，其他整数动态创建独立 Canvas 窗口（每个窗口独立显示和统计 FPS）。不设 `图号` 时默认 `0`。

## 前端开发

前端文件放在 `web/` 目录下，服务器自动 serve。

- **修改保存即生效**，不用重编译 DLL
- **支持框架开发**，Vue/React 构建后输出到 `web/` 即可
- **CORS 全开**，开发时可用自己的 dev server，API 直接请求 `http://127.0.0.1:9090`

详细前端开发指南见 [web/README_FRONTEND.md](web/README_FRONTEND.md)。

## 架构说明

```
┌──────────────────────────────────────────────────────┐
│                    Halcon / HDevelop                  │
│  WCreateWebServer  WSendWebData  WRecvWebData        │
└──────────┬───────────────┬───────────────┬───────────┘
           │               │               │
    ┌──────▼──────┐  ┌─────▼─────┐  ┌──────▼──────┐
    │ HTTP Server │  │ JPEG 编码 │  │   命令队列  │
    │ (线程池)    │  │ (GDI+)    │  │ (环形缓冲)  │
    └──────┬──────┘  └───────────┘  └──────┬──────┘
           │                               │
    ┌──────▼───────────────────────────────▼──────┐
    │              HTTP API (端口 9090)            │
    │  GET /api/stream    POST /api/command        │
    │  GET /              GET /<static files>       │
    └──────┬──────────────────────────────┬───────┘
           │                              │
    ┌──────▼──────┐                ┌──────▼──────┐
    │   浏览器    │                │  Electron   │
    │ Chrome/Edge │                │   桌面应用   │
    └─────────────┘                └─────────────┘
```

**数据流**：

- **图像推送**：Halcon → JPEG 编码 → send_queue → Chunked Stream → 浏览器
- **命令接收**：浏览器 → POST /api/command → recv_queue → Halcon WRecvWebData

**线程模型**：

- **Accept 线程**：监听端口，为每个连接创建 Handler 线程
- **Handler 线程**：处理 HTTP 请求，stream 连接保持长连接持续推帧
- **Halcon 主线程**：调用 WSendWebData / WRecvWebData

## 配置参数

| 参数 | 文件 | 默认值 | 说明 |
|------|------|--------|------|
| `WS_MAX_QUEUE_SIZE` | ws_config.h | 8 | 发送/接收队列深度，队列满时丢弃最旧帧 |
| `WS_MAX_CLIENTS` | ws_config.h | 16 | 最大同时连接数（已移除，每连接一个线程） |
| `WS_DEFAULT_BACKLOG` | ws_config.h | 10 | TCP accept 队列长度 |
| JPEG 质量 | Halcon_UI.cpp | 80 | JPEG 编码质量（1-100） |

## License

MIT
