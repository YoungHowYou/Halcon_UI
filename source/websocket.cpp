// ============================================================
// Halcon_UI WebSocket 服务器实现（RFC 6455）
//   端点 ws://<host>:<port>/ws  ：双向 WebSocket
//   GET /<file>                 ：静态文件服务（保留，方便用浏览器加载前端）
// 帧格式（应用层，承载在 WebSocket 二进制消息中）：
//   [4B magic 0xDEADBEEF][4B json_len][4B data_len][json][binary]
//   字段大端序，json_len = UTF-8 字节数（不含 \0），data_len 可为 0。
// ============================================================

#include "websocket.h"
#include "ws_config.h"
#include "ws_queue.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <process.h>
#include <atomic>
#include <vector>
#include <chrono>

#include <bcrypt.h>
#include <wincrypt.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

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

// ==================== 通用网络工具 ====================
static int SendAll(SOCKET sock, const char* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, buf + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int RecvAll(SOCKET sock, char* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = recv(sock, buf + got, (int)(len - got), 0);
        if (n <= 0) return -1;
        got += n;
    }
    return 0;
}

static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ==================== 大小端转换 ====================
static inline uint32_t Hton32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}
static inline uint32_t Ntoh32(uint32_t v) { return Hton32(v); }
static inline uint64_t Hton64(uint64_t v) {
    return ((uint64_t)Hton32((uint32_t)(v & 0xFFFFFFFFu)) << 32) |
           (uint64_t)Hton32((uint32_t)(v >> 32));
}

// ==================== HTTP 请求结构（仅用于 Upgrade 握手与静态文件）====================
typedef struct {
    char method[16];
    char path[512];
    char ws_key[128];     // Sec-WebSocket-Key
    bool is_upgrade;      // Upgrade: websocket && Sec-WebSocket-Version: 13
} HttpRequest;

static bool ParseHttpRequest(SOCKET sock, HttpRequest* req) {
    memset(req, 0, sizeof(HttpRequest));

    char header_buf[4096];
    int total = 0;
    while (total < (int)sizeof(header_buf) - 1) {
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
    if (qmark) *qmark = '\0';
    strncpy(req->path, path_start, sizeof(req->path) - 1);

    // Header 行检测（大小写不敏感）
    char* p = line_end + 2;
    bool has_upgrade_ws = false;
    bool has_connection_upgrade = false;
    bool has_version_13 = false;

    while (*p) {
        char* eol = strstr(p, "\r\n");
        if (!eol) break;
        if (eol == p) break;
        *eol = '\0';

        char* colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            char* name = p;
            char* value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;

            // 名称转小写比较
            char lname[64] = {0};
            for (int i = 0; name[i] && i < 63; i++) {
                lname[i] = (char)tolower((unsigned char)name[i]);
            }

            if (strcmp(lname, "upgrade") == 0) {
                if (_stricmp(value, "websocket") == 0) has_upgrade_ws = true;
            } else if (strcmp(lname, "connection") == 0) {
                // Connection 头可能含多个 token，比如 "keep-alive, Upgrade"
                if (strstr(value, "Upgrade") || strstr(value, "upgrade") || strstr(value, "UPGRADE"))
                    has_connection_upgrade = true;
            } else if (strcmp(lname, "sec-websocket-version") == 0) {
                if (strcmp(value, "13") == 0) has_version_13 = true;
            } else if (strcmp(lname, "sec-websocket-key") == 0) {
                strncpy(req->ws_key, value, sizeof(req->ws_key) - 1);
            }

            *colon = ':';
        }
        *eol = '\r';
        p = eol + 2;
    }

    req->is_upgrade = has_upgrade_ws && has_connection_upgrade && has_version_13 && req->ws_key[0] != '\0';
    return true;
}

// ==================== WebSocket 握手 ====================
// 计算 Sec-WebSocket-Accept = base64(sha1(key + GUID))
static bool ComputeWsAccept(const char* key, char* out, size_t out_size) {
    static const char* GUID_STR = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", key, GUID_STR);

    BYTE digest[20];
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD obj_len = 0, dummy = 0;
    BYTE* obj = nullptr;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&obj_len, sizeof(DWORD), &dummy, 0) != 0) goto done;
    obj = (BYTE*)malloc(obj_len);
    if (!obj) goto done;
    if (BCryptCreateHash(alg, &hash, obj, obj_len, nullptr, 0, 0) != 0) goto done;
    if (BCryptHashData(hash, (PUCHAR)concat, (ULONG)strlen(concat), 0) != 0) goto done;
    if (BCryptFinishHash(hash, digest, 20, 0) != 0) goto done;

    {
        DWORD b64_len = (DWORD)out_size;
        if (!CryptBinaryToStringA(digest, 20, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                                  out, &b64_len)) goto done;
        ok = true;
    }

