// ==================== Halcon_UI 前端（多窗口支持）====================

var streaming = false, abortCtrl = null;

// 多窗口管理: windows[id] = { card, canvas, ctx, fc, lt, tb }
var windows = {};

function getWindow(id) {
  if (windows[id]) return windows[id];

  // 动态创建窗口卡片
  var container = document.getElementById('windows');

  var card = document.createElement('div');
  card.className = 'win-card';

  var title = document.createElement('div');
  title.className = 'win-title';
  title.textContent = 'Window ' + id;

  var canvas = document.createElement('canvas');
  canvas.width = 640;
  canvas.height = 480;

  var info = document.createElement('div');
  info.className = 'win-info';
  info.textContent = 'waiting...';

  card.appendChild(title);
  card.appendChild(canvas);
  card.appendChild(info);
  container.appendChild(card);

  var win = {
    card: card,
    canvas: canvas,
    ctx: canvas.getContext('2d'),
    info: info,
    fc: 0,
    lt: performance.now(),
    tb: 0
  };
  windows[id] = win;
  log('info', 'Window ' + id + ' created');
  return win;
}

// ==================== 工具函数 ====================

function log(cls, text) {
  var d = document.getElementById('log');
  var time = new Date().toLocaleTimeString();
  d.innerHTML += '<div class="' + cls + '">[' + time + '] ' + text + '</div>';
  d.scrollTop = d.scrollHeight;
}

function u32le(buf, offset) {
  return buf[offset] | (buf[offset+1] << 8) | (buf[offset+2] << 16) | ((buf[offset+3] << 24) >>> 0);
}

function concatBuffers(a, b) {
  var c = new Uint8Array(a.length + b.length);
  c.set(a);
  c.set(b, a.length);
  return c;
}

// ==================== 帧处理 ====================

async function handleFrame(jl, dl, buf) {
  var jsonBytes = buf.slice(8, 8 + jl - 1);
  var jsonStr = new TextDecoder().decode(jsonBytes);
  var msg;
  try { msg = JSON.parse(jsonStr); } catch(e) { return; }

  if (msg.CMD === 0 && dl > 0) {
    var data = msg.Data;
    var px = buf.slice(8 + jl, 8 + jl + dl);

    // 窗口 ID：Data.id，默认 0
    var winId = (data.id !== undefined) ? data.id : 0;
    var win = getWindow(winId);

    win.tb += dl;

    if (data.fmt === 'jpeg') {
      // JPEG 解码（GPU 加速）
      var blob = new Blob([px], { type: 'image/jpeg' });
      var bmp = await createImageBitmap(blob);
      win.canvas.width = bmp.width;
      win.canvas.height = bmp.height;
      win.ctx.drawImage(bmp, 0, 0);
      bmp.close();
    } else {
      // RAW pixels
      var w = data['\u5bbd'], h = data['\u9ad8'], ch = data['\u901a\u9053'];
      win.canvas.width = w; win.canvas.height = h;
      var im = win.ctx.createImageData(w, h), o = im.data;
      if (ch === 1) {
        for (var i = 0; i < w * h; i++) {
          o[i*4] = o[i*4+1] = o[i*4+2] = px[i]; o[i*4+3] = 255;
        }
      } else if (ch === 3) {
        var ps = w * h;
        for (var i = 0; i < w * h; i++) {
          o[i*4]   = px[i];
          o[i*4+1] = px[i + ps];
          o[i*4+2] = px[i + ps * 2];
          o[i*4+3] = 255;
        }
      }
      win.ctx.putImageData(im, 0, 0);
    }

    // FPS 统计（每个窗口独立）
    win.fc++;
    var now = performance.now();
    if (now - win.lt >= 1000) {
      var kbs = Math.round(win.tb / 1024);
      win.tb = 0;
      var fmt = data.fmt === 'jpeg' ? 'JPEG' : 'RAW';
      win.info.textContent =
        win.canvas.width + 'x' + win.canvas.height + ' ' + fmt + ' ' +
        Math.round(dl / 1024) + 'KB  ' + win.fc + 'fps  ' + kbs + 'KB/s';
      win.fc = 0;
      win.lt = now;
    }
  } else {
    log('ok', 'CMD=' + msg.CMD + ' ' + JSON.stringify(msg.Data));
  }
}

// ==================== Stream 控制 ====================

async function toggleStream() {
  if (streaming) { stopStream(); return; }

  streaming = true;
  document.getElementById('btnStream').textContent = 'Stop Stream';
  document.getElementById('btnStream').className = 'on';
  log('ok', 'Stream connecting...');

  abortCtrl = new AbortController();
  try {
    var resp = await fetch('/api/stream', { signal: abortCtrl.signal });
    log('ok', 'Stream connected!');

    var reader = resp.body.getReader();
    var buf = new Uint8Array(0);

    while (true) {
      var rd = await reader.read();
      if (rd.done) break;
      buf = concatBuffers(buf, rd.value);

      while (buf.length >= 8) {
        var jl = u32le(buf, 0);
        var dl = u32le(buf, 4);
        var total = 8 + jl + dl;
        if (buf.length < total) break;
        await handleFrame(jl, dl, buf);
        buf = buf.slice(total);
      }
    }
  } catch(e) {
    if (e.name !== 'AbortError') log('err', 'Stream error: ' + e.message);
  }

  log('info', 'Stream ended');
  streaming = false;
  document.getElementById('btnStream').textContent = 'Start Stream';
  document.getElementById('btnStream').className = '';
}

function stopStream() {
  if (abortCtrl) abortCtrl.abort();
  streaming = false;
}

// ==================== 发送命令 ====================

function sendCmd(cmd) {
  fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ CMD: cmd, Data: {} })
  }).then(function(r) {
    log('send', 'CMD=' + cmd + ' -> ' + (r.ok ? 'OK' : 'ERR'));
  });
}
