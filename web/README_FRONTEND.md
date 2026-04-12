# Halcon_UI 前端开发文档

## 1. 概述

Halcon_UI 是一个 Halcon 扩展包，内置 HTTP 服务器，用于将 Halcon 处理的图像和数据实时推送到浏览器/Electron 前端。

**架构**：

```
┌─────────────┐     HTTP API      ┌─────────────────┐
│  Halcon      │ ◄──────────────► │  浏览器/Electron │
│  (后端DLL)   │   端口 9090       │  (前端页面)      │
└─────────────┘                   └─────────────────┘
```

- 后端：Halcon 扩展 DLL，启动 HTTP 服务器，推送图像和命令
- 前端：纯 HTML/CSS/JS，放在 `web/` 目录下，服务器自动 serve
- 通信：标准 HTTP 协议，无 WebSocket

---

## 2. 部署方式

### 2.1 目录结构

前端文件放在 Halcon_UI **项目根目录**下的 `web/` 文件夹中：

```
Halcon_UI/
├── bin/
│   └── Halcon_UI.dll      ← 编译产物
├── web/                    ← 前端文件放这里
│   ├── index.html
│   ├── style.css
│   ├── app.js
│   └── ...（其他前端资源）
├── source/
├── include/
└── ...
```

DLL 启动后会自动定位到 `bin/../web/` 目录，serve 其中的所有静态文件。

### 2.2 访问方式

Halcon 端执行 `WCreateWebServer(9090, ServerID)` 后，浏览器访问：

```
http://127.0.0.1:9090/
```

服务器自动返回 `web/index.html`。所有相对路径的资源（CSS/JS/图片/字体）都能正常加载。

### 2.3 开发流程

1. 修改 `web/` 下的 HTML/CSS/JS 文件
2. 保存文件
3. 浏览器刷新页面（F5）

**不需要重新编译 C++ DLL。** 静态文件是实时从磁盘读取的。

### 2.4 支持的文件类型

| 扩展名 | Content-Type |
|--------|-------------|
| `.html` / `.htm` | `text/html; charset=utf-8` |
| `.css` | `text/css; charset=utf-8` |
| `.js` | `application/javascript; charset=utf-8` |
| `.json` | `application/json` |
| `.png` | `image/png` |
| `.jpg` / `.jpeg` | `image/jpeg` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.woff2` | `font/woff2` |
| 其他 | `application/octet-stream` |

### 2.5 使用前端框架（Vue/React）

如果需要使用前端框架：

1. 在项目外创建 Vue/React 项目
2. 构建后将 `dist/` 目录的内容复制到 `web/` 目录
3. 或修改构建配置，直接输出到 `web/` 目录

框架开发时可以用自己的 dev server（如 `vite dev`），API 请求代理到 `http://127.0.0.1:9090`。

---

## 3. HTTP API 参考

### 3.1 GET /api/stream — 图像/数据流（Chunked Streaming）

**用途**：接收 Halcon 推送的实时图像和数据，长连接不断推帧。

**请求**：
```
GET /api/stream HTTP/1.1
Host: 127.0.0.1:9090
```

**响应**：
```
HTTP/1.1 200 OK
Content-Type: application/octet-stream
Transfer-Encoding: chunked
Connection: keep-alive
```

响应体是持续的 chunked 流，每个 chunk 包含一帧数据。

#### 帧格式（二进制）

每帧的格式为：

```
┌──────────────┬──────────────┬───────────────┬──────────────┐
│ json_len     │ data_len     │ JSON 字符串   │ 二进制数据   │
│ (4字节 LE)   │ (4字节 LE)   │ (含 \0 结尾)  │ (图像等)     │
└──────────────┴──────────────┴───────────────┴──────────────┘
```

- **json_len**：JSON 字符串的字节长度（包含末尾 `\0`）
- **data_len**：二进制数据的字节长度（无图像时为 0）
- **JSON 字符串**：UTF-8 编码，以 `\0` 结尾
- **二进制数据**：图像像素数据（格式见下文）

#### JSON 字段说明

