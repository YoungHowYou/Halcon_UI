// ============================================================
// Halcon_UI Electron 示例 — WebSocket 客户端 + 图像渲染
// 协议：WebSocket 二进制帧
//   帧格式: [magic 4B BE][json_len 4B BE][data_len 4B BE][JSON][BIN]
//   图像: CMD=0, 二进制为 JPEG
// ============================================================

// ==================== 常量 ====================
const MAGIC = 0xDEADBEEF;
const HEADER_SIZE = 12; // magic(4) + json_len(4) + data_len(4)

// ==================== 状态 ====================
let ws = null;
let fpsCounter = { frames: 0, lastTime: performance.now() };

// 多窗口 Canvas 管理
const canvases = {};   // { 图号: { canvas, ctx, w, h } }
const imageCache = {}; // { 图号: Image }

// ==================== DOM 引用 ====================
const $status = document.getElementById('status');
const $fps = document.getElementById('fps');
const $url = document.getElementById('url-input');
const $btnConn = document.getElementById('btn-connect');
const $btnDisc = document.getElementById('btn-disconnect');
const $log = document.getElementById('log');
const $cmdInput = document.getElementById('cmd-input');
const $panels = document.getElementById('image-panels');

// ==================== 日志 ====================
function log(msg, cls = '') {
    const now = new Date().toLocaleTimeString();
    const line = document.createElement('div');
    line.innerHTML = `<span class="ts">[${now}]</span> ` +
        (cls ? `<span class="${cls}">${msg}</span>` : msg);
    $log.appendChild(line);
    $log.scrollTop = $log.scrollHeight;
    // 保留最近 200 条
    while ($log.children.length > 200) $log.removeChild($log.firstChild);
}

// ==================== Canvas 管理 ====================
function ensurePanel(imgId) {
    if (canvases[imgId]) return canvases[imgId];

    // 创建新面板
    const panel = document.createElement('div');
    panel.className = 'img-panel';
    panel.id = `panel-${imgId}`;
    panel.innerHTML = `
        <div class="header">
            <span class="id">图号 ${imgId}</span>
            <span class="info" id="info-${imgId}">等待图像…</span>
        </div>
        <canvas id="canvas-${imgId}"></canvas>
    `;
    $panels.appendChild(panel);

    const canvas = document.getElementById(`canvas-${imgId}`);
    const ctx = canvas.getContext('2d');
    canvases[imgId] = { canvas, ctx, w: 0, h: 0 };
    return canvases[imgId];
}

function updatePanelInfo(imgId, w, h, channels) {
    const infoEl = document.getElementById(`info-${imgId}`);
    if (infoEl) {
        infoEl.textContent = `${w}×${h} ${channels === 1 ? '灰度' : 'RGB'}`;
    }
}

// ==================== 帧解析 ====================
function readU32BE(buf, offset) {
    return ((buf[offset] << 24) | (buf[offset + 1] << 16) |
            (buf[offset + 2] << 8) | buf[offset + 3]) >>> 0;
}

function parseFrame(data) {
    if (data.byteLength < HEADER_SIZE) {
        log('帧太短，忽略', 'err');
        return null;
    }

    const dv = new DataView(data);
    const magic = dv.getUint32(0, false);   // big-endian
    const jsonLen = dv.getUint32(4, false);
    const dataLen = dv.getUint32(8, false);

    if (magic !== MAGIC) {
        log(`Magic 不匹配: 0x${magic.toString(16)} (期望 0x${MAGIC.toString(16)})`, 'err');
        return null;
    }

    if (data.byteLength < HEADER_SIZE + jsonLen + dataLen) {
        log(`帧长度不足: 需要 ${HEADER_SIZE + jsonLen + dataLen}, 实际 ${data.byteLength}`, 'err');
        return null;
    }

    // 提取 JSON（含末尾 \0）
    const jsonBytes = new Uint8Array(data, HEADER_SIZE, jsonLen);
    const decoder = new TextDecoder('utf-8');
    const jsonStr = decoder.decode(jsonBytes).replace(/\0+$/, ''); // 去掉末尾 \0

    let json;
    try {
        json = JSON.parse(jsonStr);
    } catch (e) {
        log(`JSON 解析失败: ${e.message}`, 'err');
        return null;
    }

    // 提取二进制
    let binary = null;
    if (dataLen > 0) {
        binary = data.slice(HEADER_SIZE + jsonLen, HEADER_SIZE + jsonLen + dataLen);
    }

    return { json, binary, jsonLen, dataLen };
}

