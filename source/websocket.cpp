// ============================================================
// Halcon_UI HTTP 服务器实现
// 图像推送: GET /api/stream (Chunked Transfer Encoding, 长连接推帧)
// 命令收发: POST /api/command (前端→Halcon), GET /api/poll (Halcon→前端, 备用)
// ============================================================

#include "websocket.h"
#include "ws_config.h"
#include "ws_queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <process.h>

#pragma comment(lib, "ws2_32.lib")

// ==================== 调试日志 ====================
static FILE* g_log = nullptr;
static std::mutex g_log_mtx;
static void wslog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_log_mtx);
    if (!g_log) g_log = fopen("D:/halcon_ui_ws.log", "w");
    if (!g_log) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fprintf(g_log, "\n");
    fflush(g_log);
}

// ==================== HTTP 请求结构 ====================
typedef struct {
    char method[16];
    char path[512];
    char query[512];
    char* body;
    size_t body_len;
    size_t content_length;
} HttpRequest;

// ==================== 服务器结构 ====================
typedef enum { SERVER_IDLE, SERVER_RUNNING, SERVER_STOPPED } ServerState;

typedef struct {
    SOCKET        listen_fd;
    ServerState   state;
    bool          running;
    HANDLE        accept_thread;
    unsigned      accept_thread_id;
    WsPacketQueue send_queue;   // Halcon → 前端 (SendWebData 入队, /api/stream 或 /api/poll 出队)
    WsPacketQueue recv_queue;   // 前端 → Halcon (/api/command 入队, RecvWebData 出队)
} HttpServer;

#define MAX_SERVERS 16
static HttpServer g_servers[MAX_SERVERS];
static std::mutex g_server_mtx;
static bool g_winsock_initialized = false;

static bool InitWinSock() {
    if (g_winsock_initialized) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_winsock_initialized = true;
    return true;
}

static int FindFreeServerSlot() {
    for (int i = 0; i < MAX_SERVERS; i++)
        if (g_servers[i].state == SERVER_IDLE) return i;
    return -1;
}

// ==================== 网络工具函数 ====================

static int SendAll(SOCKET sock, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, buf + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

// 发送一个 HTTP chunked 块
static int SendChunk(SOCKET sock, const char* data, size_t len) {
    char hex[32];
    snprintf(hex, sizeof(hex), "%zx\r\n", len);
    if (SendAll(sock, hex, strlen(hex)) != 0) return -1;
    if (len > 0 && data) {
        if (SendAll(sock, data, len) != 0) return -1;
    }
    if (SendAll(sock, "\r\n", 2) != 0) return -1;
    return 0;
}

// ==================== HTTP 解析 ====================

static bool ParseHttpRequest(SOCKET sock, HttpRequest* req) {
    memset(req, 0, sizeof(HttpRequest));
    req->body = nullptr;

    char header_buf[4096];
    int total = 0;
    while (total < (int)sizeof(header_buf) - 1) {
        // 批量读取（比逐字节 recv 快 100 倍以上）
        int n = recv(sock, header_buf + total, (int)(sizeof(header_buf) - 1 - total), 0);
        if (n <= 0) return false;
        total += n;
        header_buf[total] = '\0';
        if (strstr(header_buf, "\r\n\r\n")) break;
    }

    // 请求行
    char* line_end = strstr(header_buf, "\r\n");
    if (!line_end) return false;
    char request_line[1024];
    size_t ll = line_end - header_buf;
    if (ll >= sizeof(request_line)) return false;
    memcpy(request_line, header_buf, ll);
    request_line[ll] = '\0';

    char* sp1 = strchr(request_line, ' ');
    if (!sp1) return false;
    *sp1 = '\0';
    strncpy(req->method, request_line, sizeof(req->method) - 1);

    char* path_start = sp1 + 1;
    char* sp2 = strchr(path_start, ' ');
    if (sp2) *sp2 = '\0';

    char* qmark = strchr(path_start, '?');
    if (qmark) {
        *qmark = '\0';
        strncpy(req->query, qmark + 1, sizeof(req->query) - 1);
    }
    strncpy(req->path, path_start, sizeof(req->path) - 1);

    // Content-Length
    req->content_length = 0;
    const char* cl_tag = "Content-Length: ";
    char* cl_pos = strstr(header_buf, cl_tag);
    if (!cl_pos) { cl_tag = "content-length: "; cl_pos = strstr(header_buf, cl_tag); }
    if (cl_pos) req->content_length = (size_t)atoi(cl_pos + strlen(cl_tag));

    // Body
    if (req->content_length > 0 && req->content_length < 1024 * 1024) {
        req->body = (char*)malloc(req->content_length + 1);
        if (!req->body) return false;
        size_t got = 0;
        while (got < req->content_length) {
            int n = recv(sock, req->body + got, (int)(req->content_length - got), 0);
            if (n <= 0) { free(req->body); req->body = nullptr; return false; }
            got += n;
        }
        req->body[req->content_length] = '\0';
        req->body_len = req->content_length;
    }
    return true;
}

static int QueryInt(const char* query, const char* key, int def) {
    if (!query || !query[0]) return def;
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char* pos = strstr(query, search);
    if (!pos) return def;
    return atoi(pos + strlen(search));
}

// 发送普通 HTTP 响应（短连接）
static void SendHttpResponse(SOCKET sock, int status, const char* content_type,
                             const char* body, size_t body_len) {
    const char* status_text = "OK";
    if (status == 204) status_text = "No Content";
    else if (status == 404) status_text = "Not Found";

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    SendAll(sock, header, strlen(header));
    if (body && body_len > 0) SendAll(sock, body, body_len);
}

// ==================== 静态文件服务 ====================

// web 根目录（DLL 同级的 ../web/）
static char g_web_root[512] = {0};

static void InitWebRoot() {
    if (g_web_root[0]) return;
    // 获取 DLL 所在路径
    HMODULE hm = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InitWebRoot, &hm);
    char dll_path[512];
    GetModuleFileNameA(hm, dll_path, sizeof(dll_path));
    // DLL 在 bin/ 下，web 在 bin/../web/ = 项目根/web/
    char* last_sep = strrchr(dll_path, '\\');
    if (last_sep) *last_sep = '\0';
    // 上一级目录
    last_sep = strrchr(dll_path, '\\');
    if (last_sep) *last_sep = '\0';
    snprintf(g_web_root, sizeof(g_web_root), "%s\\web", dll_path);
    wslog("Web root: %s", g_web_root);
}

