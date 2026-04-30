import http from "http";
import express from "express";
import cors from "cors";
import { Server as SocketIOServer } from "socket.io";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const HOST = process.env.HOST;
const HTML_PORT = process.env.HTML_PORT;
const SSE_PORT = process.env.SSE_PORT;
const SERVER_BLOCKED_DELAY = 30 * 60 * 1000; // 30mn

// ── State ──────────────────────────────────────────────────────────────────
const state = {
    sseRunning: false,
    streamRunning: false,
    statusCode: 200,
    locationHeader: "/redirect-events",//"http://0.0.0.0:5001/redirect-events",
    retry: 3000,
    data: "Hello from SERVER",
    intervalMs: 3000,
    sleep: false
};

// Active SSE clients: Map<res, intervalId>
const sseClients = new Map();
let eventId = 1;

// ── HTML Server (port 5000) ────────────────────────────────────────────────
const htmlApp = express();
htmlApp.use(cors());
htmlApp.use(express.json());
htmlApp.use(express.static(__dirname));

const htmlServer = http.createServer(htmlApp);

const io = new SocketIOServer(htmlServer, {
    cors: { origin: "*" },
});


// ── Shared SSE route handler (mounted on both servers) ────────────────────
async function sseHandler(req, res) {
    if (!state.sseRunning) {
        res.status(503).send("SSE server is stopped");
        return;
    }

    const code = state.statusCode;

    if (code >= 300 && code < 400) {
        if (state.locationHeader?.length > 0) {
            res.setHeader("Location", state.locationHeader);
        }
        res.status(code).end();
        return;
    }

    if (code >= 500) {
        res.status(code).send("Server error (simulated)");
        return;
    }

    if (state.sleep) {
      io.emit("log", `[SSE] Server blocked for ${SERVER_BLOCKED_DELAY / 1000 / 60}mn`);
      const result = await new Promise((resolve) => {
        const start = new Date();
        const timer = setInterval(() => {
          if ((new Date() - start) > SERVER_BLOCKED_DELAY) {
              clearInterval(timer);
              resolve(`Server unblocked after ${SERVER_BLOCKED_DELAY / 1000 / 60}mn`);
          }
          else if (state.sleep == false) {
            clearInterval(timer);
            resolve("Server unblocked by user");
          }
        }, 1000);
      });
      io.emit("log", `[SSE] ${result}`);
    }

    // 200 — open SSE stream
    req.socket.setNoDelay(true);
    req.socket.setKeepAlive(true);

    res.setHeader("Content-Type", "text/event-stream");
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("X-Accel-Buffering", "no");
    res.setHeader("Connection", "keep-alive");
    res.status(200);

    const deviceId = req.get("x-device");
    const device = deviceId ? `Device ${deviceId}` : "Browser";
    const lastId = req.get("last-event-id") ?? "none";
    console.log(
        `Device "${device}" connected to SSE. Last-Event-ID: ${lastId}`,
    );
    io.emit(
        "log",
        `[SSE] Device "${device}" connected. Last-Event-ID: ${lastId}`,
    );

    const intervalId = state.streamRunning
        ? setInterval(() => sendEvent(res), state.intervalMs)
        : null;

    sseClients.set(res, intervalId);

    if (state.streamRunning) {
        sendEvent(res);
    }

    req.on("close", () => {
        const iid = sseClients.get(res);
        if (iid) clearInterval(iid);
        sseClients.delete(res);
        console.log(`Device "${device}" disconnected from SSE`);
        io.emit("log", `[SSE] Device "${device}" disconnected`);
    });
}

// Mount SSE on the HTML server so the browser can reach it same-origin
htmlApp.get("/events", sseHandler);

htmlServer.listen(HTML_PORT, HOST, () => {
    console.log(`HTML server listening on http://${HOST}:${HTML_PORT}`);
});

// ── SSE Server (port 5001) — for direct ESP8266 / LAN access ─────────────
const sseApp = express();
sseApp.use(cors());

sseApp.get("/events", sseHandler);
sseApp.get("/redirect-events", async (req, res) => {
    console.log("/redirect-events called");
    state.statusCode = 200;
    await sseHandler(req, res);
});

