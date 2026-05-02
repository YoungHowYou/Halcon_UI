# SiRod Inspector 前后端通信协议

## 1. 通信方式

所有通信基于 Halcon_UI 内置 HTTP 服务器，端口默认 `9090`。

| 方向 | 接口 | 说明 |
|------|------|------|
| 后端 → 前端 | `GET /api/stream` | 长连接 chunked 流，推送图像和命令帧 |
| 后端 → 前端 | `GET /api/poll?timeout=ms` | 单次轮询，取一帧数据 |
| 前端 → 后端 | `POST /api/command` | 发送 JSON 命令 |

> `/api/stream` 和 `/api/poll` 共享同一个队列，同时使用会竞争数据，通常只选其一。

---

## 2. 帧格式（二进制）

`/api/stream` 和 `/api/poll` 返回的每帧结构：

```
┌──────────────┬──────────────┬───────────────┬──────────────┐
│ json_len     │ data_len     │ JSON 字符串    │ 二进制数据    │
│ (4字节 LE)   │ (4字节 LE)   │ (含 \0 结尾)  │ (图像等)     │
└──────────────┴──────────────┴───────────────┴──────────────┘
```

- `json_len`：JSON 字符串字节长度（含末尾 `\0`）
- `data_len`：二进制数据字节长度（无二进制时为 `0`）
- JSON 字符串：UTF-8 编码，统一格式 `{ "CMD": <int>, "Data": { ... } }`

---

## 3. CMD 编号总表

| CMD | 方向 | 名称 | 说明 |
|-----|------|------|------|
| `0` | 后端 → 前端 | 图像数据 | 携带二进制图像，前端渲染到 Canvas |
| `1` | 后端 → 前端 | 检测结果 | 单次检测完成后推送结果 |
| `2` | 后端 → 前端 | 晶棒编号更新 | 扫码器读到新编号时推送 |
| `3` | 后端 → 前端 | 设备连接状态 | 各外设连接状态变化时推送 |
| `4` | 后端 → 前端 | 系统资源 | CPU / 内存占用，周期推送 |
| `5` | 后端 → 前端 | 飞书同步状态 | 飞书写入成功/失败/重试时推送 |
| `100` | 前端 → 后端 | 更新 TCP 通信设置 | |
| `101` | 前端 → 后端 | 更新 MySQL 数据库设置 | |
| `102` | 前端 → 后端 | 更新班次清零设置 | |
| `103` | 前端 → 后端 | 更新缺陷类型列表 | |
| `104` | 前端 → 后端 | 更新飞书同步设置 | |
| `105` | 前端 → 后端 | 更新图像存储设置 | |
| `106` | 前端 → 后端 | 更新产线设置 | |
| `200` | 前端 → 后端 | 查询设备连接状态 | 后端回复 CMD=200 |
| `200` | 后端 → 前端 | 设备连接状态回复 | 对前端 CMD=200 的回复 |
| `201` | 前端 → 后端 | 查询统计数据 | 后端回复 CMD=201 |
| `201` | 后端 → 前端 | 统计数据回复 | 对前端 CMD=201 的回复 |
| `202` | 后端 → 前端 | 历史记录回复 | 分页返回检测记录 |
| `203` | 前端 → 后端 | 生成报告 | 请求后端导出报告 |
| `210` | 后端 → 前端 | 设置读取回复 | 后端返回当前保存的全部设置 |
| `999` | 前端 → 后端 | 退出 / 关闭服务器 | |

---

## 4. 后端 → 前端 数据格式

### 4.1 CMD=0 图像数据

