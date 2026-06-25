// ============================================================
// Halcon_UI Electron 示例 — Preload（安全桥接）
// 将 WebSocket 能力暴露给渲染进程
// ============================================================
const { contextBridge } = require('electron');

// 暴露给渲染进程的 API
contextBridge.exposeInMainWorld('electronAPI', {
    platform: process.platform,
    versions: {
        node: process.versions.node,
        chrome: process.versions.chrome,
        electron: process.versions.electron,
    },
});
