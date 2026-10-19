/**
 * Process Performance Monitor - Node.js Bridge Server
 * ----------------------------------------------------
 * Connects Arduino to smartphones and web browsers over WebSocket and HTTP.
 * 
 * Usage:
 *   node bridge.js
 *   node bridge.js --port COM3 --http-port 8765
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');

const HTTP_PORT = parseInt(process.env.PORT || '8765', 10);
const MAGIC_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

function getLocalIp() {
  const nets = os.networkInterfaces();
  for (const name of Object.keys(nets)) {
    for (const net of nets[name]) {
      if (net.family === 'IPv4' && !net.internal) return net.address;
    }
  }
  return '127.0.0.1';
}

const clients = new Set();
let serialPort = null;

// Optional serialport import if installed
let SerialPortPackage = null;
try {
  SerialPortPackage = require('serialport').SerialPort;
  const ReadlineParser = require('@serialport/parser-readline').ReadlineParser;
} catch (e) { }

function broadcast(line) {
  const data = Buffer.from(line, 'utf8');
  let header;
  if (data.length <= 125) {
    header = Buffer.from([0x81, data.length]);
  } else if (data.length <= 65535) {
    header = Buffer.from([0x81, 126, (data.length >> 8) & 0xff, data.length & 0xff]);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x81; header[1] = 127;
    header.writeBigUInt64BE(BigInt(data.length), 2);
  }
  const frame = Buffer.concat([header, data]);
  for (const client of clients) {
    try { client.write(frame); } catch (e) { clients.delete(client); }
  }
}

// HTTP Server
const server = http.createServer((req, res) => {
  let file = req.url.split('?')[0];
  if (file === '/' || file === '') file = '/Process_Monitor_Dashboard.html';
  const filePath = path.join(__dirname, file);

  const ext = path.extname(filePath).toLowerCase();
  const mime = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'application/javascript; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.svg': 'image/svg+xml'
  }[ext] || 'application/octet-stream';

  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('404 Not Found');
      return;
    }
    res.writeHead(200, { 'Content-Type': mime });
    res.end(data);
  });
});

// WebSocket Upgrade Handler
server.on('upgrade', (req, socket, head) => {
  const key = req.headers['sec-websocket-key'];
  if (!key) { socket.destroy(); return; }

  const acceptKey = crypto.createHash('sha1').update(key + MAGIC_GUID).digest('base64');
  const headers = [
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Accept: ${acceptKey}`
  ];
  socket.write(headers.join('\r\n') + '\r\n\r\n');
  clients.add(socket);
  console.log(`  [+] Smartphone / Browser client connected (${clients.size} total)`);

  socket.on('data', buffer => {
    // Basic unmasking of client frames
    if (buffer.length < 2) return;
    const opcode = buffer[0] & 0x0f;
    const masked = (buffer[1] & 0x80) !== 0;
    let len = buffer[1] & 0x7f;
    let offset = 2;
    if (len === 126) { len = buffer.readUInt16BE(2); offset = 4; }
    else if (len === 127) { offset = 10; }

    let mask = null;
    if (masked) { mask = buffer.slice(offset, offset + 4); offset += 4; }
    const payload = buffer.slice(offset, offset + len);
    if (masked && mask) {
      for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
    }

    if (opcode === 0x8) { // Close
      socket.end(); clients.delete(socket);
    } else if (opcode === 0x9) { // Ping
      socket.write(Buffer.from([0x8a, 0x00])); // Pong
    } else if (opcode === 0x1) { // Text
      const cmd = payload.toString('utf8').trim();
      console.log(`  [<- CMD] Received command: ${cmd}`);
      if (serialPort && serialPort.isOpen) serialPort.write(cmd);
    }
  });

  socket.on('close', () => {
    clients.delete(socket);
    console.log(`  [-] Client disconnected (${clients.size} remaining)`);
  });
  socket.on('error', () => clients.delete(socket));
});

const localIp = getLocalIp();
server.listen(HTTP_PORT, '0.0.0.0', () => {
  console.log('\n===============================================================');
  console.log('🚀 PROCESS MONITOR - NODE.JS SMARTPHONE & NETWORK BRIDGE');
  console.log('===============================================================');
  console.log(`🌐 Local Network IP:  ${localIp}`);
  console.log(`📱 Smartphone URL:    http://${localIp}:${HTTP_PORT}`);
  console.log(`⚡ WebSocket Stream:  ws://${localIp}:${HTTP_PORT}`);
  console.log('===============================================================');
  console.log('👉 Open the Smartphone URL in Chrome or Safari on your phone!\n');
});
