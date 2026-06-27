// ================================================================
// Halcon_UI Electron — 前端应用逻辑（可拖动分隔条版）
// ================================================================

const MAGIC = 0xDEADBEEF;
const HEADER_SIZE = 12;
const MAX_LOG = 500;

const PARAM_CATEGORY_CMD = { '算法参数': 10, '相机参数': 11, '存储参数': 12, '通信参数': 13, '运动参数': 14 };
const COMP_LABELS = ['操作员', '产线工程师', '系统工程师', '管理员'];

const state = {
    ws: null, connected: false, connecting: false,
    currentCategory: null, paramCache: {},
    windows: {}, activeWindowId: 0,
    zoom: 1.0, panX: 0, panY: 0,
    mouseDown: false, dragStartX: 0, dragStartY: 0, startPanX: 0, startPanY: 0,
    fpsFrames: 0, fpsLastTime: performance.now(),
    stats: { total: 0, pass: 0, fail: 0 }, defects: [], userLevel: 2,
    // 批量离线检测
    batchFiles: [], batchIndex: -1,
};

function $(id) { return document.getElementById(id); }
function escapeHtml(s) { var d = document.createElement('div'); d.textContent = s; return d.innerHTML; }

const dom = {
    statusDot: $('statusDot'), statusText: $('statusText'), fps: $('fps'),
    paramTabs: $('paramTabs'), windowTabs: $('windowTabs'),
    imageContainer: $('imageContainer'), imageCanvas: $('imageCanvas'), placeholder: $('placeholder'),
    logContent: $('logContent'), statTotal: $('statTotal'), statPass: $('statPass'),
    statFail: $('statFail'), statYield: $('statYield'), defectBars: $('defectBars'),
    defectList: $('defectList'), resultContent: $('resultContent'),
    paramModal: $('paramModal'), modalTitle: $('modalTitle'),
    paramTree: $('paramTree'), paramDetail: $('paramDetail'),
};
const ctx = dom.imageCanvas.getContext('2d');

function log(msg, cls) {
    var now = new Date().toLocaleTimeString();
    var entry = document.createElement('div');
    entry.className = 'log-entry ' + (cls || 'info');
    entry.innerHTML = '<span class="ts">[' + now + ']</span> <span class="msg">' + escapeHtml(msg) + '</span>';
    dom.logContent.appendChild(entry);
    dom.logContent.scrollTop = dom.logContent.scrollHeight;
    while (dom.logContent.children.length > MAX_LOG) dom.logContent.removeChild(dom.logContent.firstChild);
}

// ==================== WebSocket ====================
let reconnectTimer = null, reconnectAttempts = 0;
function getWsUrl() { return (window.electronAPI && window.electronAPI.wsUrl) || 'ws://127.0.0.1:1802/ws'; }
function setConnectionStatus(s) {
    state.connected = (s === 'connected'); state.connecting = (s === 'connecting');
    dom.statusDot.className = 'dot' + (s === 'connected' ? ' connected' : '') + (s === 'connecting' ? ' connecting' : '');
    dom.statusText.textContent = s === 'connected' ? '已连接' : s === 'connecting' ? '连接中...' : '未连接';
}
function doDisconnect() { if (state.ws) { state.ws.close(1000); state.ws = null; } }
function cancelReconnect() { if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; } reconnectAttempts = 0; }
function scheduleReconnect() {
    if (reconnectTimer) return; reconnectAttempts++;
    var d = Math.min(1000 * Math.pow(1.5, reconnectAttempts - 1), 30000);
    log('将在 ' + (d / 1000).toFixed(1) + 's 后重连 (第' + reconnectAttempts + '次)', 'info');
    reconnectTimer = setTimeout(function () { reconnectTimer = null; doConnect(); }, d);
}
function doConnect() {
    var url = getWsUrl(); if (!url) return log('WebSocket 地址为空', 'err');
    if (state.ws) { state.ws.close(); state.ws = null; }
    setConnectionStatus('connecting'); log('正在连接 ' + url + ' ...', 'info');
    var ws = new WebSocket(url); ws.binaryType = 'arraybuffer'; state.ws = ws;
    ws.onopen = function () { log('WebSocket 已连接', 'rx'); cancelReconnect(); setConnectionStatus('connected'); requestAllParams(); };
    ws.onmessage = function (e) { if (!(e.data instanceof ArrayBuffer) || e.data.byteLength > 64 * 1024 * 1024) return; var f = parseFrame(e.data); if (f) handleFrame(f); };
    ws.onerror = function () { log('WebSocket 错误', 'err'); };
    ws.onclose = function (e) { log('WebSocket 断开 (code=' + e.code + ')', 'info'); setConnectionStatus('disconnected'); state.ws = null; scheduleReconnect(); };
}