**图像数据（CMD=0）**：
```json
{
  "CMD": 0,
  "Data": {
    "宽": 1920,
    "高": 1080,
    "位深": 1,
    "通道": 3,
    "id": 0,
    "fmt": "jpeg"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `CMD` | int | 固定为 `0`，表示图像数据 |
| `Data.宽` | int | 图像宽度（像素） |
| `Data.高` | int | 图像高度（像素） |
| `Data.位深` | int | 每通道字节数：`1`=8bit, `2`=16bit |
| `Data.通道` | int | 原始通道数：`1`=灰度, `3`=RGB |
| `Data.id` | int | **窗口 ID**（可选，默认 `0`），用于区分多个图像窗口 |
| `Data.fmt` | string | **编码格式**（可选）：`"jpeg"` 表示 JPEG 压缩，不存在则为 RAW 像素 |

**命令/消息（CMD≠0）**：
```json
{
  "CMD": 1,
  "Data": {
    "message": "处理完成",
    "result": 42
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `CMD` | int | 非零整数，由 Halcon 端定义 |
| `Data` | object | 自定义数据，字段由 Halcon 端决定 |

#### 图像二进制数据格式

**当 `fmt` = `"jpeg"`（默认）**：
- 二进制数据是标准 JPEG 文件内容
- 前端可直接用 `new Blob([data], {type: 'image/jpeg'})` 构造
- 推荐用 `createImageBitmap(blob)` 解码（GPU 加速）

**当 `fmt` 不存在（RAW 模式）**：
- 灰度图（通道=1）：`[W×H×位深]` 字节，逐行存储
- RGB 图（通道=3）：Planar 格式 `[R平面][G平面][B平面]`，每平面 `W×H×位深` 字节
- 前端需逐像素转换为 Canvas 的 RGBA 格式

#### JavaScript 接收示例

```javascript
// 连接 stream
const resp = await fetch('/api/stream');
const reader = resp.body.getReader();
let buf = new Uint8Array(0);

while (true) {
  const { value, done } = await reader.read();
  if (done) break;

  // 拼接到缓冲区
  buf = concat(buf, value);

  // 解析完整帧
  while (buf.length >= 8) {
    const jsonLen = readUint32LE(buf, 0);
    const dataLen = readUint32LE(buf, 4);
    const totalLen = 8 + jsonLen + dataLen;
    if (buf.length < totalLen) break; // 帧不完整，等下次数据

    // 解析 JSON（去掉末尾 \0）
    const jsonStr = new TextDecoder().decode(buf.slice(8, 8 + jsonLen - 1));
    const msg = JSON.parse(jsonStr);

    // 提取二进制数据
    const binaryData = buf.slice(8 + jsonLen, 8 + jsonLen + dataLen);

    // 处理帧
    if (msg.CMD === 0 && dataLen > 0) {
      const winId = msg.Data.id || 0;

      if (msg.Data.fmt === 'jpeg') {
        // JPEG 图像
        const blob = new Blob([binaryData], { type: 'image/jpeg' });
        const bmp = await createImageBitmap(blob);
        canvas.width = bmp.width;
        canvas.height = bmp.height;
        ctx.drawImage(bmp, 0, 0);
        bmp.close();
      } else {
        // RAW 图像：需要手动转换为 Canvas ImageData
        // 参见 app.js 中的实现
      }
    } else {
      // 非图像命令
      console.log('Command:', msg.CMD, msg.Data);
    }

    // 移除已处理的帧
    buf = buf.slice(totalLen);
  }
}

// 工具函数
function readUint32LE(buf, offset) {
  return buf[offset] | (buf[offset+1] << 8) |
         (buf[offset+2] << 16) | ((buf[offset+3] << 24) >>> 0);
}

function concat(a, b) {
  const c = new Uint8Array(a.length + b.length);
  c.set(a);
  c.set(b, a.length);
  return c;
}
```

#### 中断 Stream

```javascript
const abortCtrl = new AbortController();
fetch('/api/stream', { signal: abortCtrl.signal });

// 停止接收
abortCtrl.abort();
```

---

### 3.2 GET /api/poll — 单次轮询（备用）

**用途**：获取 Halcon 推送队列中的一条数据。适用于不需要持续流的场景。

**请求**：
```
GET /api/poll?timeout=3000 HTTP/1.1
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `timeout` | int | 等待超时（毫秒）。`0`=立即返回，`>0`=等待指定时间 |

**响应**：

- `200 OK`：有数据，body 为一帧二进制数据（格式同 stream 的单帧）
- `204 No Content`：超时，无数据

```javascript
const resp = await fetch('/api/poll?timeout=3000');
if (resp.status === 200) {
  const buf = new Uint8Array(await resp.arrayBuffer());
  // 解析同 stream 的单帧格式
}
```

> **注意**：`/api/stream` 和 `/api/poll` 共享同一个队列。同时使用两者会竞争数据。通常只选其一。

---

### 3.3 POST /api/command — 前端发送命令到 Halcon

**用途**：前端向 Halcon 发送 JSON 命令。

**请求**：
```
POST /api/command HTTP/1.1
Content-Type: application/json

{"CMD": 100, "Data": {"threshold": 128}}
```

**响应**：
```json
{"ok": true}
```

**Halcon 端接收**（HDevelop 代码）：
```
create_dict (RecvDict)
WRecvWebData (ServerID, 1000, RecvDict)
get_dict_tuple (RecvDict, '命令', CmdJson)
get_dict_tuple (CmdJson, 'CMD', CMD)
get_dict_tuple (CmdJson, 'Data', Data)
```

#### JSON 格式要求

```json
{
  "CMD": <整数>,
  "Data": { <自定义字段> }
}
```

- `CMD`：命令编号，由前后端约定
- `Data`：命令参数，任意 JSON 对象

#### JavaScript 发送示例

```javascript
async function sendCommand(cmd, data = {}) {
  const resp = await fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ CMD: cmd, Data: data })
  });
  return resp.ok;
}

