// ================================================================
// Halcon_UI Electron 正式版 — Preload 脚本
// 通过 contextBridge 安全地将 API 暴露给渲染进程
// ================================================================
const { contextBridge } = require('electron');
const path = require('path');
const fs = require('fs');

// 读取配置文件
let config = { wsUrl: 'ws://127.0.0.1:1802/ws' };
try {
    const configPath = path.join(__dirname, 'config.json');
    const raw = fs.readFileSync(configPath, 'utf-8');
    const parsed = JSON.parse(raw);
    if (parsed.wsUrl) config.wsUrl = parsed.wsUrl;
} catch (e) {
    // 配置文件不存在或格式错误，使用默认值
}

contextBridge.exposeInMainWorld('electronAPI', {
    platform: process.platform,
    wsUrl: config.wsUrl,
    versions: {
        node: process.versions.node,
        chrome: process.versions.chrome,
        electron: process.versions.electron,
    },
});