// ==================== 帧编解码 ====================
function parseFrame(data) {
    if (data.byteLength < HEADER_SIZE) return null;
    var dv = new DataView(data), magic = dv.getUint32(0, false), jsonLen = dv.getUint32(4, false), dataLen = dv.getUint32(8, false);
    if (magic !== MAGIC) return null;
    var jsonStr = new TextDecoder('utf-8').decode(new Uint8Array(data, HEADER_SIZE, jsonLen)).replace(/\0+$/, '');
    var json; try { json = JSON.parse(jsonStr); } catch (e) { return null; }
    return { json: json, binary: dataLen > 0 ? data.slice(HEADER_SIZE + jsonLen, HEADER_SIZE + jsonLen + dataLen) : null };
}
function packFrame(json) {
    var jsonStr = JSON.stringify(json) + '\0', jsonBytes = new TextEncoder().encode(jsonStr);
    var buf = new ArrayBuffer(HEADER_SIZE + jsonBytes.length), dv = new DataView(buf);
    dv.setUint32(0, MAGIC, false); dv.setUint32(4, jsonBytes.length, false); dv.setUint32(8, 0, false);
    new Uint8Array(buf, HEADER_SIZE).set(jsonBytes); return buf;
}
function sendCommand(cmd, data) {
    if (!state.ws || state.ws.readyState !== WebSocket.OPEN) { log('未连接', 'err'); return false; }
    var buf = packFrame({ CMD: cmd, Data: data || {} }); state.ws.send(buf);
    log('发送 CMD=' + cmd + ' ' + JSON.stringify(data || {}).substring(0, 80), 'tx'); return true;
}

// ==================== 帧路由 ====================
function handleFrame(f) {
    var cmd = f.json.CMD, payload = f.json.Data || {};
    switch (cmd) { case 0: handleImage(payload, f.binary); break; case 1: handleDetection(payload); break; case 200: handleReply(payload); break; default: if (cmd >= 10 && cmd <= 14) handleParamResponse(cmd, payload); else log('收到 CMD=' + cmd, 'rx'); }
}

// ==================== 图像 (CMD=0) ====================
function handleImage(info, binary) {
    if (!binary || binary.byteLength === 0) return;
    var imgId = info['图号'] !== undefined ? info['图号'] : (info.windowId || 0);
    var w = info['宽'] || info.width || 0, h = info['高'] || info.height || 0;
    var ch = info['通道'] || info.channels || 1, fmt = info.fmt || '';
    ensureWindow(imgId, w, h, ch);
    if (fmt === 'jpeg') {
        var blob = new Blob([binary], { type: 'image/jpeg' }), url = URL.createObjectURL(blob), img = new Image();
        img.onload = function () { var win = state.windows[imgId]; if (!win) { URL.revokeObjectURL(url); return; } win.img = img; win.w = w; win.h = h; win.ch = ch; if (imgId === state.activeWindowId) renderCurrentWindow(); updateWindowTabs(); URL.revokeObjectURL(url); state.fpsFrames++; };
        img.src = url;
    } else { renderRawToWindow(imgId, new Uint8Array(binary), w, h, ch); var win = state.windows[imgId]; if (win) { win.w = w; win.h = h; win.ch = ch; updateWindowTabs(); } state.fpsFrames++; }
    dom.placeholder.style.display = 'none';
}
function renderRawToWindow(imgId, raw, w, h, ch) {
    var win = state.windows[imgId]; if (!win) return; var cvs = win.canvas; if (cvs.width !== w || cvs.height !== h) { cvs.width = w; cvs.height = h; }
    var idata = win.ctx.createImageData(w, h), px = idata.data;
    if (ch === 1) { for (var i = 0; i < w * h; i++) { var v = raw[i], o = i * 4; px[o] = px[o + 1] = px[o + 2] = v; px[o + 3] = 255; } }
    else { var ps = w * h; for (var i = 0; i < ps; i++) { var o = i * 4; px[o] = raw[i]; px[o + 1] = raw[ps + i]; px[o + 2] = raw[ps * 2 + i]; px[o + 3] = 255; } }
    win.ctx.putImageData(idata, 0, 0);
}

// ==================== 多窗口 ====================
function ensureWindow(imgId, w, h, ch) {
    if (state.windows[imgId]) return state.windows[imgId];
    if (Object.keys(state.windows).length === 0) state.activeWindowId = imgId;
    var win = { id: imgId, img: null, w: w || 0, h: h || 0, ch: ch || 1, canvas: dom.imageCanvas, ctx: ctx };
    state.windows[imgId] = win; updateWindowTabs(); return win;
}
function updateWindowTabs() {
    var ids = Object.keys(state.windows).map(Number).sort(function (a, b) { return a - b; });
    if (ids.length <= 1) { dom.windowTabs.classList.add('hidden'); return; }
    dom.windowTabs.classList.remove('hidden');
    dom.windowTabs.innerHTML = ids.map(function (id) {
        var w = state.windows[id], info = w && w.w > 0 ? w.w + 'x' + w.h + (w.ch === 1 ? ' 灰度' : ' RGB') : '';
        return '<div class="window-tab' + (id === state.activeWindowId ? ' active' : '') + '" data-id="' + id + '">图号 ' + id + (info ? ' <span style="font-size:10px;opacity:0.6">' + info + '</span>' : '') + '</div>';
    }).join('');
    dom.windowTabs.querySelectorAll('.window-tab').forEach(function (t) { t.onclick = function () { switchWindow(parseInt(t.dataset.id)); }; });
}
function switchWindow(id) { state.activeWindowId = id; updateWindowTabs(); fitImageToWindow(); }

