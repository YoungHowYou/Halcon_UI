// ================================================================
// Halcon_UI Electron 正式版 — 前端应用逻辑
// 协议：WebSocket 二进制帧
//   帧格式: [magic 4B BE][json_len 4B BE][data_len 4B BE][JSON][BIN]
//   图像: CMD=0, 二进制为 JPEG（或 RAW planar）
// ================================================================

// ==================== 常量 ====================
const MAGIC = 0xDEADBEEF;
const HEADER_SIZE = 12;
const MAX_LOG = 500;
const WS_MAX_MESSAGE_SIZE = 64 * 1024 * 1024;

const PARAM_CATEGORY_CMD = {
    '算法参数': 10,
    '相机参数': 11,
    '存储参数': 12,
    '通信参数': 13,
    '运动参数': 14,
};

const COMP_LABELS = ['操作员', '产线工程师', '系统工程师', '管理员'];
const COMP_COLORS = ['lv0', 'lv1', 'lv2', 'lv3'];

// ==================== 状态 ====================
const state = {
    ws: null,
    connected: false,
    connecting: false,
    currentCategory: null,
    paramCache: {},
    windows: {},
    activeWindowId: 0,
    zoom: 1.0,
    panX: 0,
    panY: 0,
    mouseDown: false,
    dragStartX: 0,
    dragStartY: 0,
    startPanX: 0,
    startPanY: 0,
    fpsFrames: 0,
    fpsLastTime: performance.now(),
    stats: { total: 0, pass: 0, fail: 0 },
    defects: [],
    userLevel: 2,
};

// ==================== DOM 引用 ====================
function $(id) { return document.getElementById(id); }

const dom = {
    statusDot: $('statusDot'),
    statusText: $('statusText'),
    fps: $('fps'),
    paramTabs: $('paramTabs'),
    windowTabs: $('windowTabs'),
    imageContainer: $('imageContainer'),
    imageCanvas: $('imageCanvas'),
    placeholder: $('placeholder'),
    logContent: $('logContent'),
    statTotal: $('statTotal'),
    statPass: $('statPass'),
    statFail: $('statFail'),
    statYield: $('statYield'),
    defectBars: $('defectBars'),
    defectList: $('defectList'),
    resultContent: $('resultContent'),
    paramModal: $('paramModal'),
    modalTitle: $('modalTitle'),
    paramTree: $('paramTree'),
    paramDetail: $('paramDetail'),
};

const ctx = dom.imageCanvas.getContext('2d');

// ==================== 日志 ====================
function log(msg, cls) {
    var now = new Date().toLocaleTimeString();
    var entry = document.createElement('div');
    entry.className = 'log-entry ' + (cls || 'info');
    entry.innerHTML = '<span class="ts">[' + now + ']</span> <span class="msg">' + escapeHtml(msg) + '</span>';
    dom.logContent.appendChild(entry);
    dom.logContent.scrollTop = dom.logContent.scrollHeight;
    while (dom.logContent.children.length > MAX_LOG) {
        dom.logContent.removeChild(dom.logContent.firstChild);
    }
}

function escapeHtml(str) {
    var div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

// ==================== 连接状态 ====================
var reconnectTimer = null;
var reconnectDelay = 1000;
var reconnectAttempts = 0;

function setConnectionStatus(status) {
    state.connected = (status === 'connected');
    state.connecting = (status === 'connecting');
    dom.statusDot.className = 'dot' +
        (status === 'connected' ? ' connected' : '') +
        (status === 'connecting' ? ' connecting' : '');
    dom.statusText.textContent =
        status === 'connected' ? '已连接' :
        status === 'connecting' ? '连接中...' : '未连接';
}

// ==================== WebSocket 连接 ====================
function getWsUrl() {
    return (window.electronAPI && window.electronAPI.wsUrl) || 'ws://127.0.0.1:1802/ws';
}

function doConnect() {
    var url = getWsUrl();
    if (!url) return log('WebSocket 地址为空', 'err');

    if (state.ws) {
        state.ws.close();
        state.ws = null;
    }

    setConnectionStatus('connecting');
    log('正在连接 ' + url + ' ...', 'info');

    var ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    state.ws = ws;

    ws.onopen = function () {
        log('WebSocket 已连接', 'rx');
        cancelReconnect();
        setConnectionStatus('connected');
        requestAllParams();
    };

    ws.onmessage = function (event) {
        if (!(event.data instanceof ArrayBuffer)) {
            log('收到非二进制消息，忽略', 'err');
            return;
        }
        if (event.data.byteLength > WS_MAX_MESSAGE_SIZE) {
            log('消息超过 64MB 限制，忽略', 'err');
            return;
        }
        var frame = parseFrame(event.data);
        if (frame) handleFrame(frame);
    };

    ws.onerror = function () {
        log('WebSocket 错误', 'err');
    };

    ws.onclose = function (e) {
        log('WebSocket 断开 (code=' + e.code + ')，将自动重连...', 'info');
        setConnectionStatus('disconnected');
        state.ws = null;
        scheduleReconnect();
    };
}

function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectAttempts++;
    var delay = Math.min(reconnectDelay * Math.pow(1.5, reconnectAttempts - 1), 30000);
    log('将在 ' + (delay / 1000).toFixed(1) + 's 后重连 (第' + reconnectAttempts + '次)', 'info');
    reconnectTimer = setTimeout(function () {
        reconnectTimer = null;
        doConnect();
    }, delay);
}

function cancelReconnect() {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
    reconnectAttempts = 0;
}

function doDisconnect() {
    if (state.ws) {
        state.ws.close(1000, '用户主动断开');
        state.ws = null;
    }
}

