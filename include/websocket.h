#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 创建 WebSocket 服务器
// port: 监听端口
// 返回值: 服务器 ID (>=0)，失败返回 -1
int CreateWebServer(uint16_t port);

// 发送数据（广播给所有已连接的客户端）
// server_id: 服务器 ID
// jsontext:  JSON 格式的元数据字符串（会自动添加 \0 结尾）
// data:      二进制数据缓冲区（可为 NULL）
// length:    二进制数据长度
// 返回值: 0 表示成功，-1 表示错误
int SendWebData(int server_id, const char* jsontext, const char* data, size_t length);

// 接收数据（从任意已连接的客户端）
// server_id:   服务器 ID
// jsontext:    输出参数，返回 JSON 字符串（需调用者 free）
// data:        输出参数，返回二进制数据缓冲区（需调用者 free）
// out_length:  输出参数，返回二进制数据长度
// timeout_ms:  超时时间（毫秒）
//      > 0: 等待指定毫秒数
//      = 0: 非阻塞，立即返回
//      < 0: 永久阻塞
// 返回值: 0 表示成功，-1 表示错误，-2 表示超时
int RecvWebData(int server_id, char** jsontext, char** data, size_t* out_length, int timeout_ms);

// 关闭 WebSocket 服务器
// server_id: 服务器 ID
void CloseWebServer(int server_id);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_H