// ==================== Canvas 渲染 ====================
function renderCurrentWindow() {
    var win = state.windows[state.activeWindowId], cvs = dom.imageCanvas, ctn = dom.imageContainer;
    var ctx2d = cvs.getContext('2d'), rect = ctn.getBoundingClientRect();
    if (cvs.width !== rect.width || cvs.height !== rect.height) { cvs.width = rect.width; cvs.height = rect.height; }
    ctx2d.clearRect(0, 0, cvs.width, cvs.height);
    if (!win || !win.img) return; var iw = win.img.naturalWidth || win.w, ih = win.img.naturalHeight || win.h;
    if (iw === 0 || ih === 0) return;
    var sw = iw * state.zoom, sh = ih * state.zoom;
    var sx = (cvs.width - sw) / 2 + state.panX, sy = (cvs.height - sh) / 2 + state.panY;
    ctx2d.imageSmoothingEnabled = true; ctx2d.imageSmoothingQuality = 'high'; ctx2d.drawImage(win.img, sx, sy, sw, sh);
}
function fitImageToWindow() { var win = state.windows[state.activeWindowId]; if (!win || !win.img) return; var r = dom.imageContainer.getBoundingClientRect(), iw = win.img.naturalWidth || win.w, ih = win.img.naturalHeight || win.h; state.zoom = Math.min(r.width / iw, r.height / ih) * 0.92; state.panX = state.panY = 0; renderCurrentWindow(); }

// Canvas 交互
dom.imageCanvas.onmousedown = function (e) { if (e.button !== 0 || !state.windows[state.activeWindowId] || !state.windows[state.activeWindowId].img) return; state.mouseDown = true; state.dragStartX = e.clientX; state.dragStartY = e.clientY; state.startPanX = state.panX; state.startPanY = state.panY; dom.imageContainer.style.cursor = 'grabbing'; };
window.addEventListener('mousemove', function (e) { if (!state.mouseDown) return; state.panX = state.startPanX + e.clientX - state.dragStartX; state.panY = state.startPanY + e.clientY - state.dragStartY; renderCurrentWindow(); });
window.addEventListener('mouseup', function () { state.mouseDown = false; dom.imageContainer.style.cursor = 'grab'; });
dom.imageCanvas.addEventListener('wheel', function (e) { e.preventDefault(); var win = state.windows[state.activeWindowId]; if (!win || !win.img) return; var nz = Math.min(10, Math.max(0.05, state.zoom * (e.deltaY > 0 ? 0.9 : 1.1))); var r = dom.imageContainer.getBoundingClientRect(), mx = e.clientX - r.left, my = e.clientY - r.top; var s = nz / state.zoom; state.panX = mx - s * (mx - state.panX); state.panY = my - s * (my - state.panY); state.zoom = nz; renderCurrentWindow(); }, { passive: false });
dom.imageCanvas.ondblclick = function (e) { if (e.button === 0) fitImageToWindow(); };
dom.imageCanvas.oncontextmenu = function (e) { e.preventDefault(); };
window.addEventListener('resize', function () { renderCurrentWindow(); });
var _resizeTimer = null;
new ResizeObserver(function () {
    if (_resizeTimer) clearTimeout(_resizeTimer);
    _resizeTimer = setTimeout(function () { renderCurrentWindow(); }, 80);
}).observe(dom.imageContainer);