// ==================== 帧解析 ====================
function parseFrame(data) {
    if (data.byteLength < HEADER_SIZE) {
        log('帧太短 (' + data.byteLength + 'B)，忽略', 'err');
        return null;
    }

    var dv = new DataView(data);
    var magic = dv.getUint32(0, false);
    var jsonLen = dv.getUint32(4, false);
    var dataLen = dv.getUint32(8, false);

    if (magic !== MAGIC) {
        log('Magic 不匹配: 0x' + magic.toString(16) + ' (期望 0xDEADBEEF)', 'err');
        return null;
    }

    var total = HEADER_SIZE + jsonLen + dataLen;
    if (data.byteLength < total) {
        log('帧长度不足: 需要 ' + total + ', 实际 ' + data.byteLength, 'err');
        return null;
    }

    var jsonBytes = new Uint8Array(data, HEADER_SIZE, jsonLen);
    var decoder = new TextDecoder('utf-8');
    var jsonStr = decoder.decode(jsonBytes).replace(/\0+$/, '');

    var json;
    try {
        json = JSON.parse(jsonStr);
    } catch (e) {
        log('JSON 解析失败: ' + e.message, 'err');
        return null;
    }

    var binary = null;
    if (dataLen > 0) {
        binary = data.slice(HEADER_SIZE + jsonLen, HEADER_SIZE + jsonLen + dataLen);
    }

    return { json: json, binary: binary, jsonLen: jsonLen, dataLen: dataLen };
}

// ==================== 帧打包 ====================
function packFrame(json) {
    var jsonStr = JSON.stringify(json) + '\0';
    var encoder = new TextEncoder();
    var jsonBytes = encoder.encode(jsonStr);
    var jsonLen = jsonBytes.length;
    var total = HEADER_SIZE + jsonLen;

    var buf = new ArrayBuffer(total);
    var dv = new DataView(buf);
    dv.setUint32(0, MAGIC, false);
    dv.setUint32(4, jsonLen, false);
    dv.setUint32(8, 0, false);
    new Uint8Array(buf, HEADER_SIZE, jsonLen).set(jsonBytes);

    return buf;
}

// ==================== 发送命令 ====================
function sendCommand(cmd, data) {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) {
        log('未连接，无法发送', 'err');
        return false;
    }
    var json = { CMD: cmd, Data: data || {} };
    var buf = packFrame(json);
    state.ws.send(buf);
    log('发送 CMD=' + cmd + ' ' + JSON.stringify(data).substring(0, 100), 'tx');
    return true;
}

// ==================== 帧处理 ====================
function handleFrame(frame) {
    var json = frame.json;
    var binary = frame.binary;
    var cmd = json.CMD;
    var payload = json.Data || {};

    switch (cmd) {
        case 0:
            handleImage(payload, binary);
            break;
        case 1:
            handleDetectionResult(payload);
            break;
        case 200:
            handleReply(payload);
            break;
        default:
            if (cmd >= 10 && cmd <= 14) {
                handleParamResponse(cmd, payload);
            } else {
                log('收到 CMD=' + cmd + ' ' + JSON.stringify(payload).substring(0, 100), 'rx');
            }
    }
}

// ==================== 图像处理 (CMD=0) ====================
function handleImage(info, binary) {
    if (!binary || binary.byteLength === 0) {
        log('CMD=0 但无图像数据', 'err');
        return;
    }

    var imgId = info['图号'] !== undefined ? info['图号'] : (info.windowId !== undefined ? info.windowId : 0);
    var w = info['宽'] !== undefined ? info['宽'] : (info.width !== undefined ? info.width : 0);
    var h = info['高'] !== undefined ? info['高'] : (info.height !== undefined ? info.height : 0);
    var ch = info['通道'] !== undefined ? info['通道'] : (info.channels !== undefined ? info.channels : 1);
    var fmt = info.fmt || '';

    ensureWindow(imgId, w, h, ch);

    if (fmt === 'jpeg') {
        var blob = new Blob([binary], { type: 'image/jpeg' });
        var url = URL.createObjectURL(blob);
        var img = new Image();
        img.onload = function () {
            var win = state.windows[imgId];
            if (!win) { URL.revokeObjectURL(url); return; }
            win.img = img;
            win.w = w;
            win.h = h;
            win.ch = ch;
            win.fmt = fmt;
            if (imgId === state.activeWindowId) {
                renderCurrentWindow();
            }
            updateWindowTab(imgId);
            URL.revokeObjectURL(url);
            state.fpsFrames++;
        };
        img.onerror = function () {
            log('JPEG 解码失败 (图号=' + imgId + ')', 'err');
            URL.revokeObjectURL(url);
        };
        img.src = url;
    } else {
        var raw = new Uint8Array(binary);
        renderRawToWindow(imgId, raw, w, h, ch);
        var win = state.windows[imgId];
        if (win) {
            win.w = w;
            win.h = h;
            win.ch = ch;
            win.fmt = 'raw';
            updateWindowTab(imgId);
        }
        state.fpsFrames++;
    }

    dom.placeholder.style.display = 'none';
}

function renderRawToWindow(imgId, raw, w, h, channels) {
    var win = state.windows[imgId];
    if (!win) return;

    var cvs = win.canvas;
    if (cvs.width !== w || cvs.height !== h) {
        cvs.width = w;
        cvs.height = h;
    }

    var imgData = win.ctx.createImageData(w, h);
    var pixels = imgData.data;

    if (channels === 1) {
        for (var i = 0; i < w * h; i++) {
            var v = raw[i];
            var off = i * 4;
            pixels[off] = v;
            pixels[off + 1] = v;
            pixels[off + 2] = v;
            pixels[off + 3] = 255;
        }
    } else {
        var planeSize = w * h;
        for (var i = 0; i < planeSize; i++) {
            var off = i * 4;
            pixels[off] = raw[i];
            pixels[off + 1] = raw[planeSize + i];
            pixels[off + 2] = raw[planeSize * 2 + i];
            pixels[off + 3] = 255;
        }
    }
    win.ctx.putImageData(imgData, 0, 0);
}

