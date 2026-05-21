import http from 'node:http';
import { WebSocketServer, WebSocket } from 'ws';
import { URL } from 'node:url';

const PORT = process.env.PORT ? Number(process.env.PORT) : 7000;
const HOST = process.env.HOST ?? '0.0.0.0';
const WS_PATH = process.env.WS_PATH ?? '/';
const REQUIRED_TOKEN = process.env.BRIDGE_TOKEN ?? process.env.TOKEN ?? '';

const UI_HTML = `<!DOCTYPE html>
<html lang="en" data-ws-path="${WS_PATH}">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Voice Puck Bridge</title>
    <style>
      :root {
        color-scheme: light dark;
        font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      }
      body {
        margin: 0;
        padding: 1.5rem;
        display: flex;
        flex-direction: column;
        gap: 1rem;
      }
      header {
        display: flex;
        flex-direction: column;
        gap: 0.5rem;
      }
      fieldset {
        border-radius: 0.75rem;
        border: 1px solid rgba(127, 127, 127, 0.4);
        padding: 1rem 1.25rem;
      }
      legend {
        padding: 0 0.35rem;
        font-weight: 600;
      }
      label {
        display: flex;
        flex-direction: column;
        gap: 0.35rem;
        margin-bottom: 0.75rem;
        font-size: 0.95rem;
      }
      input[type="text"],
      input[type="url"] {
        padding: 0.6rem 0.75rem;
        border-radius: 0.5rem;
        border: 1px solid rgba(127, 127, 127, 0.4);
        font-size: 1rem;
      }
      button {
        appearance: none;
        border: none;
        border-radius: 999px;
        padding: 0.6rem 1.5rem;
        background: #4f46e5;
        color: white;
        font-weight: 600;
        font-size: 1rem;
        cursor: pointer;
      }
      button[disabled] {
        opacity: 0.5;
        cursor: not-allowed;
      }
      #status {
        font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
        font-size: 0.9rem;
        white-space: pre-wrap;
      }
      ul {
        padding-left: 1.2rem;
      }
    </style>
  </head>
  <body>
    <header>
      <h1>Voice Puck Web Bridge</h1>
      <p>
        Join the audio bridge directly from your browser. Grant microphone permission and
        the page will capture 16 kHz mono PCM and forward it to all connected clients.
        Incoming puck audio is played through your default output device.
      </p>
    </header>
    <fieldset>
      <legend>Connection</legend>
      <label>
        Bridge URL
        <input id="url" type="url" autocomplete="off" spellcheck="false" />
      </label>
      <label>
        Token (optional)
        <input id="token" type="text" autocomplete="off" spellcheck="false" />
      </label>
      <div style="display:flex; gap:0.75rem; align-items:center; flex-wrap:wrap;">
        <button id="connect">Connect</button>
        <button id="disconnect" disabled>Disconnect</button>
        <span id="connection-state">Disconnected</span>
      </div>
    </fieldset>
    <fieldset>
      <legend>Activity</legend>
      <div id="status">Idle.</div>
      <h3>Connected clients</h3>
      <ul id="clients"></ul>
    </fieldset>
    <script>
      const defaultPath = document.documentElement.dataset.wsPath || '/';
      const urlInput = document.getElementById('url');
      const tokenInput = document.getElementById('token');
      const connectBtn = document.getElementById('connect');
      const disconnectBtn = document.getElementById('disconnect');
      const statusEl = document.getElementById('status');
      const connStateEl = document.getElementById('connection-state');
      const clientsEl = document.getElementById('clients');

      const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
      urlInput.value = proto + '//' + location.host + defaultPath;

      let ws = null;
      let audioCtx = null;
      let mediaStream = null;
      let processor = null;
      let seq = 0;
      let playbackTime = 0;
      const clients = new Map();

      function setConnectionState(state) {
        connStateEl.textContent = state;
      }

      function log(message) {
        const now = new Date().toLocaleTimeString();
        statusEl.textContent = '[' + now + '] ' + message + '\n' + statusEl.textContent.split('\n').slice(0, 20).join('\n');
      }

      function refreshClients() {
        clientsEl.innerHTML = '';
        for (const info of clients.values()) {
          const li = document.createElement('li');
          li.textContent = info;
          clientsEl.appendChild(li);
        }
        if (!clients.size) {
          const li = document.createElement('li');
          li.textContent = 'No clients connected.';
          clientsEl.appendChild(li);
        }
      }

      async function ensureAudio() {
        if (!audioCtx) {
          audioCtx = new AudioContext();
        }
        if (!mediaStream) {
          mediaStream = await navigator.mediaDevices.getUserMedia({ audio: true });
        }
        if (!processor) {
          const source = audioCtx.createMediaStreamSource(mediaStream);
          processor = audioCtx.createScriptProcessor(2048, 1, 1);
          source.connect(processor);
          processor.connect(audioCtx.destination);
          processor.onaudioprocess = handleAudioProcess;
        }
      }

      function downsample(buffer, inputRate, targetRate) {
        if (targetRate === inputRate) {
          const pcm = new Int16Array(buffer.length);
          for (let i = 0; i < buffer.length; i++) {
            const s = Math.max(-1, Math.min(1, buffer[i]));
            pcm[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
          }
          return pcm;
        }
        const ratio = inputRate / targetRate;
        const len = Math.floor(buffer.length / ratio);
        const result = new Int16Array(len);
        let offsetBuffer = 0;
        for (let i = 0; i < len; i++) {
          const nextOffset = Math.floor((i + 1) * ratio);
          let accum = 0;
          let count = 0;
          while (offsetBuffer < nextOffset && offsetBuffer < buffer.length) {
            accum += buffer[offsetBuffer++];
            count++;
          }
          const sample = accum / (count || 1);
          const s = Math.max(-1, Math.min(1, sample));
          result[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
        }
        return result;
      }

      function handleAudioProcess(event) {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          return;
        }
        const input = event.inputBuffer.getChannelData(0);
        const pcm = downsample(input, event.inputBuffer.sampleRate, 16000);
        const header = new ArrayBuffer(8);
        const view = new DataView(header);
        view.setUint8(0, 1); // version
        view.setUint8(1, 0); // kind: audio_tx
        view.setUint16(2, seq, false);
        view.setUint32(4, pcm.byteLength, false);
        seq = (seq + 1) & 0xffff;
        const frame = new Uint8Array(8 + pcm.byteLength);
        frame.set(new Uint8Array(header), 0);
        frame.set(new Uint8Array(pcm.buffer), 8);
        ws.send(frame);
      }

      function playAudio(pcm) {
        if (!audioCtx) {
          return;
        }
        const samples = pcm.length / 2;
        const dataView = new DataView(pcm.buffer, pcm.byteOffset, pcm.byteLength);
        const float = new Float32Array(samples);
        for (let i = 0; i < samples; i++) {
          float[i] = dataView.getInt16(i * 2, false) / 0x8000;
        }
        const buffer = audioCtx.createBuffer(1, samples, 16000);
        buffer.copyToChannel(float, 0);
        const source = audioCtx.createBufferSource();
        source.buffer = buffer;
        source.connect(audioCtx.destination);
        const startTime = Math.max(playbackTime, audioCtx.currentTime + 0.02);
        source.start(startTime);
        playbackTime = startTime + buffer.duration;
      }

      function cleanupAudio() {
        if (processor) {
          processor.disconnect();
          processor.onaudioprocess = null;
          processor = null;
        }
        if (audioCtx) {
          audioCtx.close();
          audioCtx = null;
        }
        mediaStream = null;
        playbackTime = 0;
      }

      function disconnect() {
        if (ws) {
          ws.close();
        }
        cleanupAudio();
        ws = null;
        seq = 0;
        setConnectionState('Disconnected');
        connectBtn.disabled = false;
        disconnectBtn.disabled = true;
        log('Disconnected');
      }

      connectBtn.addEventListener('click', async () => {
        try {
          await ensureAudio();
        } catch (err) {
          log('Microphone access denied: ' + err.message);
          return;
        }
        const rawUrl = urlInput.value.trim();
        if (!rawUrl) {
          log('Enter a WebSocket URL');
          return;
        }
        const url = new URL(rawUrl);
        if (!url.searchParams.has('token') && tokenInput.value.trim()) {
          url.searchParams.set('token', tokenInput.value.trim());
        }
        ws = new WebSocket(url.href);
        ws.binaryType = 'arraybuffer';
        ws.addEventListener('open', () => {
          setConnectionState('Connected');
          connectBtn.disabled = true;
          disconnectBtn.disabled = false;
          const hello = {
            type: 'hello',
            device_id: 'browser-' + Math.random().toString(16).slice(2, 8),
            mode: 'browser',
            token: tokenInput.value.trim(),
          };
          ws.send(JSON.stringify(hello));
          log('Connected, hello sent');
        });
        ws.addEventListener('close', () => {
          disconnect();
        });
        ws.addEventListener('error', (event) => {
          log('WebSocket error');
          console.error(event);
        });
        ws.addEventListener('message', (event) => {
          if (typeof event.data === 'string') {
            log('TEXT <- ' + event.data);
            try {
              const payload = JSON.parse(event.data);
              if (payload.type === 'clients') {
                clients.clear();
                for (const entry of payload.items ?? []) {
                  const name = entry.device_id || entry.id;
                  const mode = entry.mode || 'unknown';
                  clients.set(entry.id, name + ' (' + mode + ')');
                }
                refreshClients();
              }
            } catch (_) {}
            return;
          }
          const buf = new Uint8Array(event.data);
          if (buf.length < 8) {
            return;
          }
          const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
          const ver = view.getUint8(0);
          const kind = view.getUint8(1);
          const payloadBytes = view.getUint32(4, false);
          if (ver !== 1 || kind !== 1 || payloadBytes <= 0) {
            return;
          }
          const pcm = buf.subarray(8, 8 + payloadBytes);
          playAudio(pcm);
        });
      });

      disconnectBtn.addEventListener('click', () => {
        disconnect();
      });
    </script>
  </body>
</html>`;

