#ifndef WS_QUEUE_H
#define WS_QUEUE_H

#include <mutex>
#include <condition_variable>
#include <chrono>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ws_config.h"

// 单条应用层消息（帧头 + JSON + 二进制体）。
// json_len 是 UTF-8 字节数，不含 \0；json_str 为 C 串方便业务层使用，分配为 json_len + 1。
typedef struct {
    uint32_t json_len;
    uint32_t data_len;
    char*    json_str;
    char*    binary_data;
    int      client_id;   // 来源客户端槽位，仅 recv_queue 使用
} WsPacket;

// 线程安全有界 FIFO 队列。
// 满时丢弃最旧消息（PROTOCOL.md §5.3）；stop 标志用于在关闭时唤醒所有阻塞 Pop。
typedef struct {
    WsPacket* buffer[WS_MAX_QUEUE_SIZE];
    int  head;
    int  tail;
    int  count;
    bool stopped;
    std::mutex mtx;
    std::condition_variable cv;
} WsPacketQueue;

static inline void WsPacketFree(WsPacket* pkt) {
    if (!pkt) return;
    if (pkt->json_str)    free(pkt->json_str);
    if (pkt->binary_data) free(pkt->binary_data);
    free(pkt);
}

static inline void WsQueueInit(WsPacketQueue* q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->stopped = false;
    for (int i = 0; i < WS_MAX_QUEUE_SIZE; i++) q->buffer[i] = nullptr;
}

// 入队，满载时丢弃队首（最旧）消息。返回 true 表示新消息成功入队。
static inline bool WsQueuePush(WsPacketQueue* q, WsPacket* packet) {
    {
        std::unique_lock<std::mutex> lock(q->mtx);
        if (q->stopped) {
            // 队列已停止：直接丢弃，避免泄漏
            WsPacketFree(packet);
            return false;
        }
        if (q->count >= WS_MAX_QUEUE_SIZE) {
            WsPacketFree(q->buffer[q->head]);
            q->head = (q->head + 1) % WS_MAX_QUEUE_SIZE;
            q->count--;
        }
        q->buffer[q->tail] = packet;
        q->tail = (q->tail + 1) % WS_MAX_QUEUE_SIZE;
        q->count++;
    }
    q->cv.notify_one();
    return true;
}

// 出队。timeout_ms<0 永久阻塞；=0 非阻塞；>0 等待指定毫秒。
// 队列已 stop 时立即返回 nullptr。
static inline WsPacket* WsQueuePop(WsPacketQueue* q, int timeout_ms) {
    std::unique_lock<std::mutex> lock(q->mtx);

    auto ready = [q] { return q->count > 0 || q->stopped; };

    if (timeout_ms < 0) {
        q->cv.wait(lock, ready);
    } else if (timeout_ms > 0) {
        if (!q->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), ready)) {
            return nullptr;
        }
    } else {
        if (!ready()) return nullptr;
    }

    if (q->count == 0) return nullptr; // stopped 且空

    WsPacket* pkt = q->buffer[q->head];
    q->buffer[q->head] = nullptr;
    q->head = (q->head + 1) % WS_MAX_QUEUE_SIZE;
    q->count--;
    return pkt;
}

static inline int WsQueueSize(WsPacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    return q->count;
}

static inline void WsQueueClear(WsPacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    while (q->count > 0) {
        WsPacketFree(q->buffer[q->head]);
        q->buffer[q->head] = nullptr;
        q->head = (q->head + 1) % WS_MAX_QUEUE_SIZE;
        q->count--;
    }
    q->head = 0;
    q->tail = 0;
}

// 标记停止并唤醒所有等待者
static inline void WsQueueStop(WsPacketQueue* q) {
    {
        std::unique_lock<std::mutex> lock(q->mtx);
        q->stopped = true;
    }
    q->cv.notify_all();
}

// 重新启用（在 Stop 之后再次 Start 时调用）
static inline void WsQueueResume(WsPacketQueue* q) {
    std::unique_lock<std::mutex> lock(q->mtx);
    q->stopped = false;
}

#endif // WS_QUEUE_H