// ==================== 帧处理 ====================
function handleFrame(frame) {
    const { json, binary } = frame;
    const cmd = json.CMD;
    const payload = json.Data || {};

    switch (cmd) {
    case 0: // 图像数据
        handleImage(payload, binary);
        break;
    case 1: // 检测结果
        log(`📊 检测结果: ${payload.result || '?'} | 缺陷: ${payload.defectType || '--'} ×${payload.defectCount || 0}`, 'rx');
        break;
    case 2: // 晶棒编号
        log(`🆔 晶棒编号: ${payload.rodId || payload['晶棒编号'] || '?'}`, 'rx');
        break;
    case 3: // 设备连接状态
        log(`🔌 设备状态更新`, 'rx');
        break;
    case 4: // 系统资源
        log(`📈 CPU/内存`, 'rx');
        break;
    case 200: // 命令回复
        log(`↩ 回复 CMD=200: ${JSON.stringify(payload)}`, 'rx');
        break;
    default:
        log(`📨 收到 CMD=${cmd} ${JSON.stringify(payload).substring(0, 100)}`, 'rx');
    }
}

function handleImage(info, binary) {
    if (!binary || binary.byteLength === 0) {
        log('CMD=0 但无图像数据', 'err');
        return;
    }

    const imgId = info['图号'] ?? info.windowId ?? 0;
    const w = info['宽'] ?? info.width ?? 0;
    const h = info['高'] ?? info.height ?? 0;
    const ch = info['通道'] ?? info.channels ?? 1;
    const fmt = info.fmt || '';

    const panel = ensurePanel(imgId);

    if (fmt === 'jpeg') {
        // JPEG 二进制 → Blob → Image → Canvas
        const blob = new Blob([binary], { type: 'image/jpeg' });
        const url = URL.createObjectURL(blob);
        const img = new Image();
        img.onload = () => {
            const cvs = panel.canvas;
            // 自适应 Canvas 尺寸
            if (panel.w !== w || panel.h !== h) {
                panel.w = w;
                panel.h = h;
                // 保持宽高比缩放以适应面板
                const rect = cvs.parentElement.getBoundingClientRect();
                const scale = Math.min(rect.width / w, (rect.height - 30) / h, 1);
                cvs.width = w;
                cvs.height = h;
                cvs.style.width = (w * scale) + 'px';
                cvs.style.height = (h * scale) + 'px';
            }
            panel.ctx.drawImage(img, 0, 0);
            updatePanelInfo(imgId, w, h, ch);
            URL.revokeObjectURL(url);

            // FPS
            fpsCounter.frames++;
        };
        img.onerror = () => {
            log('JPEG 解码失败', 'err');
            URL.revokeObjectURL(url);
        };
        img.src = url;
    } else {
        // RAW planar 像素 (无 fmt 或 fmt != 'jpeg')
        const raw = new Uint8Array(binary);
        renderRaw(panel, raw, w, h, ch);
        updatePanelInfo(imgId, w, h, ch);
        fpsCounter.frames++;
    }
}