done:
    if (hash) BCryptDestroyHash(hash);
    if (obj)  free(obj);
    if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

static bool SendWsHandshake(SOCKET sock, const char* ws_key) {
    char accept_b64[64] = {0};
    if (!ComputeWsAccept(ws_key, accept_b64, sizeof(accept_b64))) return false;

    char resp[512];
    int n = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_b64);
    return SendAll(sock, resp, (size_t)n) == 0;
}

// ==================== WebSocket 帧编解码 ====================
// 服务端 → 客户端：单帧、不掩码
static int SendWsFrame(SOCKET sock, uint8_t opcode, const char* payload, size_t len) {
    uint8_t hdr[14];
    size_t hdr_len = 2;
    hdr[0] = 0x80 | (opcode & 0x0F);  // FIN=1
    if (len < 126) {
        hdr[1] = (uint8_t)len;
    } else if (len <= 0xFFFF) {
        hdr[1] = 126;
        uint16_t l16 = (uint16_t)len;
        hdr[2] = (uint8_t)(l16 >> 8);
        hdr[3] = (uint8_t)(l16 & 0xFF);
        hdr_len = 4;
    } else {
        hdr[1] = 127;
        uint64_t l64 = (uint64_t)len;
        for (int i = 0; i < 8; i++) hdr[2 + i] = (uint8_t)(l64 >> (56 - i * 8));
        hdr_len = 10;
    }
    if (SendAll(sock, (const char*)hdr, hdr_len) != 0) return -1;
    if (len > 0 && payload) {
        if (SendAll(sock, payload, len) != 0) return -1;
    }
    return 0;
}

// 客户端 → 服务端帧。读取一帧，若是 ping 自动回 pong，data 帧填充 *out_payload。
// 返回 opcode（含控制帧）；payload 调用方负责 free。fin=true 表示帧结束。
// 返回 -1 表示出错或连接断开。
static int RecvWsFrame(SOCKET sock, char** out_payload, size_t* out_len, bool* out_fin) {
    *out_payload = nullptr;
    *out_len = 0;
    *out_fin = false;

    uint8_t h[2];
    if (RecvAll(sock, (char*)h, 2) != 0) return -1;

    bool fin = (h[0] & 0x80) != 0;
    uint8_t opcode = h[0] & 0x0F;
    bool masked = (h[1] & 0x80) != 0;
    uint64_t plen = h[1] & 0x7F;

    if (plen == 126) {
        uint8_t e[2];
        if (RecvAll(sock, (char*)e, 2) != 0) return -1;
        plen = ((uint64_t)e[0] << 8) | e[1];
    } else if (plen == 127) {
        uint8_t e[8];
        if (RecvAll(sock, (char*)e, 8) != 0) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | e[i];
    }

    if (plen > WS_MAX_MESSAGE_SIZE) {
        wslog("WS frame too large: %llu (limit %u)", (unsigned long long)plen, WS_MAX_MESSAGE_SIZE);
        return -1;
    }

    uint8_t mask[4] = {0};
    if (masked) {
        if (RecvAll(sock, (char*)mask, 4) != 0) return -1;
    }

    char* payload = nullptr;
    if (plen > 0) {
        payload = (char*)malloc((size_t)plen);
        if (!payload) return -1;
        if (RecvAll(sock, payload, (size_t)plen) != 0) {
            free(payload);
            return -1;
        }
        if (masked) {
            for (size_t i = 0; i < (size_t)plen; i++) payload[i] ^= mask[i & 3];
        }
    }

    *out_payload = payload;
    *out_len = (size_t)plen;
    *out_fin = fin;
    return opcode;
}