// ==================== 检测结果 (CMD=1) ====================
function handleDetection(payload) {
    log('检测结果: ' + (payload.result || '?') + ' | ' + (payload.defectType || '--') + ' x' + (payload.defectCount || 0), 'rx');
    state.stats.total++; if (payload.result === 'OK' || payload.result === 'PASS' || payload.result === '良品') state.stats.pass++; else { state.stats.fail++; if (payload.defectType) addDefect(payload); }
    updateStats(); updateResult(payload); if (payload.defectBars) updateDefectBars(payload.defectBars);
}
function addDefect(payload) { state.defects.unshift({ name: payload.defectType || '未知', desc: (payload.defectDesc || '') + ' 面积:' + (payload.area || '?') + 'px²', img: payload.defectImage ? 'data:image/jpeg;base64,' + payload.defectImage : null }); if (state.defects.length > 100) state.defects.pop(); renderDefectList(); }
function updateStats() { var t = state.stats.total, p = state.stats.pass, f = state.stats.fail; dom.statTotal.textContent = t.toLocaleString(); dom.statPass.textContent = p.toLocaleString(); dom.statFail.textContent = f.toLocaleString(); dom.statYield.textContent = t > 0 ? (p / t * 100).toFixed(1) + '%' : '--'; }
function updateDefectBars(bars) { if (!Array.isArray(bars)) return; var colors = ['#ff6b6b', '#ff9f4a', '#4a9eff', '#00d4aa', '#8fa4c8']; dom.defectBars.innerHTML = bars.map(function (b, i) { var pct = Math.min(100, Math.max(0, (b.count || b.value || 0) / Math.max(b.max || 50, 1) * 100)); return '<div class="defect-bar-row"><span class="bar-label">' + escapeHtml(b.label || b.name || '?') + '</span><div class="bar-track"><div class="bar-fill" style="width:' + pct + '%;background:' + (b.color || colors[i % colors.length]) + ';"></div></div><span class="bar-value">' + (b.count || b.value || 0) + '</span></div>'; }).join(''); }
function renderDefectList() { if (state.defects.length === 0) { dom.defectList.innerHTML = '<div class="defect-empty">暂无缺陷</div>'; return; } dom.defectList.innerHTML = state.defects.map(function (d) { var ih = d.img ? '<img src="' + d.img + '" alt="" />' : '<div style="width:56px;height:56px;background:var(--bg-input);border-radius:4px;display:flex;align-items:center;justify-content:center;font-size:24px;">!</div>'; return '<div class="defect-item">' + ih + '<div class="defect-info"><div class="defect-name">' + escapeHtml(d.name) + '</div><div class="defect-desc">' + escapeHtml(d.desc) + '</div></div></div>'; }).join(''); }
function updateResult(payload) {
    var ok = payload.result === 'OK' || payload.result === 'PASS' || payload.result === '良品';
    var h = '<div class="result-header"><span class="rh-dot ' + (ok ? 'ok' : 'ng') + '"></span>判定: <span class="r-value ' + (ok ? 'ok' : 'ng') + '">' + escapeHtml(payload.result || '--') + '</span></div>';
    if (payload.defectType) h += '<div class="result-row"><span class="r-label">缺陷类型</span><span class="r-value ng">' + escapeHtml(payload.defectType) + '</span></div>';
    if (payload.defectCount !== undefined) h += '<div class="result-row"><span class="r-label">数量</span><span class="r-value' + (payload.defectCount > 0 ? ' ng' : '') + '">' + payload.defectCount + '</span></div>';
    if (payload.area !== undefined) h += '<div class="result-row"><span class="r-label">面积</span><span class="r-value">' + payload.area + ' px²</span></div>';
    if (payload.confidence !== undefined) { var c = parseFloat(payload.confidence); h += '<div class="result-row"><span class="r-label">置信度</span><span class="r-value' + (c >= 0.8 ? ' ok' : c < 0.5 ? ' ng' : '') + '">' + (c * 100).toFixed(1) + '%</span></div>'; }
    var known = ['result', 'defectType', 'defectCount', 'area', 'length', 'width', 'x', 'y', 'confidence', 'defectBars', 'defectImage', 'defectDesc'];
    var extra = false; for (var k in payload) { if (!payload.hasOwnProperty(k) || known.indexOf(k) >= 0) continue; if (!extra) { h += '<hr class="result-sep">'; extra = true; } h += '<div class="result-row"><span class="r-label">' + escapeHtml(k) + '</span><span class="r-value">' + escapeHtml(String(payload[k])) + '</span></div>'; }
    dom.resultContent.innerHTML = h || '<div class="result-empty">等待检测结果...</div>'; dom.resultContent.scrollTop = 0;
}

// ==================== 命令回复 ====================
function handleReply(p) { log('回复: ' + JSON.stringify(p).substring(0, 120), 'rx'); }

// ==================== 参数 ====================
function handleParamResponse(cmd, payload) { var cat = null; for (var k in PARAM_CATEGORY_CMD) { if (PARAM_CATEGORY_CMD[k] === cmd) { cat = k; break; } } if (!cat) return; state.paramCache[cat] = payload.params || payload.ParamList || payload; if (state.currentCategory === cat) renderParamTree(cat); }
function requestAllParams() { for (var k in PARAM_CATEGORY_CMD) sendCommand(PARAM_CATEGORY_CMD[k], { action: 'read' }); }
function requestParams(cat) { var c = PARAM_CATEGORY_CMD[cat]; if (c) sendCommand(c, { action: 'read' }); }
function openParamModal(cat) { if (!state.connected) return; state.currentCategory = cat; dom.modalTitle.textContent = cat; if (state.paramCache[cat]) renderParamTree(cat); else { dom.paramTree.innerHTML = '<div class="param-loading">正在请求参数...</div>'; dom.paramDetail.innerHTML = '<div class="empty-state"><p>加载中...</p></div>'; requestParams(cat); } dom.paramModal.classList.add('active'); }
function closeParamModal() { dom.paramModal.classList.remove('active'); }

