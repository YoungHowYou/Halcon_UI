#ifndef WS_CONFIG_H
#define WS_CONFIG_H

#include <stdint.h>

// 帧头魔数（大端序写入），与 PROTOCOL.md §4.1 一致
#define WS_MAGIC_NUMBER 0xDEADBEEFu

// 收发队列最大深度（按消息条数）
#ifndef WS_MAX_QUEUE_SIZE
#define WS_MAX_QUEUE_SIZE 8
#endif

// 最大同时连接的客户端数量
#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS 8
#endif

// 单条应用层消息最大大小（帧头 + JSON + 二进制）
#ifndef WS_MAX_MESSAGE_SIZE
#define WS_MAX_MESSAGE_SIZE (64u * 1024u * 1024u) // 64 MB
#endif

// WebSocket 协议级 ping 间隔与超时（毫秒）
#ifndef WS_PING_INTERVAL_MS
#define WS_PING_INTERVAL_MS 20000
#endif

#ifndef WS_PING_TIMEOUT_MS
#define WS_PING_TIMEOUT_MS 60000
#endif

// Accept 监听队列长度
#define WS_DEFAULT_BACKLOG 16

#endif // WS_CONFIG_H
