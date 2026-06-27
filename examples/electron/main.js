// ================================================================
// Halcon_UI Electron 正式版 — 主进程
// ================================================================
const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const fs = require('fs');

let mainWindow;

function createWindow() {
    mainWindow = new BrowserWindow({
        width: 1600,
        height: 960,
        minWidth: 900,
        minHeight: 600,
        title: 'YouEyE — 工业视觉检测',
        backgroundColor: '#0b0e17',
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            contextIsolation: true,
            nodeIntegration: false,
        },
    });

    mainWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));

    if (process.argv.includes('--dev')) {
        mainWindow.webContents.openDevTools();
    }

    mainWindow.on('closed', () => { mainWindow = null; });
}

// ==================== IPC：文件选择对话框 ====================

// 选择单张图片
ipcMain.handle('select-image-file', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
        title: '选择图片进行离线检测',
        filters: [
            { name: '图片文件', extensions: ['jpg', 'jpeg', 'png', 'bmp', 'tif', 'tiff', 'png'] },
            { name: '所有文件', extensions: ['*'] },
        ],
        properties: ['openFile'],
    });
    if (result.canceled || result.filePaths.length === 0) return null;
    return result.filePaths[0];
});

// 选择文件夹（批量离线检测）
ipcMain.handle('select-image-folder', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
        title: '选择图片文件夹进行批量离线检测',
        properties: ['openDirectory'],
    });
    if (result.canceled || result.filePaths.length === 0) return null;
    const folderPath = result.filePaths[0];
    // 扫描文件夹内的图片文件
    const exts = ['.jpg', '.jpeg', '.png', '.bmp', '.tif', '.tiff'];
    const files = [];
    try {
        const entries = fs.readdirSync(folderPath, { withFileTypes: true });
        for (const entry of entries) {
            if (entry.isFile()) {
                const ext = path.extname(entry.name).toLowerCase();
                if (exts.includes(ext)) {
                    files.push(path.join(folderPath, entry.name));
                }
            }
        }
        // 按文件名排序
        files.sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' }));
    } catch (e) {
        // ignore
    }
    return { folder: folderPath, files: files, count: files.length };
});

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
    if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
});