const sseServer = http.createServer(sseApp);


io.on("connection", (socket) => {
    console.log("Websocket client connected:", socket.id);

    // Send current state immediately on connect
    socket.emit("state", state);

    socket.on("start-sse", () => {
      sseServer.listen(SSE_PORT, HOST, () => {
          console.log(`SSE server started on http://${HOST}:${SSE_PORT}`);
          state.sseRunning = true;

          io.emit("state", state);
      });
    });

    socket.on("stop-sse", () => {
        // Close all active SSE connections
        for (const [clientRes, intervalId] of sseClients) {
            clearInterval(intervalId);
            try {
                clientRes.end();
            } catch (_) {}
        }
        sseClients.clear();

        sseServer.close(() => {
          state.sseRunning = false;
          console.log("SSE server stopped — all clients disconnected");
          io.emit("state", state);
        });
    });

    socket.on("start-stream", () => {
        state.streamRunning = true;
        console.log("Stream events started");
        // Restart intervals for all connected clients
        for (const [clientRes, oldInterval] of sseClients) {
            clearInterval(oldInterval);
            const id = setInterval(
                () => sendEvent(clientRes),
                state.intervalMs,
            );
            sseClients.set(clientRes, id);
        }
        io.emit("state", state);
    });

    socket.on("stop-stream", () => {
        state.streamRunning = false;
        console.log("Stream events stopped");
        // Clear intervals but keep connections alive
        for (const [clientRes, intervalId] of sseClients) {
            clearInterval(intervalId);
            sseClients.set(clientRes, null);
        }
        io.emit("state", state);
    });

    socket.on("enable-sse-sleep", () => {
        state.sleep = true;
        io.emit("state", state);
    });

    socket.on("disable-sse-sleep", () => {
        state.sleep = false;
        io.emit("state", state);
    });

    socket.on("config", (cfg) => {
        const intervalChanged =
            cfg.intervalMs !== undefined && cfg.intervalMs !== state.intervalMs;

        if (cfg.statusCode !== undefined) state.statusCode = cfg.statusCode;
        if (cfg.locationHeader !== undefined)
            state.locationHeader = cfg.locationHeader;
        if (cfg.retry !== undefined) state.retry = cfg.retry;
        if (cfg.data !== undefined) state.data = cfg.data;
        if (cfg.intervalMs !== undefined) state.intervalMs = cfg.intervalMs;
        if (cfg.sleep !== undefined) state.sleep = cfg.sleep;

        console.log("Config updated:", state);

        // If interval changed and streaming is active, restart intervals
        if (intervalChanged && state.streamRunning) {
            for (const [clientRes, oldInterval] of sseClients) {
                clearInterval(oldInterval);
                const id = setInterval(
                    () => sendEvent(clientRes),
                    state.intervalMs,
                );
                sseClients.set(clientRes, id);
            }
        }

        io.emit("state", state);
    });

    socket.on("disconnect", () => {
        console.log("Control client disconnected:", socket.id);
    });
});

// ── SSE event sender ──────────────────────────────────────────────────────
function sendEvent(clientRes) {
    const id = eventId++;
    let s = "";
    s += `retry: ${state.retry}\n`;
    s += `: heartbeat id=${id}\n`;
    s += `id: ${id}\n`;
    s += `event: message\n`;
    s += `data: ${state.data}\n`;
    s += "\n";
    s += `id: ${id}\n`;
    s += `event: myevent\n`;
    s += `data: ${JSON.stringify({ age: Math.floor(Math.random() * 100), ts: Date.now() })}\n`;
    s += "\n";

    try {
        clientRes.write(s);
    } catch (err) {
        console.error("Error writing to SSE client:", err.message);
    }
}

// const close_all = () => {
//   console.log('SIGTERM signal received.');
//   sseServer.close(() => {
//     console.log('Closed out remaining connections');
//     // Additional cleanup tasks go here, e.g., close database connection
//     process.exit(0);
//   });
// };
//
// process.on('SIGTERM', close_all);
//
// process.on('SIGINT', close_all);
