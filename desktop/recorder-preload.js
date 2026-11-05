'use strict';
// hidden window that records a game (F9): it gets a window or screen id, records it, and hands the video file to the app
const { ipcRenderer } = require('electron');
let rec = null, chunks = [], stream = null;
ipcRenderer.on('rec:start', async (e, sourceId) => {
  try {
    stream = await navigator.mediaDevices.getUserMedia({ audio: false, video: { mandatory: { chromeMediaSource: 'desktop', chromeMediaSourceId: sourceId, maxFrameRate: 30, maxWidth: 1920, maxHeight: 1080 } } });
    chunks = []; const mime = ['video/webm;codecs=vp9', 'video/webm;codecs=vp8', 'video/webm'].find(m => MediaRecorder.isTypeSupported(m));
    rec = new MediaRecorder(stream, { mimeType: mime, videoBitsPerSecond: 8e6 });
    rec.ondataavailable = ev => { if (ev.data && ev.data.size) chunks.push(ev.data); };
    rec.onstop = async () => { const buf = await new Blob(chunks, { type: 'video/webm' }).arrayBuffer(); stream.getTracks().forEach(t => t.stop()); ipcRenderer.send('rec:done', new Uint8Array(buf)); chunks = []; };
    rec.start(1000); ipcRenderer.send('rec:started');
  } catch (err) { ipcRenderer.send('rec:error', String(err && err.message || err)); }
});
ipcRenderer.on('rec:stop', () => { if (rec && rec.state !== 'inactive') rec.stop(); });
