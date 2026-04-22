import http from 'http';
import https from 'https';
import fs from 'fs';
import Path from 'path';
import express from 'express';
import cors from 'cors';

const SERVER_IP="192.168.1.2";
const SERVER_PORT=4001;
//const SERVER_PORT_SSL=5001;
const KEEP_ALIVE_TIMEOUT_MS = 30 * 1000;

const app = express();

// const serverOptions = {
//     key: fs.readFileSync(`./server.key`, 'utf8'),
//     cert: fs.readFileSync(`./server.cer`, 'utf8'),
//     // secureProtocol: 'TLSv1_2_method'
// };

app.use(cors());
app.use(express.json());
app.use(express.raw({ type: 'application/octet-stream' }));

app.get('/events', async (req, res) => {

    req.socket.setNoDelay(true);
    req.socket.setKeepAlive(true);

    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('X-Accel-Buffering', 'no');
    res.setHeader('Connection', 'keep-alive');
    res.statusCode = 200;

    req.on('close', () => {
        console.log('Device is disconnected from SSE server');
    });

    console.log(`Device ${req.get('x-device')} is connected to SSE server. Last event Id was ${req.get('last-event-id') ?? "Unknown"}`);
    setInterval(sendData, 5000, res);
});

let id = 1;

const sendData = (clientRes) => {

  const payload = {age:Math.floor(Math.random() * 100)};
  let s = "";
  s += (`retry: 10000\n`);
  s += (`id: ${id++}\n`);
  s += (`data: ${JSON.stringify(payload)}\n`);
  s += "\n";
  s += "event: myevent\n";
  s += "data: custom data\n";
  s += "\n";

  clientRes.write(s);
};

http.createServer(app, {keepAliveTimeout : KEEP_ALIVE_TIMEOUT_MS}).listen(SERVER_PORT, SERVER_IP, () => {
    console.log(`Listening on http://${SERVER_IP}:${SERVER_PORT}`);
});

// https.createServer({...serverOptions, keepAliveTimeout : KEEP_ALIVE_TIMEOUT_MS}, app).listen(SERVER_PORT_SSL, SERVER_IP, () => {
//     console.log(`Listening on https://${SERVER_IP}:${SERVER_PORT_SSL}`);
// });
