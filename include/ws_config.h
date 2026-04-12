#ifndef WS_CONFIG_H
#define WS_CONFIG_H

// 接收队列最大深度
#ifndef WS_MAX_QUEUE_SIZE
#define WS_MAX_QUEUE_SIZE 8
#endif

// 最大同时连接的客户端数量
#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS 16
#endif

// Accept 监听队列长度
#define WS_DEFAULT_BACKLOG 10

#endif // WS_CONFIG_H