static const char* GetContentType(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (strcmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    if (strcmp(ext, ".woff2") == 0) return "font/woff2";
    return "application/octet-stream";
}

// 从磁盘读文件并发送 HTTP 响应
static bool ServeStaticFile(SOCKET sock, const char* url_path) {
    InitWebRoot();

    // url_path: "/" → "index.html", "/style.css" → "style.css"
    char rel_path[512];
    if (strcmp(url_path, "/") == 0) {
        strcpy(rel_path, "index.html");
    } else {
        // 去掉开头的 /，防止路径穿越
        const char* p = url_path + 1;
        if (strstr(p, "..")) return false; // 安全检查
        strncpy(rel_path, p, sizeof(rel_path) - 1);
    }

    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s\\%s", g_web_root, rel_path);

    // 读取文件
    FILE* f = fopen(full_path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = (char*)malloc(file_size);
    if (!content) { fclose(f); return false; }
    fread(content, 1, file_size, f);
    fclose(f);

    SendHttpResponse(sock, 200, GetContentType(rel_path), content, (size_t)file_size);
    free(content);
    return true;
}

// 将 WsPacket 打包为二进制帧 [json_len(4LE) + data_len(4LE) + json + binary]
// 返回 malloc 的 buffer，调用者负责 free
static char* PacketToFrame(WsPacket* pkt, size_t* out_size) {
    uint32_t jl = pkt->json_len;
    uint32_t dl = pkt->data_len;
    size_t total = 8 + jl + dl;
    char* frame = (char*)malloc(total);
    if (!frame) { *out_size = 0; return nullptr; }
    memcpy(frame, &jl, 4);
    memcpy(frame + 4, &dl, 4);
    if (pkt->json_str && jl > 0) memcpy(frame + 8, pkt->json_str, jl);
    if (pkt->binary_data && dl > 0) memcpy(frame + 8 + jl, pkt->binary_data, dl);
    *out_size = total;
    return frame;
}

// ==================== HTTP 请求处理线程 ====================

typedef struct {
    SOCKET sock;
    int server_slot;
} HandlerArg;

static unsigned __stdcall HttpHandlerThread(void* arg) {
    HandlerArg ha = *(HandlerArg*)arg;
    free(arg);

    HttpServer* svr = &g_servers[ha.server_slot];
    HttpRequest req;

    if (!ParseHttpRequest(ha.sock, &req)) {
        closesocket(ha.sock);
        return 0;
    }

    wslog("HTTP %s %s (body=%zu)", req.method, req.path, req.body_len);

    // ---- OPTIONS (CORS preflight) ----
    if (strcmp(req.method, "OPTIONS") == 0) {
        SendHttpResponse(ha.sock, 204, "text/plain", nullptr, 0);
    }
    // ---- GET /api/stream → Chunked 长连接推帧 ----
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/stream") == 0) {
        wslog("Stream client connected");

        // 发送 chunked response header
        const char* stream_header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Transfer-Encoding: chunked\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        if (SendAll(ha.sock, stream_header, strlen(stream_header)) != 0) {
            wslog("Stream: failed to send header");
            goto stream_end;
        }

        // 持续推帧（热循环，不写日志）
        while (svr->running) {
            WsPacket* pkt = WsQueuePop(&svr->send_queue, 1000);
            if (!pkt) continue;

            size_t frame_size = 0;
            char* frame = PacketToFrame(pkt, &frame_size);
            WsPacketFree(pkt);
            if (!frame) continue;

            if (SendChunk(ha.sock, frame, frame_size) != 0) {
                free(frame);
                break;
            }
            free(frame);
        }

        // 发送结束 chunk
        SendChunk(ha.sock, nullptr, 0);
    stream_end:
        wslog("Stream client disconnected");
    }
    // ---- GET /api/poll → 单次轮询（备用） ----
    else if (strcmp(req.method, "GET") == 0 && strcmp(req.path, "/api/poll") == 0) {
        int timeout = QueryInt(req.query, "timeout", 0);
        WsPacket* pkt = WsQueuePop(&svr->send_queue, timeout);
        if (pkt) {
            size_t frame_size = 0;
            char* frame = PacketToFrame(pkt, &frame_size);
            WsPacketFree(pkt);
            if (frame) {
                SendHttpResponse(ha.sock, 200, "application/octet-stream", frame, frame_size);
                free(frame);
            }
        } else {
            SendHttpResponse(ha.sock, 204, "text/plain", nullptr, 0);
        }
    }
    // ---- POST /api/command → 前端发命令给 Halcon ----
    else if (strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/command") == 0) {
        if (req.body && req.body_len > 0) {
            WsPacket* pkt = (WsPacket*)malloc(sizeof(WsPacket));
            if (pkt) {
                memset(pkt, 0, sizeof(WsPacket));
                pkt->json_len = (uint32_t)(req.body_len + 1);
                pkt->json_str = (char*)malloc(req.body_len + 1);
                if (pkt->json_str) {
                    memcpy(pkt->json_str, req.body, req.body_len);
                    pkt->json_str[req.body_len] = '\0';
                }
                WsQueuePush(&svr->recv_queue, pkt);
                SendHttpResponse(ha.sock, 200, "application/json", "{\"ok\":true}", 11);
            }
        } else {
            SendHttpResponse(ha.sock, 400, "text/plain", "Empty body", 10);
        }
    }
    // ---- 静态文件 (GET /xxx → web/xxx) ----
    else if (strcmp(req.method, "GET") == 0) {
        if (!ServeStaticFile(ha.sock, req.path)) {
            SendHttpResponse(ha.sock, 404, "text/plain", "Not Found", 9);
        }
    }
    // ---- 404 ----
    else {
        SendHttpResponse(ha.sock, 404, "text/plain", "Not Found", 9);
    }

    if (req.body) free(req.body);
    closesocket(ha.sock);
    return 0;
}

// ==================== Accept 线程 ====================

static unsigned __stdcall AcceptThreadFunc(void* arg) {
    int server_slot = *(int*)arg;
    free(arg);

    HttpServer* svr = &g_servers[server_slot];
    wslog("AcceptThread started, slot=%d", server_slot);

    while (svr->running) {
        struct sockaddr_in addr;
        int addr_len = sizeof(addr);
        SOCKET fd = accept(svr->listen_fd, (struct sockaddr*)&addr, &addr_len);
        if (fd == INVALID_SOCKET) {
            if (!svr->running) break;
            continue;
        }

        HandlerArg* ha = (HandlerArg*)malloc(sizeof(HandlerArg));
        if (!ha) { closesocket(fd); continue; }
        ha->sock = fd;
        ha->server_slot = server_slot;

        HANDLE ht = (HANDLE)_beginthreadex(nullptr, 0, HttpHandlerThread, ha, 0, nullptr);
        if (ht) {
            CloseHandle(ht);
        } else {
            free(ha);
            closesocket(fd);
        }
    }

    wslog("AcceptThread exiting");
    return 0;
}

// ==================== 对外接口实现 ====================

int CreateWebServer(uint16_t port) {
    std::unique_lock<std::mutex> lock(g_server_mtx);
    if (!InitWinSock()) return -1;

    int slot = FindFreeServerSlot();
    if (slot < 0) return -1;

    HttpServer* svr = &g_servers[slot];
    svr->listen_fd        = INVALID_SOCKET;
    svr->state            = SERVER_IDLE;
    svr->accept_thread    = nullptr;
    svr->accept_thread_id = 0;
    svr->running          = false;
    WsQueueInit(&svr->send_queue);
    WsQueueInit(&svr->recv_queue);

    svr->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (svr->listen_fd == INVALID_SOCKET) return -1;

    BOOL opt = TRUE;
    setsockopt(svr->listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family      = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port        = htons(port);

    if (bind(svr->listen_fd, (struct sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
        closesocket(svr->listen_fd); svr->listen_fd = INVALID_SOCKET; return -1;
    }
    if (listen(svr->listen_fd, WS_DEFAULT_BACKLOG) == SOCKET_ERROR) {
        closesocket(svr->listen_fd); svr->listen_fd = INVALID_SOCKET; return -1;
    }

    svr->state   = SERVER_RUNNING;
    svr->running = true;

    int* ta = (int*)malloc(sizeof(int));
    *ta = slot;
    svr->accept_thread = (HANDLE)_beginthreadex(
        nullptr, 0, AcceptThreadFunc, ta, 0, &svr->accept_thread_id);
    if (!svr->accept_thread) {
        closesocket(svr->listen_fd); svr->listen_fd = INVALID_SOCKET;
        svr->state = SERVER_IDLE; svr->running = false;
        free(ta); return -1;
    }

    wslog("CreateWebServer OK: slot=%d, port=%d", slot, (int)port);
    return slot;
}

int SendWebData(int server_id, const char* jsontext, const char* data, size_t length) {
    if (server_id < 0 || server_id >= MAX_SERVERS) return -1;
    HttpServer* svr = &g_servers[server_id];
    if (svr->state != SERVER_RUNNING) return -1;

    WsPacket* pkt = (WsPacket*)malloc(sizeof(WsPacket));
    if (!pkt) return -1;
    memset(pkt, 0, sizeof(WsPacket));

    if (jsontext) {
        pkt->json_len = (uint32_t)(strlen(jsontext) + 1);
        pkt->json_str = (char*)malloc(pkt->json_len);
        if (pkt->json_str) strcpy(pkt->json_str, jsontext);
    }
    if (data && length > 0) {
        pkt->data_len = (uint32_t)length;
        pkt->binary_data = (char*)malloc(length);
        if (pkt->binary_data) memcpy(pkt->binary_data, data, length);
    }

    WsQueuePush(&svr->send_queue, pkt);
    return 0;
}

int RecvWebData(int server_id, char** jsontext, char** data, size_t* out_length, int timeout_ms) {
    if (server_id < 0 || server_id >= MAX_SERVERS || !jsontext || !data || !out_length)
        return -1;

    *jsontext = nullptr; *data = nullptr; *out_length = 0;

    HttpServer* svr = &g_servers[server_id];
    if (svr->state != SERVER_RUNNING) return -1;

    WsPacket* pkt = WsQueuePop(&svr->recv_queue, timeout_ms);
    if (!pkt) return -2;

    *jsontext   = pkt->json_str;
    *data       = pkt->binary_data;
    *out_length = pkt->data_len;
    free(pkt);
    return 0;
}

void CloseWebServer(int server_id) {
    if (server_id < 0 || server_id >= MAX_SERVERS) return;
    HttpServer* svr = &g_servers[server_id];
    if (svr->state == SERVER_IDLE) return;

    wslog("CloseWebServer: slot=%d", server_id);
    svr->running = false;

    if (svr->listen_fd != INVALID_SOCKET) {
        closesocket(svr->listen_fd);
        svr->listen_fd = INVALID_SOCKET;
    }

    if (svr->accept_thread) {
        WaitForSingleObject(svr->accept_thread, 3000);
        CloseHandle(svr->accept_thread);
        svr->accept_thread = nullptr;
    }

    WsQueueClear(&svr->send_queue);
    WsQueueClear(&svr->recv_queue);
    svr->state = SERVER_IDLE;
}