// ==================== 多窗口管理 ====================
function ensureWindow(imgId, w, h, ch) {
    if (state.windows[imgId]) return state.windows[imgId];

    if (Object.keys(state.windows).length === 0) {
        state.activeWindowId = imgId;
    }

    var win = {
        id: imgId,
        img: null,
        w: w || 0,
        h: h || 0,
        ch: ch || 1,
        fmt: '',
        canvas: dom.imageCanvas,
        ctx: ctx,
    };
    state.windows[imgId] = win;
    renderWindowTabs();
    return win;
}

function renderWindowTabs() {
    var ids = Object.keys(state.windows).map(Number).sort(function (a, b) { return a - b; });
    if (ids.length <= 1) {
        dom.windowTabs.classList.add('hidden');
        return;
    }
    dom.windowTabs.classList.remove('hidden');
    dom.windowTabs.innerHTML = ids.map(function (id) {
        var info = '';
        var w = state.windows[id];
        if (w && w.w > 0) {
            info = w.w + 'x' + w.h + (w.ch === 1 ? ' 灰度' : ' RGB');
        }
        var activeClass = (id === state.activeWindowId) ? ' active' : '';
        return '<div class="window-tab' + activeClass + '" data-window-id="' + id + '">图号 ' + id + (info ? ' <span style="font-size:10px;opacity:0.6">' + info + '</span>' : '') + '</div>';
    }).join('');

    dom.windowTabs.querySelectorAll('.window-tab').forEach(function (tab) {
        tab.addEventListener('click', function () {
            switchWindow(parseInt(this.dataset.windowId, 10));
        });
    });
}

function updateWindowTab(imgId) {
    renderWindowTabs();
}

function switchWindow(imgId) {
    state.activeWindowId = imgId;
    renderWindowTabs();
    fitImageToWindow();
}

// ==================== Canvas 渲染 & 交互 ====================
function renderCurrentWindow() {
    var win = state.windows[state.activeWindowId];
    var cvs = dom.imageCanvas;
    var ctx2d = cvs.getContext('2d');

    var rect = dom.imageContainer.getBoundingClientRect();
    if (cvs.width !== rect.width || cvs.height !== rect.height) {
        cvs.width = rect.width;
        cvs.height = rect.height;
    }

    ctx2d.clearRect(0, 0, cvs.width, cvs.height);

    if (!win || !win.img) return;

    var img = win.img;
    var iw = img.naturalWidth || win.w;
    var ih = img.naturalHeight || win.h;
    if (iw === 0 || ih === 0) return;

    var scale = state.zoom;
    var sw = iw * scale;
    var sh = ih * scale;
    var sx = (cvs.width - sw) / 2 + state.panX;
    var sy = (cvs.height - sh) / 2 + state.panY;

    ctx2d.imageSmoothingEnabled = true;
    ctx2d.imageSmoothingQuality = 'high';
    ctx2d.drawImage(img, sx, sy, sw, sh);
}

function fitImageToWindow() {
    var win = state.windows[state.activeWindowId];
    if (!win || !win.img) return;

    var cvs = dom.imageCanvas;
    var rect = dom.imageContainer.getBoundingClientRect();
    var iw = win.img.naturalWidth || win.w;
    var ih = win.img.naturalHeight || win.h;
    if (iw === 0 || ih === 0) return;

    var scaleX = rect.width / iw;
    var scaleY = rect.height / ih;
    state.zoom = Math.min(scaleX, scaleY) * 0.92;
    state.panX = 0;
    state.panY = 0;
    renderCurrentWindow();
}

// ---- 鼠标事件 ----
dom.imageCanvas.addEventListener('mousedown', function (e) {
    if (e.button !== 0) return;
    var win = state.windows[state.activeWindowId];
    if (!win || !win.img) return;
    state.mouseDown = true;
    state.dragStartX = e.clientX;
    state.dragStartY = e.clientY;
    state.startPanX = state.panX;
    state.startPanY = state.panY;
    dom.imageContainer.style.cursor = 'grabbing';
});

window.addEventListener('mousemove', function (e) {
    if (!state.mouseDown) return;
    var dx = e.clientX - state.dragStartX;
    var dy = e.clientY - state.dragStartY;
    state.panX = state.startPanX + dx;
    state.panY = state.startPanY + dy;
    renderCurrentWindow();
});

window.addEventListener('mouseup', function () {
    if (state.mouseDown) {
        state.mouseDown = false;
        dom.imageContainer.style.cursor = 'grab';
    }
});

// 滚轮缩放
dom.imageCanvas.addEventListener('wheel', function (e) {
    e.preventDefault();
    var win = state.windows[state.activeWindowId];
    if (!win || !win.img) return;

    var delta = e.deltaY > 0 ? 0.9 : 1.1;
    var newZoom = Math.min(10, Math.max(0.05, state.zoom * delta));

    var rect = dom.imageContainer.getBoundingClientRect();
    var mx = e.clientX - rect.left;
    var my = e.clientY - rect.top;
    var scale = newZoom / state.zoom;
    state.panX = mx - scale * (mx - state.panX);
    state.panY = my - scale * (my - state.panY);
    state.zoom = newZoom;
    renderCurrentWindow();
}, { passive: false });

