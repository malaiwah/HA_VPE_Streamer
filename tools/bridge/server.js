import { WebSocketServer, WebSocket } from 'ws';

const PORT = process.env.PORT ? Number(process.env.PORT) : 7000;
const PATH = '/puck';

const server = new WebSocketServer({ port: PORT, path: PATH });
const clients = new Map();

console.log(`WebSocket bridge listening on ws://localhost:${PORT}${PATH}`);

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

server.on('connection', (socket, req) => {
  const peer = `${req.socket.remoteAddress}:${req.socket.remotePort}`;
  const info = { peer, deviceId: null, mode: null };
  clients.set(socket, info);
  console.log(`Client connected: ${peer}`);

  socket.on('message', (data, isBinary) => {
    if (!isBinary) {
      try {
        const text = data.toString();
        console.log(`TEXT <- ${peer}: ${text}`);
        const payload = JSON.parse(text);
        if (payload?.type === 'hello') {
          info.deviceId = payload.device_id ?? info.deviceId;
          info.mode = payload.mode ?? info.mode;
          socket.send(JSON.stringify({ type: 'hello_ack' }));
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
      header.writeUInt8(1, 0); // version
      header.writeUInt8(1, 1); // kind=1 audio_rx
      header.writeUInt16BE(seq, 2);
      header.writeUInt32BE(payload.length, 4);
      const frame = Buffer.concat([header, payload]);
      broadcastAudio(socket, frame);
    }
  });

  socket.on('close', () => {
    clients.delete(socket);
    console.log(`Client disconnected: ${peer}`);
  });

  socket.on('error', (err) => {
    console.error(`Socket error from ${peer}`, err);
    clients.delete(socket);
  });
});
