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
#include <atomic>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifdef _WIN32
  #include <bcrypt.h>
  #include <wincrypt.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "bcrypt.lib")
  #pragma comment(lib, "crypt32.lib")
#else
  #include <signal.h>
  #include <strings.h>
  #include <dlfcn.h>
  #include <limits.h>
  #include <sys/stat.h>
  static int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
#endif

// ==================== 平台抽象 ====================
#ifdef _WIN32
  using thread_id_t = std::thread;
  static int CloseSocketCompat(SOCKET s) { return closesocket(s); }
  static int ShutdownBoth(SOCKET s)      { return shutdown(s, SD_BOTH); }
  static void SleepMs(int ms)            { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#else
  static int CloseSocketCompat(SOCKET s) { return ::close(s); }
  static int ShutdownBoth(SOCKET s)      { return shutdown(s, SHUT_RDWR); }
  static void SleepMs(int ms)            { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif

// ==================== 调试日志 ====================
static FILE* g_log = nullptr;
static std::mutex g_log_mtx;
static void wslog(const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_log_mtx);
    if (!g_log) {
#ifdef _WIN32
        g_log = fopen("D:/halcon_ui_ws.log", "w");
#else
        g_log = fopen("/tmp/halcon_ui_ws.log", "w");
#endif
    }
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
        int n = (int)send(sock, buf + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int RecvAll(SOCKET sock, char* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = (int)recv(sock, buf + got, (int)(len - got), 0);
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
        int n = (int)recv(sock, header_buf + total, (int)(sizeof(header_buf) - 1 - total), 0);
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

// ==================== SHA1 + Base64（跨平台实现）====================
// SHA1 (RFC 3174)：用于 WebSocket 握手 accept 计算。
namespace {
struct Sha1Ctx {
    uint32_t state[5];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
};

static inline uint32_t Rol32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void Sha1Init(Sha1Ctx* c) {
    c->state[0] = 0x67452301u;
    c->state[1] = 0xEFCDAB89u;
    c->state[2] = 0x98BADCFEu;
    c->state[3] = 0x10325476u;
    c->state[4] = 0xC3D2E1F0u;
    c->bitlen = 0;
    c->buflen = 0;
}

static void Sha1Block(Sha1Ctx* c, const uint8_t blk[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) |
               ((uint32_t)blk[i*4+2] << 8) | (uint32_t)blk[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = Rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3], e = c->state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);     k = 0x5A827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ cc ^ d;                k = 0xCA62C1D6u; }
        uint32_t t = Rol32(a, 5) + f + e + k + w[i];
        e = d; d = cc; cc = Rol32(b, 30); b = a; a = t;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d; c->state[4] += e;
}

static void Sha1Update(Sha1Ctx* c, const uint8_t* data, size_t len) {
    c->bitlen += (uint64_t)len * 8;
    while (len > 0) {
        size_t take = 64 - c->buflen;
        if (take > len) take = len;
        memcpy(c->buf + c->buflen, data, take);
        c->buflen += take;
        data += take;
        len  -= take;
        if (c->buflen == 64) {
            Sha1Block(c, c->buf);
            c->buflen = 0;
        }
    }
}

static void Sha1Final(Sha1Ctx* c, uint8_t out[20]) {
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        while (c->buflen < 64) c->buf[c->buflen++] = 0;
        Sha1Block(c, c->buf);
        c->buflen = 0;
    }
    while (c->buflen < 56) c->buf[c->buflen++] = 0;
    uint64_t bl = c->bitlen;
    for (int i = 7; i >= 0; i--) c->buf[c->buflen++] = (uint8_t)(bl >> (i * 8));
    Sha1Block(c, c->buf);
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(c->state[i] >> 24);
        out[i*4+1] = (uint8_t)(c->state[i] >> 16);
        out[i*4+2] = (uint8_t)(c->state[i] >> 8);
        out[i*4+3] = (uint8_t)(c->state[i]);
    }
}

static const char B64_TBL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 输出 base64（不含换行），最大长度 = ((len+2)/3)*4 + 1。返回写入字符数（不含 \0）。
static size_t Base64Encode(const uint8_t* in, size_t len, char* out, size_t out_size) {
    size_t need = ((len + 2) / 3) * 4 + 1;
    if (out_size < need) return 0;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= (uint32_t)in[i + 2];
        out[o++] = B64_TBL[(v >> 18) & 0x3F];
        out[o++] = B64_TBL[(v >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? B64_TBL[(v >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? B64_TBL[v & 0x3F]        : '=';
    }
    out[o] = '\0';
    return o;
}
} // namespace

// ==================== WebSocket 握手 ====================
// 计算 Sec-WebSocket-Accept = base64(sha1(key + GUID))
static bool ComputeWsAccept(const char* key, char* out, size_t out_size) {
    static const char* GUID_STR = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[256];
    snprintf(concat, sizeof(concat), "%s%s", key, GUID_STR);

    Sha1Ctx ctx;
    uint8_t digest[20];
    Sha1Init(&ctx);
    Sha1Update(&ctx, (const uint8_t*)concat, strlen(concat));
    Sha1Final(&ctx, digest);

    return Base64Encode(digest, 20, out, out_size) > 0;
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
    std::thread read_thread;
    std::thread write_thread;
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

    std::thread accept_thread;
    std::thread ping_thread;

    std::vector<WsClient*> clients;
    std::mutex clients_mtx;

    WsPacketQueue recv_queue;     // 所有客户端汇聚的接收队列
} WsServerSlot;

#define MAX_SERVERS 16
static WsServerSlot g_servers[MAX_SERVERS];
static std::mutex g_server_mtx;

static bool InitSockets() {
#ifdef _WIN32
    static bool g_winsock_initialized = false;
    if (g_winsock_initialized) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_winsock_initialized = true;
#else
    // 写已断开的 socket 时不要触发 SIGPIPE，让 send() 返回 EPIPE
    static bool g_signal_set = false;
    if (!g_signal_set) {
        signal(SIGPIPE, SIG_IGN);
        g_signal_set = true;
    }
#endif
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
#ifdef _WIN32
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
#else
    // 用 dladdr 拿到当前 .so 的真实路径，再回溯两级到工程根
    Dl_info info;
    char so_path[PATH_MAX];
    if (dladdr((void*)&InitWebRoot, &info) && info.dli_fname) {
        // dli_fname 可能是相对路径，先 realpath
        if (!realpath(info.dli_fname, so_path)) {
            strncpy(so_path, info.dli_fname, sizeof(so_path) - 1);
            so_path[sizeof(so_path) - 1] = '\0';
        }
    } else {
        strcpy(so_path, ".");
    }
    char* last_sep = strrchr(so_path, '/');
    if (last_sep) *last_sep = '\0';
    last_sep = strrchr(so_path, '/');
    if (last_sep) *last_sep = '\0';
    snprintf(g_web_root, sizeof(g_web_root), "%s/web", so_path);
#endif
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
#ifdef _WIN32
    snprintf(full_path, sizeof(full_path), "%s\\%s", g_web_root, rel_path);
#else
    snprintf(full_path, sizeof(full_path), "%s/%s", g_web_root, rel_path);
#endif

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
static void DropClient(WsServerSlot* /*svr*/, WsClient* cli) {
    cli->alive = false;
    if (cli->sock != INVALID_SOCKET) {
        ShutdownBoth(cli->sock);
        CloseSocketCompat(cli->sock);
        cli->sock = INVALID_SOCKET;
    }
    WsQueueStop(&cli->send_queue);
}

static void ClientReadThread(WsClient* cli) {
    WsServerSlot* svr = &g_servers[cli->server_id];

    char* msg_buf = nullptr;
    size_t msg_cap = 0;
    size_t msg_len = 0;
    int    msg_opcode = 0;
    (void)msg_opcode;

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
}

static void ClientWriteThread(WsClient* cli) {
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
            if (cli->read_thread.joinable())  cli->read_thread.join();
            if (cli->write_thread.joinable()) cli->write_thread.join();
            WsQueueClear(&cli->send_queue);
            delete cli;
            svr->clients[i] = nullptr;
        }
    }
}

// 处理一个新 TCP 连接：读 HTTP，决定是 WS 升级还是静态文件
static void HandleConnectionThread(SOCKET client_sock, int server_id) {
    WsServerSlot* svr = &g_servers[server_id];

    HttpRequest req;
    if (!ParseHttpRequest(client_sock, &req)) {
        CloseSocketCompat(client_sock);
        return;
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
                SendHttpResponse(client_sock, 503, "text/plain", "Too many clients", 16);
                CloseSocketCompat(client_sock);
                return;
            }
        }

        if (!SendWsHandshake(client_sock, req.ws_key)) {
            wslog("WS handshake failed");
            CloseSocketCompat(client_sock);
            return;
        }

        // 注册客户端
        WsClient* cli = new WsClient();
        cli->sock = client_sock;
        cli->server_id = server_id;
        cli->alive = true;
        cli->last_pong_ms = NowMs();
        WsQueueInit(&cli->send_queue);

        RegisterClient(svr, cli);
        wslog("WS client connected (slot=%d)", cli->slot);

        cli->read_thread  = std::thread(ClientReadThread,  cli);
        cli->write_thread = std::thread(ClientWriteThread, cli);
        return;  // 客户端线程接管
    }

    // 静态文件
    if (strcmp(req.method, "GET") == 0) {
        if (!ServeStaticFile(client_sock, req.path)) {
            SendHttpResponse(client_sock, 404, "text/plain", "Not Found", 9);
        }
    } else {
        SendHttpResponse(client_sock, 400, "text/plain", "Bad Request", 11);
    }
    CloseSocketCompat(client_sock);
}