// 双击适应
dom.imageCanvas.addEventListener('dblclick', function (e) {
    if (e.button === 0) {
        fitImageToWindow();
    }
});

// 禁止右键菜单
dom.imageCanvas.addEventListener('contextmenu', function (e) {
    e.preventDefault();
});

// 窗口大小变化
window.addEventListener('resize', function () {
    renderCurrentWindow();
});

var resizeObserver = new ResizeObserver(function () {
    renderCurrentWindow();
});
resizeObserver.observe(dom.imageContainer);

// ==================== 检测结果处理 (CMD=1) ====================
function handleDetectionResult(payload) {
    log('检测结果: ' + (payload.result || '?') + ' | 缺陷: ' + (payload.defectType || '--') + ' x' + (payload.defectCount || 0), 'rx');

    state.stats.total = (state.stats.total || 0) + 1;
    if (payload.result === 'OK' || payload.result === 'PASS' || payload.result === '良品') {
        state.stats.pass = (state.stats.pass || 0) + 1;
    } else {
        state.stats.fail = (state.stats.fail || 0) + 1;
        if (payload.defectType) {
            addDefectRecord(payload);
        }
    }
    updateStatsDisplay();
    updateResultArea(payload);

    if (payload.defectBars) {
        updateDefectBars(payload.defectBars);
    }
}

function addDefectRecord(payload) {
    var defect = {
        name: payload.defectType || '未知缺陷',
        desc: (payload.defectDesc || '') + ' 面积:' + (payload.area || '?') + 'px²',
        img: null,
    };

    if (payload.defectImage) {
        defect.img = 'data:image/jpeg;base64,' + payload.defectImage;
    }

    state.defects.unshift(defect);
    if (state.defects.length > 100) state.defects.pop();
    renderDefectList();
}

function updateStatsDisplay() {
    var total = state.stats.total || 0;
    var pass = state.stats.pass || 0;
    var fail = state.stats.fail || 0;
    dom.statTotal.textContent = total.toLocaleString();
    dom.statPass.textContent = pass.toLocaleString();
    dom.statFail.textContent = fail.toLocaleString();
    dom.statYield.textContent = total > 0 ? (pass / total * 100).toFixed(1) + '%' : '--';
}

function updateDefectBars(bars) {
    if (!Array.isArray(bars)) return;
    var html = '';
    var colors = ['#ff6b6b', '#ff9f4a', '#4a9eff', '#00d4aa', '#8fa4c8', '#ffd93d', '#a29bfe', '#fd79a8'];
    bars.forEach(function (bar, idx) {
        var pct = Math.min(100, Math.max(0, (bar.count || bar.value || 0) / Math.max(bar.max || 50, 1) * 100));
        html += '<div class="defect-bar-row">' +
            '<span class="bar-label">' + escapeHtml(bar.label || bar.name || '?') + '</span>' +
            '<div class="bar-track"><div class="bar-fill" style="width:' + pct + '%;background:' + (bar.color || colors[idx % colors.length]) + ';"></div></div>' +
            '<span class="bar-value">' + (bar.count || bar.value || 0) + '</span>' +
            '</div>';
    });
    dom.defectBars.innerHTML = html;
}

function renderDefectList() {
    if (state.defects.length === 0) {
        dom.defectList.innerHTML = '<div class="defect-empty">暂无缺陷</div>';
        return;
    }
    var html = '';
    state.defects.forEach(function (d) {
        var imgHtml = d.img
            ? '<img src="' + d.img + '" alt="缺陷" />'
            : '<div style="width:56px;height:56px;background:var(--bg-input);border-radius:4px;display:flex;align-items:center;justify-content:center;font-size:24px;">!</div>';
        html += '<div class="defect-item">' +
            imgHtml +
            '<div class="defect-info">' +
            '<div class="defect-name">' + escapeHtml(d.name) + '</div>' +
            '<div class="defect-desc">' + escapeHtml(d.desc) + '</div>' +
            '</div></div>';
    });
    dom.defectList.innerHTML = html;
}

// ==================== 结果展示区 ====================
function updateResultArea(payload) {
    var isOk = payload.result === 'OK' || payload.result === 'PASS' || payload.result === '良品';
    var html = '';

    // 判定结果
    html += '<div class="result-header">' +
        '<span class="rh-dot ' + (isOk ? 'ok' : 'ng') + '"></span>' +
        '判定: <span class="r-value ' + (isOk ? 'ok' : 'ng') + '">' + escapeHtml(payload.result || '--') + '</span>' +
        '</div>';

    // 缺陷类型
    if (payload.defectType) {
        html += '<div class="result-row">' +
            '<span class="r-label">缺陷类型</span>' +
            '<span class="r-value ng">' + escapeHtml(payload.defectType) + '</span>' +
            '</div>';
    }

    // 缺陷数量
    if (payload.defectCount !== undefined) {
        html += '<div class="result-row">' +
            '<span class="r-label">缺陷数量</span>' +
            '<span class="r-value' + (payload.defectCount > 0 ? ' ng' : '') + '">' + payload.defectCount + '</span>' +
            '</div>';
    }

    // 面积
    if (payload.area !== undefined) {
        html += '<div class="result-row">' +
            '<span class="r-label">面积</span>' +
            '<span class="r-value">' + payload.area + ' px²</span>' +
            '</div>';
    }

    // 长度
    if (payload.length !== undefined) {
        html += '<div class="result-row">' +
            '<span class="r-label">长度</span>' +
            '<span class="r-value">' + payload.length + ' px</span>' +
            '</div>';
    }

    // 宽度
    if (payload.width !== undefined) {
        html += '<div class="result-row">' +
            '<span class="r-label">宽度</span>' +
            '<span class="r-value">' + payload.width + ' px</span>' +
            '</div>';
    }

    // 位置
    if (payload.x !== undefined || payload.y !== undefined) {
        html += '<div class="result-row">' +
            '<span class="r-label">位置</span>' +
            '<span class="r-value">(' + (payload.x || 0) + ', ' + (payload.y || 0) + ')</span>' +
            '</div>';
    }

    // 置信度
    if (payload.confidence !== undefined) {
        var conf = parseFloat(payload.confidence);
        var confClass = conf >= 0.8 ? ' ok' : (conf >= 0.5 ? '' : ' ng');
        html += '<div class="result-row">' +
            '<span class="r-label">置信度</span>' +
            '<span class="r-value' + confClass + '">' + (conf * 100).toFixed(1) + '%</span>' +
            '</div>';
    }

    // 额外字段（遍历 unknown 字段）
    var knownKeys = ['result', 'defectType', 'defectCount', 'area', 'length', 'width', 'x', 'y', 'confidence', 'defectBars', 'defectImage', 'defectDesc'];
    var hasExtra = false;
    for (var key in payload) {
        if (payload.hasOwnProperty(key) && knownKeys.indexOf(key) === -1) {
            if (!hasExtra) {
                html += '<hr class="result-sep">';
                hasExtra = true;
            }
            html += '<div class="result-row">' +
                '<span class="r-label">' + escapeHtml(key) + '</span>' +
                '<span class="r-value">' + escapeHtml(String(payload[key])) + '</span>' +
                '</div>';
        }
    }

    dom.resultContent.innerHTML = html || '<div class="result-empty">等待检测结果...</div>';
    dom.resultContent.scrollTop = 0;
}