var selectedParamPath = [];
function renderParamTree(cat) { var d = state.paramCache[cat]; if (!d) return; dom.paramTree.innerHTML = ''; buildTreeData(d, [cat]).forEach(function (n) { renderTreeNode(n, dom.paramTree, 0); }); dom.paramTree.querySelectorAll('.tree-node > .children').forEach(function (e) { e.classList.add('open'); }); dom.paramTree.querySelectorAll('.tree-node > .node-label .arrow').forEach(function (e) { e.classList.add('open'); }); var f = dom.paramTree.querySelector('.tree-node > .node-label'); if (f) f.click(); }
function buildTreeData(obj, path) { var r = []; for (var k in obj) { if (!obj.hasOwnProperty(k)) continue; var v = obj[k], leaf = isParamLeaf(v); r.push({ key: k, path: path.concat([k]), isLeaf: leaf, children: leaf ? [] : buildTreeData(v, path.concat([k])), data: leaf ? v : null }); } return r; }
function isParamLeaf(v) { if (!v || typeof v !== 'object') return false; if (v.Type !== undefined) return true; if (v.Value !== undefined && v.Type === undefined) return !Object.keys(v).some(function (k) { return v[k] && typeof v[k] === 'object' && !Array.isArray(v[k]); }); return false; }
function renderTreeNode(node, parent, depth) {
    var d = document.createElement('div'); d.className = 'tree-node';
    var lbl = document.createElement('div'); lbl.className = 'node-label'; lbl.style.paddingLeft = (12 + depth * 16) + 'px'; lbl.dataset.path = JSON.stringify(node.path);
    var arrow = document.createElement('span'); arrow.className = 'arrow'; if (!node.isLeaf) arrow.textContent = '▶'; else arrow.style.visibility = 'hidden'; lbl.appendChild(arrow);
    var icon = document.createElement('span'); icon.className = 'icon'; icon.textContent = node.isLeaf ? '📄' : '📁'; lbl.appendChild(icon);
    var nm = document.createElement('span'); nm.className = 'name'; nm.textContent = node.key; lbl.appendChild(nm);
    var badge = document.createElement('span'); badge.className = 'badge';
    if (!node.isLeaf) badge.textContent = node.children.length; else if (node.data) { var comp = node.data.Competence || node.data.competence || 0; badge.textContent = 'Lv' + comp; badge.style.color = ['#8fa4c8', '#00d4aa', '#4a9eff', '#ff9f4a'][comp] || '#8fa4c8'; }
    lbl.appendChild(badge); d.appendChild(lbl);
    if (!node.isLeaf) { var ch = document.createElement('div'); ch.className = 'children'; node.children.forEach(function (c) { renderTreeNode(c, ch, depth + 1); }); d.appendChild(ch); lbl.onclick = function (e) { e.stopPropagation(); ch.classList.toggle('open'); arrow.classList.toggle('open'); selectParamNode(node.path, lbl); }; }
    else lbl.onclick = function (e) { e.stopPropagation(); selectParamNode(node.path, lbl); };
    parent.appendChild(d);
}
function selectParamNode(path, lbl) { selectedParamPath = path; dom.paramTree.querySelectorAll('.node-label.active').forEach(function (e) { e.classList.remove('active'); }); if (lbl) lbl.classList.add('active'); showParamDetail(path); }
function showParamDetail(path) { if (!path || !path.length) return; var cat = path[0], data = state.paramCache[cat]; if (!data) return; var obj = data; for (var i = 1; i < path.length; i++) { if (obj && obj[path[i]] !== undefined) obj = obj[path[i]]; else { obj = null; break; } } if (!obj || typeof obj !== 'object') return; if (isParamLeaf(obj)) { var pp = path.slice(0, -1), po = data; for (var j = 1; j < pp.length; j++) { if (po && po[pp[j]] !== undefined) po = po[pp[j]]; else { po = null; break; } } if (po) renderParamGroup(pp, po); } else renderParamGroup(path, obj); }
function renderParamGroup(path, obj) { var h = '<div class="param-group-title">📂 ' + escapeHtml(path[path.length - 1] || '根') + '</div><div class="param-group-desc">路径: ' + path.join(' / ') + ' · ' + Object.keys(obj).length + ' 项</div>'; var items = []; for (var k in obj) { if (!obj.hasOwnProperty(k)) continue; var v = obj[k]; if (v && typeof v === 'object' && v.Type !== undefined) items.push({ key: k, data: v, leaf: true }); else if (v && typeof v === 'object' && !Array.isArray(v)) items.push({ key: k, data: v, leaf: false }); } for (var i = 0; i < items.length; i++) { var item = items[i]; if (item.leaf) h += renderParamItem(item.key, item.data, path); else { var sp = path.concat([item.key]); h += '<div style="margin:8px 0 4px;padding:6px 10px;background:var(--bg-primary);border-radius:4px;border-left:2px solid var(--accent-cyan);"><div style="display:flex;align-items:center;gap:6px;cursor:pointer;font-weight:500;color:var(--text-secondary);" onclick="toggleSubGroup(this)"><span class="arrow-sub">▶</span> 📁 ' + escapeHtml(item.key) + '<span style="font-size:11px;color:var(--text-muted);font-weight:400;">' + Object.keys(item.data).length + ' 项</span></div><div class="sub-group" style="padding-left:16px;display:none;margin-top:4px;">'; var subs = []; for (var sk in item.data) { if (!item.data.hasOwnProperty(sk)) continue; var sv = item.data[sk]; if (sv && typeof sv === 'object' && sv.Type !== undefined) subs.push({ key: sk, data: sv }); } subs.forEach(function (s) { h += renderParamItem(s.key, s.data, sp); }); h += '</div></div>'; } } dom.paramDetail.innerHTML = h; }
function renderParamItem(key, data, parentPath) { var comp = data.Competence || data.competence || 0, type = data.Type || data.type || 'string', limit = data.Limit || data.limit || '', value = data.Value !== undefined ? data.Value : (data.value !== undefined ? data.value : ''), canEdit = comp <= state.userLevel, compColor = ['lv0', 'lv1', 'lv2', 'lv3'][comp] || 'lv0', fullPath = parentPath.concat([key]), pathStr = JSON.stringify(fullPath).replace(/'/g, "\\'"); var ctl = ''; switch (type) { case 'int': case 'float': var iv = type === 'int', vv = (value !== undefined && value !== '') ? value : (iv ? 0 : 0.0); ctl = '<input type="number" step="' + (iv ? '1' : '0.1') + '" value="' + vv + '" ' + (canEdit ? '' : 'disabled') + ' data-path="' + pathStr + '" data-type="' + type + '" class="param-input" />' + (Array.isArray(limit) ? '<span class="range-hint">[' + limit[0] + ' ~ ' + limit[1] + ']</span>' : ''); break; case 'bool': var on = value === 'TRUE' || value === true || value === 1; ctl = '<div class="bool-switch" data-path="' + pathStr + '" data-type="bool"><div class="track' + (on ? ' on' : '') + '" onclick="toggleBoolSwitch(this)"><div class="thumb"></div></div><span class="bool-label">' + (on ? 'TRUE' : 'FALSE') + '</span></div>'; break; case 'string': ctl = '<input type="text" value="' + escapeHtml(String(value || '')) + '" ' + (canEdit ? '' : 'disabled') + ' data-path="' + pathStr + '" data-type="string" class="param-input" />'; break; case 'string_enum': case 'int_enum': case 'float_enum': var opts = Array.isArray(limit) ? limit : []; ctl = '<select ' + (canEdit ? '' : 'disabled') + ' data-path="' + pathStr + '" data-type="enum" class="param-input">' + opts.map(function (o) { return '<option value="' + o + '"' + (String(o) === String(value) ? ' selected' : '') + '>' + o + '</option>'; }).join('') + '</select>'; break; case 'directory': case 'path': ctl = '<div style="display:flex;gap:4px;flex:1;max-width:280px;"><input type="text" value="' + escapeHtml(String(value || '')) + '" ' + (canEdit ? '' : 'disabled') + ' data-path="' + pathStr + '" data-type="path" class="param-input" style="flex:1;" /><button class="btn btn-outline btn-sm" style="padding:0 10px;height:30px;" onclick="browsePath(this)" ' + (canEdit ? '' : 'disabled') + '>📂</button></div>'; break; default: ctl = '<span class="text-muted">' + escapeHtml(String(value)) + ' (' + type + ')</span>'; } return '<div class="param-item"><span class="p-name">' + escapeHtml(key) + '</span><div class="p-meta"><span class="type-tag">' + type + '</span><span class="comp-tag ' + compColor + '">Lv' + comp + ' · ' + COMP_LABELS[comp] + '</span>' + (canEdit ? '' : '<span style="color:var(--accent-red);font-size:11px;">🔒</span>') + '</div><div class="p-control">' + ctl + '</div></div>'; }

window.toggleSubGroup = function (el) { var s = el.nextElementSibling; if (s) { var h = s.style.display === 'none'; s.style.display = h ? 'block' : 'none'; var a = el.querySelector('.arrow-sub'); if (a) a.textContent = h ? '▼' : '▶'; } };
window.toggleBoolSwitch = function (track) { var c = track.closest('.bool-switch'); if (!c) return; var on = track.classList.contains('on'); track.classList.toggle('on'); var lbl = c.querySelector('.bool-label'); if (lbl) lbl.textContent = on ? 'FALSE' : 'TRUE'; try { var p = JSON.parse(c.dataset.path || '[]'); setParamValue(p, on ? 'FALSE' : 'TRUE'); sendParamUpdate(p, on ? 'FALSE' : 'TRUE'); } catch (e) {} };
window.browsePath = function (btn) { var inp = btn.parentElement.querySelector('input'); if (!inp) return; var v = prompt('请输入路径:', inp.value || './'); if (v !== null) { inp.value = v; try { var p = JSON.parse(inp.dataset.path || '[]'); setParamValue(p, v); sendParamUpdate(p, v); } catch (e) {} } };
function setParamValue(path, value) { var cat = path[0], obj = state.paramCache[cat]; if (!obj) return; for (var i = 1; i < path.length - 1; i++) { if (obj[path[i]] === undefined) obj[path[i]] = {}; obj = obj[path[i]]; } var lk = path[path.length - 1]; if (obj && typeof obj === 'object') { if (obj[lk] && typeof obj[lk] === 'object' && obj[lk].Value !== undefined) obj[lk].Value = value; else obj[lk] = value; } }
function sendParamUpdate(path, value) { if (!state.connected) return; var cat = path[0], cmd = PARAM_CATEGORY_CMD[cat]; if (cmd) sendCommand(cmd, { action: 'write', path: path, value: value }); }

document.addEventListener('input', function (e) { var el = e.target; if (!el.classList.contains('param-input') || el.disabled) return; try { var p = JSON.parse(el.dataset.path || '[]'); if (!p.length) return; var v = el.value, t = el.dataset.type || 'string'; if (t === 'int') v = parseInt(v, 10) || 0; else if (t === 'float') v = parseFloat(v) || 0; setParamValue(p, v); } catch (ex) {} });
document.addEventListener('change', function (e) { var el = e.target; if (el.tagName !== 'SELECT' || !el.classList.contains('param-input') || el.disabled) return; try { var p = JSON.parse(el.dataset.path || '[]'); if (!p.length) return; setParamValue(p, el.value); sendParamUpdate(p, el.value); } catch (ex) {} });
document.addEventListener('focusout', function (e) { var el = e.target; if (!el.classList.contains('param-input') || el.disabled) return; try { var p = JSON.parse(el.dataset.path || '[]'); if (!p.length) return; var v = el.value, t = el.dataset.type || 'string'; if (t === 'int') v = parseInt(v, 10) || 0; else if (t === 'float') v = parseFloat(v) || 0; sendParamUpdate(p, v); } catch (ex) {} });

// ==================== 顶部栏 ====================
dom.paramTabs.querySelectorAll('.param-tab[data-category]').forEach(function (t) {
    t.onclick = function () {
        var c = t.dataset.category; if (!c) return;
        dom.paramTabs.querySelectorAll('.param-tab').forEach(function (x) { x.classList.remove('active'); });
        t.classList.add('active'); openParamModal(c);
    };
});
$('btnCloseParams').onclick = closeParamModal;
dom.paramModal.onclick = function (e) { if (e.target === dom.paramModal) closeParamModal(); };
$('btnRefreshParams').onclick = function () { if (state.currentCategory) { requestParams(state.currentCategory); log('刷新参数 (' + state.currentCategory + ')', 'tx'); } };
$('btnSaveParams').onclick = function () { if (state.currentCategory) { var cmd = PARAM_CATEGORY_CMD[state.currentCategory]; if (cmd) { sendCommand(cmd, { action: 'save' }); var o = this.textContent; this.textContent = '✅ 已保存'; setTimeout(function () { $('btnSaveParams').textContent = o; }, 1500); } } };

// ==================== 离线检测 ====================
function sendOfflinePath(filePath) {
    if (!state.connected) { log('请先连接后端', 'err'); return; }
    log('发送离线检测路径: ' + filePath, 'tx');
    sendCommand(50, { path: filePath });
}

function updateBatchInfo() {
    var el = $('batchInfo');
    var btn = $('btnBatchExit');
    if (!el) return;
    if (state.batchFiles.length === 0) {
        el.style.display = 'none';
        if (btn) btn.style.display = 'none';
        return;
    }
    el.style.display = '';
    el.textContent = (state.batchIndex + 1) + ' / ' + state.batchFiles.length;
    if (btn) btn.style.display = '';
}

function exitBatchMode() {
    state.batchFiles = [];
    state.batchIndex = -1;
    updateBatchInfo();
    log('已退出批量检测模式', 'info');
}

function sendCurrentBatchImage() {
    if (state.batchFiles.length === 0 || state.batchIndex < 0) return;
    var path = state.batchFiles[state.batchIndex];
    sendOfflinePath(path);
    updateBatchInfo();
}

// 离线检测（单张）—— 使用原生文件选择
$('btnOffline').onclick = function () {
    var input = $('fileInput');
    if (!input) return;
    input.value = '';
    input.onchange = function () {
        var file = input.files[0];
        if (!file) return;
        var filePath = file.path || file.name;
        if (!isImageFile(filePath)) { log('仅支持 bmp/png/tif/tiff/jpg/jpeg 格式', 'err'); return; }
        state.batchFiles = [];
        state.batchIndex = -1;
        updateBatchInfo();
        sendOfflinePath(filePath);
    };
    input.click();
};

// 批量离线检测（文件夹）—— 使用 webkitdirectory
$('btnBatch').onclick = function () {
    var input = $('folderInput');
    if (!input) return;
    input.value = '';
    input.onchange = function () {
        var files = Array.from(input.files).filter(function (f) { return isImageFile(f.path || f.name); });
        if (files.length === 0) {
            log('文件夹中没有支持的图片文件 (bmp/png/tif/tiff/jpg/jpeg)', 'err');
            return;
        }
        var paths = files.map(function (f) { return f.path || f.name; });
        paths.sort(function (a, b) { return a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' }); });
        state.batchFiles = paths;
        state.batchIndex = 0;
        updateBatchInfo();
        log('加载 ' + paths.length + ' 张图片，← → 键切换', 'info');
        sendCurrentBatchImage();
    };
    input.click();
};

// 退出批量模式
$('btnBatchExit').onclick = function () { exitBatchMode(); };

// 图片文件名后缀校验
function isImageFile(filePath) {
    var ext = filePath.split('.').pop().toLowerCase();
    return ['bmp', 'png', 'tif', 'tiff', 'jpg', 'jpeg'].indexOf(ext) !== -1;
}

// 按键导航（批量模式）
document.addEventListener('keydown', function (e) {
    // Escape 关闭参数弹窗
    if (e.key === 'Escape' && dom.paramModal.classList.contains('active')) {
        closeParamModal(); return;
    }
    // 批量模式 ← → 导航
    if (state.batchFiles.length > 0) {
        if (e.key === 'ArrowRight') {
            e.preventDefault();
            if (state.batchIndex < state.batchFiles.length - 1) {
                state.batchIndex++;
                sendCurrentBatchImage();
            }
        } else if (e.key === 'ArrowLeft') {
            e.preventDefault();
            if (state.batchIndex > 0) {
                state.batchIndex--;
                sendCurrentBatchImage();
            }
        }
    }
});

// ==================== 可拖动分隔条 ====================
function initSplitters() {
    function dragSplitter(splitter, dir) {
        var prev = splitter.previousElementSibling;
        var next = splitter.nextElementSibling;
        if (!prev || !next) return;

        var isH = dir === 'h';
        var prop = isH ? 'width' : 'height';
        var clientProp = isH ? 'clientX' : 'clientY';

        splitter.addEventListener('mousedown', function (e) {
            e.preventDefault();
            if (e.button !== 0) return;

            // 禁用文本选择和交互
            document.body.style.userSelect = 'none';
            document.body.style.cursor = isH ? 'col-resize' : 'row-resize';
            splitter.classList.add('active');

            // 记录起始状态
            var startCoord = e[clientProp];
            var prevSize = prev.getBoundingClientRect()[prop];
            var nextSize = next.getBoundingClientRect()[prop];
            var totalSize = prevSize + nextSize;

            function onMove(ev) {
                var delta = ev[clientProp] - startCoord;
                var newPrev = Math.max(60, Math.min(totalSize - 60, prevSize + delta));
                var newNext = totalSize - newPrev;
                prev.style[prop] = newPrev + 'px';
                prev.style.flex = 'none';
                next.style[prop] = newNext + 'px';
                next.style.flex = 'none';
                if (isH) renderCurrentWindow();
            }

            function onUp() {
                document.body.style.userSelect = '';
                document.body.style.cursor = '';
                splitter.classList.remove('active');
                document.removeEventListener('mousemove', onMove);
                document.removeEventListener('mouseup', onUp);
            }

            document.addEventListener('mousemove', onMove);
            document.addEventListener('mouseup', onUp);
        });
    }

    var s1 = $('splitter1'), s2 = $('splitter2'), s3 = $('splitter3'), s4 = $('splitter4');
    if (s1) dragSplitter(s1, 'h');
    if (s2) dragSplitter(s2, 'v');
    if (s3) dragSplitter(s3, 'h');
    if (s4) dragSplitter(s4, 'v');
}

// ==================== FPS ====================
setInterval(function () { var n = performance.now(), e = (n - state.fpsLastTime) / 1000, f = e > 0 ? Math.round(state.fpsFrames / e) : 0; dom.fps.textContent = 'FPS: ' + f; state.fpsFrames = 0; state.fpsLastTime = n; }, 1000);

// ==================== 启动 ====================
function init() {
    log('Halcon_UI Electron 客户端就绪', 'info');
    if (window.electronAPI) log('Electron ' + (window.electronAPI.versions && window.electronAPI.versions.electron || '?'), 'info');
    var rect = dom.imageContainer.getBoundingClientRect(); dom.imageCanvas.width = rect.width; dom.imageCanvas.height = rect.height;
    dom.imageContainer.style.cursor = 'grab';
    var defaultTab = document.querySelector('[data-category="算法参数"]'); if (defaultTab) defaultTab.classList.add('active');
    initSplitters();
    log('自动连接 ' + getWsUrl() + ' ...', 'info');
    doConnect();
}
init();