function renderRaw(panel, raw, w, h, channels) {
    const cvs = panel.canvas;
    if (panel.w !== w || panel.h !== h) {
        panel.w = w; panel.h = h;
        cvs.width = w; cvs.height = h;
        const rect = cvs.parentElement.getBoundingClientRect();
        const scale = Math.min(rect.width / w, (rect.height - 30) / h, 1);
        cvs.style.width = (w * scale) + 'px';
        cvs.style.height = (h * scale) + 'px';
    }

    const imgData = panel.ctx.createImageData(w, h);
    const pixels = imgData.data;

    if (channels === 1) {
        for (let i = 0; i < w * h; i++) {
            const v = raw[i];
            pixels[i * 4] = v;
            pixels[i * 4 + 1] = v;
            pixels[i * 4 + 2] = v;
            pixels[i * 4 + 3] = 255;
        }
    } else {
        // Planar RGB: [R][G][B] 各 W×H
        const planeSize = w * h;
        for (let i = 0; i < planeSize; i++) {
            pixels[i * 4] = raw[i];               // R
            pixels[i * 4 + 1] = raw[planeSize + i]; // G
            pixels[i * 4 + 2] = raw[planeSize * 2 + i]; // B
            pixels[i * 4 + 3] = 255;
        }
    }
    panel.ctx.putImageData(imgData, 0, 0);
}

// ==================== WebSocket 连接 ====================
function doConnect() {
    const url = $url.value.trim();
    if (!url) return log('请输入 WebSocket 地址', 'err');

    if (ws) {
        ws.close();
        ws = null;
    }

    log(`正在连接 ${url} …`);
    ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
        log('✅ WebSocket 已连接', 'rx');
        $status.textContent = '● 已连接';
        $status.className = 'connected';
        $btnConn.style.display = 'none';
        $btnDisc.style.display = '';
        $url.disabled = true;
    };

    ws.onmessage = (event) => {
        if (!(event.data instanceof ArrayBuffer)) {
            log('收到非二进制消息，忽略', 'err');
            return;
        }
        const frame = parseFrame(event.data);
        if (frame) handleFrame(frame);
    };

    ws.onerror = (e) => {
        log('❌ WebSocket 错误', 'err');
    };

    ws.onclose = (e) => {
        log(`🔌 WebSocket 断开 (code=${e.code})`);
        $status.textContent = '● 未连接';
        $status.className = 'disconnected';
        $btnConn.style.display = '';
        $btnDisc.style.display = 'none';
        $url.disabled = false;
        ws = null;
    };
}

function doDisconnect() {
    if (ws) {
        ws.close();
        ws = null;
    }
}

// ==================== 发送命令 ====================
function sendCommand() {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        log('未连接，无法发送', 'err');
        return;
    }

    const text = $cmdInput.value.trim();
    if (!text) return;

    let json;
    try {
        json = JSON.parse(text);
    } catch (e) {
        log(`JSON 格式错误: ${e.message}`, 'err');
        return;
    }

    // 打包帧: [magic 4B BE][json_len 4B BE][data_len 4B BE][JSON \0]
    const jsonStr = JSON.stringify(json) + '\0';
    const encoder = new TextEncoder();
    const jsonBytes = encoder.encode(jsonStr);
    const jsonLen = jsonBytes.length;
    const dataLen = 0;
    const total = HEADER_SIZE + jsonLen;

    const buf = new ArrayBuffer(total);
    const dv = new DataView(buf);
    dv.setUint32(0, MAGIC, false);
    dv.setUint32(4, jsonLen, false);
    dv.setUint32(8, dataLen, false);
    new Uint8Array(buf, HEADER_SIZE, jsonLen).set(jsonBytes);

    ws.send(buf);
    log(`📤 发送 CMD=${json.CMD}: ${text.substring(0, 80)}`, 'tx');
}

// ==================== FPS 更新 ====================
setInterval(() => {
    const now = performance.now();
    const elapsed = (now - fpsCounter.lastTime) / 1000;
    const fps = elapsed > 0 ? Math.round(fpsCounter.frames / elapsed) : 0;
    $fps.textContent = `FPS: ${fps}`;
    fpsCounter = { frames: 0, lastTime: now };
}, 1000);

// ==================== 键盘快捷键 ====================
document.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && e.ctrlKey) {
        sendCommand();
    }
});

// ==================== 启动时自动连接 ====================
log('🚀 Halcon_UI Electron 客户端就绪');
log(`Chrome ${window.electronAPI?.versions?.chrome || '?'} | Node ${window.electronAPI?.versions?.node || '?'}`);
// 可选：自动连接
// doConnect();