// ==================== 命令回复 (CMD=200) ====================
function handleReply(payload) {
    log('回复: ' + JSON.stringify(payload).substring(0, 150), 'rx');
    if (payload.status === 'ok' || payload.status === 'saved') {
        log('操作成功', 'rx');
    } else if (payload.status === 'error') {
        log('错误: ' + (payload.message || '未知错误'), 'err');
    }
}

// ==================== 参数响应 (CMD 10-14) ====================
function handleParamResponse(cmd, payload) {
    var category = null;
    for (var key in PARAM_CATEGORY_CMD) {
        if (PARAM_CATEGORY_CMD[key] === cmd) { category = key; break; }
    }
    if (!category) return;

    log('收到参数数据 (' + category + ')', 'rx');

    var paramData = payload.params || payload.ParamList || payload;
    state.paramCache[category] = paramData;

    if (state.currentCategory === category) {
        renderParamTree(category);
    }
}

// ==================== 参数弹窗 ====================
function openParamModal(category) {
    if (!state.connected) {
        log('请先连接后端', 'err');
        return;
    }
    state.currentCategory = category;
    dom.modalTitle.textContent = category;

    if (state.paramCache[category]) {
        renderParamTree(category);
    } else {
        dom.paramTree.innerHTML = '<div class="param-loading">正在请求参数...</div>';
        dom.paramDetail.innerHTML = '<div class="empty-state"><p>加载中...</p></div>';
        requestParams(category);
    }

    dom.paramModal.classList.add('active');
}

function closeParamModal() {
    dom.paramModal.classList.remove('active');
}

function requestParams(category) {
    var cmd = PARAM_CATEGORY_CMD[category];
    if (cmd) {
        sendCommand(cmd, { action: 'read' });
    }
}

function requestAllParams() {
    Object.keys(PARAM_CATEGORY_CMD).forEach(function (category) {
        sendCommand(PARAM_CATEGORY_CMD[category], { action: 'read' });
    });
}

// ---- 参数树渲染 ----
var selectedParamPath = [];

function renderParamTree(category) {
    var data = state.paramCache[category];
    if (!data) {
        dom.paramTree.innerHTML = '<div class="param-loading">无参数数据</div>';
        dom.paramDetail.innerHTML = '<div class="empty-state"><p>无数据</p></div>';
        return;
    }

    dom.paramTree.innerHTML = '';
    var treeData = buildTreeData(data, [category]);
    treeData.forEach(function (node) {
        renderTreeNode(node, dom.paramTree, 0);
    });

    var childrenEls = dom.paramTree.querySelectorAll('.tree-node > .children');
    childrenEls.forEach(function (el) { el.classList.add('open'); });
    var arrows = dom.paramTree.querySelectorAll('.tree-node > .node-label .arrow');
    arrows.forEach(function (el) { el.classList.add('open'); });

    var firstLabel = dom.paramTree.querySelector('.tree-node > .node-label');
    if (firstLabel) firstLabel.click();
}

function buildTreeData(obj, path) {
    var result = [];
    for (var key in obj) {
        if (!obj.hasOwnProperty(key)) continue;
        var val = obj[key];
        var isLeaf = isParamLeaf(val);
        var node = {
            key: key,
            path: path.concat([key]),
            isLeaf: isLeaf,
            children: isLeaf ? [] : buildTreeData(val, path.concat([key])),
            data: isLeaf ? val : null,
        };
        result.push(node);
    }
    return result;
}

function isParamLeaf(val) {
    if (!val || typeof val !== 'object') return false;
    if (val.Type !== undefined) return true;
    if (val.Value !== undefined && val.Type === undefined) {
        var keys = Object.keys(val);
        var hasSubObj = keys.some(function (k) {
            return val[k] && typeof val[k] === 'object' && !Array.isArray(val[k]);
        });
        if (!hasSubObj) return true;
    }
    return false;
}