// 示例
sendCommand(100, { threshold: 128 });
sendCommand(200);
sendCommand(999); // 退出
```

---

### 3.4 GET / 和 GET /\<path\> — 静态文件

**用途**：加载前端页面和资源文件。

```
GET /              → web/index.html
GET /style.css     → web/style.css
GET /app.js        → web/app.js
GET /img/logo.png  → web/img/logo.png
```

支持子目录，路径映射关系：`http://host:port/<path>` → `web/<path>`

---

## 4. 多窗口支持

Halcon 端在发送图像时，通过 `Data.id` 字段指定目标窗口：

```
* Halcon 端示例
set_dict_tuple (DataDict, 'id', 0)   * → 前端 Window 0
set_dict_tuple (DataDict, 'id', 1)   * → 前端 Window 1
set_dict_tuple (DataDict, 'id', 2)   * → 前端 Window 2
```

**前端处理逻辑**：
- 根据 `msg.Data.id` 找到或创建对应的 Canvas
- 每个窗口独立渲染、独立 FPS 统计
- `id` 字段可选，不传时默认为 `0`

**建议的前端实现**：

```javascript
// 维护窗口映射表
const windows = {};

function getOrCreateWindow(id) {
  if (windows[id]) return windows[id];

  // 创建 DOM 元素
  const canvas = document.createElement('canvas');
  canvas.width = 640;
  canvas.height = 480;
  document.getElementById('container').appendChild(canvas);

  windows[id] = {
    canvas: canvas,
    ctx: canvas.getContext('2d')
  };
  return windows[id];
}

// 在 handleFrame 中使用
const winId = msg.Data.id || 0;
const win = getOrCreateWindow(winId);
win.ctx.drawImage(bmp, 0, 0);
```

---

## 5. CORS 说明

所有 API 响应都包含以下 CORS 头：

```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type
```

因此前端可以从任意域名/端口访问 API。这意味着：
- 前端可以用 Vite dev server（端口 5173）开发，直接请求 `http://127.0.0.1:9090/api/...`
- Electron 应用可以直接请求后端 API
- 不需要代理配置

