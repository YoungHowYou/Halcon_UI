# Halcon_UI

Halcon 扩展包 —— 内置 HTTP 服务器，将 Halcon 处理的图像和数据实时推送到浏览器 / Electron 前端。

## 特性

- **纯 HTTP 协议**，浏览器 / Electron 原生支持，无需 WebSocket
- **Chunked Streaming 推送图像**，服务器主动推帧，零轮询延迟
- **JPEG 自动压缩**（GDI+），4000x3000 RGB 图从 36MB 压缩到 ~200KB
- **多窗口支持**，通过 `id` 字段区分不同图像显示窗口
- **前端热更新**，修改 `web/` 目录下的 HTML/CSS/JS 保存即生效，不用重新编译 DLL
- **双向通信**，前端可通过 POST 发送命令给 Halcon
- **CORS 全开**，支持前端框架（Vue/React/Electron）独立开发
- **Halcon 接口简洁**，4 个操作符，和原有扩展包风格一致

## 项目结构

```
Halcon_UI/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文件
├── def/
│   └── Halcon_UI.def           # Halcon 操作符定义
├── include/
│   ├── Halcon_UI.h             # DLL 导出声明
│   ├── websocket.h             # HTTP 服务器 C 接口
│   ├── ws_queue.h              # 线程安全环形队列
│   └── ws_config.h             # 配置常量
├── source/
│   ├── websocket.cpp           # HTTP 服务器实现（Chunked Streaming + 静态文件服务）
│   ├── Halcon_UI.cpp           # Halcon 扩展包装层（含 GDI+ JPEG 编码）
│   └── Halcon_UI.c             # C 包装层（DEF 入口）
├── web/                        # 前端文件（服务器自动 serve）
│   ├── index.html
│   ├── style.css
│   ├── app.js
│   └── README_FRONTEND.md      # 前端开发文档
├── examples/                   # HDevelop 例程
│   ├── test_minimal.hdev       # 最小测试
│   ├── demo_stream_image.hdev  # 连续推送图像流
│   └── demo_command_loop.hdev  # 双向命令交互
└── bin/                        # 编译输出
    └── Halcon_UI.dll
```

## 环境要求

- **Halcon** 24.11 或更高版本
- **Visual Studio** 2022 Build Tools（或完整版）
- **CMake** 4.1+
- **操作系统** Windows 10/11（GDI+ 系统自带）

## 编译

```bash
# 确保 HALCONROOT 和 HALCONEXAMPLES 环境变量已设置
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Debug
```

编译产物输出到 `bin/` 目录。

## 安装

将项目路径添加到系统环境变量 `HALCONEXTENSIONS`：

```
HALCONEXTENSIONS=...;D:\path\to\Halcon_UI
```

添加后**重启 HDevelop** 使扩展包生效。

## Halcon 操作符

### WCreateWebServer — 创建 HTTP 服务器

```
WCreateWebServer (Port, ServerID)
```

| 参数 | 方向 | 类型 | 说明 |
|------|------|------|------|
| Port | 输入 | integer | 监听端口（如 9090） |
| ServerID | 输出 | integer | 服务器 ID，后续操作使用 |

### WSendWebData — 发送数据到前端

```
WSendWebData (ServerID, DictHandle)
```

| 参数 | 方向 | 类型 | 说明 |
|------|------|------|------|
| ServerID | 输入 | integer | 服务器 ID |
| DictHandle | 输入 | handle | 包含数据的字典 |

**字典结构**：

```
DictHandle
├── "命令" → CmdDict
│   ├── "CMD"  → 0（图像）或其他整数（命令）
│   └── "Data" → DataDict
│       ├── "宽"   → 图像宽度
│       ├── "高"   → 图像高度
│       ├── "位深"  → 1（8bit）或 2（16bit）
│       ├── "通道"  → 1（灰度）或 3（RGB）
│       └── "id"   → 窗口 ID（可选，默认 0）
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

Halcon 端通过 `Data.id` 字段指定目标窗口：

```
set_dict_tuple (DataDict, 'id', 0)   * 窗口 0
set_dict_tuple (DataDict, 'id', 1)   * 窗口 1
set_dict_tuple (DataDict, 'id', 2)   * 窗口 2
```

前端根据 `id` 自动创建独立的 Canvas 窗口，每个窗口独立显示和统计 FPS。不设 `id` 时默认窗口 0。

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
