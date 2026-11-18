'use strict';
// The Control Center over a game (console mode): data and actions, nothing else.
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('CC', {
  data: () => ipcRenderer.invoke('cc:data'),
  act: (what, arg) => ipcRenderer.invoke('cc:act', what, arg)
});