static void AcceptThreadFunc(int server_id) {
    WsServerSlot* svr = &g_servers[server_id];
    wslog("AcceptThread started (server_id=%d)", server_id);

    while (svr->running) {
        struct sockaddr_in addr;
#ifdef _WIN32
        int addr_len = sizeof(addr);
#else
        socklen_t addr_len = sizeof(addr);
#endif
        SOCKET fd = accept(svr->listen_fd, (struct sockaddr*)&addr, &addr_len);
        if (fd == INVALID_SOCKET) {
            if (!svr->running) break;
            continue;
        }
        // 顺手回收已死客户端
        ReapDeadClients(svr);

        std::thread(HandleConnectionThread, fd, server_id).detach();
    }
    wslog("AcceptThread exit (server_id=%d)", server_id);
}

static void PingThreadFunc(int server_id) {
    WsServerSlot* svr = &g_servers[server_id];
    wslog("PingThread started (server_id=%d)", server_id);

    int64_t last_ping = NowMs();
    while (svr->running) {
        SleepMs(500);
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
}

// ==================== 对外 API ====================

int CreateWebServer(uint16_t port) {
    std::unique_lock<std::mutex> lock(g_server_mtx);
    if (!InitSockets()) return -1;

    int slot = FindFreeServerSlot();
    if (slot < 0) return -1;

    WsServerSlot* svr = &g_servers[slot];
    svr->listen_fd     = INVALID_SOCKET;
    svr->state         = SERVER_IDLE;
    svr->running       = false;
    svr->clients.clear();
    WsQueueInit(&svr->recv_queue);
    WsQueueResume(&svr->recv_queue);

    svr->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (svr->listen_fd == INVALID_SOCKET) return -1;

    int opt = 1;
    setsockopt(svr->listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family      = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port        = htons(port);

    if (bind(svr->listen_fd, (struct sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
        CloseSocketCompat(svr->listen_fd); svr->listen_fd = INVALID_SOCKET; return -1;
    }
    if (listen(svr->listen_fd, WS_DEFAULT_BACKLOG) == SOCKET_ERROR) {
        CloseSocketCompat(svr->listen_fd); svr->listen_fd = INVALID_SOCKET; return -1;
    }

    svr->state   = SERVER_RUNNING;
    svr->running = true;

    svr->accept_thread = std::thread(AcceptThreadFunc, slot);
    svr->ping_thread   = std::thread(PingThreadFunc,   slot);

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
        CloseSocketCompat(svr->listen_fd);
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
    if (svr->accept_thread.joinable()) svr->accept_thread.join();
    if (svr->ping_thread.joinable())   svr->ping_thread.join();

    // 等待客户端线程退出，释放资源
    {
        std::lock_guard<std::mutex> lk(svr->clients_mtx);
        for (auto* cli : svr->clients) {
            if (!cli) continue;
            if (cli->read_thread.joinable())  cli->read_thread.join();
            if (cli->write_thread.joinable()) cli->write_thread.join();
            WsQueueClear(&cli->send_queue);
            delete cli;
        }
        svr->clients.clear();
    }

    WsQueueClear(&svr->recv_queue);
    WsQueueResume(&svr->recv_queue);  // 允许下次 Start 复用
    svr->state = SERVER_IDLE;
}