function renderTreeNode(node, parentEl, depth) {
    var div = document.createElement('div');
    div.className = 'tree-node';

    var label = document.createElement('div');
    label.className = 'node-label';
    label.style.paddingLeft = (12 + depth * 16) + 'px';
    label.dataset.path = JSON.stringify(node.path);

    var arrow = document.createElement('span');
    arrow.className = 'arrow';
    if (!node.isLeaf) {
        arrow.textContent = '▶';
    } else {
        arrow.style.visibility = 'hidden';
    }
    label.appendChild(arrow);

    var icon = document.createElement('span');
    icon.className = 'icon';
    icon.textContent = node.isLeaf ? '📄' : '📁';
    label.appendChild(icon);

    var nameSpan = document.createElement('span');
    nameSpan.className = 'name';
    nameSpan.textContent = node.key;
    label.appendChild(nameSpan);

    var badge = document.createElement('span');
    badge.className = 'badge';
    if (!node.isLeaf) {
        badge.textContent = node.children.length;
    } else if (node.data) {
        var comp = node.data.Competence || node.data.competence || 0;
        badge.textContent = 'Lv' + comp;
        badge.style.color = ['#8fa4c8', '#00d4aa', '#4a9eff', '#ff9f4a'][comp] || '#8fa4c8';
    }
    label.appendChild(badge);

    div.appendChild(label);

    if (!node.isLeaf) {
        var childrenDiv = document.createElement('div');
        childrenDiv.className = 'children';
        node.children.forEach(function (child) {
            renderTreeNode(child, childrenDiv, depth + 1);
        });
        div.appendChild(childrenDiv);

        label.addEventListener('click', function (e) {
            e.stopPropagation();
            var isOpen = childrenDiv.classList.contains('open');
            childrenDiv.classList.toggle('open');
            arrow.classList.toggle('open');
            selectParamNode(node.path, label);
        });
    } else {
        label.addEventListener('click', function (e) {
            e.stopPropagation();
            selectParamNode(node.path, label);
        });
    }

    parentEl.appendChild(div);
}

function selectParamNode(path, labelEl) {
    selectedParamPath = path;
    dom.paramTree.querySelectorAll('.node-label.active').forEach(function (el) {
        el.classList.remove('active');
    });
    if (labelEl) labelEl.classList.add('active');
    showParamDetail(path);
}

function showParamDetail(path) {
    if (!path || path.length === 0) {
        dom.paramDetail.innerHTML = '<div class="empty-state"><svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8zm-1-13h2v6h-2zm0 8h2v2h-2z"/></svg><p>从左侧选择参数分组</p></div>';
        return;
    }

    var category = path[0];
    var data = state.paramCache[category];
    if (!data) {
        dom.paramDetail.innerHTML = '<div class="empty-state"><p>无数据</p></div>';
        return;
    }

    var obj = data;
    for (var i = 1; i < path.length; i++) {
        if (obj && obj[path[i]] !== undefined) {
            obj = obj[path[i]];
        } else {
            obj = null;
            break;
        }
    }

    if (!obj || typeof obj !== 'object') {
        dom.paramDetail.innerHTML = '<div class="empty-state"><p>无参数</p></div>';
        return;
    }

    if (isParamLeaf(obj)) {
        var parentPath = path.slice(0, -1);
        var parentObj = data;
        for (var j = 1; j < parentPath.length; j++) {
            if (parentObj && parentObj[parentPath[j]] !== undefined) {
                parentObj = parentObj[parentPath[j]];
            } else {
                parentObj = null;
                break;
            }
        }
        if (parentObj) {
            renderParamGroup(parentPath, parentObj);
        } else {
            dom.paramDetail.innerHTML = '<div class="empty-state"><p>无参数</p></div>';
        }
        return;
    }

    renderParamGroup(path, obj);
}

function renderParamGroup(path, obj) {
    var name = path[path.length - 1] || '根';
    var html = '<div class="param-group-title">📂 ' + escapeHtml(name) + '</div>';
    html += '<div class="param-group-desc">路径: ' + path.join(' / ') + ' · ' + Object.keys(obj).length + ' 项</div>';

    var items = [];
    for (var key in obj) {
        if (!obj.hasOwnProperty(key)) continue;
        var val = obj[key];
        if (val && typeof val === 'object' && val.Type !== undefined) {
            items.push({ key: key, data: val, isLeaf: true });
        } else if (val && typeof val === 'object' && !Array.isArray(val)) {
            items.push({ key: key, data: val, isLeaf: false });
        }
    }

    if (items.length === 0) {
        html += '<div class="text-muted" style="padding:12px;">此分组为空</div>';
        dom.paramDetail.innerHTML = html;
        return;
    }

    for (var i = 0; i < items.length; i++) {
        var item = items[i];
        if (item.isLeaf) {
            html += renderParamItem(item.key, item.data, path);
        } else {
            var subPath = path.concat([item.key]);
            html += '<div style="margin:8px 0 4px 0;padding:6px 10px;background:var(--bg-primary);border-radius:4px;border-left:2px solid var(--accent-cyan);">' +
                '<div style="display:flex;align-items:center;gap:6px;cursor:pointer;font-weight:500;color:var(--text-secondary);" onclick="toggleSubGroup(this)">' +
                '<span class="arrow-sub">▶</span> 📁 ' + escapeHtml(item.key) +
                '<span style="font-size:11px;color:var(--text-muted);font-weight:400;">' + Object.keys(item.data).length + ' 项</span>' +
                '</div><div class="sub-group" style="padding-left:16px;display:none;margin-top:4px;">';

            var subItems = [];
            for (var sk in item.data) {
                if (!item.data.hasOwnProperty(sk)) continue;
                var sv = item.data[sk];
                if (sv && typeof sv === 'object' && sv.Type !== undefined) {
                    subItems.push({ key: sk, data: sv });
                }
            }
            if (subItems.length === 0) {
                html += '<div class="text-muted" style="padding:4px 0;">此分组无直接参数</div>';
            } else {
                for (var si = 0; si < subItems.length; si++) {
                    html += renderParamItem(subItems[si].key, subItems[si].data, subPath);
                }
            }
            html += '</div></div>';
        }
    }

    dom.paramDetail.innerHTML = html;
}