// 应用层帧打包：[magic][json_len][data_len][json][binary]，全部大端
// 返回 malloc 的 buffer，调用者 free。
static char* PacketToFrame(WsPacket* pkt, size_t* out_size) {
    uint32_t jl = pkt->json_len;
    uint32_t dl = pkt->data_len;
    size_t total = 12 + jl + dl;
    char* frame = (char*)malloc(total);
    if (!frame) { *out_size = 0; return nullptr; }
    uint32_t m = Hton32(WS_MAGIC_NUMBER);
    uint32_t bjl = Hton32(jl);
    uint32_t bdl = Hton32(dl);
    memcpy(frame, &m, 4);
    memcpy(frame + 4, &bjl, 4);
    memcpy(frame + 8, &bdl, 4);
    if (pkt->json_str && jl > 0)    memcpy(frame + 12, pkt->json_str, jl);
    if (pkt->binary_data && dl > 0) memcpy(frame + 12 + jl, pkt->binary_data, dl);
    *out_size = total;
    return frame;
}

// 解析收到的应用层 payload 为 WsPacket。失败返回 nullptr。
static WsPacket* FrameToPacket(const char* buf, size_t len) {
    if (len < 12) return nullptr;
    uint32_t m, jl, dl;
    memcpy(&m, buf, 4);
    memcpy(&jl, buf + 4, 4);
    memcpy(&dl, buf + 8, 4);
    m = Ntoh32(m); jl = Ntoh32(jl); dl = Ntoh32(dl);
    if (m != WS_MAGIC_NUMBER) {
        wslog("Bad magic: 0x%08X", m);
        return nullptr;
    }
    if ((uint64_t)12 + jl + dl != len) {
        wslog("Frame size mismatch: hdr=%u+%u, total=%zu", jl, dl, len);
        return nullptr;
    }
    WsPacket* pkt = (WsPacket*)calloc(1, sizeof(WsPacket));
    if (!pkt) return nullptr;
    pkt->json_len = jl;
    pkt->data_len = dl;
    if (jl > 0) {
        pkt->json_str = (char*)malloc((size_t)jl + 1);
        if (!pkt->json_str) { WsPacketFree(pkt); return nullptr; }
        memcpy(pkt->json_str, buf + 12, jl);
        pkt->json_str[jl] = '\0';
    }
    if (dl > 0) {
        pkt->binary_data = (char*)malloc(dl);
        if (!pkt->binary_data) { WsPacketFree(pkt); return nullptr; }
        memcpy(pkt->binary_data, buf + 12 + jl, dl);
    }
    return pkt;
}

// ==================== 客户端结构 ====================
typedef struct WsClient {
    SOCKET sock;
    int slot;             // 在 server.clients 中的索引
    int server_id;
    HANDLE read_thread;
    HANDLE write_thread;
    std::atomic<bool> alive;
    std::atomic<int64_t> last_pong_ms;
    WsPacketQueue send_queue;
    std::mutex send_mtx;  // 串行化 send() 调用，避免 ping 和数据帧交错
} WsClient;

// ==================== 服务器结构 ====================
typedef enum { SERVER_IDLE, SERVER_RUNNING, SERVER_STOPPED } ServerState;

typedef struct {
    SOCKET listen_fd;
    ServerState state;
    std::atomic<bool> running;

    HANDLE accept_thread;
    HANDLE ping_thread;

    std::vector<WsClient*> clients;
    std::mutex clients_mtx;

    WsPacketQueue recv_queue;     // 所有客户端汇聚的接收队列
} WsServerSlot;

#define MAX_SERVERS 16
static WsServerSlot g_servers[MAX_SERVERS];
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

// ==================== 静态文件服务 ====================
static char g_web_root[512] = {0};

static void InitWebRoot() {
    if (g_web_root[0]) return;
    HMODULE hm = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&InitWebRoot, &hm);
    char dll_path[512];
    GetModuleFileNameA(hm, dll_path, sizeof(dll_path));
    char* last_sep = strrchr(dll_path, '\\');
    if (last_sep) *last_sep = '\0';
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

static void SendHttpResponse(SOCKET sock, int status, const char* content_type,
                             const char* body, size_t body_len) {
    const char* status_text = "OK";
    if (status == 204) status_text = "No Content";
    else if (status == 400) status_text = "Bad Request";
    else if (status == 404) status_text = "Not Found";

    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    SendAll(sock, header, (size_t)n);
    if (body && body_len > 0) SendAll(sock, body, body_len);
}

