const { contextBridge, ipcRenderer } = require('electron');
// Account usage is read in the main process; OAuth credentials never cross IPC.
const channels = ['aiStatus', 'aiAccount', 'aiModels', 'aiCollect', 'aiAnalyze', 'aiCancel', 'aiReset', 'aiReport', 'questDevices', 'questGames', 'questImport', 'importZip', 'setupCheck', 'setupStart', 'setupCancel', 'chooseSetupArchives', 'setupLicense', 'state', 'sync', 'login', 'logout', 'search', 'add', 'lookup', 'builds', 'download', 'dlc', 'downloadDlc', 'cancel', 'retry', 'import', 'importAssets', 'install', 'uninstall', 'patch', 'play', 'stop', 'startAndroid', 'stopAndroid', 'fpsHud', 'settings', 'chooseFolder', 'chooseCli', 'openFolder', 'openStore', 'diagnostics', 'diagnosticsRead', 'performanceScan', 'copyText', 'storage', 'storageInfo', 'permissions', 'setPermission'];
contextBridge.exposeInMainWorld('axrb', Object.fromEntries([
  ['onAiUpdate', callback => { const listener = (_event, value) => callback(value); ipcRenderer.on('axrb:ai-update', listener); return () => ipcRenderer.removeListener('axrb:ai-update', listener); }],
  ...channels.map(name => [name, (...args) => ipcRenderer.invoke(`axrb:${name}`, ...args)]),
  ['onChange', callback => { const listener = (_event, state) => callback(state); ipcRenderer.on('axrb:changed', listener); return () => ipcRenderer.removeListener('axrb:changed', listener); }],
  ['onLaunchError', callback => { const listener = (_event, error) => callback(error); ipcRenderer.on('axrb:launch-error', listener); return () => ipcRenderer.removeListener('axrb:launch-error', listener); }],
  ['onDiagnostics', callback => { const listener = () => callback(); ipcRenderer.on('axrb:diagnostics', listener); return () => ipcRenderer.removeListener('axrb:diagnostics', listener); }]
]));