function renderParamItem(key, data, parentPath) {
    var comp = data.Competence || data.competence || 0;
    var type = data.Type || data.type || 'string';
    var limit = data.Limit || data.limit || '';
    var value = data.Value !== undefined ? data.Value : (data.value !== undefined ? data.value : '');
    var canEdit = comp <= state.userLevel;
    var compLabel = COMP_LABELS[comp] || '未知';
    var compColor = COMP_COLORS[comp] || 'lv0';
    var fullPath = parentPath.concat([key]);
    var pathStr = JSON.stringify(fullPath).replace(/'/g, "\\'");

    var controlHtml = '';
    switch (type) {
        case 'int':
        case 'float':
            var isInt = type === 'int';
            var step = isInt ? '1' : '0.1';
            var min = Array.isArray(limit) ? limit[0] : '';
            var max = Array.isArray(limit) ? limit[1] : '';
            var val = (value !== undefined && value !== '') ? value : (isInt ? 0 : 0.0);
            controlHtml =
                '<input type="number" step="' + step + '" value="' + val + '" ' + (canEdit ? '' : 'disabled') +
                ' data-path="' + pathStr + '" data-type="' + type + '" class="param-input" />' +
                (Array.isArray(limit) ? '<span class="range-hint">[' + limit[0] + ' ~ ' + limit[1] + ']</span>' : '');
            break;
        case 'bool':
            var isOn = value === 'TRUE' || value === true || value === 1;
            controlHtml =
                '<div class="bool-switch" data-path="' + pathStr + '" data-type="bool">' +
                '<div class="track' + (isOn ? ' on' : '') + '" onclick="toggleBoolSwitch(this)"><div class="thumb"></div></div>' +
                '<span class="bool-label">' + (isOn ? 'TRUE' : 'FALSE') + '</span></div>';
            break;
        case 'string':
            controlHtml =
                '<input type="text" value="' + escapeHtml(String(value || '')) + '" ' + (canEdit ? '' : 'disabled') +
                ' data-path="' + pathStr + '" data-type="string" class="param-input" />';
            break;
        case 'string_enum':
        case 'int_enum':
        case 'float_enum':
            var options = Array.isArray(limit) ? limit : [];
            controlHtml = '<select ' + (canEdit ? '' : 'disabled') +
                ' data-path="' + pathStr + '" data-type="enum" class="param-input">' +
                options.map(function (opt) {
                    return '<option value="' + opt + '"' + (String(opt) === String(value) ? ' selected' : '') + '>' + opt + '</option>';
                }).join('') + '</select>';
            break;
        case 'directory':
        case 'path':
            controlHtml =
                '<div style="display:flex;gap:4px;flex:1;max-width:280px;">' +
                '<input type="text" value="' + escapeHtml(String(value || '')) + '" ' + (canEdit ? '' : 'disabled') +
                ' data-path="' + pathStr + '" data-type="path" class="param-input" style="flex:1;" />' +
                '<button class="btn btn-outline btn-sm" style="padding:0 10px;height:30px;" onclick="browsePath(this)" ' + (canEdit ? '' : 'disabled') + '>📂</button></div>';
            break;
        default:
            controlHtml = '<span class="text-muted">' + escapeHtml(String(value)) + ' (' + type + ')</span>';
    }

    return '<div class="param-item">' +
        '<span class="p-name">' + escapeHtml(key) + '</span>' +
        '<div class="p-meta">' +
        '<span class="type-tag">' + type + '</span>' +
        '<span class="comp-tag ' + compColor + '">Lv' + comp + ' · ' + compLabel + '</span>' +
        (canEdit ? '' : '<span style="color:var(--accent-red);font-size:11px;">🔒</span>') +
        '</div>' +
        '<div class="p-control">' + controlHtml + '</div></div>';
}

// ---- 全局函数（供 HTML onclick 使用）----
window.toggleSubGroup = function (el) {
    var sub = el.nextElementSibling;
    if (sub) {
        var isHidden = sub.style.display === 'none';
        sub.style.display = isHidden ? 'block' : 'none';
        var arrow = el.querySelector('.arrow-sub');
        if (arrow) arrow.textContent = isHidden ? '▼' : '▶';
    }
};

window.toggleBoolSwitch = function (trackEl) {
    var container = trackEl.closest('.bool-switch');
    if (!container) return;
    var isOn = trackEl.classList.contains('on');
    trackEl.classList.toggle('on');
    var labelEl = container.querySelector('.bool-label');
    if (labelEl) labelEl.textContent = isOn ? 'FALSE' : 'TRUE';
    try {
        var path = JSON.parse(container.dataset.path || '[]');
        var newVal = isOn ? 'FALSE' : 'TRUE';
        setParamValue(path, newVal);
        sendParamUpdate(path, newVal);
    } catch (ex) { /* ignore */ }
};

window.browsePath = function (btn) {
    var input = btn.parentElement.querySelector('input');
    if (!input) return;
    var fakePath = prompt('请输入路径:', input.value || './');
    if (fakePath !== null) {
        input.value = fakePath;
        try {
            var path = JSON.parse(input.dataset.path || '[]');
            setParamValue(path, fakePath);
            sendParamUpdate(path, fakePath);
        } catch (ex) { /* ignore */ }
    }
};

function getParamValue(path) {
    var category = path[0];
    var obj = state.paramCache[category];
    if (!obj) return undefined;
    for (var i = 1; i < path.length - 1; i++) {
        if (obj && obj[path[i]] !== undefined) {
            obj = obj[path[i]];
        } else {
            return undefined;
        }
    }
    var lastKey = path[path.length - 1];
    if (obj && typeof obj === 'object' && obj[lastKey] !== undefined) {
        if (typeof obj[lastKey] === 'object' && obj[lastKey].Value !== undefined) {
            return obj[lastKey].Value;
        }
        return obj[lastKey];
    }
    return undefined;
}

function setParamValue(path, value) {
    var category = path[0];
    var obj = state.paramCache[category];
    if (!obj) return;
    for (var i = 1; i < path.length - 1; i++) {
        if (obj[path[i]] === undefined) obj[path[i]] = {};
        obj = obj[path[i]];
    }
    var lastKey = path[path.length - 1];
    if (obj && typeof obj === 'object') {
        if (obj[lastKey] && typeof obj[lastKey] === 'object' && obj[lastKey].Value !== undefined) {
            obj[lastKey].Value = value;
        } else {
            obj[lastKey] = value;
        }
    }
}

function sendParamUpdate(path, value) {
    if (!state.connected) return;
    var category = path[0];
    var cmd = PARAM_CATEGORY_CMD[category];
    if (!cmd) return;
    sendCommand(cmd, { action: 'write', path: path, value: value });
}

// 监听参数输入变化
document.addEventListener('input', function (e) {
    var el = e.target;
    if (!el.classList.contains('param-input') || el.disabled) return;
    try {
        var path = JSON.parse(el.dataset.path || '[]');
        if (path.length === 0) return;
        var value = el.value;
        var type = el.dataset.type || 'string';
        if (type === 'int') value = parseInt(value, 10) || 0;
        else if (type === 'float') value = parseFloat(value) || 0.0;
        setParamValue(path, value);
    } catch (ex) { /* ignore */ }
});

document.addEventListener('change', function (e) {
    var el = e.target;
    if (el.tagName !== 'SELECT' || !el.classList.contains('param-input') || el.disabled) return;
    try {
        var path = JSON.parse(el.dataset.path || '[]');
        if (path.length === 0) return;
        setParamValue(path, el.value);
        sendParamUpdate(path, el.value);
    } catch (ex) { /* ignore */ }
});

document.addEventListener('focusout', function (e) {
    var el = e.target;
    if (!el.classList.contains('param-input') || el.disabled) return;
    try {
        var path = JSON.parse(el.dataset.path || '[]');
        if (path.length === 0) return;
        var value = el.value;
        var type = el.dataset.type || 'string';
        if (type === 'int') value = parseInt(value, 10) || 0;
        else if (type === 'float') value = parseFloat(value) || 0.0;
        sendParamUpdate(path, value);
    } catch (ex) { /* ignore */ }
});

// ==================== 按钮事件 ====================
dom.paramTabs.querySelectorAll('.param-tab').forEach(function (tab) {
    tab.addEventListener('click', function () {
        var category = this.dataset.category;
        if (!category) return;
        dom.paramTabs.querySelectorAll('.param-tab').forEach(function (t) { t.classList.remove('active'); });
        this.classList.add('active');
        openParamModal(category);
    });
});

$('btnCloseParams').addEventListener('click', closeParamModal);
dom.paramModal.addEventListener('click', function (e) {
    if (e.target === dom.paramModal) closeParamModal();
});

$('btnRefreshParams').addEventListener('click', function () {
    if (state.currentCategory) {
        requestParams(state.currentCategory);
        log('刷新参数 (' + state.currentCategory + ')', 'tx');
    }
});

$('btnSaveParams').addEventListener('click', function () {
    if (state.currentCategory) {
        var cmd = PARAM_CATEGORY_CMD[state.currentCategory];
        if (cmd) {
            sendCommand(cmd, { action: 'save' });
            var orig = this.textContent;
            this.textContent = '✅ 已保存';
            setTimeout(function () { $('btnSaveParams').textContent = orig; }, 1500);
        }
    }
});

// 键盘快捷键
document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') {
        if (dom.paramModal.classList.contains('active')) {
            closeParamModal();
        }
    }
});

