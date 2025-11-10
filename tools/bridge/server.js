import { WebSocketServer } from 'ws';

const PORT = process.env.PORT ? Number(process.env.PORT) : 7000;
const PATH = '/puck';

const server = new WebSocketServer({ port: PORT, path: PATH });
console.log(`WebSocket bridge listening on ws://localhost:${PORT}${PATH}`);

server.on('connection', (socket, req) => {
  const peer = `${req.socket.remoteAddress}:${req.socket.remotePort}`;
  console.log(`Client connected: ${peer}`);

  socket.on('message', (data, isBinary) => {
    if (!isBinary) {
      try {
        const text = data.toString();
        console.log(`TEXT <- ${peer}: ${text}`);
        const payload = JSON.parse(text);
        if (payload?.type === 'hello') {
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
    console.log(`BIN <- ${peer}: ver=${ver} kind=${kind} seq=${seq} bytes=${payload.length}`);

    if (kind === 0) {
      const header = Buffer.alloc(8);
      header.writeUInt8(1, 0); // version
      header.writeUInt8(1, 1); // kind=1 audio_rx
      header.writeUInt16BE(seq, 2);
      header.writeUInt32BE(payload.length, 4);
      socket.send(Buffer.concat([header, payload]));
    }
  });

  socket.on('close', () => {
    console.log(`Client disconnected: ${peer}`);
  });

  socket.on('error', (err) => {
    console.error(`Socket error from ${peer}`, err);
  });
});