const httpServer = http.createServer((req, res) => {
  if (!req.url) {
    res.writeHead(400);
    res.end('Bad request');
    return;
  }
  if (req.method !== 'GET') {
    res.writeHead(405, { 'Allow': 'GET' });
    res.end('Method Not Allowed');
    return;
  }
  const url = new URL(req.url, `http://${req.headers.host}`);
  if (url.pathname === '/' || url.pathname === '/index.html') {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(UI_HTML);
    return;
  }
  if (url.pathname === '/clients') {
    const items = [];
    for (const [socket, info] of clients.entries()) {
      if (socket.readyState === WebSocket.OPEN) {
        items.push({
          id: info.peer,
          device_id: info.deviceId,
          mode: info.mode,
        });
      }
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ items }));
    return;
  }
  res.writeHead(404);
  res.end('Not found');
});

const wss = new WebSocketServer({ noServer: true });
const clients = new Map();

function broadcastAudio(from, frame) {
  for (const [socket, info] of clients.entries()) {
    if (socket === from || socket.readyState !== WebSocket.OPEN) {
      continue;
    }
    try {
      socket.send(frame);
    } catch (err) {
      console.error(`Failed to forward audio to ${info.peer}`, err);
    }
  }
}

function broadcastClients() {
  const items = [];
  for (const [socket, info] of clients.entries()) {
    if (socket.readyState === WebSocket.OPEN) {
      items.push({ id: info.peer, device_id: info.deviceId, mode: info.mode });
    }
  }
  const payload = JSON.stringify({ type: 'clients', items });
  for (const [socket] of clients.entries()) {
    if (socket.readyState === WebSocket.OPEN) {
      socket.send(payload);
    }
  }
}

