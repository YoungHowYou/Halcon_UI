#ifndef WS_QUEUE_H
#define WS_QUEUE_H

#include <windows.h>
#include <mutex>
#include <condition_variable>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ws_config.h"

// WebSocket 数据包结构
typedef struct {
    uint32_t json_len;    // JSON 字符串长度（含 \0）
    uint32_t data_len;    // 二进制数据长度
    char*    json_str;    // JSON 字符串（已带 \0 结尾）
    char*    binary_data; // 二进制数据（图像等）
    int      client_id;   // 来源客户端 ID
} WsPacket;

// 线程安全环形队列
typedef struct {
    WsPacket* buffer[WS_MAX_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    std::mutex mtx;
    std::condition_variable not_empty;
} WsPacketQueue;

// 初始化队列
static inline void WsQueueInit(WsPacketQueue* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

// 释放单个数据包
static inline void WsPacketFree(WsPacket* pkt) {
    if (!pkt) return;
    if (pkt->json_str)    free(pkt->json_str);
    if (pkt->binary_data) free(pkt->binary_data);
    free(pkt);
}

// 入队（满时自动丢弃最旧数据包）
static inline bool WsQueuePush(WsPacketQueue* q, WsPacket* packet) {
    std::unique_lock<std::mutex> lock(q->mtx);

    if (q->count >= WS_MAX_QUEUE_SIZE) {
        WsPacketFree(q->buffer[q->head]);
        q->head = (q->head + 1) % WS_MAX_QUEUE_SIZE;
        q->count--;
    }

    q->buffer[q->tail] = packet;
    q->tail = (q->tail + 1) % WS_MAX_QUEUE_SIZE;
    q->count++;

    lock.unlock();
    q->not_empty.notify_one();
    return true;
}

// 出队（带超时）
// timeout_ms > 0 : 等待指定毫秒
// timeout_ms = 0 : 非阻塞
// timeout_ms < 0 : 永久阻塞
static inline WsPacket* WsQueuePop(WsPacketQueue* q, int timeout_ms) {
    std::unique_lock<std::mutex> lock(q->mtx);

    if (timeout_ms < 0) {
        q->not_empty.wait(lock, [q] { return q->count > 0; });
    } else if (timeout_ms > 0) {
        if (!q->not_empty.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                   [q] { return q->count > 0; })) {
            return nullptr;
        }
    } else {
        if (q->count == 0) return nullptr;
    }

    if (q->count == 0) return nullptr;

    WsPacket* pkt = q->buffer[q->head];
    q->buffer[q->head] = nullptr;
    q->head = (q->head + 1) % WS_MAX_QUEUE_SIZE;
    q->count--;
    return pkt;
}

// 获取队列大小
static inline int WsQueueSize(WsPacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    return q->count;
}

// 清空队列
static inline void WsQueueClear(WsPacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    while (q->count > 0) {
        WsPacketFree(q->buffer[q->head]);
        q->head = (q->head + 1) % WS_MAX_QUEUE_SIZE;
        q->count--;
    }
    q->head = 0;
    q->tail = 0;
}

#endif // WS_QUEUE_H