static bool ServeStaticFile(SOCKET sock, const char* url_path) {
    InitWebRoot();
    char rel_path[512];
    if (strcmp(url_path, "/") == 0) {
        strcpy(rel_path, "index.html");
    } else {
        const char* p = url_path + 1;
        if (strstr(p, "..")) return false;
        strncpy(rel_path, p, sizeof(rel_path) - 1);
        rel_path[sizeof(rel_path) - 1] = '\0';
    }
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s\\%s", g_web_root, rel_path);

    FILE* f = fopen(full_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* content = (char*)malloc((size_t)file_size);
    if (!content) { fclose(f); return false; }
    fread(content, 1, file_size, f);
    fclose(f);
    SendHttpResponse(sock, 200, GetContentType(rel_path), content, (size_t)file_size);
    free(content);
    return true;
}

// ==================== 客户端读写线程 ====================

// 注销并释放某个客户端
static void DropClient(WsServerSlot* svr, WsClient* cli) {
    cli->alive = false;
    if (cli->sock != INVALID_SOCKET) {
        shutdown(cli->sock, SD_BOTH);
        closesocket(cli->sock);
        cli->sock = INVALID_SOCKET;
    }
    WsQueueStop(&cli->send_queue);
}

static unsigned __stdcall ClientReadThread(void* arg) {
    WsClient* cli = (WsClient*)arg;
    WsServerSlot* svr = &g_servers[cli->server_id];

    char* msg_buf = nullptr;
    size_t msg_cap = 0;
    size_t msg_len = 0;
    int    msg_opcode = 0;

    while (svr->running && cli->alive) {
        char* payload = nullptr;
        size_t plen = 0;
        bool fin = false;
        int opcode = RecvWsFrame(cli->sock, &payload, &plen, &fin);
        if (opcode < 0) break;

        if (opcode == 0x8) {        // close
            wslog("Client %d sent close", cli->slot);
            if (payload) free(payload);
            // 回应 close
            std::lock_guard<std::mutex> lk(cli->send_mtx);
            SendWsFrame(cli->sock, 0x8, nullptr, 0);
            break;
        }
        if (opcode == 0x9) {        // ping → pong
            std::lock_guard<std::mutex> lk(cli->send_mtx);
            SendWsFrame(cli->sock, 0xA, payload, plen);
            if (payload) free(payload);
            continue;
        }
        if (opcode == 0xA) {        // pong
            cli->last_pong_ms = NowMs();
            if (payload) free(payload);
            continue;
        }

        // 数据帧（0x1 text / 0x2 binary / 0x0 continuation）
        if (opcode == 0x1 || opcode == 0x2) {
            // 新消息开始
            if (msg_buf) { free(msg_buf); msg_buf = nullptr; }
            msg_cap = plen;
            msg_buf = (char*)malloc(msg_cap > 0 ? msg_cap : 1);
            msg_len = 0;
            msg_opcode = opcode;
        }
        // 追加 payload
        if (plen > 0) {
            if (msg_len + plen > msg_cap) {
                size_t nc = msg_len + plen;
                char* nb = (char*)realloc(msg_buf, nc);
                if (!nb) { free(payload); break; }
                msg_buf = nb;
                msg_cap = nc;
            }
            memcpy(msg_buf + msg_len, payload, plen);
            msg_len += plen;
        }
        if (payload) free(payload);

        if (fin && msg_buf) {
            // 完整消息 → 解析为 WsPacket，推入接收队列
            WsPacket* pkt = FrameToPacket(msg_buf, msg_len);
            if (pkt) {
                pkt->client_id = cli->slot;
                WsQueuePush(&svr->recv_queue, pkt);
            }
            free(msg_buf);
            msg_buf = nullptr;
            msg_cap = msg_len = 0;
        }
    }

    if (msg_buf) free(msg_buf);
    DropClient(svr, cli);
    wslog("Read thread exit (slot=%d)", cli->slot);
    return 0;
}

static unsigned __stdcall ClientWriteThread(void* arg) {
    WsClient* cli = (WsClient*)arg;
    WsServerSlot* svr = &g_servers[cli->server_id];

    while (svr->running && cli->alive) {
        WsPacket* pkt = WsQueuePop(&cli->send_queue, 500);
        if (!pkt) continue;

        size_t fsize = 0;
        char* frame = PacketToFrame(pkt, &fsize);
        WsPacketFree(pkt);
        if (!frame) continue;

        if (fsize > WS_MAX_MESSAGE_SIZE) {
            wslog("Send drop oversize msg: %zu", fsize);
            free(frame);
            continue;
        }

        std::lock_guard<std::mutex> lk(cli->send_mtx);
        if (SendWsFrame(cli->sock, 0x2 /*binary*/, frame, fsize) != 0) {
            free(frame);
            break;
        }
        free(frame);
    }

    DropClient(svr, cli);
    wslog("Write thread exit (slot=%d)", cli->slot);
    return 0;
}

// ==================== 服务器线程：accept / ping ====================

static void RegisterClient(WsServerSlot* svr, WsClient* cli) {
    std::lock_guard<std::mutex> lk(svr->clients_mtx);
    // 找空槽
    int slot = -1;
    for (size_t i = 0; i < svr->clients.size(); i++) {
        if (svr->clients[i] == nullptr) { slot = (int)i; break; }
    }
    if (slot < 0) {
        slot = (int)svr->clients.size();
        svr->clients.push_back(nullptr);
    }
    cli->slot = slot;
    svr->clients[slot] = cli;
}

// 清理已死的客户端：等待其线程结束、回收资源
static void ReapDeadClients(WsServerSlot* svr) {
    std::lock_guard<std::mutex> lk(svr->clients_mtx);
    for (size_t i = 0; i < svr->clients.size(); i++) {
        WsClient* cli = svr->clients[i];
        if (!cli) continue;
        if (!cli->alive.load()) {
            // 等待两个线程都退出（短超时，避免阻塞 accept）
            if (cli->read_thread) {
                if (WaitForSingleObject(cli->read_thread, 100) == WAIT_OBJECT_0) {
                    CloseHandle(cli->read_thread);
                    cli->read_thread = nullptr;
                }
            }
            if (cli->write_thread) {
                if (WaitForSingleObject(cli->write_thread, 100) == WAIT_OBJECT_0) {
                    CloseHandle(cli->write_thread);
                    cli->write_thread = nullptr;
                }
            }
            if (!cli->read_thread && !cli->write_thread) {
                WsQueueClear(&cli->send_queue);
                delete cli;
                svr->clients[i] = nullptr;
            }
        }
    }
}

// 处理一个新 TCP 连接：读 HTTP，决定是 WS 升级还是静态文件
static unsigned __stdcall HandleConnectionThread(void* arg) {
    struct Args { SOCKET sock; int server_id; };
    Args a = *(Args*)arg;
    free(arg);

    WsServerSlot* svr = &g_servers[a.server_id];

    HttpRequest req;
    if (!ParseHttpRequest(a.sock, &req)) {
        closesocket(a.sock);
        return 0;
    }

    wslog("HTTP %s %s upgrade=%d", req.method, req.path, (int)req.is_upgrade);

    // WebSocket 升级（端点 /ws）
    if (req.is_upgrade && (strcmp(req.path, "/ws") == 0 || strcmp(req.path, "/") == 0)) {
        // 检查最大连接数
        {
            std::lock_guard<std::mutex> lk(svr->clients_mtx);
            int alive_count = 0;
            for (auto* c : svr->clients) if (c && c->alive.load()) alive_count++;
            if (alive_count >= WS_MAX_CLIENTS) {
                wslog("Reject WS upgrade: max clients reached (%d)", alive_count);
                SendHttpResponse(a.sock, 503, "text/plain", "Too many clients", 16);
                closesocket(a.sock);
                return 0;
            }
        }

        if (!SendWsHandshake(a.sock, req.ws_key)) {
            wslog("WS handshake failed");
            closesocket(a.sock);
            return 0;
        }

        // 注册客户端
        WsClient* cli = new WsClient();
        cli->sock = a.sock;
        cli->server_id = a.server_id;
        cli->read_thread = nullptr;
        cli->write_thread = nullptr;
        cli->alive = true;
        cli->last_pong_ms = NowMs();
        WsQueueInit(&cli->send_queue);

        RegisterClient(svr, cli);
        wslog("WS client connected (slot=%d)", cli->slot);

        cli->read_thread  = (HANDLE)_beginthreadex(nullptr, 0, ClientReadThread,  cli, 0, nullptr);
        cli->write_thread = (HANDLE)_beginthreadex(nullptr, 0, ClientWriteThread, cli, 0, nullptr);
        return 0;  // 客户端线程接管
    }

    // 静态文件
    if (strcmp(req.method, "GET") == 0) {
        if (!ServeStaticFile(a.sock, req.path)) {
            SendHttpResponse(a.sock, 404, "text/plain", "Not Found", 9);
        }
    } else {
        SendHttpResponse(a.sock, 400, "text/plain", "Bad Request", 11);
    }
    closesocket(a.sock);
    return 0;
}

static unsigned __stdcall AcceptThreadFunc(void* arg) {
    int server_id = *(int*)arg;
    free(arg);
    WsServerSlot* svr = &g_servers[server_id];
    wslog("AcceptThread started (server_id=%d)", server_id);

    while (svr->running) {
        struct sockaddr_in addr;
        int addr_len = sizeof(addr);
        SOCKET fd = accept(svr->listen_fd, (struct sockaddr*)&addr, &addr_len);
        if (fd == INVALID_SOCKET) {
            if (!svr->running) break;
            continue;
        }
        // 顺手回收已死客户端
        ReapDeadClients(svr);

        struct Args { SOCKET sock; int server_id; };
        Args* a = (Args*)malloc(sizeof(Args));
        if (!a) { closesocket(fd); continue; }
        a->sock = fd;
        a->server_id = server_id;
        HANDLE ht = (HANDLE)_beginthreadex(nullptr, 0, HandleConnectionThread, a, 0, nullptr);
        if (ht) CloseHandle(ht); else { free(a); closesocket(fd); }
    }
    wslog("AcceptThread exit (server_id=%d)", server_id);
    return 0;
}

static unsigned __stdcall PingThreadFunc(void* arg) {
    int server_id = *(int*)arg;
    free(arg);
    WsServerSlot* svr = &g_servers[server_id];
    wslog("PingThread started (server_id=%d)", server_id);

    int64_t last_ping = NowMs();
    while (svr->running) {
        Sleep(500);
        int64_t now = NowMs();
        if (now - last_ping < WS_PING_INTERVAL_MS) continue;
        last_ping = now;

        std::lock_guard<std::mutex> lk(svr->clients_mtx);
        for (auto* cli : svr->clients) {
            if (!cli || !cli->alive.load()) continue;
            // pong 超时 → 关闭
            if (now - cli->last_pong_ms.load() > WS_PING_TIMEOUT_MS) {
                wslog("Client %d pong timeout, dropping", cli->slot);
                DropClient(svr, cli);
                continue;
            }
            std::lock_guard<std::mutex> sl(cli->send_mtx);
            SendWsFrame(cli->sock, 0x9 /*ping*/, nullptr, 0);
        }
    }
    wslog("PingThread exit (server_id=%d)", server_id);
    return 0;
}

// ==================== 对外 API ====================

int CreateWebServer(uint16_t port) {
    std::unique_lock<std::mutex> lock(g_server_mtx);
    if (!InitWinSock()) return -1;

    int slot = FindFreeServerSlot();
    if (slot < 0) return -1;

    WsServerSlot* svr = &g_servers[slot];
    svr->listen_fd     = INVALID_SOCKET;
    svr->state         = SERVER_IDLE;
    svr->running       = false;
    svr->accept_thread = nullptr;
    svr->ping_thread   = nullptr;
    svr->clients.clear();
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

    int* arg1 = (int*)malloc(sizeof(int)); *arg1 = slot;
    svr->accept_thread = (HANDLE)_beginthreadex(nullptr, 0, AcceptThreadFunc, arg1, 0, nullptr);

    int* arg2 = (int*)malloc(sizeof(int)); *arg2 = slot;
    svr->ping_thread = (HANDLE)_beginthreadex(nullptr, 0, PingThreadFunc, arg2, 0, nullptr);

    if (!svr->accept_thread || !svr->ping_thread) {
        svr->running = false;
        closesocket(svr->listen_fd); svr->listen_fd = INVALID_SOCKET;
        if (svr->accept_thread) { WaitForSingleObject(svr->accept_thread, 1000); CloseHandle(svr->accept_thread); svr->accept_thread = nullptr; }
        if (svr->ping_thread)   { WaitForSingleObject(svr->ping_thread, 1000);   CloseHandle(svr->ping_thread);   svr->ping_thread = nullptr; }
        svr->state = SERVER_IDLE;
        return -1;
    }

    wslog("CreateWebServer OK: slot=%d, port=%d", slot, (int)port);
    return slot;
}

int SendWebData(int server_id, const char* jsontext, const char* data, size_t length) {
    if (server_id < 0 || server_id >= MAX_SERVERS) return -1;
    WsServerSlot* svr = &g_servers[server_id];
    if (svr->state != SERVER_RUNNING) return -1;

    uint32_t jl = jsontext ? (uint32_t)strlen(jsontext) : 0;
    uint32_t dl = (data && length > 0) ? (uint32_t)length : 0;
    if ((uint64_t)12 + jl + dl > WS_MAX_MESSAGE_SIZE) {
        wslog("SendWebData: oversize message %llu", (unsigned long long)((uint64_t)12 + jl + dl));
        return -1;
    }

    // 广播到每个活跃客户端：每个客户端独立 packet 副本
    std::lock_guard<std::mutex> lk(svr->clients_mtx);
    int delivered = 0;
    for (auto* cli : svr->clients) {
        if (!cli || !cli->alive.load()) continue;
        WsPacket* pkt = (WsPacket*)calloc(1, sizeof(WsPacket));
        if (!pkt) continue;
        pkt->json_len = jl;
        pkt->data_len = dl;
        if (jl > 0) {
            pkt->json_str = (char*)malloc(jl + 1);
            if (!pkt->json_str) { WsPacketFree(pkt); continue; }
            memcpy(pkt->json_str, jsontext, jl);
            pkt->json_str[jl] = '\0';
        }
        if (dl > 0) {
            pkt->binary_data = (char*)malloc(dl);
            if (!pkt->binary_data) { WsPacketFree(pkt); continue; }
            memcpy(pkt->binary_data, data, dl);
        }
        WsQueuePush(&cli->send_queue, pkt);
        delivered++;
    }
    // 即使当前没有客户端，也算 OK：业务层发送不该因无连接而失败
    return 0;
}

int RecvWebData(int server_id, char** jsontext, char** data, size_t* out_length, int timeout_ms) {
    if (server_id < 0 || server_id >= MAX_SERVERS || !jsontext || !data || !out_length)
        return -1;

    *jsontext = nullptr; *data = nullptr; *out_length = 0;

    WsServerSlot* svr = &g_servers[server_id];
    if (svr->state != SERVER_RUNNING) return -1;

    WsPacket* pkt = WsQueuePop(&svr->recv_queue, timeout_ms);
    if (!pkt) return -2;

    *jsontext   = pkt->json_str;
    *data       = pkt->binary_data;
    *out_length = pkt->data_len;
    free(pkt);  // 注意：内部缓冲区所有权转给调用者
    return 0;
}

void CloseWebServer(int server_id) {
    if (server_id < 0 || server_id >= MAX_SERVERS) return;
    WsServerSlot* svr = &g_servers[server_id];
    if (svr->state == SERVER_IDLE) return;

    wslog("CloseWebServer: slot=%d", server_id);
    svr->running = false;

    if (svr->listen_fd != INVALID_SOCKET) {
        closesocket(svr->listen_fd);
        svr->listen_fd = INVALID_SOCKET;
    }

    // 唤醒可能阻塞在 Recv 的业务线程
    WsQueueStop(&svr->recv_queue);

    // 关闭所有客户端
    {
        std::lock_guard<std::mutex> lk(svr->clients_mtx);
        for (auto* cli : svr->clients) {
            if (!cli) continue;
            DropClient(svr, cli);
        }
    }

    // 等待 accept / ping 线程退出
    if (svr->accept_thread) {
        WaitForSingleObject(svr->accept_thread, 3000);
        CloseHandle(svr->accept_thread);
        svr->accept_thread = nullptr;
    }
    if (svr->ping_thread) {
        WaitForSingleObject(svr->ping_thread, 3000);
        CloseHandle(svr->ping_thread);
        svr->ping_thread = nullptr;
    }

    // 等待客户端线程退出，释放资源
    {
        std::lock_guard<std::mutex> lk(svr->clients_mtx);
        for (auto* cli : svr->clients) {
            if (!cli) continue;
            if (cli->read_thread)  { WaitForSingleObject(cli->read_thread, 2000);  CloseHandle(cli->read_thread); }
            if (cli->write_thread) { WaitForSingleObject(cli->write_thread, 2000); CloseHandle(cli->write_thread); }
            WsQueueClear(&cli->send_queue);
            delete cli;
        }
        svr->clients.clear();
    }

    WsQueueClear(&svr->recv_queue);
    WsQueueResume(&svr->recv_queue);  // 允许下次 Start 复用
    svr->state = SERVER_IDLE;
}