// ==================== FPS ====================
setInterval(function () {
    var now = performance.now();
    var elapsed = (now - state.fpsLastTime) / 1000;
    var fps = elapsed > 0 ? Math.round(state.fpsFrames / elapsed) : 0;
    dom.fps.textContent = 'FPS: ' + fps;
    state.fpsFrames = 0;
    state.fpsLastTime = now;
}, 1000);

// ==================== 初始化 ====================
function init() {
    log('Halcon_UI Electron 客户端就绪', 'info');
    if (window.electronAPI) {
        log('Chrome ' + (window.electronAPI.versions && window.electronAPI.versions.chrome || '?') +
            ' | Node ' + (window.electronAPI.versions && window.electronAPI.versions.node || '?') +
            ' | Electron ' + (window.electronAPI.versions && window.electronAPI.versions.electron || '?'), 'info');
    }

    var rect = dom.imageContainer.getBoundingClientRect();
    dom.imageCanvas.width = rect.width;
    dom.imageCanvas.height = rect.height;

    dom.imageContainer.style.cursor = 'grab';

    var defaultTab = dom.paramTabs.querySelector('[data-category="算法参数"]');
    if (defaultTab) defaultTab.classList.add('active');

    // 自动连接
    log('自动连接 ' + getWsUrl() + ' ...', 'info');
    doConnect();
}

init();