---

## 6. 注意事项

### 6.1 服务器生命周期

- Halcon 调用 `WCreateWebServer` 后服务器启动
- Halcon 调用 `WCloseWebServer` 后服务器关闭，所有连接断开
- 前端应处理 stream 断开后的重连逻辑

### 6.2 队列机制

- Halcon → 前端的数据经过一个深度为 **8** 的环形队列
- 队列满时**自动丢弃最旧的帧**（保证实时性）
- 前端消费速度跟不上时，会跳帧而不是卡顿

### 6.3 图像编码

- 当前后端默认将所有图像编码为 **JPEG（质量 80）** 后传输
- 一张 4000x3000 RGB 图：RAW ≈ 36MB → JPEG ≈ 200KB
- 前端通过 `Data.fmt === "jpeg"` 判断编码格式
- 如无 `fmt` 字段，数据为 RAW 像素（向后兼容）

### 6.4 Halcon 端 CMD 约定（建议）

| CMD | 含义 | 方向 |
|-----|------|------|
| `0` | 图像数据 | Halcon → 前端 |
| `1`-`99` | 通用消息/通知 | Halcon → 前端 |
| `100`-`199` | 前端请求 | 前端 → Halcon |
| `200`-`299` | 状态查询 | 双向 |
| `999` | 退出/关闭 | 前端 → Halcon |

> 以上为建议约定，实际 CMD 编号由具体项目定义。

---

## 7. 快速开始示例

### 7.1 最小前端（3 个文件）

**index.html**：
```html
<!DOCTYPE html>
<html>
<head><meta charset="UTF-8"><title>My Halcon UI</title></head>
<body>
  <canvas id="cv" width="640" height="480"></canvas>
  <button onclick="start()">Start</button>
  <script src="app.js"></script>
</body>
</html>
```

**app.js**：
```javascript
const cv = document.getElementById('cv');
const ctx = cv.getContext('2d');

function u32(b, o) {
  return b[o] | (b[o+1]<<8) | (b[o+2]<<16) | ((b[o+3]<<24)>>>0);
}
function cat(a, b) {
  const c = new Uint8Array(a.length + b.length);
  c.set(a); c.set(b, a.length); return c;
}

async function start() {
  const resp = await fetch('/api/stream');
  const reader = resp.body.getReader();
  let buf = new Uint8Array(0);

  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    buf = cat(buf, value);

    while (buf.length >= 8) {
      const jl = u32(buf, 0), dl = u32(buf, 4), total = 8 + jl + dl;
      if (buf.length < total) break;

      const json = JSON.parse(new TextDecoder().decode(buf.slice(8, 8+jl-1)));
      if (json.CMD === 0 && dl > 0 && json.Data.fmt === 'jpeg') {
        const blob = new Blob([buf.slice(8+jl, 8+jl+dl)], {type:'image/jpeg'});
        const bmp = await createImageBitmap(blob);
        cv.width = bmp.width; cv.height = bmp.height;
        ctx.drawImage(bmp, 0, 0);
        bmp.close();
      }
      buf = buf.slice(total);
    }
  }
}
```

### 7.2 Halcon 端示例

```
* 启动服务器
WCreateWebServer (9090, ServerID)
stop ()

* 推送一张图像到窗口 0
read_image (Image, 'fabrik')
get_image_size (Image, W, H)
count_channels (Image, Ch)

create_dict (SendDict)
create_dict (CmdDict)
create_dict (DataDict)
set_dict_tuple (CmdDict, 'CMD', 0)
set_dict_tuple (DataDict, '宽', W)
set_dict_tuple (DataDict, '高', H)
set_dict_tuple (DataDict, '位深', 1)
set_dict_tuple (DataDict, '通道', Ch)
set_dict_tuple (DataDict, 'id', 0)
set_dict_tuple (CmdDict, 'Data', DataDict)
set_dict_tuple (SendDict, '命令', CmdDict)
set_dict_object (Image, SendDict, '图')
WSendWebData (ServerID, SendDict)
```
