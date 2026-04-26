// ==================== SiRod Inspector 前端应用 ====================
// 基于 Halcon_UI WebSocket API，纯 JS 无依赖
// 协议帧（承载在 WebSocket 二进制消息中）：
//   [4B magic 0xDEADBEEF][4B json_len][4B data_len][json][binary]   全部大端

(function () {
  'use strict';

  // ==================== WebSocket 连接 ====================
  var WS_MAGIC = 0xDEADBEEF;
  var ws = null;
  var streaming = false;
  var reconnectTimer = null;

  // 多窗口: windows[id] = { canvas, ctx, fc, lt, tb }
  // window 0 = 主预览区, 其他在 extraWindows 容器
  var windows = {};

  // 检测数据（模拟本地存储，实际由后端推送）
  var inspections = [];
  var gallery = [];
  var histPage = 1;
  var histPerPage = 20;

  // 统计
  var stats = {
    total: 0, ok: 0, ng: 0,
    totalYesterday: 0,
    avgTime: 0, fps: 0,
    rodId: '--', rodCount: 0,
    hourly: new Array(24).fill(0),
    defectTypes: {},
    dailyOK: [],
    durations: []
  };

  // ==================== 初始化 ====================

  document.addEventListener('DOMContentLoaded', function () {
    initTabs();
    initClock();
    initPreviewInteraction();
    initZoomInteraction();
    renderHistory();
    drawAllCharts();

    // 自动建立 WebSocket 连接（连接成功后会自动查询初始状态）
    connectWs();

    // 每 3 秒轮询设备状态和系统资源（WS 未连接时静默跳过）
    setInterval(queryBackendStatus, 3000);
  });

  // 向后端查询状态
  function queryBackendStatus() {
    window.sendCmd(200, {});  // 查询设备连接状态 → 后端回复 CMD=200 或 CMD=3
    window.sendCmd(201, {});  // 查询统计数据     → 后端回复 CMD=201
  }

  // ==================== Tab 切换 ====================

  function initTabs() {
    var btns = document.querySelectorAll('.tab-btn');
    btns.forEach(function (btn) {
      btn.addEventListener('click', function () {
        btns.forEach(function (b) { b.classList.remove('active'); });
        btn.classList.add('active');

        var pages = document.querySelectorAll('.page');
        pages.forEach(function (p) { p.classList.remove('active'); });
        var target = document.getElementById('page-' + btn.dataset.tab);
        if (target) target.classList.add('active');

        // 切换到统计页时重绘图表
        if (btn.dataset.tab === 'stats') drawAllCharts();
      });
    });
  }

  // ==================== 实时时钟 ====================

  function initClock() {
    function tick() {
      var now = new Date();
      var h = String(now.getHours()).padStart(2, '0');
      var m = String(now.getMinutes()).padStart(2, '0');
      var s = String(now.getSeconds()).padStart(2, '0');
      document.getElementById('clock').textContent = h + ':' + m + ':' + s;
    }
    tick();
    setInterval(tick, 1000);
  }

  // ==================== WebSocket 连接管理 ====================

  window.toggleStream = function () {
    if (streaming || (ws && ws.readyState === WebSocket.OPEN)) {
      disconnectWs();
      return;
    }
    connectWs();
  };

  function wsUrl() {
    var proto = (location.protocol === 'https:') ? 'wss://' : 'ws://';
    var host = location.host || ('localhost:' + (location.port || '3000'));
    return proto + host + '/ws';
  }

  function connectWs() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;

    var btn = document.getElementById('btnStartStream');
    if (btn) btn.textContent = '连接中...';

    try {
      ws = new WebSocket(wsUrl());
    } catch (e) {
      console.error('WebSocket create failed:', e);
      onWsClosed();
      scheduleReconnect();
      return;
    }
    ws.binaryType = 'arraybuffer';

    ws.onopen = function () {
      streaming = true;
      console.log('WS connected:', wsUrl());

      if (btn) btn.textContent = '断开连接';
      var badge = document.getElementById('statusBadge');
      badge.textContent = '运行中';
      badge.className = 'status-badge running';
      document.getElementById('previewPlaceholder').style.display = 'none';

      // 连接成功后查询初始状态
      queryBackendStatus();
    };

    ws.onmessage = function (ev) {
      if (typeof ev.data === 'string') {
        // 兼容文本消息（不在协议里，但保留以便调试）
        try { handleCommandFrame(JSON.parse(ev.data)); } catch (e) {}
        return;
      }
      var buf = new Uint8Array(ev.data);
      handleFrame(buf);
    };

    ws.onerror = function (e) {
      console.error('WS error', e);
    };

    ws.onclose = function (ev) {
      console.log('WS closed:', ev.code, ev.reason);
      onWsClosed();
      // 用户主动断开（streaming 已被 disconnectWs 设为 false）则不重连
      if (streaming === false && ev.wasClean) return;
      scheduleReconnect();
    };
  }

  function disconnectWs() {
    streaming = false;
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (ws) {
      try { ws.close(1000, 'client closed'); } catch (e) {}
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(function () {
      reconnectTimer = null;
      connectWs();
    }, 2000);
  }

  function onWsClosed() {
    streaming = false;
    var btn = document.getElementById('btnStartStream');
    if (btn) btn.textContent = '连接图像流';
    var ph = document.getElementById('previewPlaceholder');
    if (ph) ph.style.display = '';

    var badge = document.getElementById('statusBadge');
    badge.textContent = '已断开';
    badge.className = 'status-badge stopped';
  }

  // ==================== 二进制工具 ====================

  function u32be(buf, offset) {
    return ((buf[offset] << 24) >>> 0) +
           (buf[offset + 1] << 16) +
           (buf[offset + 2] << 8) +
           buf[offset + 3];
  }

  function writeU32be(view, offset, value) {
    view.setUint32(offset, value >>> 0, false); // false = big-endian
  }

  // ==================== 帧处理 ====================

  function handleFrame(buf) {
    if (buf.length < 12) {
      console.warn('Frame too short:', buf.length);
      return;
    }
    var magic = u32be(buf, 0);
    if (magic !== WS_MAGIC) {
      console.warn('Bad magic:', magic.toString(16));
      return;
    }
    var jl = u32be(buf, 4);
    var dl = u32be(buf, 8);
    if (12 + jl + dl !== buf.length) {
      console.warn('Frame size mismatch: hdr=', jl, dl, 'total=', buf.length);
      return;
    }

    var jsonStr = new TextDecoder().decode(buf.subarray(12, 12 + jl));
    var msg;
    try { msg = JSON.parse(jsonStr); } catch (e) { console.warn('JSON parse failed:', e); return; }

    if (msg.CMD === 0 && dl > 0) {
      handleImageFrame(msg.Data, buf.subarray(12 + jl, 12 + jl + dl), dl);
    } else if (msg.CMD !== 0) {
      handleCommandFrame(msg);
    }
  }

  function handleImageFrame(data, px, dl) {
    var winId = (data.id !== undefined) ? data.id : 0;
    var win = getOrCreateWindow(winId);

    win.tb += dl;

    if (data.fmt === 'jpeg') {
      var blob = new Blob([px], { type: 'image/jpeg' });
      createImageBitmap(blob).then(function (bmp) {
        win.canvas.width = bmp.width;
        win.canvas.height = bmp.height;
        win.ctx.drawImage(bmp, 0, 0);
        bmp.close();
        updateWindowStats(win, data, dl);
      });
    } else {
      // RAW
      var w = data['\u5bbd'], h = data['\u9ad8'], ch = data['\u901a\u9053'];
      win.canvas.width = w;
      win.canvas.height = h;
      var im = win.ctx.createImageData(w, h);
      var o = im.data;
      if (ch === 1) {
        for (var i = 0; i < w * h; i++) {
          o[i * 4] = o[i * 4 + 1] = o[i * 4 + 2] = px[i]; o[i * 4 + 3] = 255;
        }
      } else if (ch === 3) {
        var ps = w * h;
        for (var i = 0; i < ps; i++) {
          o[i * 4] = px[i];
          o[i * 4 + 1] = px[i + ps];
          o[i * 4 + 2] = px[i + ps * 2];
          o[i * 4 + 3] = 255;
        }
      }
      win.ctx.putImageData(im, 0, 0);
      updateWindowStats(win, data, dl);
    }
  }

  function updateWindowStats(win, data, dl) {
    win.fc++;
    var now = performance.now();
    if (now - win.lt >= 1000) {
      var kbs = Math.round(win.tb / 1024);
      win.tb = 0;
      var fmt = data.fmt === 'jpeg' ? 'JPEG' : 'RAW';
      var w = win.canvas.width, h = win.canvas.height;
      var infoStr = w + 'x' + h + ' ' + fmt + ' ' + Math.round(dl / 1024) + 'KB  ' + win.fc + 'fps  ' + kbs + 'KB/s';

      if (win.infoEl) win.infoEl.textContent = infoStr;

      // 更新主预览区 footer
      if (win.isMain) {
        document.getElementById('previewRes').textContent = w + 'x' + h;
        document.getElementById('previewFmt').textContent = fmt;
        document.getElementById('previewFps').textContent = win.fc + ' FPS';
        document.getElementById('previewBw').textContent = kbs + ' KB/s';
        document.getElementById('bbRes').textContent = w + 'x' + h;
      }

      win.fc = 0;
      win.lt = now;
    }
  }

  function handleCommandFrame(msg) {
    var d = msg.Data || {};
    console.log('CMD=' + msg.CMD, d);

    switch (msg.CMD) {
      // ---- 后端 → 前端：通用消息 / 通知 ----
      case 1: // 检测结果
        processInspectionResult(d);
        break;

      case 2: // 晶棒编号更新
        updateRodInfo(d);
        break;

      case 3: // 设备连接状态推送
        updateDeviceStatus(d);
        break;

      case 4: // 系统资源推送 (CPU / MEM)
        updateSystemResources(d);
        break;

      case 5: // 飞书同步状态
        updateFeishuStatus(d);
        break;

      // ---- 后端 → 前端：状态查询回复 ----
      case 200: // 设备连接状态回复
        updateDeviceStatus(d);
        break;

      case 201: // 统计数据回复
        updateStatsFromBackend(d);
        break;

      case 202: // 历史记录回复
        updateHistoryFromBackend(d);
        break;

      case 210: // 设置读取回复（后端发送当前保存的设置）
        loadSettingsFromBackend(d);
        break;

      default:
        break;
    }
  }

  // ---- 晶棒编号更新 ----
  function updateRodInfo(d) {
    var rodId = d.rodId || d['晶棒编号'] || '--';
    var source = d.source || d['来源'] || '--';
    stats.rodId = rodId;
    stats.rodCount = d.rodCount || d['本棒检测数'] || 0;
    document.getElementById('rodId').textContent = rodId;
    document.getElementById('rodCount').textContent = stats.rodCount;
    document.getElementById('rodLen').textContent = rodId.length;
    document.getElementById('rodSource').textContent = source;

    var badge = document.getElementById('rodScanBadge');
    if (rodId !== '--') {
      badge.textContent = '已扫码';
      badge.className = 'rod-badge scanned';
    } else {
      badge.textContent = '待扫码';
      badge.className = 'rod-badge';
    }
  }

  // ---- 设备连接状态 ----
  function updateDeviceStatus(d) {
    // d 格式: { cam: true/false, plc: true/false, db: true/false, feishu: "syncing"/"ok"/"error", scanner: true/false }
    var dots = document.querySelectorAll('#bottombar .conn-dot');
    var labels = document.querySelectorAll('#bottombar .conn-label');
    if (dots.length < 5) return;

    // NIR CAM
    if (d.cam !== undefined) {
      dots[0].className = 'conn-dot ' + (d.cam ? 'green' : 'red');
      labels[0].textContent = 'NIR CAM-01 ' + (d.cam ? '已连接' : '已断开');
    }
    // PLC
    if (d.plc !== undefined) {
      dots[1].className = 'conn-dot ' + (d.plc ? 'green' : 'red');
      labels[1].textContent = 'PLC ' + (d.plc ? '已连接' : '已断开');
    }
    // DB
    if (d.db !== undefined) {
      dots[2].className = 'conn-dot ' + (d.db ? 'green' : 'red');
      labels[2].textContent = '本地 DB ' + (d.db ? '正常' : '异常');
    }
    // 飞书
    if (d.feishu !== undefined) {
      var fs = d.feishu;
      dots[3].className = 'conn-dot ' + (fs === 'error' ? 'red' : (fs === 'syncing' ? 'blue' : 'green'));
      labels[3].textContent = '飞书同步' + (fs === 'error' ? '异常' : (fs === 'syncing' ? '进行中' : '正常'));
    }
    // 扫码器
    if (d.scanner !== undefined) {
      dots[4].className = 'conn-dot ' + (d.scanner ? 'green' : 'red');
      labels[4].textContent = '二维码扫描器 ' + (d.scannerPort || 'COM3') + (d.scanner ? ' 已连接' : ' 已断开');
    }
  }

  // ---- 系统资源 ----
  function updateSystemResources(d) {
    if (d.cpu !== undefined) document.getElementById('bbCpu').textContent = 'CPU ' + d.cpu + '%';
    if (d.mem !== undefined) document.getElementById('bbMem').textContent = 'MEM ' + d.mem + '%';
    if (d.resolution) document.getElementById('bbRes').textContent = d.resolution;
  }

  // ---- 飞书同步状态 ----
  function updateFeishuStatus(d) {
    var dots = document.querySelectorAll('#bottombar .conn-dot');
    var labels = document.querySelectorAll('#bottombar .conn-label');
    if (dots.length < 5) return;
    var status = d.status || 'ok';
    dots[3].className = 'conn-dot ' + (status === 'error' ? 'red' : (status === 'syncing' ? 'blue' : 'green'));
    labels[3].textContent = '飞书同步' + (status === 'error' ? '异常: ' + (d.message || '') : (status === 'syncing' ? '进行中' : '正常'));
  }

  // ---- 后端回传统计数据 ----
  function updateStatsFromBackend(d) {
    if (d.total !== undefined) stats.total = d.total;
    if (d.ok !== undefined) stats.ok = d.ok;
    if (d.ng !== undefined) stats.ng = d.ng;
    if (d.totalYesterday !== undefined) stats.totalYesterday = d.totalYesterday;
    if (d.avgTime !== undefined) stats.avgTime = d.avgTime;
    if (d.hourly) stats.hourly = d.hourly;
    if (d.defectTypes) stats.defectTypes = d.defectTypes;
    if (d.dailyOK) stats.dailyOK = d.dailyOK;
    if (d.durations) stats.durations = d.durations;

    updateOverviewUI();
    // 统计报表页汇总
    document.getElementById('sTotalCount').textContent = stats.total;
    document.getElementById('sOKRate').textContent = stats.total > 0
      ? (stats.ok / stats.total * 100).toFixed(1) + '%' : '--';
    document.getElementById('sNGCount').textContent = stats.ng;
    drawAllCharts();
  }

  // ---- 后端回传历史记录 ----
  function updateHistoryFromBackend(d) {
    if (d.records && Array.isArray(d.records)) {
      // 后端分页返回的数据直接渲染
      inspections = d.records.map(function (r, i) {
        return {
          id: r.id || i + 1,
          rodId: r.rodId || r['晶棒编号'] || '--',
          time: r.time || r['检测时间'] || '--',
          result: r.result || r['结果'] || 'OK',
          defectType: r.defectType || r['缺陷类型'] || '--',
          defectCount: r.defectCount || r['缺陷数'] || 0,
          duration: r.duration || r['耗时'] || 0,
          confidence: r.confidence || r['置信度'] || 0,
          imageData: r.imageData || null
        };
      });
    }
    if (d.totalRecords !== undefined) {
      document.getElementById('histPageInfo').textContent = '共 ' + d.totalRecords + ' 条记录';
    }
    renderHistory();
  }

  // ---- 后端回传设置 ----
  function loadSettingsFromBackend(d) {
    if (d.tcp) { setVal('setTcpIp', d.tcp.ip); setVal('setTcpPort', d.tcp.port); }
    if (d.db) {
      setVal('setDbIp', d.db.ip); setVal('setDbPort', d.db.port);
      setVal('setDbUser', d.db.user); setVal('setDbPass', d.db.pass);
      setVal('setDbName', d.db.name); setVal('setDbTable', d.db.table);
    }
    if (d.shift) { setVal('setShiftDay', d.shift.day); setVal('setShiftNight', d.shift.night); }
    if (d.feishu) {
      document.getElementById('setFeishuEnable').checked = !!d.feishu.enable;
      setVal('setFeishuAppId', d.feishu.appId); setVal('setFeishuAppSecret', d.feishu.appSecret);
      setVal('setFeishuAppToken', d.feishu.appToken); setVal('setFeishuTableId', d.feishu.tableId);
      setVal('setFeishuApi', d.feishu.api);
    }
    if (d.image) { document.getElementById('setImgEnable').checked = !!d.image.enable; setVal('setImgDir', d.image.dir); }
    if (d.pipeline) {
      setVal('setPipeline', d.pipeline);
      document.querySelector('.pipeline-id').textContent = d.pipeline;
    }
    if (d.defectTypes && Array.isArray(d.defectTypes)) {
      var list = document.getElementById('defectTypeList');
      list.innerHTML = '';
      d.defectTypes.forEach(function (name) {
        var item = document.createElement('div');
        item.className = 'defect-type-item';
        item.innerHTML = '<span>' + escapeHtml(name) + '</span><button class="btn btn-sm btn-danger-outline" onclick="removeDefectType(this)">删除</button>';
        list.appendChild(item);
      });
      updateDefectFilters();
    }
  }

  // ==================== 多窗口管理 ====================

  function getOrCreateWindow(id) {
    if (windows[id]) return windows[id];

    var win;
    if (id === 0) {
      // 窗口 0 = 主预览区
      var canvas = document.getElementById('previewCanvas');
      win = {
        canvas: canvas,
        ctx: canvas.getContext('2d'),
        infoEl: null,
        fc: 0, lt: performance.now(), tb: 0,
        isMain: true
      };
    } else {
      // 额外窗口
      var container = document.getElementById('extraWindows');
      var card = document.createElement('div');
      card.className = 'extra-win';

      var title = document.createElement('div');
      title.className = 'extra-win-title';
      title.textContent = 'Window ' + id;

      var canvas = document.createElement('canvas');
      canvas.width = 640;
      canvas.height = 480;

      var info = document.createElement('div');
      info.className = 'extra-win-info';
      info.textContent = 'waiting...';

      card.appendChild(title);
      card.appendChild(canvas);
      card.appendChild(info);
      container.appendChild(card);

      win = {
        canvas: canvas,
        ctx: canvas.getContext('2d'),
        infoEl: info,
        fc: 0, lt: performance.now(), tb: 0,
        isMain: false
      };
    }

    windows[id] = win;
    return win;
  }

  // ==================== 预览交互（缩放 + 拖拽） ====================

  function initPreviewInteraction() {
    var container = document.getElementById('previewContainer');
    var canvas = document.getElementById('previewCanvas');
    var scale = 1, panX = 0, panY = 0, dragging = false, startX, startY;

    container.addEventListener('wheel', function (e) {
      e.preventDefault();
      var delta = e.deltaY > 0 ? 0.9 : 1.1;
      scale = Math.max(0.05, Math.min(50, scale * delta));
      applyTransform();
    }, { passive: false });

    container.addEventListener('mousedown', function (e) {
      if (e.button !== 0) return;
      dragging = true;
      startX = e.clientX - panX;
      startY = e.clientY - panY;
    });

    window.addEventListener('mousemove', function (e) {
      if (!dragging) return;
      panX = e.clientX - startX;
      panY = e.clientY - startY;
      applyTransform();
    });

    window.addEventListener('mouseup', function () { dragging = false; });

    container.addEventListener('dblclick', function () {
      scale = 1; panX = 0; panY = 0;
      applyTransform();
    });

    function applyTransform() {
      canvas.style.transform = 'translate(' + panX + 'px,' + panY + 'px) scale(' + scale + ')';
      canvas.style.maxWidth = 'none';
      canvas.style.maxHeight = 'none';
    }
  }

  // ==================== 发送命令 ====================

  /**
   * 向后端发送命令（通用）
   * @param {number} cmd  CMD 编号
   * @param {object} data Data 负载
   * @returns {Promise<boolean>} 是否发送成功
   *
   * CMD 约定（前端 → 后端）：
   *   100  更新 TCP 通信设置      Data: { ip, port }
   *   101  更新 MySQL 数据库设置   Data: { ip, port, user, pass, name, table }
   *   102  更新班次清零设置        Data: { dayShift, nightShift }
   *   103  更新缺陷类型列表        Data: { types: [...] }
   *   104  更新飞书同步设置        Data: { enable, appId, appSecret, appToken, tableId, api }
   *   105  更新图像存储设置        Data: { enable, dir }
   *   106  更新产线设置            Data: { pipeline }
   *   200  查询设备连接状态        Data: {}
   *   201  查询统计数据            Data: {}
   *   202  查询历史记录            Data: { page, perPage, search, result, defect, dateFrom, dateTo }
   *   999  退出 / 关闭服务器       Data: {}
   */
  window.sendCmd = function (cmd, data) {
    data = data || {};
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      console.warn('sendCmd skipped (ws not open): CMD=' + cmd);
      return Promise.resolve(false);
    }
    var jsonStr = JSON.stringify({ CMD: cmd, Data: data });
    var jsonBytes = new TextEncoder().encode(jsonStr);
    var total = 12 + jsonBytes.length; // 无二进制体
    var frame = new ArrayBuffer(total);
    var view = new DataView(frame);
    view.setUint32(0, WS_MAGIC, false);          // magic, BE
    view.setUint32(4, jsonBytes.length, false);  // json_len, BE
    view.setUint32(8, 0, false);                 // data_len, BE
    new Uint8Array(frame, 12, jsonBytes.length).set(jsonBytes);
    try {
      ws.send(frame);
      return Promise.resolve(true);
    } catch (e) {
      console.error('Send CMD=' + cmd + ' error:', e);
      return Promise.resolve(false);
    }
  };

  // ==================== 检测结果处理 ====================

  function processInspectionResult(data) {
    var record = {
      id: inspections.length + 1,
      rodId: data.rodId || data['\u6676\u68d2\u7f16\u53f7'] || '--',
      time: new Date().toLocaleString(),
      result: data.result || data['\u7ed3\u679c'] || 'OK',
      defectType: data.defectType || data['\u7f3a\u9677\u7c7b\u578b'] || '--',
      defectCount: data.defectCount || data['\u7f3a\u9677\u6570'] || 0,
      duration: data.duration || data['\u8017\u65f6'] || 0,
      confidence: data.confidence || data['\u7f6e\u4fe1\u5ea6'] || 0,
      imageData: data.imageData || null
    };

    inspections.unshift(record);

    // 更新统计
    stats.total++;
    if (record.result === 'OK') stats.ok++;
    else stats.ng++;
    stats.rodId = record.rodId;
    stats.rodCount++;
    stats.durations.push(record.duration);
    stats.avgTime = Math.round(stats.durations.reduce(function (a, b) { return a + b; }, 0) / stats.durations.length);

    var hour = new Date().getHours();
    stats.hourly[hour]++;

    if (record.defectType !== '--') {
      stats.defectTypes[record.defectType] = (stats.defectTypes[record.defectType] || 0) + 1;
    }

    // 缺陷图片加入图库
    if (record.result === 'NG' && record.imageData) {
      gallery.unshift(record);
    }

    updateOverviewUI();
    renderHistory();
    renderGallery();
  }

  // ==================== 总览页 UI 更新 ====================

  function updateOverviewUI() {
    document.getElementById('statTotal').textContent = stats.total;
    document.getElementById('statOK').textContent = stats.ok;
    document.getElementById('statNG').textContent = stats.ng;
    document.getElementById('statAvgTime').textContent = stats.avgTime + 'ms';

    var okRate = stats.total > 0 ? (stats.ok / stats.total * 100).toFixed(1) + '%' : '--';
    var ngRate = stats.total > 0 ? (stats.ng / stats.total * 100).toFixed(1) + '%' : '--';
    document.getElementById('statOKRate').textContent = '合格率 ' + okRate;
    document.getElementById('statNGRate').textContent = '占比 ' + ngRate;

    // 较昨日涨幅
    if (stats.totalYesterday > 0) {
      var diff = stats.total - stats.totalYesterday;
      var pct = (diff / stats.totalYesterday * 100).toFixed(1);
      document.getElementById('statTotalSub').textContent = '较昨日 ' + (diff >= 0 ? '+' : '') + pct + '%';
    }

    // FPS（从主窗口获取）
    if (windows[0] && windows[0].fc !== undefined) {
      document.getElementById('statFPS').textContent = document.getElementById('previewFps').textContent;
    }

    document.getElementById('rodId').textContent = stats.rodId;
    document.getElementById('rodCount').textContent = stats.rodCount;
    document.getElementById('rodLen').textContent = stats.rodId !== '--' ? stats.rodId.length : 0;

    // 预览信息栏
    if (inspections.length > 0) {
      var latest = inspections[0];
      document.getElementById('prevRod').textContent = latest.rodId.substring(0, 12);
      var resEl = document.getElementById('prevResult');
      resEl.textContent = latest.result;
      resEl.className = latest.result === 'OK' ? 'result-ok' : 'result-ng';
      document.getElementById('prevDefect').textContent = latest.defectType;
      document.getElementById('prevTime').textContent = latest.duration + 'ms';
    }

    // 同步更新统计报表页的汇总卡片
    document.getElementById('sTotalCount').textContent = stats.total;
    document.getElementById('sOKRate').textContent = okRate;
    document.getElementById('sNGCount').textContent = stats.ng;
  }

  // ==================== 历史记录 ====================

  function renderHistory() {
    var tbody = document.getElementById('histBody');
    var search = document.getElementById('histSearch').value.toLowerCase();
    var resultFilter = document.getElementById('histResultFilter').value;
    var defectFilter = document.getElementById('histDefectFilter').value;
    var dateFrom = document.getElementById('histDateFrom').value;
    var dateTo = document.getElementById('histDateTo').value;

    var filtered = inspections.filter(function (r) {
      if (search && r.rodId.toLowerCase().indexOf(search) === -1) return false;
      if (resultFilter && r.result !== resultFilter) return false;
      if (defectFilter && r.defectType !== defectFilter) return false;
      return true;
    });

    var totalPages = Math.max(1, Math.ceil(filtered.length / histPerPage));
    if (histPage > totalPages) histPage = totalPages;

    var start = (histPage - 1) * histPerPage;
    var pageData = filtered.slice(start, start + histPerPage);

    var html = '';
    pageData.forEach(function (r, i) {
      var resCls = r.result === 'OK' ? 'ok' : 'ng';
      html += '<tr>'
        + '<td>' + (start + i + 1) + '</td>'
        + '<td class="mono">' + escapeHtml(r.rodId) + '</td>'
        + '<td>' + r.time + '</td>'
        + '<td class="' + resCls + '">' + r.result + '</td>'
        + '<td>' + r.defectType + '</td>'
        + '<td>' + r.defectCount + '</td>'
        + '<td>' + r.duration + 'ms</td>'
        + '<td>' + (r.confidence * 100).toFixed(1) + '%</td>'
        + '</tr>';
    });

    tbody.innerHTML = html;
    document.getElementById('histPageInfo').textContent = '\u5171 ' + filtered.length + ' \u6761\u8bb0\u5f55';
    document.getElementById('histPageNum').textContent = histPage + ' / ' + totalPages;
  }

  window.histPrevPage = function () { if (histPage > 1) { histPage--; renderHistory(); } };
  window.histNextPage = function () { histPage++; renderHistory(); };

  // 筛选事件
  document.addEventListener('DOMContentLoaded', function () {
    ['histSearch', 'histResultFilter', 'histDefectFilter', 'histDateFrom', 'histDateTo'].forEach(function (id) {
      var el = document.getElementById(id);
      if (el) el.addEventListener('input', function () { histPage = 1; renderHistory(); });
      if (el) el.addEventListener('change', function () { histPage = 1; renderHistory(); });
    });
  });

  window.exportHistoryExcel = function () {
    // 简易 CSV 导出
    var header = '\u5e8f\u53f7,\u6676\u68d2\u7f16\u53f7,\u68c0\u6d4b\u65f6\u95f4,\u7ed3\u679c,\u7f3a\u9677\u7c7b\u578b,\u7f3a\u9677\u6570,\u68c0\u6d4b\u65f6\u957f,\u7f6e\u4fe1\u5ea6\n';
    var csv = header + inspections.map(function (r, i) {
      return [i + 1, r.rodId, r.time, r.result, r.defectType, r.defectCount, r.duration + 'ms', (r.confidence * 100).toFixed(1) + '%'].join(',');
    }).join('\n');
    var blob = new Blob(['\uFEFF' + csv], { type: 'text/csv;charset=utf-8' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'inspection_history_' + new Date().toISOString().slice(0, 10) + '.csv';
    a.click();
  };

  window.generateReport = function () {
    // 通知后端生成报告
    window.sendCmd(203, {
      dateFrom: val('histDateFrom'),
      dateTo: val('histDateTo'),
      resultFilter: document.getElementById('histResultFilter').value,
      defectFilter: document.getElementById('histDefectFilter').value
    });
    alert('报告生成请求已发送到后端');
  };

  // ==================== 缺陷图库 ====================

  function renderGallery() {
    var grid = document.getElementById('galleryGrid');
    var filter = document.getElementById('galFilter').value;

    var filtered = gallery.filter(function (r) {
      if (filter && r.defectType !== filter) return false;
      return true;
    });

    document.getElementById('galCount').textContent = '\u5171 ' + filtered.length + ' \u6761';

    if (filtered.length === 0) {
      grid.innerHTML = '<div class="gallery-empty">\u6682\u65e0\u7f3a\u9677\u8bb0\u5f55</div>';
      return;
    }

    var html = '';
    filtered.forEach(function (r, idx) {
      var tagClass = r.defectType === '\u9690\u88c2' ? 'crack' : (r.defectType === '\u5d29\u8fb9' ? 'chip' : 'other');
      html += '<div class="defect-card" onclick="openZoom(' + idx + ')">'
        + '<div class="defect-card-img"><span class="no-img">\u7f3a\u9677\u56fe\u50cf</span></div>'
        + '<div class="defect-card-info">'
        + '<div class="defect-card-rod">' + escapeHtml(r.rodId) + '</div>'
        + '<div class="defect-card-tags">'
        + '<span class="defect-tag ' + tagClass + '">' + r.defectType + '</span>'
        + '<span class="defect-card-time">' + r.time + '</span>'
        + '</div></div></div>';
    });

    grid.innerHTML = html;
  }

  window.refreshGallery = function () { renderGallery(); };
  window.clearGallery = function () {
    if (confirm('\u786e\u5b9a\u6e05\u7a7a\u6240\u6709\u7f3a\u9677\u8bb0\u5f55\uff1f')) {
      gallery = [];
      renderGallery();
    }
  };

  document.addEventListener('DOMContentLoaded', function () {
    var galFilter = document.getElementById('galFilter');
    if (galFilter) galFilter.addEventListener('change', renderGallery);
  });

  // ==================== 大图查看器 ====================

  var zoomIndex = 0;
  var zoomScale = 1, zoomPanX = 0, zoomPanY = 0;

  window.openZoom = function (idx) {
    zoomIndex = idx;
    document.getElementById('zoomOverlay').style.display = '';
    renderZoomImage();
    updateZoomPos();
  };

  window.closeZoom = function () {
    document.getElementById('zoomOverlay').style.display = 'none';
  };

  window.zoomNav = function (dir) {
    zoomIndex = Math.max(0, Math.min(gallery.length - 1, zoomIndex + dir));
    renderZoomImage();
    updateZoomPos();
  };

  function renderZoomImage() {
    var canvas = document.getElementById('zoomCanvas');
    var ctx = canvas.getContext('2d');
    canvas.width = 640;
    canvas.height = 480;
    ctx.fillStyle = '#222';
    ctx.fillRect(0, 0, 640, 480);
    ctx.fillStyle = '#666';
    ctx.font = '16px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('\u7f3a\u9677\u56fe\u50cf\u9884\u89c8', 320, 240);

    zoomScale = 1; zoomPanX = 0; zoomPanY = 0;
    canvas.style.transform = '';

    if (gallery[zoomIndex]) {
      document.getElementById('zoomTitle').textContent =
        gallery[zoomIndex].rodId + ' - ' + gallery[zoomIndex].defectType;
    }
  }

  function updateZoomPos() {
    document.getElementById('zoomPos').textContent = (zoomIndex + 1) + ' / ' + gallery.length;
  }

  function initZoomInteraction() {
    var body = document.getElementById('zoomBody');
    var canvas = document.getElementById('zoomCanvas');
    var dragging = false, sx, sy;

    body.addEventListener('wheel', function (e) {
      e.preventDefault();
      var d = e.deltaY > 0 ? 0.9 : 1.1;
      zoomScale = Math.max(0.05, Math.min(50, zoomScale * d));
      canvas.style.transform = 'translate(' + zoomPanX + 'px,' + zoomPanY + 'px) scale(' + zoomScale + ')';
    }, { passive: false });

    body.addEventListener('mousedown', function (e) {
      if (e.button !== 0) return;
      dragging = true;
      sx = e.clientX - zoomPanX;
      sy = e.clientY - zoomPanY;
    });

    window.addEventListener('mousemove', function (e) {
      if (!dragging) return;
      zoomPanX = e.clientX - sx;
      zoomPanY = e.clientY - sy;
      canvas.style.transform = 'translate(' + zoomPanX + 'px,' + zoomPanY + 'px) scale(' + zoomScale + ')';
    });

    window.addEventListener('mouseup', function () { dragging = false; });

    body.addEventListener('dblclick', function () {
      zoomScale = 1; zoomPanX = 0; zoomPanY = 0;
      canvas.style.transform = '';
    });

    // Esc 关闭
    document.addEventListener('keydown', function (e) {
      if (document.getElementById('zoomOverlay').style.display !== 'none') {
        if (e.key === 'Escape') closeZoom();
        if (e.key === 'ArrowLeft') zoomNav(-1);
        if (e.key === 'ArrowRight') zoomNav(1);
      }
    });
  }

  // ==================== 统计图表（Canvas 自绘） ====================

  function drawAllCharts() {
    drawDefectBarChart();
    drawHourlyLineChart();
    drawDailyTrendChart();
    drawDurationHistChart();
  }

  // 通用绘图工具
  function getChartCtx(id) {
    var canvas = document.getElementById(id);
    if (!canvas) return null;
    // 适配 HiDPI
    var rect = canvas.parentElement.getBoundingClientRect();
    var w = Math.floor(rect.width - 32);
    if (w < 200) w = 500;
    var h = 280;
    var dpr = window.devicePixelRatio || 1;
    canvas.width = w * dpr;
    canvas.height = h * dpr;
    canvas.style.width = w + 'px';
    canvas.style.height = h + 'px';
    var ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);
    return { ctx: ctx, w: w, h: h };
  }

  // 缺陷类型分布条形图
  function drawDefectBarChart() {
    var c = getChartCtx('chartDefectBar');
    if (!c) return;
    var ctx = c.ctx, w = c.w, h = c.h;

    var types = Object.keys(stats.defectTypes);
    var values = types.map(function (t) { return stats.defectTypes[t]; });

    // demo 数据
    if (types.length === 0) {
      types = ['\u9690\u88c2', '\u5d29\u8fb9', '\u6c14\u6ce1', '\u5212\u75d5', '\u5176\u4ed6'];
      values = [42, 18, 12, 8, 5];
    }

    var maxVal = Math.max.apply(null, values) || 1;
    var colors = ['#F85149', '#DB6D28', '#D29922', '#58A6FF', '#BC8CFF'];
    var pad = { t: 20, b: 40, l: 50, r: 20 };
    var plotW = w - pad.l - pad.r;
    var plotH = h - pad.t - pad.b;
    var barW = Math.min(40, plotW / types.length * 0.6);
    var gap = plotW / types.length;

    // 网格
    ctx.strokeStyle = 'rgba(48,54,61,0.5)';
    ctx.lineWidth = 0.5;
    for (var i = 0; i <= 4; i++) {
      var y = pad.t + plotH * (1 - i / 4);
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = '#8B949E';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'right';
      ctx.fillText(Math.round(maxVal * i / 4), pad.l - 6, y + 3);
    }

    // 条形
    types.forEach(function (t, i) {
      var x = pad.l + gap * i + (gap - barW) / 2;
      var barH = (values[i] / maxVal) * plotH;
      var y = pad.t + plotH - barH;
      ctx.fillStyle = colors[i % colors.length];
      ctx.beginPath();
      roundRect(ctx, x, y, barW, barH, 3);
      ctx.fill();

      // 数值
      ctx.fillStyle = '#E6EDF3';
      ctx.font = '11px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(values[i], x + barW / 2, y - 5);

      // 标签
      ctx.fillStyle = '#8B949E';
      ctx.font = '11px sans-serif';
      ctx.fillText(t, x + barW / 2, h - pad.b + 16);
    });
  }

  // 每小时检测量折线图
  function drawHourlyLineChart() {
    var c = getChartCtx('chartHourlyLine');
    if (!c) return;
    var ctx = c.ctx, w = c.w, h = c.h;

    var data = stats.hourly.slice();
    // demo
    if (data.every(function (v) { return v === 0; })) {
      data = [2, 5, 8, 15, 22, 35, 48, 62, 55, 70, 68, 75, 80, 72, 65, 58, 50, 45, 38, 30, 22, 15, 8, 3];
    }

    var maxVal = Math.max.apply(null, data) || 1;
    var pad = { t: 20, b: 40, l: 50, r: 20 };
    var plotW = w - pad.l - pad.r;
    var plotH = h - pad.t - pad.b;

    // 网格
    ctx.strokeStyle = 'rgba(48,54,61,0.5)';
    ctx.lineWidth = 0.5;
    for (var i = 0; i <= 4; i++) {
      var y = pad.t + plotH * (1 - i / 4);
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = '#8B949E';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'right';
      ctx.fillText(Math.round(maxVal * i / 4), pad.l - 6, y + 3);
    }

    // 面积
    var points = data.map(function (v, i) {
      return {
        x: pad.l + (plotW / 23) * i,
        y: pad.t + plotH * (1 - v / maxVal)
      };
    });

    ctx.beginPath();
    ctx.moveTo(points[0].x, pad.t + plotH);
    points.forEach(function (p) { ctx.lineTo(p.x, p.y); });
    ctx.lineTo(points[points.length - 1].x, pad.t + plotH);
    ctx.closePath();
    ctx.fillStyle = 'rgba(88,166,255,0.1)';
    ctx.fill();

    // 线
    ctx.beginPath();
    points.forEach(function (p, i) {
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    });
    ctx.strokeStyle = '#58A6FF';
    ctx.lineWidth = 2;
    ctx.stroke();

    // 点
    points.forEach(function (p) {
      ctx.beginPath();
      ctx.arc(p.x, p.y, 2.5, 0, Math.PI * 2);
      ctx.fillStyle = '#58A6FF';
      ctx.fill();
    });

    // X轴标签
    ctx.fillStyle = '#8B949E';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    for (var i = 0; i < 24; i += 3) {
      ctx.fillText(i + ':00', pad.l + (plotW / 23) * i, h - pad.b + 16);
    }
  }

  // 近 7 日合格率趋势
  function drawDailyTrendChart() {
    var c = getChartCtx('chartDailyTrend');
    if (!c) return;
    var ctx = c.ctx, w = c.w, h = c.h;

    // demo 数据
    var labels = [];
    var data = [];
    for (var d = 6; d >= 0; d--) {
      var date = new Date();
      date.setDate(date.getDate() - d);
      labels.push((date.getMonth() + 1) + '/' + date.getDate());
      data.push(95 + Math.random() * 4.5);
    }

    if (stats.dailyOK.length >= 7) {
      data = stats.dailyOK.slice(-7);
    }

    var pad = { t: 20, b: 40, l: 50, r: 20 };
    var plotW = w - pad.l - pad.r;
    var plotH = h - pad.t - pad.b;

    // 网格
    ctx.strokeStyle = 'rgba(48,54,61,0.5)';
    ctx.lineWidth = 0.5;
    [90, 92, 94, 96, 98, 100].forEach(function (v) {
      var y = pad.t + plotH * (1 - (v - 90) / 10);
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = '#8B949E';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'right';
      ctx.fillText(v + '%', pad.l - 6, y + 3);
    });

    var points = data.map(function (v, i) {
      return {
        x: pad.l + (plotW / 6) * i,
        y: pad.t + plotH * (1 - (v - 90) / 10)
      };
    });

    // 面积
    ctx.beginPath();
    ctx.moveTo(points[0].x, pad.t + plotH);
    points.forEach(function (p) { ctx.lineTo(p.x, p.y); });
    ctx.lineTo(points[points.length - 1].x, pad.t + plotH);
    ctx.closePath();
    ctx.fillStyle = 'rgba(63,185,80,0.1)';
    ctx.fill();

    // 线
    ctx.beginPath();
    points.forEach(function (p, i) {
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    });
    ctx.strokeStyle = '#3FB950';
    ctx.lineWidth = 2;
    ctx.stroke();

    // 点 + 数值
    points.forEach(function (p, i) {
      ctx.beginPath();
      ctx.arc(p.x, p.y, 3, 0, Math.PI * 2);
      ctx.fillStyle = '#3FB950';
      ctx.fill();
      ctx.fillStyle = '#E6EDF3';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(data[i].toFixed(1) + '%', p.x, p.y - 8);
    });

    // X轴
    ctx.fillStyle = '#8B949E';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    labels.forEach(function (l, i) {
      ctx.fillText(l, pad.l + (plotW / 6) * i, h - pad.b + 16);
    });
  }

  // 检测时长分布直方图
  function drawDurationHistChart() {
    var c = getChartCtx('chartDurationHist');
    if (!c) return;
    var ctx = c.ctx, w = c.w, h = c.h;

    // demo 数据：时长分布桶 (ms)
    var buckets = ['0-50', '50-100', '100-150', '150-200', '200-250', '250-300', '300+'];
    var counts = [5, 25, 45, 35, 18, 8, 3];

    if (stats.durations.length > 10) {
      counts = [0, 0, 0, 0, 0, 0, 0];
      stats.durations.forEach(function (d) {
        var idx = Math.min(6, Math.floor(d / 50));
        counts[idx]++;
      });
    }

    var maxVal = Math.max.apply(null, counts) || 1;
    var pad = { t: 20, b: 40, l: 50, r: 20 };
    var plotW = w - pad.l - pad.r;
    var plotH = h - pad.t - pad.b;
    var barW = Math.min(40, plotW / buckets.length * 0.7);
    var gap = plotW / buckets.length;

    // 网格
    ctx.strokeStyle = 'rgba(48,54,61,0.5)';
    ctx.lineWidth = 0.5;
    for (var i = 0; i <= 4; i++) {
      var y = pad.t + plotH * (1 - i / 4);
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(w - pad.r, y); ctx.stroke();
      ctx.fillStyle = '#8B949E';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'right';
      ctx.fillText(Math.round(maxVal * i / 4), pad.l - 6, y + 3);
    }

    buckets.forEach(function (b, i) {
      var x = pad.l + gap * i + (gap - barW) / 2;
      var barH = (counts[i] / maxVal) * plotH;
      var y = pad.t + plotH - barH;
      ctx.fillStyle = '#D29922';
      ctx.beginPath();
      roundRect(ctx, x, y, barW, barH, 3);
      ctx.fill();

      ctx.fillStyle = '#E6EDF3';
      ctx.font = '10px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(counts[i], x + barW / 2, y - 5);

      ctx.fillStyle = '#8B949E';
      ctx.font = '9px sans-serif';
      ctx.fillText(b, x + barW / 2, h - pad.b + 14);
    });
  }

  // 圆角矩形
  function roundRect(ctx, x, y, w, h, r) {
    if (h <= 0) { ctx.rect(x, y, w, 0); return; }
    r = Math.min(r, h / 2, w / 2);
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.arcTo(x + w, y, x + w, y + r, r);
    ctx.lineTo(x + w, y + h);
    ctx.lineTo(x, y + h);
    ctx.lineTo(x, y + r);
    ctx.arcTo(x, y, x + r, y, r);
    ctx.closePath();
  }

  // ==================== 设置 ====================

  window.saveSettings = function () {
    // 按模块分别发送到后端，每个模块一个 CMD
    var tcpData = { ip: val('setTcpIp'), port: parseInt(val('setTcpPort')) || 3000 };
    var dbData = {
      ip: val('setDbIp'), port: parseInt(val('setDbPort')) || 3306,
      user: val('setDbUser'), pass: val('setDbPass'),
      name: val('setDbName'), table: val('setDbTable')
    };
    var shiftData = { dayShift: val('setShiftDay'), nightShift: val('setShiftNight') };
    var defectTypes = getDefectTypeList();
    var feishuData = {
      enable: document.getElementById('setFeishuEnable').checked,
      appId: val('setFeishuAppId'), appSecret: val('setFeishuAppSecret'),
      appToken: val('setFeishuAppToken'), tableId: val('setFeishuTableId'),
      api: val('setFeishuApi')
    };
    var imageData = { enable: document.getElementById('setImgEnable').checked, dir: val('setImgDir') };
    var pipelineData = { pipeline: val('setPipeline') };

    // 逐模块发送到后端
    window.sendCmd(100, tcpData);       // TCP 通信设置
    window.sendCmd(101, dbData);        // MySQL 数据库设置
    window.sendCmd(102, shiftData);     // 班次清零设置
    window.sendCmd(103, { types: defectTypes }); // 缺陷类型列表
    window.sendCmd(104, feishuData);    // 飞书同步设置
    window.sendCmd(105, imageData);     // 图像存储设置
    window.sendCmd(106, pipelineData);  // 产线设置

    // 更新页面上的产线编号
    document.querySelector('.pipeline-id').textContent = val('setPipeline');

    // 同步保存到 localStorage 作为备份
    var allSettings = {
      tcp: tcpData, db: dbData, shift: shiftData,
      defectTypes: defectTypes, feishu: feishuData,
      image: imageData, pipeline: val('setPipeline')
    };
    try { localStorage.setItem('sirod_settings', JSON.stringify(allSettings)); } catch (e) { }

    alert('设置已保存并同步到后端');
  };

  window.resetSettings = function () {
    if (confirm('确定恢复默认设置？')) {
      try { localStorage.removeItem('sirod_settings'); } catch (e) { }
      location.reload();
    }
  };

  // 获取缺陷类型列表
  function getDefectTypeList() {
    var items = document.querySelectorAll('.defect-type-item span');
    var types = [];
    items.forEach(function (el) { types.push(el.textContent); });
    return types;
  }

  // 加载设置（先尝试从后端获取，再兜底 localStorage）
  document.addEventListener('DOMContentLoaded', function () {
    // 向后端请求当前设置（后端收到 CMD=200 会回复 CMD=210）
    window.sendCmd(200, {});

    // 兜底：从 localStorage 加载
    try {
      var saved = localStorage.getItem('sirod_settings');
      if (saved) {
        var s = JSON.parse(saved);
        if (s.tcp) { setVal('setTcpIp', s.tcp.ip); setVal('setTcpPort', s.tcp.port); }
        if (s.db) {
          setVal('setDbIp', s.db.ip); setVal('setDbPort', s.db.port);
          setVal('setDbUser', s.db.user); setVal('setDbPass', s.db.pass);
          setVal('setDbName', s.db.name); setVal('setDbTable', s.db.table);
        }
        if (s.shift) { setVal('setShiftDay', s.shift.dayShift || s.shift.day); setVal('setShiftNight', s.shift.nightShift || s.shift.night); }
        if (s.feishu) {
          document.getElementById('setFeishuEnable').checked = !!s.feishu.enable;
          setVal('setFeishuAppId', s.feishu.appId); setVal('setFeishuAppSecret', s.feishu.appSecret);
          setVal('setFeishuAppToken', s.feishu.appToken); setVal('setFeishuTableId', s.feishu.tableId);
          setVal('setFeishuApi', s.feishu.api);
        }
        if (s.image) { document.getElementById('setImgEnable').checked = !!s.image.enable; setVal('setImgDir', s.image.dir); }
        if (s.pipeline) {
          setVal('setPipeline', s.pipeline);
          document.querySelector('.pipeline-id').textContent = s.pipeline;
        }
        if (s.defectTypes && Array.isArray(s.defectTypes)) {
          var list = document.getElementById('defectTypeList');
          list.innerHTML = '';
          s.defectTypes.forEach(function (name) {
            var item = document.createElement('div');
            item.className = 'defect-type-item';
            item.innerHTML = '<span>' + escapeHtml(name) + '</span><button class="btn btn-sm btn-danger-outline" onclick="removeDefectType(this)">删除</button>';
            list.appendChild(item);
          });
          updateDefectFilters();
        }
      }
    } catch (e) { }
  });

  function val(id) { var el = document.getElementById(id); return el ? el.value : ''; }
  function setVal(id, v) { var el = document.getElementById(id); if (el && v != null) el.value = v; }

  // ==================== 缺陷类型管理 ====================

  window.addDefectType = function () {
    var input = document.getElementById('newDefectType');
    var name = input.value.trim();
    if (!name) return;

    var list = document.getElementById('defectTypeList');
    var item = document.createElement('div');
    item.className = 'defect-type-item';
    item.innerHTML = '<span>' + escapeHtml(name) + '</span><button class="btn btn-sm btn-danger-outline" onclick="removeDefectType(this)">删除</button>';
    list.appendChild(item);
    input.value = '';

    updateDefectFilters();
    // 通知后端缺陷类型变更
    window.sendCmd(103, { types: getDefectTypeList() });
  };

  window.removeDefectType = function (btn) {
    btn.parentElement.remove();
    updateDefectFilters();
    // 通知后端缺陷类型变更
    window.sendCmd(103, { types: getDefectTypeList() });
  };

  function updateDefectFilters() {
    var items = document.querySelectorAll('.defect-type-item span');
    var types = [];
    items.forEach(function (el) { types.push(el.textContent); });

    ['histDefectFilter', 'galFilter'].forEach(function (id) {
      var sel = document.getElementById(id);
      var current = sel.value;
      sel.innerHTML = '<option value="">\u5168\u90e8\u7f3a\u9677' + (id === 'galFilter' ? '\u7c7b\u578b' : '') + '</option>';
      types.forEach(function (t) {
        sel.innerHTML += '<option value="' + escapeHtml(t) + '">' + escapeHtml(t) + '</option>';
      });
      sel.value = current;
    });
  }

  // ==================== 工具函数 ====================

  function escapeHtml(s) {
    var d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
  }

  // 窗口 resize 重绘图表
  var resizeTimer;
  window.addEventListener('resize', function () {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(function () {
      if (document.getElementById('page-stats').classList.contains('active')) {
        drawAllCharts();
      }
    }, 200);
  });

})();