httpServer.on('upgrade', (req, socket, head) => {
  const { url } = req;
  let parsed;
  try {
    parsed = new URL(url ?? '/', `http://${req.headers.host}`);
  } catch (err) {
    socket.write('HTTP/1.1 400 Bad Request\r\n\r\n');
    socket.destroy();
    return;
  }
  const pathname = parsed.pathname || '/';
  const normalizedPath = WS_PATH.endsWith('/') ? WS_PATH : WS_PATH + (WS_PATH === '/' ? '' : '/');
  const allowed = pathname === WS_PATH || (WS_PATH !== '/' && pathname === normalizedPath);
  if (!allowed && WS_PATH !== '/') {
    socket.write('HTTP/1.1 404 Not Found\r\n\r\n');
    socket.destroy();
    return;
  }
  const token = parsed.searchParams.get('token') ?? '';
  if (REQUIRED_TOKEN && token !== REQUIRED_TOKEN) {
    socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n');
    socket.destroy();
    return;
  }
  wss.handleUpgrade(req, socket, head, (ws) => {
    wss.emit('connection', ws, req, { token });
  });
});

wss.on('connection', (socket, req, context = {}) => {
  const peer = `${req.socket.remoteAddress}:${req.socket.remotePort}`;
  const info = { peer, deviceId: null, mode: null, token: context.token ?? '' };
  clients.set(socket, info);
  console.log(`Client connected: ${peer}`);
  broadcastClients();

  socket.on('message', (data, isBinary) => {
    if (!isBinary) {
      try {
        const text = data.toString();
        console.log(`TEXT <- ${peer}: ${text}`);
        const payload = JSON.parse(text);
        if (payload?.type === 'hello') {
          info.deviceId = payload.device_id ?? info.deviceId;
          info.mode = payload.mode ?? info.mode;
          if (REQUIRED_TOKEN && payload.token !== REQUIRED_TOKEN) {
            console.warn(`Client ${peer} provided invalid token in hello`);
            socket.close(4403, 'Invalid token');
            return;
          }
          socket.send(JSON.stringify({ type: 'hello_ack' }));
          broadcastClients();
        }
      } catch (err) {
        console.error('Failed to parse text frame', err);
      }
      return;
    }

    const buffer = Buffer.from(data);
    if (buffer.length < 8) {
      console.warn('Ignoring short binary frame');
      return;
    }
    const ver = buffer.readUInt8(0);
    const kind = buffer.readUInt8(1);
    const seq = buffer.readUInt16BE(2);
    const nbytes = buffer.readUInt32BE(4);
    const payload = buffer.subarray(8, 8 + nbytes);
    console.log(
      `BIN <- ${peer}: ver=${ver} kind=${kind} seq=${seq} bytes=${payload.length}`
    );

    if (kind === 0) {
      const header = Buffer.alloc(8);
      header.writeUInt8(1, 0);
      header.writeUInt8(1, 1);
      header.writeUInt16BE(seq, 2);
      header.writeUInt32BE(payload.length, 4);
      const frame = Buffer.concat([header, payload]);
      broadcastAudio(socket, frame);
    }
  });

  socket.on('close', () => {
    clients.delete(socket);
    console.log(`Client disconnected: ${peer}`);
    broadcastClients();
  });

  socket.on('error', (err) => {
    console.error(`Socket error from ${peer}`, err);
    clients.delete(socket);
    broadcastClients();
  });
});

httpServer.listen(PORT, HOST, () => {
  console.log(`Bridge listening on http://${HOST === '0.0.0.0' ? '0.0.0.0' : HOST}:${PORT}`);
  const wsUrlHost = HOST === '0.0.0.0' ? 'localhost' : HOST;
  console.log(`Web UI: http://${wsUrlHost}:${PORT}/`);
  const wsScheme = 'ws';
  const path = WS_PATH === '/' ? '/' : WS_PATH;
  console.log(`WebSocket endpoint: ${wsScheme}://${wsUrlHost}:${PORT}${path}`);
});