```json
{
  "CMD": 0,
  "Data": {
    "宽": 1920,
    "高": 1080,
    "图号": 0,
    "通道": 3,
    "fmt": "jpeg"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `Data.宽` | int | 图像宽度（像素） |
| `Data.高` | int | 图像高度（像素） |
| `Data.图号` | int | 图像控件编号，前端据此路由到对应控件。`0`=主预览区，其他值动态创建额外窗口 |
| `Data.通道` | int | `1`=灰度, `3`=RGB |
| `Data.fmt` | string | `"jpeg"` = JPEG 压缩；不存在 = RAW 像素（Planar RGB） |

二进制部分：
- `fmt="jpeg"` 时为标准 JPEG 文件字节流
- 无 `fmt` 时为 RAW 像素（8bit/通道），灰度 `[W×H]`，RGB Planar `[R][G][B]` 各 `W×H`

---

### 4.2 CMD=1 检测结果

每完成一次检测后推送。

```json
{
  "CMD": 1,
  "Data": {
    "rodId": "INGOT-20260414-001",
    "result": "NG",
    "defectType": "隐裂",
    "defectCount": 2,
    "duration": 156,
    "confidence": 0.973,
    "imageData": null
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `rodId` / `晶棒编号` | string | 当前晶棒编号 |
| `result` / `结果` | string | `"OK"` 或 `"NG"` |
| `defectType` / `缺陷类型` | string | 缺陷类型名称，无缺陷时为 `"--"` |
| `defectCount` / `缺陷数` | int | 本次检测发现的缺陷数量 |
| `duration` / `耗时` | int | 检测耗时（毫秒） |
| `confidence` / `置信度` | float | 0~1，模型置信度 |
| `imageData` | string/null | Base64 编码的缺陷图像（可选），用于缺陷图库展示 |

> 字段名支持中英文双写，前端优先取英文名。

---

### 4.3 CMD=2 晶棒编号更新

扫码器读取到新编号时推送。

```json
{
  "CMD": 2,
  "Data": {
    "rodId": "INGOT-20260414-002",
    "source": "二维码",
    "rodCount": 0
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `rodId` / `晶棒编号` | string | 新的晶棒编号 |
| `source` / `来源` | string | 编号来源：`"二维码"` / `"手动输入"` / `"PLC"` |
| `rodCount` / `本棒检测数` | int | 该棒已检测次数（新棒为 `0`） |

---

### 4.4 CMD=3 设备连接状态

设备状态变化时推送，或作为前端 CMD=200 查询的回复。

```json
{
  "CMD": 3,
  "Data": {
    "cam": true,
    "plc": true,
    "db": true,
    "feishu": "syncing",
    "scanner": true,
    "scannerPort": "COM3"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `cam` | bool | NIR 相机连接状态 |
| `plc` | bool | PLC 连接状态 |
| `db` | bool | MySQL 数据库连接状态 |
| `feishu` | string | 飞书同步状态：`"ok"` / `"syncing"` / `"error"` |
| `scanner` | bool | 二维码扫描器连接状态 |
| `scannerPort` | string | 扫描器串口号，如 `"COM3"` |

前端颜色映射：`true`/`"ok"` → 绿色, `"syncing"` → 蓝色, `false`/`"error"` → 红色

---

### 4.5 CMD=4 系统资源

周期推送（建议 1~3 秒），用于底部状态栏显示。

```json
{
  "CMD": 4,
  "Data": {
    "cpu": 23.5,
    "mem": 61.2,
    "resolution": "4000x3000"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `cpu` | float | CPU 占用百分比 |
| `mem` | float | 内存占用百分比 |
| `resolution` | string | 当前图像分辨率（可选） |

---

### 4.6 CMD=5 飞书同步状态

每次飞书写入后推送。

```json
{
  "CMD": 5,
  "Data": {
    "status": "ok",
    "message": ""
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | `"ok"` 写入成功 / `"syncing"` 重试中 / `"error"` 失败 |
| `message` | string | 错误信息（仅 `status="error"` 时有值） |

---

### 4.7 CMD=200 设备连接状态回复

格式同 CMD=3。当前端发送 CMD=200 查询时，后端以此回复。

---

### 4.8 CMD=201 统计数据回复

前端查询统计或后端主动推送。

```json
{
  "CMD": 201,
  "Data": {
    "total": 1024,
    "ok": 980,
    "ng": 44,
    "totalYesterday": 950,
    "avgTime": 142,
    "hourly": [2, 5, 8, 15, 22, 35, 48, 62, 55, 70, 68, 75, 80, 72, 65, 58, 50, 45, 38, 30, 22, 15, 8, 3],
    "defectTypes": {
      "隐裂": 28,
      "崩边": 12,
      "其他": 4
    },
    "dailyOK": [96.2, 97.1, 95.8, 98.0, 97.5, 96.8, 97.3],
    "durations": [120, 135, 98, 156, 142, 110, 189]
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `total` | int | 本班次总检测数 |
| `ok` | int | 合格数量 |
| `ng` | int | NG 数量 |
| `totalYesterday` | int | 昨日总检测数（用于计算涨幅） |
| `avgTime` | int | 平均检测耗时（ms） |
| `hourly` | int[24] | 0~23 时每小时检测量数组 |
| `defectTypes` | object | 缺陷类型 → 数量的映射 |
| `dailyOK` | float[] | 近 7 日合格率数组（百分比） |
| `durations` | int[] | 近期检测时长样本（用于直方图） |

---

### 4.9 CMD=202 历史记录回复

```json
{
  "CMD": 202,
  "Data": {
    "totalRecords": 1024,
    "records": [
      {
        "id": 1,
        "rodId": "INGOT-20260414-001",
        "time": "2026-04-14 09:32:15",
        "result": "OK",
        "defectType": "--",
        "defectCount": 0,
        "duration": 135,
        "confidence": 0.998
      }
    ]
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `totalRecords` | int | 符合筛选条件的总记录数 |
| `records` | array | 当页记录数组，每条格式同 CMD=1 的 Data |

---

### 4.10 CMD=210 设置读取回复

后端返回当前保存的全部设置，用于前端回填表单。

```json
{
  "CMD": 210,
  "Data": {
    "tcp": { "ip": "127.0.0.1", "port": 3000 },
    "db": {
      "ip": "127.0.0.1", "port": 3306,
      "user": "root", "pass": "",
      "name": "sirod", "table": "inspections"
    },
    "shift": { "dayShift": "08:00", "nightShift": "20:00" },
    "defectTypes": ["隐裂", "崩边", "其他"],
    "feishu": {
      "enable": true,
      "appId": "cli_xxxx",
      "appSecret": "****",
      "appToken": "bascxxxx",
      "tableId": "tblxxxx",
      "api": "https://open.feishu.cn"
    },
    "image": { "enable": true, "dir": "D:/SiRod_Images" },
    "pipeline": "PV-B02"
  }
}
```

---

## 5. 前端 → 后端 数据格式

所有前端命令通过 `POST /api/command` 发送，Body 为 JSON：

```json
{ "CMD": <int>, "Data": { ... } }
```

---

### 5.1 CMD=100 更新 TCP 通信设置

```json
{
  "CMD": 100,
  "Data": {
    "ip": "127.0.0.1",
    "port": 3000
  }
}
```

---

### 5.2 CMD=101 更新 MySQL 数据库设置

```json
{
  "CMD": 101,
  "Data": {
    "ip": "127.0.0.1",
    "port": 3306,
    "user": "root",
    "pass": "password123",
    "name": "sirod",
    "table": "inspections"
  }
}
```

---

### 5.3 CMD=102 更新班次清零设置

```json
{
  "CMD": 102,
  "Data": {
    "dayShift": "08:00",
    "nightShift": "20:00"
  }
}
```

后端收到后应在对应时刻清零当班统计。

---

### 5.4 CMD=103 更新缺陷类型列表

```json
{
  "CMD": 103,
  "Data": {
    "types": ["隐裂", "崩边", "气泡", "划痕", "其他"]
  }
}
```

前端在添加/删除缺陷类型时会立即发送此命令，不需要等用户点"保存"。

---

### 5.5 CMD=104 更新飞书同步设置

```json
{
  "CMD": 104,
  "Data": {
    "enable": true,
    "appId": "cli_xxxxxxxxxxxx",
    "appSecret": "xxxxxxxxxxxxxxxx",
    "appToken": "bascxxxxxxxxxxxx",
    "tableId": "tblxxxxxxxxxxxx",
    "api": "https://open.feishu.cn"
  }
}
```

---

### 5.6 CMD=105 更新图像存储设置

```json
{
  "CMD": 105,
  "Data": {
    "enable": true,
    "dir": "D:/SiRod_Images"
  }
}
```

---

### 5.7 CMD=106 更新产线设置

```json
{
  "CMD": 106,
  "Data": {
    "pipeline": "PV-B02"
  }
}
```

---

### 5.8 CMD=200 查询设备连接状态

```json
{
  "CMD": 200,
  "Data": {}
}
```

后端收到后应回复 CMD=200（或通过 stream 推送 CMD=3），携带各设备连接状态。

前端会每 **3 秒** 发送一次此查询。

---

### 5.9 CMD=201 查询统计数据

```json
{
  "CMD": 201,
  "Data": {}
}
```

后端收到后应回复 CMD=201，携带完整统计数据。

前端会每 **3 秒** 发送一次此查询。

---

### 5.10 CMD=203 生成报告

```json
{
  "CMD": 203,
  "Data": {
    "dateFrom": "2026-04-01",
    "dateTo": "2026-04-14",
    "resultFilter": "NG",
    "defectFilter": "隐裂"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `dateFrom` | string | 起始日期，空字符串表示不限 |
| `dateTo` | string | 截止日期，空字符串表示不限 |
| `resultFilter` | string | 结果筛选：`""` / `"OK"` / `"NG"` |
| `defectFilter` | string | 缺陷类型筛选，空字符串表示全部 |

---

### 5.11 CMD=999 退出 / 关闭服务器

```json
{
  "CMD": 999,
  "Data": {}
}
```

后端收到后应安全关闭 HTTP 服务器和所有连接。

---

## 6. 时序示例

### 6.1 页面启动

```
前端                                      后端
  │                                         │
  │──── GET /api/stream ──────────────────►│  建立长连接
  │                                         │
  │──── POST CMD=200 {} ──────────────────►│  查询设备状态
  │◄─── stream CMD=200 {cam,plc,db,...} ───│  回复设备状态
  │                                         │
  │──── POST CMD=201 {} ──────────────────►│  查询统计数据
  │◄─── stream CMD=201 {total,ok,ng,...} ──│  回复统计数据
  │                                         │
  │◄─── stream CMD=0 {图像} ───────────────│  持续推送图像帧
  │◄─── stream CMD=0 {图像} ───────────────│
  │     ...                                 │
```

### 6.2 检测流程

```
前端                                      后端
  │                                         │
  │◄─── stream CMD=2 {rodId,source} ───────│  扫码器读到新编号
  │                                         │
  │◄─── stream CMD=0 {图像 图号=0} ───────│  推送 NIR 图像
  │                                         │
  │◄─── stream CMD=1 {result,defect,...} ──│  推送检测结果
  │                                         │
  │◄─── stream CMD=5 {status:"ok"} ────────│  飞书写入完成
```

### 6.3 修改设置

```
前端                                      后端
  │                                         │
  │──── POST CMD=100 {ip,port} ───────────►│  更新 TCP 设置
  │──── POST CMD=101 {ip,port,user,...} ──►│  更新 MySQL 设置
  │──── POST CMD=102 {dayShift,...} ───────►│  更新班次设置
  │──── POST CMD=103 {types:[...]} ────────►│  更新缺陷类型
  │──── POST CMD=104 {enable,appId,...} ──►│  更新飞书设置
  │──── POST CMD=105 {enable,dir} ─────────►│  更新图像存储
  │──── POST CMD=106 {pipeline} ───────────►│  更新产线标识
  │                                         │
  │◄─── (各模块返回 HTTP 200 {"ok":true}) ─│
```

---

## 7. 后端实现要点

### 7.1 Halcon 端接收前端命令

```
* 接收并分发前端命令
create_dict (RecvDict)
WRecvWebData (ServerID, 1000, RecvDict)
get_dict_tuple (RecvDict, '命令', CmdJson)
get_dict_tuple (CmdJson, 'CMD', CMD)
get_dict_tuple (CmdJson, 'Data', Data)

* 根据 CMD 分发处理
if (CMD == 100)
    * 处理 TCP 设置: get_dict_tuple(Data, 'ip', IP) ...
elseif (CMD == 101)
    * 处理 MySQL 设置
elseif (CMD == 102)
    * 处理班次设置
elseif (CMD == 103)
    * 处理缺陷类型列表: get_dict_tuple(Data, 'types', Types)
elseif (CMD == 200)
    * 查询设备状态 → 回复 CMD=200
elseif (CMD == 201)
    * 查询统计 → 回复 CMD=201
elseif (CMD == 999)
    * 关闭服务器
endif
```

### 7.2 后端推送命令到前端

```
* 推送检测结果示例
create_dict (SendDict)
create_dict (CmdDict)
create_dict (DataDict)
set_dict_tuple (CmdDict, 'CMD', 1)
set_dict_tuple (DataDict, 'rodId', RodId)
set_dict_tuple (DataDict, 'result', 'NG')
set_dict_tuple (DataDict, 'defectType', '隐裂')
set_dict_tuple (DataDict, 'defectCount', 2)
set_dict_tuple (DataDict, 'duration', 156)
set_dict_tuple (DataDict, 'confidence', 0.973)
set_dict_tuple (CmdDict, 'Data', DataDict)
set_dict_tuple (SendDict, '命令', CmdDict)
WSendWebData (ServerID, SendDict)
```

---

## 8. 注意事项

1. **队列深度**：后端环形队列深度为 8，队列满时自动丢弃最旧帧，保证实时性
2. **轮询频率**：前端每 3 秒发送 CMD=200 和 CMD=201，后端应能在此频率下正常响应
3. **字段兼容**：检测结果 CMD=1 的字段名同时支持中文（`晶棒编号`）和英文（`rodId`），建议后端统一用英文
4. **设置持久化**：前端在 localStorage 中保存一份设置副本作为兜底，但以后端持久化为准
5. **缺陷类型实时同步**：前端添加/删除缺陷类型时立即发送 CMD=103，无需等待用户点"保存"
6. **CORS**：后端已配置 `Access-Control-Allow-Origin: *`，前端可从任意域名访问
