#include "StringQueue.h"
#include <queue>
#include <string>
#include <mutex>
#include <condition_variable>
#include <cstring>

// 队列内部结构
struct StringQueue {
    std::queue<std::string> q;
    std::mutex mtx;
    std::condition_variable cv;
    bool active = true;  // 队列是否有效
};

// 创建队列
int create_String_queue(QueueHandle* Queue) {
    if (!Queue) return QUEUE_ERROR_GENERAL;
    
    try {
        *Queue = new StringQueue();
        return QUEUE_SUCCESS;
    } catch (...) {
        *Queue = nullptr;
        return QUEUE_ERROR_GENERAL;
    }
}

// 入队
int enqueue_String(QueueHandle Queue, const char* string) {
    if (!Queue || !string) return QUEUE_ERROR_GENERAL;
    
    StringQueue* sq = static_cast<StringQueue*>(Queue);
    if (!sq->active) return QUEUE_ERROR_GENERAL;
    
    try {
        std::unique_lock<std::mutex> lock(sq->mtx);
        sq->q.push(std::string(string));  // 自动拷贝
        sq->cv.notify_one();  // 通知消费者
        return QUEUE_SUCCESS;
    } catch (...) {
        return QUEUE_ERROR_GENERAL;
    }
}

// 出队（带超时）
int dequeue_String(QueueHandle Queue, char** string, int timeout_ms) {
    if (!Queue || !string) return QUEUE_ERROR_GENERAL;
    
    StringQueue* sq = static_cast<StringQueue*>(Queue);
    if (!sq->active) return QUEUE_ERROR_GENERAL;
    
    std::unique_lock<std::mutex> lock(sq->mtx);
    
    // 等待直到队列非空或超时
    if (sq->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                        [sq] { return !sq->q.empty() || !sq->active; })) {
        if (!sq->active) return QUEUE_ERROR_GENERAL;
        if (sq->q.empty()) return QUEUE_TIMEOUT;  // 可能是虚假唤醒
        
        // 取出字符串
        std::string front = std::move(sq->q.front());
        sq->q.pop();
        
        // 分配内存返回（调用者必须用delete[]释放！）
        *string = new char[front.size() + 1];
        std::strcpy(*string, front.c_str());
        return QUEUE_SUCCESS;
    }
    
    return QUEUE_TIMEOUT;  // 超时
}

// 销毁队列
void destroy_String_queue(QueueHandle Queue) {
    if (!Queue) return;
    
    StringQueue* sq = static_cast<StringQueue*>(Queue);
    {
        std::lock_guard<std::mutex> lock(sq->mtx);
        sq->active = false;
        sq->cv.notify_all();  // 唤醒所有等待线程
    }
    delete sq;
}