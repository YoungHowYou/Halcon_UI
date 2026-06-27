// Halcon_UI Electron — Preload 脚本
const { contextBridge } = require('electron');
const path = require('path');
const fs = require('fs');

let config = { wsUrl: 'ws://127.0.0.1:1802/ws' };
try {
    config = JSON.parse(fs.readFileSync(path.join(__dirname, 'config.json'), 'utf-8'));
} catch (e) {}

contextBridge.exposeInMainWorld('electronAPI', {
    platform: process.platform,
    wsUrl: config.wsUrl || 'ws://127.0.0.1:1802/ws',
    versions: {
        node: process.versions.node,
        chrome: process.versions.chrome,
        electron: process.versions.electron,
    },
});
