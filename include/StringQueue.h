#ifndef STRING_QUEUE_H
#define STRING_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

// 错误码定义
#define QUEUE_SUCCESS        1
#define QUEUE_ERROR_GENERAL  10000
#define QUEUE_TIMEOUT        9400

// 队列句柄（不透明指针）
typedef struct StringQueue* QueueHandle;

// 函数声明
int create_String_queue(QueueHandle* Queue);
int enqueue_String(QueueHandle Queue, const char* string);
int dequeue_String(QueueHandle Queue, char** string, int timeout_ms);
void destroy_String_queue(QueueHandle Queue);

#ifdef __cplusplus
}
#endif

#endif // STRING_QUEUE_H