# Halcon_UI 算子文档

本扩展包共提供 **4 个算子**，全部归属于 `foundation` 模块、`UserExtensions` 章节，用于在 Halcon 内部启动 WebSocket 服务器，与浏览器 / Electron 前端进行双向数据交换。

| 算子 | 作用 | 参数签名 |
|------|------|----------|
| [WCreateWebServer](#wcreatewebserver) | 创建并启动 WebSocket 服务器 | `(::Port : ServerID)` |
| [WRecvWebData](#wrecvwebdata)         | 从前端接收一帧数据 | `(::ServerID, Timeout, DictHandle :)` |
| [WSendWebData](#wsendwebdata)         | 向所有已连接的前端推送一帧数据 | `(::ServerID, DictHandle :)` |
| [WCloseWebServer](#wclosewebserver)   | 关闭服务器并断开所有客户端 | `(::ServerID :)` |

> 字典约定：传入 `WSendWebData` / 接收自 `WRecvWebData` 的字典统一使用键 `'命令'` 包装一个子字典，子字典内含 `'CMD'` (整数) 与 `'Data'` (子字典)。若需随帧附带图像，再向最外层字典使用 `set_dict_object` 写入键 `'图'`。完整命令编号与字段定义详见 [PROTOCOL.md](PROTOCOL.md)。

---

## WCreateWebServer

创建一个 WebSocket 服务器并开始监听指定端口。返回一个服务器句柄，用于后续收发与关闭操作。

### 签名

```
WCreateWebServer( : : Port : ServerID)
```

### 参数

| 参数 | 方向 | 类型 | 语义 | 说明 |
|------|------|------|------|------|
| `Port`     | input_control  | integer | number | 监听端口号，例如 `3000`、`9090` |
| `ServerID` | output_control | handle  | handle | 新建服务器的句柄，传给后续算子 |

### 行为

- 启动后台线程接受 WebSocket 连接，并同时托管 `web/` 目录的静态文件。
- 同一进程可以多次调用以创建多个服务器（端口必须不同）。
- 若端口被占用或绑定失败，算子返回错误。

### HDevelop 示例

```
Port := 3000
WCreateWebServer (Port, ServerID)
* 浏览器打开 http://127.0.0.1:3000 即可建立 WebSocket 连接
```

---

## WRecvWebData

从前端接收一帧命令数据。算子会阻塞直至有新数据到达或超时。

### 签名

```
WRecvWebData( : : ServerID, Timeout, DictHandle : )
```

### 参数

| 参数 | 方向 | 类型 | 语义 | 说明 |
|------|------|------|------|------|
| `ServerID`   | input_control | handle  | handle | `WCreateWebServer` 返回的服务器句柄 |
| `Timeout`    | input_control | integer | number | 等待超时时间（毫秒）。`0` 表示不等待，立即返回 |
| `DictHandle` | input_control | handle  | handle | 由用户预先 `create_dict` 创建的字典；算子将解析后的命令写入其中 |

> `DictHandle` 在 .def 中声明为 `input_control`，但语义上是 **被算子填充** 的：调用方先 `create_dict` 一个空字典传入，返回后从中读字段。

### 返回字典结构

成功收到一帧后，`DictHandle` 中至少包含键 `'命令'`，其值为子字典：

```
DictHandle
└─ '命令'  (dict)
   ├─ 'CMD'  : integer   # 命令编号
   └─ 'Data' : dict      # 命令负载
```

若 `Timeout` 时间内未收到数据，算子返回错误（或字典为空，由调用方自行判断）。

### HDevelop 示例

```
create_dict (RecvDict)
WRecvWebData (ServerID, 1000, RecvDict)
get_dict_tuple (RecvDict, '命令', CmdDict)
get_dict_tuple (CmdDict, 'CMD', CMD)
get_dict_tuple (CmdDict, 'Data', Data)

if (CMD == 200)
    * 前端查询设备连接状态，应回复 CMD=200
endif
```

---

## WSendWebData

将一帧 JSON（可附带图像）广播给所有已连接的前端客户端。

### 签名

```
WSendWebData( : : ServerID, DictHandle : )
```

### 参数

| 参数 | 方向 | 类型 | 语义 | 说明 |
|------|------|------|------|------|
| `ServerID`   | input_control | handle | handle | 服务器句柄 |
| `DictHandle` | input_control | handle | handle | 待发送的字典，结构见下文 |

### 入参字典结构

```
DictHandle
├─ '命令'  (dict)
│  ├─ 'CMD'  : integer   # 命令编号
│  └─ 'Data' : dict      # 业务字段
└─ '图'    : Image       # 可选；若存在，将被自动 JPEG 压缩并作为二进制体随帧发出
```

- 不带图像时，仅传输 JSON 部分。
- 带图像时，扩展会自动用 GDI+ 进行 JPEG 编码（4000×3000 RGB ~200KB），并按帧格式（4 字节 `json_len` + 4 字节 `data_len` + JSON + 二进制）打包发送。
- 如果 `Data` 中包含 `'图号'` 字段，前端按其值路由到对应控件。
- 内部环形队列深度为 8，队列满时丢弃最旧帧以保证实时性。

### HDevelop 示例

推送一张图像到主预览区：

```
read_image (Image, 'fabrik')
get_image_size (Image, W, H)
count_channels (Image, Ch)

create_dict (SendDict)
create_dict (CmdDict)
create_dict (DataDict)
set_dict_tuple (CmdDict, 'CMD', 0)
set_dict_tuple (DataDict, '图号', 0)
set_dict_tuple (DataDict, '宽',  W)
set_dict_tuple (DataDict, '高',  H)
set_dict_tuple (DataDict, '通道', Ch)
set_dict_tuple (CmdDict, 'Data', DataDict)
set_dict_tuple (SendDict, '命令', CmdDict)
set_dict_object (Image, SendDict, '图')

WSendWebData (ServerID, SendDict)
```

推送一条检测结果（不带图像）：

```
create_dict (SendDict)
create_dict (CmdDict)
create_dict (DataDict)
set_dict_tuple (CmdDict, 'CMD', 1)
set_dict_tuple (DataDict, 'rodId',       'INGOT-20260414-001')
set_dict_tuple (DataDict, 'result',      'NG')
set_dict_tuple (DataDict, 'defectType',  '隐裂')
set_dict_tuple (DataDict, 'defectCount', 2)
set_dict_tuple (DataDict, 'duration',    156)
set_dict_tuple (DataDict, 'confidence',  0.973)
set_dict_tuple (CmdDict, 'Data', DataDict)
set_dict_tuple (SendDict, '命令', CmdDict)

WSendWebData (ServerID, SendDict)
```

---

## WCloseWebServer

关闭服务器、停止后台线程、断开所有客户端，并销毁句柄。

### 签名

```
WCloseWebServer( : : ServerID : )
```

### 参数

| 参数 | 方向 | 类型 | 语义 | 说明 |
|------|------|------|------|------|
| `ServerID` | input_control | handle | handle | 待关闭的服务器句柄；调用后该句柄不再有效 |

### HDevelop 示例

```
WCloseWebServer (ServerID)
```

---

## 端到端流程示例

```
* 1. 启动服务器
WCreateWebServer (3000, ServerID)

* 2. 等待前端连上后，进入收发循环
while (true)
    create_dict (RecvDict)
    WRecvWebData (ServerID, 100, RecvDict)
    get_dict_tuple (RecvDict, '命令', CmdDict)
    get_dict_tuple (CmdDict, 'CMD', CMD)

    if (CMD == 999)
        break
    endif

    * 推一张图回去
    read_image (Image, 'fabrik')
    get_image_size (Image, W, H)
    count_channels (Image, Ch)
    create_dict (SendDict)
    create_dict (OutCmd)
    create_dict (OutData)
    set_dict_tuple (OutCmd, 'CMD', 0)
    set_dict_tuple (OutData, '图号', 0)
    set_dict_tuple (OutData, '宽', W)
    set_dict_tuple (OutData, '高', H)
    set_dict_tuple (OutData, '通道', Ch)
    set_dict_tuple (OutCmd, 'Data', OutData)
    set_dict_tuple (SendDict, '命令', OutCmd)
    set_dict_object (Image, SendDict, '图')
    WSendWebData (ServerID, SendDict)
endwhile

* 3. 关闭
WCloseWebServer (ServerID)
```

更多 CMD 编号、字段定义、时序图见 [PROTOCOL.md](PROTOCOL.md)；可运行例程见 [examples/demo_ws_send_image.hdev](examples/demo_ws_send_image.hdev)。
